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
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "case.h"
#include "command.h"
#include "dictionary.h"
#include "error.h"
#include "file-handle.h"
#include "hash.h"
#include "lexer.h"
#include "misc.h"
#include "pfm-read.h"
#include "pfm-write.h"
#include "settings.h"
#include "sfm-read.h"
#include "sfm-write.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

#include "debug-print.h"

/* Rearranging and reducing a dictionary. */
static void start_case_map (struct dictionary *);
static struct case_map *finish_case_map (struct dictionary *);
static void map_case (const struct case_map *,
                      const struct ccase *, struct ccase *);
static void destroy_case_map (struct case_map *);

/* Operation type. */
enum operation 
  {
    OP_READ,    /* GET or IMPORT. */
    OP_SAVE,    /* SAVE or XSAVE. */
    OP_EXPORT,  /* EXPORT. */
    OP_MATCH    /* MATCH FILES. */
  };

static int trim_dictionary (struct dictionary *,
                            enum operation, int *compress);

/* GET input program. */
struct get_pgm 
  {
    struct sfm_reader *reader;  /* System file reader. */
    struct case_map *map;       /* Map from system file to active file dict. */
    struct ccase bounce;        /* Bounce buffer. */
  };

static void get_pgm_free (struct get_pgm *);

/* Parses the GET command. */
int
cmd_get (void)
{
  struct get_pgm *pgm = NULL;
  struct file_handle *fh;
  struct dictionary *dict = NULL;

  pgm = xmalloc (sizeof *pgm);
  pgm->reader = NULL;
  pgm->map = NULL;
  case_nullify (&pgm->bounce);

  discard_variables ();

  lex_match ('/');
  if (lex_match_id ("FILE"))
    lex_match ('=');
  fh = fh_parse ();
  if (fh == NULL)
    goto error;

  pgm->reader = sfm_open_reader (fh, &dict, NULL);
  if (pgm->reader == NULL)
    goto error;
  case_create (&pgm->bounce, dict_get_next_value_idx (dict));

  start_case_map (dict);
  if (!trim_dictionary (dict, OP_READ, NULL))
    goto error;
  pgm->map = finish_case_map (dict);

  dict_destroy (default_dict);
  default_dict = dict;

  vfm_source = create_case_source (&get_source_class, pgm);

  return CMD_SUCCESS;

 error:
  get_pgm_free (pgm);
  if (dict != NULL)
    dict_destroy (dict);
  return CMD_FAILURE;
}

/* Frees a struct get_pgm. */
static void
get_pgm_free (struct get_pgm *pgm) 
{
  if (pgm != NULL) 
    {
      sfm_close_reader (pgm->reader);
      destroy_case_map (pgm->map);
      case_destroy (&pgm->bounce);
      free (pgm);
    }
}

/* Clears internal state related to GET input procedure. */
static void
get_source_destroy (struct case_source *source)
{
  struct get_pgm *pgm = source->aux;
  get_pgm_free (pgm);
}

/* Reads all the cases from the data file into C and passes them
   to WRITE_CASE one by one, passing WC_DATA. */
static void
get_source_read (struct case_source *source,
                 struct ccase *c,
                 write_case_func *write_case, write_case_data wc_data)
{
  struct get_pgm *pgm = source->aux;
  int ok;

  do
    {
      if (pgm->map == NULL)
        ok = sfm_read_case (pgm->reader, c);
      else
        {
          ok = sfm_read_case (pgm->reader, &pgm->bounce);
          if (ok)
            map_case (pgm->map, &pgm->bounce, c);
        }

      if (ok)
        ok = write_case (wc_data);
    }
  while (ok);
}

const struct case_source_class get_source_class =
  {
    "GET",
    NULL,
    get_source_read,
    get_source_destroy,
  };

/* XSAVE transformation and SAVE procedure. */
struct save_trns
  {
    struct trns_header h;
    struct sfm_writer *writer;  /* System file writer. */
    struct case_map *map;       /* Map from active file to system file dict. */
    struct ccase bounce;        /* Bounce buffer. */
  };

static int save_write_case_func (struct ccase *, void *);
static trns_proc_func save_trns_proc;
static trns_free_func save_trns_free;

/* Parses the SAVE or XSAVE command
   and returns the parsed transformation. */
