/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "hash.h"
#include "lexer.h"
#include "misc.h"
#include "pfm.h"
#include "settings.h"
#include "sfm.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

#include "debug-print.h"

/* XSAVE transformation (and related SAVE, EXPORT procedures). */
struct save_trns
  {
    struct trns_header h;
    struct file_handle *f;	/* Associated system file. */
    int nvar;			/* Number of variables. */
    struct variable **var;      /* Variables. */
    flt64 *case_buf;		/* Case transfer buffer. */
  };

/* Options bits set by trim_dictionary(). */
#define GTSV_OPT_COMPRESSED	001	/* Compression; (X)SAVE only. */
#define GTSV_OPT_SAVE		002	/* The SAVE/XSAVE/EXPORT procedures. */
#define GTSV_OPT_MATCH_FILES	004	/* The MATCH FILES procedure. */
#define GTSV_OPT_NONE		0

/* The file being read by the input program. */
static struct file_handle *get_file;

/* The transformation being used by the SAVE procedure. */
static struct save_trns *trns;

static int trim_dictionary (struct dictionary * dict, int *options);
static int save_write_case_func (struct ccase *);
static int save_trns_proc (struct trns_header *, struct ccase *);
static void save_trns_free (struct trns_header *);

#if DEBUGGING
void dump_dict_variables (struct dictionary *);
#endif

/* Parses the GET command. */
int
cmd_get (void)
{
  struct file_handle *handle;
  struct dictionary *dict;
  int options = GTSV_OPT_NONE;

  lex_match_id ("GET");
  discard_variables ();

  lex_match ('/');
  if (lex_match_id ("FILE"))
    lex_match ('=');

  handle = fh_parse_file_handle ();
  if (handle == NULL)
    return CMD_FAILURE;

  dict = sfm_read_dictionary (handle, NULL);
  if (dict == NULL)
    return CMD_FAILURE;

#if DEBUGGING
  dump_dict_variables (dict);
#endif
  if (0 == trim_dictionary (dict, &options))
    {
      fh_close_handle (handle);
      return CMD_FAILURE;
    }
#if DEBUGGING
  dump_dict_variables (dict);
#endif

  dict_compact_values (dict);

#if DEBUGGING
  printf (_("GET translation table from file to memory:\n"));
  for (i = 0; i < dict->nvar; i++)
    {
      struct variable *v = dict->var[i];

      printf (_("  %8s from %3d,%3d to %3d,%3d\n"), v->name,
	      v->get.fv, v->get.nv, v->fv, v->nv);
    }
#endif

  dict_destroy (default_dict);
  default_dict = dict;

  vfm_source = &get_source;
  get_file = handle;

  return CMD_SUCCESS;
}

/* Parses the SAVE (for XSAVE==0) and XSAVE (for XSAVE==1)
   commands.  */
static int
cmd_save_internal (int xsave)
{
  struct file_handle *handle;
  struct dictionary *dict;
  int options = GTSV_OPT_SAVE;

  struct save_trns *t;
  struct sfm_write_info inf;

  int i;

  lex_match_id ("SAVE");

  lex_match ('/');
  if (lex_match_id ("OUTFILE"))
    lex_match ('=');

  handle = fh_parse_file_handle ();
  if (handle == NULL)
    return CMD_FAILURE;

  dict = dict_clone (default_dict);
#if DEBUGGING
  dump_dict_variables (dict);
#endif
  for (i = 0; i < dict_get_var_cnt (dict); i++) 
    dict_get_var (dict, i)->aux = dict_get_var (default_dict, i);
  if (0 == trim_dictionary (dict, &options))
    {
      fh_close_handle (handle);
      return CMD_FAILURE;
    }

#if DEBUGGING
  dump_dict_variables (dict);
#endif

  /* Write dictionary. */
  inf.h = handle;
  inf.dict = dict;
  inf.compress = !!(options & GTSV_OPT_COMPRESSED);
  if (!sfm_write_dictionary (&inf))
    {
      dict_destroy (dict);
      fh_close_handle (handle);
      return CMD_FAILURE;
    }

  /* Fill in transformation structure. */
  t = trns = xmalloc (sizeof *t);
  t->h.proc = save_trns_proc;
  t->h.free = save_trns_free;
  t->f = handle;
  t->nvar = dict_get_var_cnt (dict);
  t->var = xmalloc (sizeof *t->var * t->nvar);
  for (i = 0; i < t->nvar; i++)
    t->var[i] = dict_get_var (dict, i)->aux;
  t->case_buf = xmalloc (sizeof *t->case_buf * inf.case_size);
  dict_destroy (dict);

  if (xsave == 0)
    /* SAVE. */
    {
      procedure (NULL, save_write_case_func, NULL);
      save_trns_free (&t->h);
    }
  else
    /* XSAVE. */
    add_transformation (&t->h);

  return CMD_SUCCESS;
}

/* Parses and performs the SAVE procedure. */
int
cmd_save (void)
{
  return cmd_save_internal (0);
}