static struct save_trns *
cmd_save_internal (void)
{
  struct file_handle *fh;
  struct dictionary *dict = NULL;
  struct save_trns *t = NULL;
  int compress = get_scompression ();

  t = xmalloc (sizeof *t);
  t->h.proc = save_trns_proc;
  t->h.free = save_trns_free;
  t->writer = NULL;
  t->map = NULL;
  case_nullify (&t->bounce);
  
  lex_match ('/');
  if (lex_match_id ("OUTFILE"))
    lex_match ('=');
  fh = fh_parse ();
  if (fh == NULL)
    goto error;

  dict = dict_clone (default_dict);
  start_case_map (dict);
  if (!trim_dictionary (dict, OP_SAVE, &compress))
    goto error;
  t->map = finish_case_map (dict);
  if (t->map != NULL)
    case_create (&t->bounce, dict_get_next_value_idx (dict));

  t->writer = sfm_open_writer (fh, dict, compress);
  if (t->writer == NULL)
    goto error;

  dict_destroy (dict);

  return t;

 error:
  assert (t != NULL);
  dict_destroy (dict);
  save_trns_free (&t->h);
  return NULL;
}

/* Parses and performs the SAVE procedure. */
int
cmd_save (void)
{
  struct save_trns *t = cmd_save_internal ();
  if (t != NULL) 
    {
      procedure (save_write_case_func, t);
      save_trns_free (&t->h);
      free(t);
      return CMD_SUCCESS;
    }
  else
    return CMD_FAILURE;
}

/* Parses the XSAVE transformation command. */
int
cmd_xsave (void)
{
  struct save_trns *t = cmd_save_internal ();
  if (t != NULL) 
    {
      add_transformation (&t->h);
      return CMD_SUCCESS; 
    }
  else
    return CMD_FAILURE;
}

/* Writes the given C to the file specified by T. */
static void
do_write_case (struct save_trns *t, struct ccase *c) 
{
  if (t->map == NULL)
    sfm_write_case (t->writer, c);
  else 
    {
      map_case (t->map, c, &t->bounce);
      sfm_write_case (t->writer, &t->bounce);
    }
}

/* Writes case C to the system file specified on SAVE. */
static int
save_write_case_func (struct ccase *c, void *aux UNUSED)
{
  do_write_case (aux, c);
  return 1;
}

/* Writes case C to the system file specified on XSAVE. */
static int
save_trns_proc (struct trns_header *h, struct ccase *c, int case_num UNUSED)
{
  struct save_trns *t = (struct save_trns *) h;
  do_write_case (t, c);
  return -1;
}

/* Frees a SAVE transformation. */
static void
save_trns_free (struct trns_header *t_)
{
  struct save_trns *t = (struct save_trns *) t_;

  if (t != NULL) 
    {
      sfm_close_writer (t->writer);
      destroy_case_map (t->map);
      case_destroy (&t->bounce);
    }
}

static int rename_variables (struct dictionary *dict);

/* Commands that read and write system files share a great deal
   of common syntactic structure for rearranging and dropping
   variables.  This function parses this syntax and modifies DICT
   appropriately.

   OP is the operation being performed.  For operations that
   write a system file, *COMPRESS is set to 1 if the system file
   should be compressed, 0 otherwise.
   
   Returns nonzero on success, zero on failure. */
/* FIXME: IN, FIRST, LAST, MAP. */
static int
trim_dictionary (struct dictionary *dict, enum operation op, int *compress)
{
  assert ((compress != NULL) == (op == OP_SAVE));
  if (get_scompression())
    *compress = 1;

  if (op == OP_SAVE || op == OP_EXPORT)
    {
      /* Delete all the scratch variables. */
      struct variable **v;
      size_t nv;
      size_t i;

      v = xmalloc (sizeof *v * dict_get_var_cnt (dict));
      nv = 0;
      for (i = 0; i < dict_get_var_cnt (dict); i++) 
        if (dict_class_from_id (dict_get_var (dict, i)->name) == DC_SCRATCH)
          v[nv++] = dict_get_var (dict, i);
      dict_delete_vars (dict, v, nv);
      free (v);
    }
  
  while (op == OP_MATCH || lex_match ('/'))
    {
      if (op == OP_SAVE && lex_match_id ("COMPRESSED"))
	*compress = 1;
      else if (op == OP_SAVE && lex_match_id ("UNCOMPRESSED"))
	*compress = 0;
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

      if (op == OP_MATCH)
        goto success;
    }

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }

 success:
  if (op != OP_MATCH)
    dict_compact_values (dict);
  return 1;
}

/* Parses and performs the RENAME subcommand of GET and SAVE. */
static int
rename_variables (struct dictionary *dict)
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

/* EXPORT procedure. */
struct export_proc 
  {
    struct pfm_writer *writer;  /* System file writer. */
    struct case_map *map;       /* Map from active file to system file dict. */
    struct ccase bounce;        /* Bounce buffer. */
  };

static int export_write_case_func (struct ccase *, void *);
static void export_proc_free (struct export_proc *);
     
/* Parses the EXPORT command.  */
/* FIXME: same as cmd_save_internal(). */
int
cmd_export (void)
{
  struct file_handle *fh;
  struct dictionary *dict;
  struct export_proc *proc;

  proc = xmalloc (sizeof *proc);
  proc->writer = NULL;
  proc->map = NULL;
  case_nullify (&proc->bounce);

  lex_match ('/');
  if (lex_match_id ("OUTFILE"))
    lex_match ('=');
  fh = fh_parse ();
  if (fh == NULL)
    return CMD_FAILURE;

  dict = dict_clone (default_dict);
  start_case_map (dict);
  if (!trim_dictionary (dict, OP_EXPORT, NULL))
    goto error;
  proc->map = finish_case_map (dict);
  if (proc->map != NULL)
    case_create (&proc->bounce, dict_get_next_value_idx (dict));

  proc->writer = pfm_open_writer (fh, dict);
  if (proc->writer == NULL)
    goto error;
  
  dict_destroy (dict);

  procedure (export_write_case_func, proc);
  export_proc_free (proc);
  free (proc);

  return CMD_SUCCESS;

 error:
  dict_destroy (dict);
  export_proc_free (proc);
  free (proc);
  return CMD_FAILURE;
}

/* Writes case C to the EXPORT file. */
static int
export_write_case_func (struct ccase *c, void *aux) 
{
  struct export_proc *proc = aux;
  if (proc->map == NULL)
    pfm_write_case (proc->writer, c);
  else 
    {
      map_case (proc->map, c, &proc->bounce);
      pfm_write_case (proc->writer, &proc->bounce);
    }
  return 1;
}

static void
export_proc_free (struct export_proc *proc) 
{
  if (proc != NULL) 
    {
      pfm_close_writer (proc->writer);
      destroy_case_map (proc->map);
      case_destroy (&proc->bounce);
    }
}

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
    struct file_handle *handle; /* File handle. */
    struct sfm_reader *reader;  /* System file reader. */
    struct dictionary *dict;	/* Dictionary from system file. */
    char in[9];			/* Name of the variable from IN=. */
    char first[9], last[9];	/* Name of the variables from FIRST=, LAST=. */
    struct ccase input;         /* Input record. */
  };

/* MATCH FILES procedure. */
struct mtf_proc 
  {
    struct mtf_file *head;      /* First file mentioned on FILE or TABLE. */
    struct mtf_file *tail;      /* Last file mentioned on FILE or TABLE. */
    
    struct variable **by;       /* Variables on the BY subcommand. */
    size_t by_cnt;              /* Number of variables on BY subcommand. */

    struct dictionary *dict;    /* Dictionary of output file. */
    struct case_sink *sink;     /* Sink to receive output. */
    struct ccase *mtf_case;     /* Case used for output. */

    unsigned seq_num;           /* Have we initialized this variable? */
    unsigned *seq_nums;         /* Sequence numbers for each var in dict. */
  };

static void mtf_free (struct mtf_proc *);
static void mtf_free_file (struct mtf_file *);
static int mtf_merge_dictionary (struct dictionary *const, struct mtf_file *);
static void mtf_delete_file_in_place (struct mtf_proc *, struct mtf_file **);

static void mtf_read_nonactive_records (void *);
static void mtf_processing_finish (void *);
static int mtf_processing (struct ccase *, void *);

static char *var_type_description (struct variable *);

static void set_master (struct variable *, struct variable *master);
static struct variable *get_master (struct variable *);