/* Parses the XSAVE transformation command. */
int
cmd_xsave (void)
{
  return cmd_save_internal (1);
}

static int
save_write_case_func (struct ccase * c)
{
  save_trns_proc (&trns->h, c);
  return 1;
}

static int
save_trns_proc (struct trns_header * t unused, struct ccase * c)
{
  flt64 *p = trns->case_buf;
  int i;

  for (i = 0; i < trns->nvar; i++)
    {
      struct variable *v = trns->var[i];
      if (v->type == NUMERIC)
	{
	  double src = c->data[v->fv].f;
	  if (src == SYSMIS)
	    *p++ = -FLT64_MAX;
	  else
	    *p++ = src;
	}
      else
	{
	  memcpy (p, c->data[v->fv].s, v->width);
	  memset (&((char *) p)[v->width], ' ',
		  REM_RND_UP (v->width, sizeof *p));
	  p += DIV_RND_UP (v->width, sizeof *p);
	}
    }

  sfm_write_case (trns->f, trns->case_buf, p - trns->case_buf);
  return -1;
}

static void
save_trns_free (struct trns_header *pt)
{
  struct save_trns *t = (struct save_trns *) pt;

  fh_close_handle (t->f);
  free (t->var);
  free (t->case_buf);
  free (t);
}

static int rename_variables (struct dictionary * dict);

/* The GET and SAVE commands have a common structure after the
   FILE/OUTFILE subcommand.  This function parses this structure and
   returns nonzero on success, zero on failure.  It both reads
   *OPTIONS, for the GTSV_OPT_SAVE bit, and writes it, for the
   GTSV_OPT_COMPRESSED bit. */
/* FIXME: IN, FIRST, LAST, MAP. */
/* FIXME?  Should we call dict_compact_values() on dict as a
   final step? */
static int
trim_dictionary (struct dictionary *dict, int *options)
{
  if (set_scompression)
    *options |= GTSV_OPT_COMPRESSED;

  if (*options & GTSV_OPT_SAVE)
    {
      /* Delete all the scratch variables. */
      struct variable **v;
      size_t nv;
      size_t i;

      v = xmalloc (sizeof *v * dict_get_var_cnt (dict));
      nv = 0;
      for (i = 0; i < dict_get_var_cnt (dict); i++) 
        if (dict_get_var (dict, i)->name[0] == '#')
          v[nv++] = dict_get_var (dict, i);
      dict_delete_vars (dict, v, nv);
      free (v);
    }
  
  while ((*options & GTSV_OPT_MATCH_FILES) || lex_match ('/'))
    {
      if (!(*options & GTSV_OPT_MATCH_FILES) && lex_match_id ("COMPRESSED"))
	*options |= GTSV_OPT_COMPRESSED;
      else if (!(*options & GTSV_OPT_MATCH_FILES) && lex_match_id ("UNCOMPRESSED"))
	*options &= ~GTSV_OPT_COMPRESSED;
      else if (lex_match_id ("DROP"))
	{
	  struct variable **v;
	  int nv;

	  lex_match ('=');
	  if (!parse_variables (dict, &v, &nv, PV_NONE))
	    return 0;
          dict_delete_vars (dict, v, nv);
          free (v);
	}
      else if (lex_match_id ("KEEP"))
	{
	  struct variable **v;
	  int nv;
          int i;

	  lex_match ('=');
	  if (!parse_variables (dict, &v, &nv, PV_NONE))
	    return 0;

          /* Move the specified variables to the beginning. */
          dict_reorder_vars (dict, v, nv);
          
          /* Delete the remaining variables. */
          v = xrealloc (v, (dict_get_var_cnt (dict) - nv) * sizeof *v);
          for (i = nv; i < dict_get_var_cnt (dict); i++)
            v[i - nv] = dict_get_var (dict, i);
          dict_delete_vars (dict, v, dict_get_var_cnt (dict) - nv);
          free (v);
	}
      else if (lex_match_id ("RENAME"))
	{
	  if (!rename_variables (dict))
	    return 0;
	}
      else
	{
	  lex_error (_("while expecting a valid subcommand"));
	  return 0;
	}

      if (dict_get_var_cnt (dict) == 0)
	{
	  msg (SE, _("All variables deleted from system file dictionary."));
	  return 0;
	}

      if (*options & GTSV_OPT_MATCH_FILES)
	return 1;
    }

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  
  return 1;
}