/* Parse and execute the MATCH FILES command. */
int
cmd_match_files (void)
{
  struct mtf_proc mtf;
  struct mtf_file *first_table = NULL;
  struct mtf_file *iter;
  
  int seen = 0;
  
  mtf.head = mtf.tail = NULL;
  mtf.by = NULL;
  mtf.by_cnt = 0;
  mtf.dict = dict_create ();
  mtf.sink = NULL;
  mtf.mtf_case = NULL;
  mtf.seq_num = 0;
  mtf.seq_nums = NULL;
  dict_set_case_limit (mtf.dict, dict_get_case_limit (default_dict));
  
  do
    {
      lex_match ('/');

      if (lex_match (T_BY))
	{
	  if (seen & 1)
	    {
	      msg (SE, _("The BY subcommand may be given once at most."));
	      goto error;
	    }
	  seen |= 1;
	      
	  lex_match ('=');
	  if (!parse_variables (mtf.dict, &mtf.by, &mtf.by_cnt,
				PV_NO_DUPLICATE | PV_NO_SCRATCH))
	    goto error;
	}
      else if (token != T_ID)
	{
	  lex_error (NULL);
	  goto error;
	}
      else if (lex_id_match ("FILE", tokid) || lex_id_match ("TABLE", tokid))
	{
	  struct mtf_file *file = xmalloc (sizeof *file);

	  if (lex_match_id ("FILE"))
	    file->type = MTF_FILE;
	  else if (lex_match_id ("TABLE"))
	    {
	      file->type = MTF_TABLE;
	      seen |= 4;
	    }
	  else
	    assert (0);

	  file->by = NULL;
          file->handle = NULL;
          file->reader = NULL;
	  file->dict = NULL;
	  file->in[0] = '\0';
          file->first[0] = '\0';
          file->last[0] = '\0';
          case_nullify (&file->input);

	  /* FILEs go first, then TABLEs. */
	  if (file->type == MTF_TABLE || first_table == NULL)
	    {
	      file->next = NULL;
	      file->prev = mtf.tail;
	      if (mtf.tail)
		mtf.tail->next = file;
	      mtf.tail = file;
	      if (mtf.head == NULL)
		mtf.head = file;
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
		mtf.head = file;
	      first_table->prev = file;
	    }
	  
	  lex_match ('=');
	  
	  if (lex_match ('*'))
	    {
              file->handle = NULL;
	      file->reader = NULL;
              
	      if (seen & 2)
		{
		  msg (SE, _("The active file may not be specified more "
			     "than once."));
		  goto error;
		}
	      seen |= 2;

	      assert (pgm_state != STATE_INPUT);
	      if (pgm_state == STATE_INIT)
		{
		  msg (SE, _("Cannot specify the active file since no active "
			     "file has been defined."));
		  goto error;
		}

              if (temporary != 0)
                {
                  msg (SE,
                       _("MATCH FILES may not be used after TEMPORARY when "
                         "the active file is an input source.  "
                         "Temporary transformations will be made permanent."));
                  cancel_temporary (); 
                }

              file->dict = default_dict;
	    }
	  else
	    {
              file->handle = fh_parse ();
	      if (file->handle == NULL)
		goto error;

              file->reader = sfm_open_reader (file->handle, &file->dict, NULL);
              if (file->reader == NULL)
                goto error;

              case_create (&file->input, dict_get_next_value_idx (file->dict));
	    }
	}
      else if (lex_id_match ("IN", tokid)
	       || lex_id_match ("FIRST", tokid)
	       || lex_id_match ("LAST", tokid))
	{
	  const char *sbc;
	  char *name;
	  
	  if (mtf.tail == NULL)
	    {
	      msg (SE, _("IN, FIRST, and LAST subcommands may not occur "
			 "before the first FILE or TABLE."));
	      goto error;
	    }

	  if (lex_match_id ("IN"))
	    {
	      name = mtf.tail->in;
	      sbc = "IN";
	    }
	  else if (lex_match_id ("FIRST"))
	    {
	      name = mtf.tail->first;
	      sbc = "FIRST";
	    }
	  else if (lex_match_id ("LAST"))
	    {
	      name = mtf.tail->last;
	      sbc = "LAST";
	    }
	  else 
            {
              assert (0);
              abort ();
            }

	  lex_match ('=');
	  if (token != T_ID)
	    {
	      lex_error (NULL);
	      goto error;
	    }

	  if (*name)
	    {
	      msg (SE, _("Multiple %s subcommands for a single FILE or "
			 "TABLE."),
		   sbc);
	      goto error;
	    }
	  strcpy (name, tokid);
	  lex_get ();

	  if (!dict_create_var (mtf.dict, name, 0))
	    {
	      msg (SE, _("Duplicate variable name %s while creating %s "
			 "variable."),
		   name, sbc);
	      goto error;
	    }
	}
      else if (lex_id_match ("RENAME", tokid)
	       || lex_id_match ("KEEP", tokid)
	       || lex_id_match ("DROP", tokid))
	{
	  if (mtf.tail == NULL)
	    {
	      msg (SE, _("RENAME, KEEP, and DROP subcommands may not occur "
			 "before the first FILE or TABLE."));
	      goto error;
	    }

	  if (!trim_dictionary (mtf.tail->dict, OP_MATCH, NULL))
	    goto error;
	}
      else if (lex_match_id ("MAP"))
	{
	  /* FIXME. */
	}
      else
	{
	  lex_error (NULL);
	  goto error;
	}
    }
  while (token != '.');

  for (iter = mtf.head; iter != NULL; iter = iter->next) 
    mtf_merge_dictionary (mtf.dict, iter);

  if (seen & 4)
    {
      if (!(seen & 1))
	{
	  msg (SE, _("The BY subcommand is required when a TABLE subcommand "
		     "is given."));
	  goto error;
	}
    }

  if (seen & 1)
    {
      for (iter = mtf.head; iter != NULL; iter = iter->next)
	{
	  int i;
	  
	  iter->by = xmalloc (sizeof *iter->by * mtf.by_cnt);

	  for (i = 0; i < mtf.by_cnt; i++)
	    {
	      iter->by[i] = dict_lookup_var (iter->dict, mtf.by[i]->name);
	      if (iter->by[i] == NULL)
		{
		  msg (SE, _("File %s lacks BY variable %s."),
		       iter->handle ? handle_get_name (iter->handle) : "*",
		       mtf.by[i]->name);
		  goto error;
		}
	    }
	}
    }

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
     values. */

  if (!(seen & 2))
    discard_variables ();

  mtf.sink = create_case_sink (&storage_sink_class, mtf.dict, NULL);

  mtf.seq_nums = xmalloc (dict_get_var_cnt (mtf.dict)
                          * sizeof *mtf.seq_nums);
  memset (mtf.seq_nums, 0,
          dict_get_var_cnt (mtf.dict) * sizeof *mtf.seq_nums);
  mtf.mtf_case = xmalloc (dict_get_case_size (mtf.dict));

  mtf_read_nonactive_records (NULL);
  if (seen & 2)
    procedure (mtf_processing, NULL);
  mtf_processing_finish (NULL);

  dict_destroy (default_dict);
  default_dict = mtf.dict;
  mtf.dict = NULL;
  vfm_source = mtf.sink->class->make_source (mtf.sink);
  free_case_sink (mtf.sink);
  
  mtf_free (&mtf);
  return CMD_SUCCESS;
  
error:
  mtf_free (&mtf);
  return CMD_FAILURE;
}