/* Parses and performs the RENAME subcommand of GET and SAVE. */
static int
rename_variables (struct dictionary * dict)
{
  int i;

  int success = 0;

  struct variable **v;
  char **new_names;
  int nv, nn;
  char *err_name;

  int group;

  lex_match ('=');
  if (token != '(')
    {
      struct variable *v;

      v = parse_dict_variable (dict);
      if (v == NULL)
	return 0;
      if (!lex_force_match ('=')
	  || !lex_force_id ())
	return 0;
      if (!strncmp (tokid, v->name, 8))
	return 1;
      if (dict_lookup_var (dict, tokid) != NULL)
	{
	  msg (SE, _("Cannot rename %s as %s because there already exists "
		     "a variable named %s.  To rename variables with "
		     "overlapping names, use a single RENAME subcommand "
		     "such as \"/RENAME (A=B)(B=C)(C=A)\", or equivalently, "
		     "\"/RENAME (A B C=B C A)\"."), v->name, tokid, tokid);
	  return 0;
	}
      
      dict_rename_var (dict, v, tokid);
      lex_get ();
      return 1;
    }

  nv = nn = 0;
  v = NULL;
  new_names = 0;
  group = 1;
  while (lex_match ('('))
    {
      int old_nv = nv;

      if (!parse_variables (dict, &v, &nv, PV_NO_DUPLICATE | PV_APPEND))
	goto done;
      if (!lex_match ('='))
	{
	  msg (SE, _("`=' expected after variable list."));
	  goto done;
	}
      if (!parse_DATA_LIST_vars (&new_names, &nn, PV_APPEND | PV_NO_SCRATCH))
	goto done;
      if (nn != nv)
	{
	  msg (SE, _("Number of variables on left side of `=' (%d) does not "
	       "match number of variables on right side (%d), in "
	       "parenthesized group %d of RENAME subcommand."),
	       nv - old_nv, nn - old_nv, group);
	  goto done;
	}
      if (!lex_force_match (')'))
	goto done;
      group++;
    }

  if (!dict_rename_vars (dict, v, new_names, nv, &err_name)) 
    {
      msg (SE, _("Requested renaming duplicates variable name %s."), err_name);
      goto done;
    }
  success = 1;

done:
  for (i = 0; i < nn; i++)
    free (new_names[i]);
  free (new_names);
  free (v);

  return success;
}

#if DEBUGGING
void
dump_dict_variables (struct dictionary * dict)
{
  int i;

  printf (_("\nVariables in dictionary:\n"));
  for (i = 0; i < dict->nvar; i++)
    printf ("%s, ", dict->var[i]->name);
  printf ("\n");
}
#endif

/* Clears internal state related to GET input procedure. */
static void
get_source_destroy_source (void)
{
  /* It is not necessary to destroy the dictionary because if we get
     to this point then the dictionary is default_dict. */
  fh_close_handle (get_file);
}

/* Reads all the cases from the data file and passes them to
   write_case(). */
static void
get_source_read (void)
{
  while (sfm_read_case (get_file, temp_case->data, default_dict)
	 && write_case ())
    ;
  get_source_destroy_source ();
}

struct case_stream get_source =
  {
    NULL,
    get_source_read,
    NULL,
    NULL,
    get_source_destroy_source,
    NULL,
    "GET",
  };


/* MATCH FILES. */

#include "debug-print.h"

/* File types. */
enum
  {
    MTF_FILE,			/* Specified on FILE= subcommand. */
    MTF_TABLE			/* Specified on TABLE= subcommand. */
  };

/* One of the files on MATCH FILES. */
struct mtf_file
  {
    struct mtf_file *next, *prev;
				/* Next, previous in the list of files. */
    struct mtf_file *next_min;	/* Next in the chain of minimums. */
    
    int type;			/* One of MTF_*. */
    struct variable **by;	/* List of BY variables for this file. */
    struct file_handle *handle;	/* File handle for the file. */
    struct dictionary *dict;	/* Dictionary from system file. */
    char in[9];			/* Name of the variable from IN=. */
    char first[9], last[9];	/* Name of the variables from FIRST=, LAST=. */
    union value *input;		/* Input record. */
  };

/* All the files mentioned on FILE or TABLE. */
static struct mtf_file *mtf_head, *mtf_tail;

/* Variables on the BY subcommand. */
static struct variable **mtf_by;
static int mtf_n_by;

/* Master dictionary. */
static struct dictionary *mtf_master;

/* Used to determine whether we've already initialized this
   variable. */
static unsigned mtf_seq_num;

/* Sequence numbers for each variable in mtf_master. */
static unsigned *mtf_seq_nums;

static void mtf_free (void);
static void mtf_free_file (struct mtf_file *file);
static int mtf_merge_dictionary (struct mtf_file *f);
static void mtf_delete_file_in_place (struct mtf_file **file);

static void mtf_read_nonactive_records (void);
static void mtf_processing_finish (void);
static int mtf_processing (struct ccase *);

static char *var_type_description (struct variable *);