/* Repeats 2...8 an arbitrary number of times. */
static void
mtf_processing_finish (void *mtf_)
{
  struct mtf_proc *mtf = mtf_;
  struct mtf_file *iter;

  /* Find the active file and delete it. */
  for (iter = mtf->head; iter; iter = iter->next)
    if (iter->handle == NULL)
      {
        mtf_delete_file_in_place (mtf, &iter);
        break;
      }
  
  while (mtf->head && mtf->head->type == MTF_FILE)
    if (!mtf_processing (NULL, mtf))
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
  free (file->by);
  sfm_close_reader (file->reader);
  if (file->dict != default_dict)
    dict_destroy (file->dict);
  case_destroy (&file->input);
  free (file);
}

/* Free all the data for the MATCH FILES procedure. */
static void
mtf_free (struct mtf_proc *mtf)
{
  struct mtf_file *iter, *next;

  for (iter = mtf->head; iter; iter = next)
    {
      next = iter->next;

      mtf_free_file (iter);
    }
  
  free (mtf->by);
  if (mtf->dict)
    dict_destroy (mtf->dict);
  free (mtf->seq_nums);
}

/* Remove *FILE from the mtf_file chain.  Make *FILE point to the next
   file in the chain, or to NULL if was the last in the chain. */
static void
mtf_delete_file_in_place (struct mtf_proc *mtf, struct mtf_file **file)
{
  struct mtf_file *f = *file;

  if (f->prev)
    f->prev->next = f->next;
  if (f->next)
    f->next->prev = f->prev;
  if (f == mtf->head)
    mtf->head = f->next;
  if (f == mtf->tail)
    mtf->tail = f->prev;
  *file = f->next;

  {
    int i;

    for (i = 0; i < dict_get_var_cnt (f->dict); i++)
      {
	struct variable *v = dict_get_var (f->dict, i);
        union value *out = case_data_rw (mtf->mtf_case, get_master (v)->fv);
	  
	if (v->type == NUMERIC)
          out->f = SYSMIS;
	else
	  memset (out->s, ' ', v->width);
      }
  }

  mtf_free_file (f);
}