/* Parse and execute the MATCH FILES command. */
int
cmd_match_files (void)
{
  struct mtf_file *first_table = NULL;
  
  int seen = 0;
  
  lex_match_id ("MATCH");
  lex_match_id ("FILES");

  mtf_head = mtf_tail = NULL;
  mtf_by = NULL;
  mtf_n_by = 0;
  mtf_master = dict_create ();
  mtf_seq_num = 0;
  mtf_seq_nums = NULL;
  dict_set_case_limit (mtf_master, dict_get_case_limit (default_dict));
  
  do
    {
      lex_match ('/');

      if (lex_match (T_BY))
	{
	  if (seen & 1)
	    {
	      msg (SE, _("The BY subcommand may be given once at most."));
	      goto lossage;
	    }
	  seen |= 1;
	      
	  lex_match ('=');
	  if (!parse_variables (mtf_master, &mtf_by, &mtf_n_by,
				PV_NO_DUPLICATE | PV_NO_SCRATCH))
	    goto lossage;
	}
      else if (token != T_ID)
	{
	  lex_error (NULL);
	  goto lossage;
	}
      else if (lex_id_match ("FILE", tokid) || lex_id_match ("TABLE", tokid))
	{
	  struct mtf_file *file = xmalloc (sizeof *file);

	  file->in[0] = file->first[0] = file->last[0] = '\0';
	  file->dict = NULL;
	  file->by = NULL;
	  file->input = NULL;

	  if (lex_match_id ("FILE"))
	    file->type = MTF_FILE;
	  else if (lex_match_id ("TABLE"))
	    {
	      file->type = MTF_TABLE;
	      seen |= 4;
	    }
	  else
	    assert (0);

	  /* FILEs go first, then TABLEs. */
	  if (file->type == MTF_TABLE || first_table == NULL)
	    {
	      file->next = NULL;
	      file->prev = mtf_tail;
	      if (mtf_tail)
		mtf_tail->next = file;
	      mtf_tail = file;
	      if (mtf_head == NULL)
		mtf_head = file;
	      if (file->type == MTF_TABLE && first_table == NULL)
		first_table = file;
	    }
	  else 
	    {
	      assert (file->type == MTF_FILE);
	      file->next = first_table;
	      file->prev = first_table->prev;
	      if (first_table->prev)
		first_table->prev->next = file;
	      else
		mtf_head = file;
	      first_table->prev = file;
	    }
	  
	  lex_match ('=');
	  
	  if (lex_match ('*'))
	    {
	      file->handle = NULL;

	      if (seen & 2)
		{
		  msg (SE, _("The active file may not be specified more "
			     "than once."));
		  goto lossage;
		}
	      seen |= 2;

	      assert (pgm_state != STATE_INPUT);
	      if (pgm_state == STATE_INIT)
		{
		  msg (SE, _("Cannot specify the active file since no active "
			     "file has been defined."));
		  goto lossage;
		}
	    }
	  else
	    {
	      file->handle = fh_parse_file_handle ();
	      if (!file->handle)
		goto lossage;
	    }

	  if (file->handle)
	    {
	      file->dict = sfm_read_dictionary (file->handle, NULL);
	      if (!file->dict)
		goto lossage;
	    }
	  else
	    file->dict = default_dict;
	  if (!mtf_merge_dictionary (file))
	    goto lossage;
	}
      else if (lex_id_match ("IN", tokid)
	       || lex_id_match ("FIRST", tokid)
	       || lex_id_match ("LAST", tokid))
	{
	  const char *sbc;
	  char *name;
	  
	  if (mtf_tail == NULL)
	    {
	      msg (SE, _("IN, FIRST, and LAST subcommands may not occur "
			 "before the first FILE or TABLE."));
	      goto lossage;
	    }

	  if (lex_match_id ("IN"))
	    {
	      name = mtf_tail->in;
	      sbc = "IN";
	    }
	  else if (lex_match_id ("FIRST"))
	    {
	      name = mtf_tail->first;
	      sbc = "FIRST";
	    }
	  else if (lex_match_id ("LAST"))
	    {
	      name = mtf_tail->last;
	      sbc = "LAST";
	    }
	  else
	    assert (0);

	  lex_match ('=');
	  if (token != T_ID)
	    {
	      lex_error (NULL);
	      goto lossage;
	    }

	  if (*name)
	    {
	      msg (SE, _("Multiple %s subcommands for a single FILE or "
			 "TABLE."),
		   sbc);
	      goto lossage;
	    }
	  strcpy (name, tokid);
	  lex_get ();

	  if (!dict_create_var (mtf_master, name, 0))
	    {
	      msg (SE, _("Duplicate variable name %s while creating %s "
			 "variable."),
		   name, sbc);
	      goto lossage;
	    }
	}
      else if (lex_id_match ("RENAME", tokid)
	       || lex_id_match ("KEEP", tokid)
	       || lex_id_match ("DROP", tokid))
	{
	  int options = GTSV_OPT_MATCH_FILES;
	  
	  if (mtf_tail == NULL)
	    {
	      msg (SE, _("RENAME, KEEP, and DROP subcommands may not occur "
			 "before the first FILE or TABLE."));
	      goto lossage;
	    }

	  if (!trim_dictionary (mtf_tail->dict, &options))
	    goto lossage;
	}
      else if (lex_match_id ("MAP"))
	{
	  /* FIXME. */
	}
      else
	{
	  lex_error (NULL);
	  goto lossage;
	}
    }
  while (token != '.');

  if (seen & 4)
    {
      if (!(seen & 1))
	{
	  msg (SE, _("The BY subcommand is required when a TABLE subcommand "
		     "is given."));
	  goto lossage;
	}
    }

  if (seen & 1)
    {
      struct mtf_file *iter;

      for (iter = mtf_head; iter; iter = iter->next)
	{
	  int i;
	  
	  iter->by = xmalloc (sizeof *iter->by * mtf_n_by);

	  for (i = 0; i < mtf_n_by; i++)
	    {
	      iter->by[i] = dict_lookup_var (iter->dict, mtf_by[i]->name);
	      if (iter->by[i] == NULL)
		{
		  msg (SE, _("File %s lacks BY variable %s."),
		       iter->handle ? fh_handle_name (iter->handle) : "*",
		       mtf_by[i]->name);
		  goto lossage;
		}
	    }
	}
    }

#if DEBUGGING
  {
    /* From sfm-read.c. */
    extern void dump_dictionary (struct dictionary *);

    dump_dictionary (mtf_master);
  }
#endif

  /* MATCH FILES performs an n-way merge on all its input files.
     Abstract algorithm:

     1. Read one input record from every input FILE.

     2. If no FILEs are left, stop.  Otherwise, proceed to step 3.

     3. Find the FILE input record with minimum BY values.  Store all
     the values from this input record into the output record.

     4. Find all the FILE input records with BY values identical to
     the minimums.  Store all the values from these input records into
     the output record.

     5. For every TABLE, read another record as long as the BY values
     on the TABLE's input record are less than the FILEs' BY values.
     If an exact match is found, store all the values from the TABLE
     input record into the output record.

     6. Write the output record.

     7. Read another record from each input file FILE and TABLE that
     we stored values from above.  If we come to the end of one of the
     input files, remove it from the list of input files.

     8. Repeat from step 2.

     Unfortunately, this algorithm can't be directly implemented
     because there's no function to read a record from the active
     file; instead, it has to be done using callbacks.

     FIXME: For merging large numbers of files (more than 10?) a
     better algorithm would use a heap for finding minimum
     values, or replacement selection, as described by Knuth in
     _Art of Computer Programming, Vol. 3_.  The SORT CASES
     procedure does this, and perhaps some of its code could be
     adapted. */

  if (!(seen & 2))
    discard_variables ();

  temporary = 2;
  temp_dict = mtf_master;
  temp_trns = n_trns;

  mtf_seq_nums = xmalloc (dict_get_var_cnt (mtf_master)
                          * sizeof *mtf_seq_nums);
  memset (mtf_seq_nums, 0,
          dict_get_var_cnt (mtf_master) * sizeof *mtf_seq_nums);

  process_active_file (mtf_read_nonactive_records, mtf_processing,
		       mtf_processing_finish);
  mtf_master = NULL;
  
  mtf_free ();
  return CMD_SUCCESS;
  
lossage:
  mtf_free ();
  return CMD_FAILURE;
}

/* Repeats 2...8 an arbitrary number of times. */
static void
mtf_processing_finish (void)
{
  /* Find the active file and delete it. */
  {
    struct mtf_file *iter;
    
    for (iter = mtf_head; iter; iter = iter->next)
      if (iter->handle == NULL)
	{
	  mtf_delete_file_in_place (&iter);
	  break;
	}
  }
  
  while (mtf_head && mtf_head->type == MTF_FILE)
    if (!mtf_processing (temp_case))
      break;
}

/* Return a string in a static buffer describing V's variable type and
   width. */
static char *
var_type_description (struct variable *v)
{
  static char buf[2][32];
  static int x = 0;
  char *s;

  x ^= 1;
  s = buf[x];

  if (v->type == NUMERIC)
    strcpy (s, "numeric");
  else
    {
      assert (v->type == ALPHA);
      sprintf (s, "string with width %d", v->width);
    }
  return s;
}

/* Free FILE and associated data. */
static void
mtf_free_file (struct mtf_file *file)
{
  fh_close_handle (file->handle);
  if (file->dict != NULL && file->dict != default_dict)
    dict_destroy (file->dict);
  free (file->by);
  if (file->handle)
    free (file->input);
  free (file);
}

/* Free all the data for the MATCH FILES procedure. */
static void
mtf_free (void)
{
  struct mtf_file *iter, *next;

  for (iter = mtf_head; iter; iter = next)
    {
      next = iter->next;

      mtf_free_file (iter);
    }
  
  free (mtf_by);
  if (mtf_master)
    dict_destroy (mtf_master);
  free (mtf_seq_nums);
}

/* Remove *FILE from the mtf_file chain.  Make *FILE point to the next
   file in the chain, or to NULL if was the last in the chain. */