/* Read a record from every input file except the active file. */
static void
mtf_read_nonactive_records (void *mtf_ UNUSED)
{
  struct mtf_proc *mtf = mtf_;
  struct mtf_file *iter;

  for (iter = mtf->head; iter; )
    {
      if (iter->handle)
	{
	  if (!sfm_read_case (iter->reader, &iter->input))
	    mtf_delete_file_in_place (mtf, &iter);
	  else
	    iter = iter->next;
	}
      else
        iter = iter->next;
    }
}

/* Compare the BY variables for files A and B; return -1 if A < B, 0
   if A == B, 1 if A > B. */
static inline int
mtf_compare_BY_values (struct mtf_proc *mtf,
                       struct mtf_file *a, struct mtf_file *b,
                       struct ccase *c)
{
  struct ccase *a_input, *b_input;
  int i;

  assert ((a == NULL) + (b == NULL) + (c == NULL) <= 1);
  a_input = case_is_null (&a->input) ? c : &a->input;
  b_input = case_is_null (&b->input) ? c : &b->input;
  for (i = 0; i < mtf->by_cnt; i++)
    {
      assert (a->by[i]->type == b->by[i]->type);
      assert (a->by[i]->width == b->by[i]->width);
      
      if (a->by[i]->type == NUMERIC)
	{
	  double af = case_num (a_input, a->by[i]->fv);
	  double bf = case_num (b_input, b->by[i]->fv);

	  if (af < bf)
	    return -1;
	  else if (af > bf)
	    return 1;
	}
      else 
	{
	  int result;
	  
	  assert (a->by[i]->type == ALPHA);
	  result = memcmp (case_str (a_input, a->by[i]->fv),
			   case_str (b_input, b->by[i]->fv),
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
mtf_processing (struct ccase *c, void *mtf_ UNUSED)
{
  struct mtf_proc *mtf = mtf_;
  struct mtf_file *min_head, *min_tail; /* Files with minimum BY values. */
  struct mtf_file *max_head, *max_tail; /* Files with non-minimum BY values. */
  struct mtf_file *iter;                /* Iterator. */

  for (;;)
    {
      /* If the active file doesn't have the minimum BY values, don't
	 return because that would cause a record to be skipped. */
      int advance = 1;

      if (mtf->head->type == MTF_TABLE)
	return 0;
      
      /* 3. Find the FILE input record with minimum BY values.  Store
	 all the values from this input record into the output record.

	 4. Find all the FILE input records with BY values identical
	 to the minimums.  Store all the values from these input
	 records into the output record. */
      min_head = min_tail = mtf->head;
      max_head = max_tail = NULL;
      for (iter = mtf->head->next; iter && iter->type == MTF_FILE;
	   iter = iter->next)
	switch (mtf_compare_BY_values (mtf, min_head, iter, c))
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
	  switch (mtf_compare_BY_values (mtf, min_head, iter, c))
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
	      if (sfm_read_case (iter->reader, &iter->input))
		goto again;
	      mtf_delete_file_in_place (mtf, &iter);
	      break;

	    default:
	      assert (0);
	    }

	  iter = next;
	}

      /* Next sequence number. */
      mtf->seq_num++;
  
      /* Store data to all the records we are using. */
      if (min_tail)
	min_tail->next_min = NULL;
      for (iter = min_head; iter; iter = iter->next_min)
	{
	  int i;

	  for (i = 0; i < dict_get_var_cnt (iter->dict); i++)
	    {
	      struct variable *v = dict_get_var (iter->dict, i);
              struct ccase *record;
              union value *out;
	  
	      if (mtf->seq_nums[get_master (v)->index] == mtf->seq_num)
		continue;
              mtf->seq_nums[get_master (v)->index] = mtf->seq_num;

              record = case_is_null (&iter->input) ? c : &iter->input;

              assert (v->type == NUMERIC || v->type == ALPHA);
              out = case_data_rw (mtf->mtf_case, get_master (v)->fv);
	      if (v->type == NUMERIC)
		out->f = case_num (record, v->fv);
	      else
                memcpy (out->s, case_str (record, v->fv), v->width);
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
              union value *out;
	  
	      if (mtf->seq_nums[get_master (v)->index] == mtf->seq_num)
		continue;
              mtf->seq_nums[get_master (v)->index] = mtf->seq_num;

              out = case_data_rw (mtf->mtf_case, get_master (v)->fv);
	      if (v->type == NUMERIC)
                out->f = SYSMIS;
	      else
                memset (out->s, ' ', v->width);
	    }

	  if (iter->handle == NULL)
	    advance = 0;
	}

      /* 6. Write the output record. */
      mtf->sink->class->write (mtf->sink, mtf->mtf_case);

      /* 7. Read another record from each input file FILE and TABLE
	 that we stored values from above.  If we come to the end of
	 one of the input files, remove it from the list of input
	 files. */
      for (iter = min_head; iter && iter->type == MTF_FILE; )
	{
	  struct mtf_file *next = iter->next_min;
	  
	  if (iter->reader != NULL)
	    {
	      if (!sfm_read_case (iter->reader, &iter->input))
		mtf_delete_file_in_place (mtf, &iter);
	    }

	  iter = next;
	}
      
      if (advance)
	break;
    }

  return (mtf->head && mtf->head->type != MTF_TABLE);
}

/* Merge the dictionary for file F into master dictionary M. */
static int
mtf_merge_dictionary (struct dictionary *const m, struct mtf_file *f)
{
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
		 dv->name, handle_get_name (f->handle),
		 var_type_description (dv), var_type_description (mv));
	    return 0;
	  }
        set_master (dv, mv);
      }
  }

  return 1;
}