static void
mtf_delete_file_in_place (struct mtf_file **file)
{
  struct mtf_file *f = *file;

  if (f->prev)
    f->prev->next = f->next;
  if (f->next)
    f->next->prev = f->prev;
  if (f == mtf_head)
    mtf_head = f->next;
  if (f == mtf_tail)
    mtf_tail = f->prev;
  *file = f->next;

  {
    int i;

    for (i = 0; i < dict_get_var_cnt (f->dict); i++)
      {
	struct variable *v = dict_get_var (f->dict, i);
	  
	if (v->type == NUMERIC)
	  compaction_case->data[v->p.mtf.master->fv].f = SYSMIS;
	else
	  memset (compaction_case->data[v->p.mtf.master->fv].s, ' ',
		  v->width);
      }
  }
  
  mtf_free_file (f);
}

/* Read a record from every input file except the active file. */
static void
mtf_read_nonactive_records (void)
{
  struct mtf_file *iter;

  for (iter = mtf_head; iter; )
    {
      if (iter->handle)
	{
	  assert (iter->input == NULL);
	  iter->input = xmalloc (sizeof *iter->input
                                 * dict_get_value_cnt (iter->dict));
	  
	  if (!sfm_read_case (iter->handle, iter->input, iter->dict))
	    mtf_delete_file_in_place (&iter);
	  else
	    iter = iter->next;
	}
      else
	{
	  iter->input = temp_case->data;
	  iter = iter->next;
	}
    }
}

/* Compare the BY variables for files A and B; return -1 if A < B, 0
   if A == B, 1 if A > B. */
static inline int
mtf_compare_BY_values (struct mtf_file *a, struct mtf_file *b)
{
  int i;
  
  for (i = 0; i < mtf_n_by; i++)
    {
      assert (a->by[i]->type == b->by[i]->type);
      assert (a->by[i]->width == b->by[i]->width);
      
      if (a->by[i]->type == NUMERIC)
	{
	  double af = a->input[a->by[i]->fv].f;
	  double bf = b->input[b->by[i]->fv].f;

	  if (af < bf)
	    return -1;
	  else if (af > bf)
	    return 1;
	}
      else 
	{
	  int result;
	  
	  assert (a->by[i]->type == ALPHA);
	  result = memcmp (a->input[a->by[i]->fv].s,
			   b->input[b->by[i]->fv].s,
			   a->by[i]->width);
	  if (result < 0)
	    return -1;
	  else if (result > 0)
	    return 1;
	}
    }
  return 0;
}

/* Perform one iteration of steps 3...7 above. */
static int
mtf_processing (struct ccase *c unused)
{
  /* List of files with minimum BY values. */
  struct mtf_file *min_head, *min_tail;

  /* List of files with non-minimum BY values. */
  struct mtf_file *max_head, *max_tail;

  /* Iterator. */
  struct mtf_file *iter;

  for (;;)
    {
      /* If the active file doesn't have the minimum BY values, don't
	 return because that would cause a record to be skipped. */
      int advance = 1;

      if (mtf_head->type == MTF_TABLE)
	return 0;
      
      /* 3. Find the FILE input record with minimum BY values.  Store
	 all the values from this input record into the output record.

	 4. Find all the FILE input records with BY values identical
	 to the minimums.  Store all the values from these input
	 records into the output record. */
      min_head = min_tail = mtf_head;
      max_head = max_tail = NULL;
      for (iter = mtf_head->next; iter && iter->type == MTF_FILE;
	   iter = iter->next)
	switch (mtf_compare_BY_values (min_head, iter))
	  {
	  case -1:
	    if (max_head)
	      max_tail = max_tail->next_min = iter;
	    else
	      max_head = max_tail = iter;
	    break;

	  case 0:
	    min_tail = min_tail->next_min = iter;
	    break;

	  case 1:
	    if (max_head)
	      {
		max_tail->next_min = min_head;
		max_tail = min_tail;
	      }
	    else
	      {
		max_head = min_head;
		max_tail = min_tail;
	      }
	    min_head = min_tail = iter;
	    break;

	  default:
	    assert (0);
	  }

      /* 5. For every TABLE, read another record as long as the BY
	 values on the TABLE's input record are less than the FILEs'
	 BY values.  If an exact match is found, store all the values
	 from the TABLE input record into the output record. */
      while (iter)
	{
	  struct mtf_file *next = iter->next;
	  
	  assert (iter->type == MTF_TABLE);
      
	  if (iter->handle == NULL)
	    advance = 0;

	again:
	  switch (mtf_compare_BY_values (min_head, iter))
	    {
	    case -1:
	      if (max_head)
		max_tail = max_tail->next_min = iter;
	      else
		max_head = max_tail = iter;
	      break;

	    case 0:
	      min_tail = min_tail->next_min = iter;
	      break;

	    case 1:
	      if (iter->handle == NULL)
		return 1;
	      if (sfm_read_case (iter->handle, iter->input, iter->dict))
		goto again;
	      mtf_delete_file_in_place (&iter);
	      break;

	    default:
	      assert (0);
	    }

	  iter = next;
	}

      /* Next sequence number. */
      mtf_seq_num++;
  
      /* Store data to all the records we are using. */
      if (min_tail)
	min_tail->next_min = NULL;
      for (iter = min_head; iter; iter = iter->next_min)
	{
	  int i;

	  for (i = 0; i < dict_get_var_cnt (iter->dict); i++)
	    {
	      struct variable *v = dict_get_var (iter->dict, i);
	  
	      if (mtf_seq_nums[v->p.mtf.master->index] == mtf_seq_num)
		continue;
              mtf_seq_nums[v->p.mtf.master->index] = mtf_seq_num;

#if 0
	      printf ("%s/%s: dest-fv=%d, src-fv=%d\n",
		      fh_handle_name (iter->handle),
		      v->name,
		      v->p.mtf.master->fv, v->fv);
#endif
	      if (v->type == NUMERIC)
		compaction_case->data[v->p.mtf.master->fv].f
		  = iter->input[v->fv].f;
	      else
		{
		  assert (v->type == ALPHA);
		  memcpy (compaction_case->data[v->p.mtf.master->fv].s,
			  iter->input[v->fv].s, v->width);
		}
	    }
	}

      /* Store missing values to all the records we're not using. */
      if (max_tail)
	max_tail->next_min = NULL;
      for (iter = max_head; iter; iter = iter->next_min)
	{
	  int i;

	  for (i = 0; i < dict_get_var_cnt (iter->dict); i++)
	    {
	      struct variable *v = dict_get_var (iter->dict, i);
	  
	      if (mtf_seq_nums[v->p.mtf.master->index] == mtf_seq_num)
		continue;
              mtf_seq_nums[v->p.mtf.master->index] = mtf_seq_num;

#if 0
	      printf ("%s/%s: dest-fv=%d\n",
		      fh_handle_name (iter->handle),
		      v->name,
		      v->p.mtf.master->fv);
#endif
	      if (v->type == NUMERIC)
		compaction_case->data[v->p.mtf.master->fv].f = SYSMIS;
	      else
                memset (compaction_case->data[v->p.mtf.master->fv].s, ' ',
                        v->width);
	    }

	  if (iter->handle == NULL)
	    advance = 0;
	}

      /* 6. Write the output record. */
      process_active_file_output_case ();

      /* 7. Read another record from each input file FILE and TABLE
	 that we stored values from above.  If we come to the end of
	 one of the input files, remove it from the list of input
	 files. */
      for (iter = min_head; iter && iter->type == MTF_FILE; )
	{
	  struct mtf_file *next = iter->next_min;
	  
	  if (iter->handle)
	    {
	      assert (iter->input != NULL);

	      if (!sfm_read_case (iter->handle, iter->input, iter->dict))
		mtf_delete_file_in_place (&iter);
	    }

	  iter = next;
	}
      
      if (advance)
	break;
    }

  return (mtf_head && mtf_head->type != MTF_TABLE);
}

/* Merge the dictionary for file F into the master dictionary
   mtf_master. */
static int
mtf_merge_dictionary (struct mtf_file *f)
{
  struct dictionary *const m = mtf_master;
  struct dictionary *d = f->dict;
  const char *d_docs, *m_docs;

  if (dict_get_label (m) == NULL)
    dict_set_label (m, dict_get_label (d));

  d_docs = dict_get_documents (d);
  m_docs = dict_get_documents (m);
  if (d_docs != NULL) 
    {
      if (m_docs == NULL)
        dict_set_documents (m, d_docs);
      else
        {
          char *new_docs;
          size_t new_len;

          new_len = strlen (m_docs) + strlen (d_docs);
          new_docs = xmalloc (new_len + 1);
          strcpy (new_docs, m_docs);
          strcat (new_docs, d_docs);
          dict_set_documents (m, new_docs);
          free (new_docs);
        }
    }
  
  dict_compact_values (d);

  {
    int i;

    for (i = 0; i < dict_get_var_cnt (d); i++)
      {
	struct variable *dv = dict_get_var (d, i);
	struct variable *mv = dict_lookup_var (m, dv->name);

	assert (dv->type == ALPHA || dv->width == 0);
	assert (!mv || mv->type == ALPHA || mv->width == 0);
	if (mv && dv->width == mv->width)
	  {
	    if (val_labs_count (dv->val_labs)
                && !val_labs_count (mv->val_labs))
	      mv->val_labs = val_labs_copy (dv->val_labs);
	    if (dv->miss_type != MISSING_NONE
                && mv->miss_type == MISSING_NONE)
	      copy_missing_values (mv, dv);
	  }
	if (mv && dv->label && !mv->label)
	  mv->label = xstrdup (dv->label);
	if (!mv) 
          {
            mv = dict_clone_var (m, dv, dv->name);
            assert (mv != NULL);
          }
	else if (mv->width != dv->width)
	  {
	    msg (SE, _("Variable %s in file %s (%s) has different "
		       "type or width from the same variable in "
		       "earlier file (%s)."),
		 dv->name, fh_handle_name (f->handle),
		 var_type_description (dv), var_type_description (mv));
	    return 0;
	  }
	dv->p.mtf.master = mv;
      }
  }

  return 1;
}