/* Marks V's master variable as MASTER. */
static void
set_master (struct variable *v, struct variable *master) 
{
  var_attach_aux (v, master, NULL);
}

/* Returns the master variable corresponding to V,
   as set with set_master(). */
static struct variable *
get_master (struct variable *v) 
{
  assert (v->aux != NULL);
  return v->aux;
}

/* IMPORT command. */

/* IMPORT input program. */
struct import_pgm 
  {
    struct pfm_reader *reader;  /* Portable file reader. */
    struct case_map *map;       /* Map from system file to active file dict. */
    struct ccase bounce;        /* Bounce buffer. */
  };

static void import_pgm_free (struct import_pgm *);

/* Parses the IMPORT command. */
int
cmd_import (void)
{
  struct import_pgm *pgm = NULL;
  struct file_handle *fh = NULL;
  struct dictionary *dict = NULL;
  int type;

  pgm = xmalloc (sizeof *pgm);
  pgm->reader = NULL;
  pgm->map = NULL;
  case_nullify (&pgm->bounce);

  for (;;)
    {
      lex_match ('/');
      
      if (lex_match_id ("FILE") || token == T_STRING)
	{
	  lex_match ('=');

	  fh = fh_parse ();
	  if (fh == NULL)
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

  pgm->reader = pfm_open_reader (fh, &dict, NULL);
  if (pgm->reader == NULL)
    return CMD_FAILURE;
  case_create (&pgm->bounce, dict_get_next_value_idx (dict));
  
  start_case_map (dict);
  if (!trim_dictionary (dict, OP_READ, NULL))
    goto error;
  pgm->map = finish_case_map (dict);
  
  dict_destroy (default_dict);
  default_dict = dict;

  vfm_source = create_case_source (&import_source_class, pgm);

  return CMD_SUCCESS;

 error:
  import_pgm_free (pgm);
  if (dict != NULL)
    dict_destroy (dict);
  return CMD_FAILURE;
}

/* Frees a struct import_pgm. */
static void
import_pgm_free (struct import_pgm *pgm) 
{
  if (pgm != NULL) 
    {
      pfm_close_reader (pgm->reader);
      destroy_case_map (pgm->map);
      case_destroy (&pgm->bounce);
      free (pgm);
    }
}

/* Clears internal state related to IMPORT input procedure. */
static void
import_source_destroy (struct case_source *source)
{
  struct import_pgm *pgm = source->aux;
  import_pgm_free (pgm);
}

/* Reads all the cases from the data file into C and passes them
   to WRITE_CASE one by one, passing WC_DATA. */
static void
import_source_read (struct case_source *source,
                 struct ccase *c,
                 write_case_func *write_case, write_case_data wc_data)
{
  struct import_pgm *pgm = source->aux;
  int ok;

  do
    {
      if (pgm->map == NULL)
        ok = pfm_read_case (pgm->reader, c);
      else
        {
          ok = pfm_read_case (pgm->reader, &pgm->bounce);
          if (ok)
            map_case (pgm->map, &pgm->bounce, c);
        }

      if (ok)
        ok = write_case (wc_data);
    }
  while (ok);
}

const struct case_source_class import_source_class =
  {
    "IMPORT",
    NULL,
    import_source_read,
    import_source_destroy,
  };


/* Case map.

   A case map copies data from a case that corresponds for one
   dictionary to a case that corresponds to a second dictionary
   derived from the first by, optionally, deleting, reordering,
   or renaming variables.  (No new variables may be created.)
   */

/* A case map. */
struct case_map
  {
    size_t value_cnt;   /* Number of values in map. */
    int *map;           /* For each destination index, the
                           corresponding source index. */
  };

/* Prepares dictionary D for producing a case map.  Afterward,
   the caller may delete, reorder, or rename variables within D
   at will before using finish_case_map() to produce the case
   map.

   Uses D's aux members, which may not otherwise be in use. */
static void
start_case_map (struct dictionary *d) 
{
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int *src_fv = xmalloc (sizeof *src_fv);
      *src_fv = v->fv;
      var_attach_aux (v, src_fv, var_dtor_free);
    }
}

/* Produces a case map from dictionary D, which must have been
   previously prepared with start_case_map().

   Does not retain any reference to D, and clears the aux members
   set up by start_case_map().

   Returns the new case map, or a null pointer if no mapping is
   required (that is, no data has changed position). */
static struct case_map *
finish_case_map (struct dictionary *d) 
{
  struct case_map *map;
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  int identity_map;

  map = xmalloc (sizeof *map);
  map->value_cnt = dict_get_next_value_idx (d);
  map->map = xmalloc (sizeof *map->map * map->value_cnt);
  for (i = 0; i < map->value_cnt; i++)
    map->map[i] = -1;

  identity_map = 1;
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (d, i);
      int *src_fv = (int *) var_detach_aux (v);
      size_t idx;

      if (v->fv != *src_fv)
        identity_map = 0;
      
      for (idx = 0; idx < v->nv; idx++)
        {
          int src_idx = *src_fv + idx;
          int dst_idx = v->fv + idx;
          
          assert (map->map[dst_idx] == -1);
          map->map[dst_idx] = src_idx;
        }
      free (src_fv);
    }

  if (identity_map) 
    {
      destroy_case_map (map);
      return NULL;
    }

  while (map->value_cnt > 0 && map->map[map->value_cnt - 1] == -1)
    map->value_cnt--;

  return map;
}

/* Maps from SRC to DST, applying case map MAP. */
static void
map_case (const struct case_map *map,
          const struct ccase *src, struct ccase *dst) 
{
  size_t dst_idx;

  assert (map != NULL);
  assert (src != NULL);
  assert (dst != NULL);
  assert (src != dst);

  for (dst_idx = 0; dst_idx < map->value_cnt; dst_idx++)
    {
      int src_idx = map->map[dst_idx];
      if (src_idx != -1)
        *case_data_rw (dst, dst_idx) = *case_data (src, src_idx);
    }
}

/* Destroys case map MAP. */
static void
destroy_case_map (struct case_map *map) 
{
  if (map != NULL) 
    {
      free (map->map);
      free (map);
    }
}