/* IMPORT command. */

/* Parses the IMPORT command. */
int
cmd_import (void)
{
  struct file_handle *handle = NULL;
  struct dictionary *dict;
  int options = GTSV_OPT_NONE;
  int type;

  lex_match_id ("IMPORT");

  for (;;)
    {
      lex_match ('/');
      
      if (lex_match_id ("FILE") || token == T_STRING)
	{
	  lex_match ('=');

	  handle = fh_parse_file_handle ();
	  if (handle == NULL)
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("TYPE"))
	{
	  lex_match ('=');

	  if (lex_match_id ("COMM"))
	    type = PFM_COMM;
	  else if (lex_match_id ("TAPE"))
	    type = PFM_TAPE;
	  else
	    {
	      lex_error (_("expecting COMM or TAPE"));
	      return CMD_FAILURE;
	    }
	}
      else break;
    }
  if (!lex_match ('/') && token != '.')
    {
      lex_error (NULL);
      return CMD_FAILURE;
    }

  discard_variables ();

  dict = pfm_read_dictionary (handle, NULL);
  if (dict == NULL)
    return CMD_FAILURE;

#if DEBUGGING
  dump_dict_variables (dict);
#endif
  if (0 == trim_dictionary (dict, &options))
    {
      fh_close_handle (handle);
      return CMD_FAILURE;
    }
#if DEBUGGING
  dump_dict_variables (dict);
#endif

  dict_compact_values (dict);

#if DEBUGGING
  printf (_("IMPORT translation table from file to memory:\n"));
  for (i = 0; i < dict->nvar; i++)
    {
      struct variable *v = dict->var[i];

      printf (_("  %8s from %3d,%3d to %3d,%3d\n"), v->name,
	      v->get.fv, v->get.nv, v->fv, v->nv);
    }
#endif

  dict_destroy (default_dict);
  default_dict = dict;

  vfm_source = &import_source;
  get_file = handle;

  return CMD_SUCCESS;
}

/* Reads all the cases from the data file and passes them to
   write_case(). */
static void
import_source_read (void)
{
  while (pfm_read_case (get_file, temp_case->data, default_dict)
	 && write_case ())
    ;
  get_source_destroy_source ();
}

struct case_stream import_source =
  {
    NULL,
    import_source_read,
    NULL,
    NULL,
    get_source_destroy_source,
    NULL,
    "IMPORT",
  };

static int export_write_case_func (struct ccase *c);
     
/* Parses the EXPORT command.  */
/* FIXME: same as cmd_save_internal(). */
int
cmd_export (void)
{
  struct file_handle *handle;
  struct dictionary *dict;
  int options = GTSV_OPT_SAVE;

  struct save_trns *t;

  int i;

  lex_match_id ("EXPORT");

  lex_match ('/');
  if (lex_match_id ("OUTFILE"))
    lex_match ('=');

  handle = fh_parse_file_handle ();
  if (handle == NULL)
    return CMD_FAILURE;

  dict = dict_clone (default_dict);
#if DEBUGGING
  dump_dict_variables (dict);
#endif
  for (i = 0; i < dict_get_var_cnt (dict); i++)
    dict_get_var (dict, i)->aux = dict_get_var (default_dict, i);
  if (0 == trim_dictionary (dict, &options))
    {
      fh_close_handle (handle);
      return CMD_FAILURE;
    }

#if DEBUGGING
  dump_dict_variables (dict);
#endif

  /* Write dictionary. */
  if (!pfm_write_dictionary (handle, dict))
    {
      dict_destroy (dict);
      fh_close_handle (handle);
      return CMD_FAILURE;
    }

  /* Fill in transformation structure. */
  t = trns = xmalloc (sizeof *t);
  t->h.proc = save_trns_proc;
  t->h.free = save_trns_free;
  t->f = handle;
  t->nvar = dict_get_var_cnt (dict);
  t->var = xmalloc (sizeof *t->var * t->nvar);
  for (i = 0; i < t->nvar; i++)
    t->var[i] = dict_get_var (dict, i)->aux;
  t->case_buf = xmalloc (sizeof *t->case_buf * t->nvar);
  dict_destroy (dict);

  procedure (NULL, export_write_case_func, NULL);
  save_trns_free (&t->h);

  return CMD_SUCCESS;
}

static int
export_write_case_func (struct ccase *c)
{
  union value *p = (union value *) trns->case_buf;
  int i;

  for (i = 0; i < trns->nvar; i++)
    {
      struct variable *v = trns->var[i];

      if (v->type == NUMERIC)
	*p++ = c->data[v->fv];
      else
	(*p++).c = c->data[v->fv].s;
    }

  pfm_write_case (trns->f, (union value *) trns->case_buf);
  return 1;
}
