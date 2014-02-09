/* PSPP - computes sample statistics.
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */


#include <config.h>

/* The Gnulib "strftime" module defines my_strftime in <config.h> for use by
   gl/strftime.c.  Perl also defines my_strftime in embed.h for some other
   purpose.  The former definition doesn't matter in this file, so suppress it
   to avoid a compiler warning. */
#undef my_strftime

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "minmax.h"
#include <libpspp/message.h>
#include <libpspp/version.h>
#include <libpspp/i18n.h>
#include <gl/xalloc.h>
#include <data/dictionary.h>
#include <data/case.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/variable.h>
#include <data/attributes.h>
#include <data/file-handle-def.h>
#include <data/identifier.h>
#include <data/settings.h>
#include <data/sys-file-writer.h>
#include <data/sys-file-reader.h>
#include <data/value.h>
#include <data/vardict.h>
#include <data/value-labels.h>
#include <data/format.h>
#include <data/data-in.h>
#include <data/data-out.h>
#include <string.h>

typedef struct fmt_spec input_format ;
typedef struct fmt_spec output_format ;


/*  A thin wrapper around sfm_writer */
struct syswriter_info
{
  bool opened;

  /* A pointer to the writer. The writer is owned by the struct */
  struct casewriter *writer;

  /* A pointer to the dictionary. Owned externally */
  const struct pspp_dict *dict;

  /* The scalar containing the dictionary */
  SV *dict_sv;
};


/*  A thin wrapper around sfm_reader */
struct sysreader_info
{
  struct sfm_read_info opts;

  /* A pointer to the reader. The reader is owned by the struct */
  struct casereader *reader;

  /* A pointer to the dictionary. */
  struct pspp_dict *dict;
};


struct input_format {
  struct hmap_node hmap_node;   /* In struct pspp_dict's input_formats map. */
  const struct variable *var;
  struct fmt_spec input_format;
};

/* A thin wrapper around struct dictionary.*/
struct pspp_dict {
  struct dictionary *dict;
  struct hmap input_formats;	/* Contains struct input_format. */
};


/*  A message handler which writes messages to PSPP::errstr */
static void
message_handler (const struct msg *m, void *aux)
{
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, m->text);
}

static int
sysfile_close (struct syswriter_info *swi)
{
  int retval ;
  if ( ! swi->opened )
    return 0;

  retval = casewriter_destroy (swi->writer);
  if (retval > 0 )
    swi->opened = false;

  return retval;
}

static void
scalar_to_value (union value *val, SV *scalar, const struct variable *var)
{
  if ( var_is_numeric (var))
    {
	if ( SvNOK (scalar) || SvIOK (scalar) )
	   val->f = SvNV (scalar);
	else
	   val->f = SYSMIS;
    }
  else
    {
	STRLEN len;
	const char *p = SvPV (scalar, len);
	int width = var_get_width (var);
	value_set_missing (val, width);
	memcpy (value_str_rw (val, width), p, len);
    }
}


static SV *
value_to_scalar (const union value *val, const struct variable *var)
{
  if ( var_is_numeric (var))
    {
      if ( var_is_value_missing (var, val, MV_SYSTEM))
	return newSVpvn ("", 0);

      return newSVnv (val->f);
    }
  else
    {
      int width = var_get_width (var);
      return newSVpvn (value_str (val, width), width);
    }
}


static void
make_value_from_scalar (union value *uv, SV *val, const struct variable *var)
{
 value_init (uv, var_get_width (var));
 scalar_to_value (uv, val, var);
}

static struct pspp_dict *
create_pspp_dict (struct dictionary *dict)
{
  struct pspp_dict *pspp_dict = xmalloc (sizeof *pspp_dict);
  pspp_dict->dict = dict;
  hmap_init (&pspp_dict->input_formats);
  return pspp_dict;
}

static const struct fmt_spec *
find_input_format (const struct pspp_dict *dict, const struct variable *var)
{
  struct input_format *input_format;

  HMAP_FOR_EACH_IN_BUCKET (input_format, struct input_format, hmap_node,
                           hash_pointer (var, 0), &dict->input_formats)
    if (input_format->var == var)
      return &input_format->input_format;

  return NULL;
}


MODULE = PSPP

MODULE = PSPP		PACKAGE = PSPP

PROTOTYPES: ENABLE

void
onBoot (ver)
 const char *ver
CODE:
 /* Check that the version is correct up to the length of 'ver'.
    This allows PSPP autobuilders to add a "-build#" suffix to the
    PSPP version without causing failures here. */
 assert (0 == strncmp (ver, bare_version, strlen (ver)));

 i18n_init ();
 msg_set_handler (message_handler, NULL);
 settings_init ();
 fh_init ();

SV *
format_value (val, var)
 SV *val
 struct variable *var
CODE:
 SV *ret;
 const struct fmt_spec *fmt = var_get_print_format (var);
 union value uv;
 char *s;
 make_value_from_scalar (&uv, val, var);
 s = data_out (&uv, var_get_encoding (var), fmt);
 value_destroy (&uv, var_get_width (var));
 ret = newSVpv (s, fmt->w);
 free (s);
 RETVAL = ret;
 OUTPUT:
RETVAL


int
value_is_missing (val, var)
 SV *val
 struct variable *var
CODE:
 union value uv;
 int ret;
 make_value_from_scalar (&uv, val, var);
 ret = var_is_value_missing (var, &uv, MV_ANY);
 value_destroy (&uv, var_get_width (var));
 RETVAL = ret;
 OUTPUT:
RETVAL



MODULE = PSPP		PACKAGE = PSPP::Dict

struct pspp_dict *
pxs_dict_new()
CODE:
 RETVAL = create_pspp_dict (dict_create ("UTF-8"));
OUTPUT:
 RETVAL


void
DESTROY (dict)
 struct pspp_dict *dict
CODE:
 if (dict != NULL)
   {
     struct input_format *input_format, *next_input_format;

     HMAP_FOR_EACH_SAFE (input_format, next_input_format,
			 struct input_format, hmap_node, &dict->input_formats)
       {
         hmap_delete (&dict->input_formats, &input_format->hmap_node);
	 free (input_format);
       }
     hmap_destroy (&dict->input_formats);
     dict_destroy (dict->dict);
     free (dict);
   }

int
get_var_cnt (dict)
 struct pspp_dict *dict
CODE:
 RETVAL = dict_get_var_cnt (dict->dict);
OUTPUT:
RETVAL

void
set_label (dict, label)
 struct pspp_dict *dict
 char *label
CODE:
 dict_set_label (dict->dict, label);

void
set_documents (dict, docs)
 struct pspp_dict *dict
 char *docs
CODE:
 dict_set_documents_string (dict->dict, docs);


void
add_document (dict, doc)
 struct pspp_dict *dict
 char *doc
CODE:
 dict_add_document_line (dict->dict, doc, false);


void
clear_documents (dict)
 struct pspp_dict *dict
CODE:
 dict_clear_documents (dict->dict);


void
set_weight (dict, var)
 struct pspp_dict *dict
 struct variable *var
CODE:
 dict_set_weight (dict->dict, var);


struct variable *
pxs_get_variable (dict, idx)
 struct pspp_dict *dict
 SV *idx
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
 if ( SvIV (idx) >= dict_get_var_cnt (dict->dict))
  {
    sv_setpv (errstr, "The dictionary doesn't have that many variables.");
    XSRETURN_UNDEF;
  }
CODE:
 RETVAL = dict_get_var (dict->dict, SvIV (idx));
 OUTPUT:
RETVAL


struct variable *
pxs_get_var_by_name (dict, name)
 struct pspp_dict *dict
 const char *name
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
CODE:
 struct variable *var = dict_lookup_var (dict->dict, name);
 if ( ! var )
      sv_setpv (errstr, "No such variable.");
 RETVAL = var;
 OUTPUT:
RETVAL


MODULE = PSPP		PACKAGE = PSPP::Var


struct variable *
pxs_dict_create_var (dict, name, ip_fmt)
 struct pspp_dict * dict
 char *name
 input_format ip_fmt
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
 if ( ! id_is_plausible (name, false))
  {
    sv_setpv (errstr, "The variable name is not valid.");
    XSRETURN_UNDEF;
  }
CODE:
 struct fmt_spec op_fmt;
 struct input_format *input_format;

 struct variable *v;
 op_fmt = fmt_for_output_from_input (&ip_fmt);
 v = dict_create_var (dict->dict, name,
	fmt_is_string (op_fmt.type) ? op_fmt.w : 0);
 if ( NULL == v )
  {
    sv_setpv (errstr, "The variable could not be created (probably already exists).");
    XSRETURN_UNDEF;
  }
 var_set_both_formats (v, &op_fmt);

 input_format = xmalloc (sizeof *input_format);
 input_format->var = v;
 input_format->input_format = ip_fmt;
 hmap_insert (&dict->input_formats, &input_format->hmap_node,
              hash_pointer (v, 0));

 RETVAL = v;
OUTPUT:
 RETVAL


int
set_missing_values (var, v1, ...)
 struct variable *var;
 SV *v1;
INIT:
 int i;
 union value val[3];

 if ( items > 4 )
  croak ("No more than 3 missing values are permitted");

 for (i = 0; i < items - 1; ++i)
   scalar_to_value (&val[i], ST(i+1), var);
CODE:
 struct missing_values mv;
 mv_init (&mv, var_get_width (var));
 for (i = 0 ; i < items - 1; ++i )
   mv_add_value (&mv, &val[i]);
 var_set_missing_values (var, &mv);


void
set_label (var, label)
 struct variable *var;
 char *label
CODE:
  var_set_label (var, label, false);


void
clear_value_labels (var)
 struct variable *var;
CODE:
 var_clear_value_labels (var);

SV *
get_write_format (var)
 struct variable *var
CODE:
 HV *fmthash = (HV *) sv_2mortal ((SV *) newHV());
 const struct fmt_spec *fmt = var_get_write_format (var);

 hv_store (fmthash, "fmt", 3, newSVnv (fmt->type), 0);
 hv_store (fmthash, "decimals", 8, newSVnv (fmt->d), 0);
 hv_store (fmthash, "width", 5, newSVnv (fmt->w), 0);

 RETVAL = newRV ((SV *) fmthash);
 OUTPUT:
RETVAL

SV *
get_print_format (var)
 struct variable *var
CODE:
 HV *fmthash = (HV *) sv_2mortal ((SV *) newHV());
 const struct fmt_spec *fmt = var_get_print_format (var);

 hv_store (fmthash, "fmt", 3, newSVnv (fmt->type), 0);
 hv_store (fmthash, "decimals", 8, newSVnv (fmt->d), 0);
 hv_store (fmthash, "width", 5, newSVnv (fmt->w), 0);

 RETVAL = newRV ((SV *) fmthash);
 OUTPUT:
RETVAL


void
pxs_set_write_format (var, fmt)
 struct variable *var
 output_format fmt
CODE:
 var_set_write_format (var, &fmt);


void
pxs_set_print_format (var, fmt)
 struct variable *var
 output_format fmt
CODE:
 var_set_print_format (var, &fmt);

void
pxs_set_output_format (var, fmt)
 struct variable *var
 output_format fmt
CODE:
 var_set_both_formats (var, &fmt);


int
add_value_label (var, key, label)
 struct variable *var
 SV *key
 char *label
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
CODE:
 union value the_value;
 int width = var_get_width (var);
 int ok;

 value_init (&the_value, width);
 if ( var_is_numeric (var))
 {
  if ( ! looks_like_number (key))
    {
      sv_setpv (errstr, "Cannot add label with string key to a numeric variable");
      value_destroy (&the_value, width);
      XSRETURN_IV (0);
    }
  the_value.f = SvNV (key);
 }
 else
 {
  value_copy_str_rpad (&the_value, width, SvPV_nolen(key), ' ');
 }
 ok = var_add_value_label (var, &the_value, label);
 value_destroy (&the_value, width);
 if (!ok)
 {
   sv_setpv (errstr, "Something went wrong");
   XSRETURN_IV (0);
 }
 XSRETURN_IV (1);


SV *
get_attributes (var)
 struct variable *var
CODE:
 HV *attrhash = (HV *) sv_2mortal ((SV *) newHV());

 struct attrset *as = var_get_attributes (var);

 if ( as )
   {
     struct attrset_iterator iter;
     struct attribute *attr;

     for (attr = attrset_first (as, &iter);
	  attr;
	  attr = attrset_next (as, &iter))
       {
	 int i;
	 const char *name = attribute_get_name (attr);

	 AV *values = newAV ();

	 for (i = 0 ; i < attribute_get_n_values (attr); ++i )
	   {
	     const char *value = attribute_get_value (attr, i);
	     av_push (values, newSVpv (value, 0));
	   }

	 hv_store (attrhash, name, strlen (name),
		   newRV_noinc ((SV*) values), 0);
       }
   }

 RETVAL = newRV ((SV *) attrhash);
 OUTPUT:
RETVAL


const char *
get_name (var)
 struct variable * var
CODE:
 RETVAL = var_get_name (var);
 OUTPUT:
RETVAL


const char *
get_label (var)
 struct variable * var
CODE:
 RETVAL = var_get_label (var);
 OUTPUT:
RETVAL


SV *
get_value_labels (var)
 struct variable *var
CODE:
 HV *labelhash = (HV *) sv_2mortal ((SV *) newHV());
 const struct val_lab *vl;
 struct val_labs_iterator *viter = NULL;
 const struct val_labs *labels = var_get_value_labels (var);

 if ( labels )
   {
     for (vl = val_labs_first (labels);
	  vl;
	  vl = val_labs_next (labels, vl))
       {
	 SV *sv = value_to_scalar (&vl->value, var);
	 STRLEN len;
	 const char *s = SvPV (sv, len);
	 hv_store (labelhash, s, len, newSVpv (val_lab_get_label (vl), 0), 0);
       }
   }

 RETVAL = newRV ((SV *) labelhash);
 OUTPUT:
RETVAL



MODULE = PSPP		PACKAGE = PSPP::Sysfile


struct syswriter_info *
pxs_create_sysfile (name, dict, opts_hr)
 char *name
 struct pspp_dict *dict;
 SV *opts_hr
INIT:
 SV *dict_sv = ST(1);
 struct sfm_write_options opts;
 if (!SvROK (opts_hr))
  {
    opts = sfm_writer_default_options ();
  }
 else
  {
    HV *opt_h = (HV *) SvRV (opts_hr);
    SV** readonly = hv_fetch(opt_h, "readonly", 8, 0);
    SV** compress = hv_fetch(opt_h, "compress", 8, 0);
    SV** version = hv_fetch(opt_h, "version", 7, 0);

    opts.create_writeable = readonly ? ! SvIV (*readonly) : true;
    opts.compression = (compress && SvIV (*compress)
                        ? SFM_COMP_SIMPLE
			: SFM_COMP_NONE);
    opts.version = version ? SvIV (*version) : 3 ;
  }
CODE:
 struct file_handle *fh =
  fh_create_file (NULL, name, fh_default_properties () );
 struct syswriter_info *swi = xmalloc (sizeof (*swi));
 swi->writer = sfm_open_writer (fh, dict->dict, opts);
 swi->dict = dict;
 swi->opened = true;
 swi->dict_sv = dict_sv;
 SvREFCNT_inc (swi->dict_sv);
 
 RETVAL = swi;
 OUTPUT:
RETVAL

int
close (swi)
 struct syswriter_info *swi
CODE:
 RETVAL = sysfile_close (swi);
OUTPUT:
 RETVAL

void
DESTROY (swi)
 struct syswriter_info *swi
CODE:
 sysfile_close (swi);
 SvREFCNT_dec (swi->dict_sv);
 free (swi);

int
append_case (swi, ccase)
 struct syswriter_info *swi
 SV *ccase
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
 if ( (!SvROK(ccase)))
  {
    XSRETURN_UNDEF;
  }
CODE:
 int i = 0;
 AV *av_case = (AV*) SvRV (ccase);

 const struct variable **vv;
 size_t nv;
 struct ccase *c;
 SV *sv;

 if ( av_len (av_case) >= dict_get_var_cnt (swi->dict->dict))
   XSRETURN_UNDEF;

 c =  case_create (dict_get_proto (swi->dict->dict));

 dict_get_vars (swi->dict->dict, &vv, &nv,
                1u << DC_ORDINARY | 1u << DC_SYSTEM);

 for (sv = av_shift (av_case); SvOK (sv);  sv = av_shift (av_case))
 {
    const struct variable *v = vv[i++];
    const struct fmt_spec *ifmt = find_input_format (swi->dict, v);

    /* If an input format has been set, then use it.
       Otherwise just convert the raw value.
    */
    if ( ifmt )
      {
	struct substring ss = ss_cstr (SvPV_nolen (sv));
	char *error;
	bool ok;

	error = data_in (ss, SvUTF8(sv) ? UTF8: "iso-8859-1", ifmt->type,
 	       	         case_data_rw (c, v), var_get_width (v),
			 dict_get_encoding (swi->dict->dict));
        ok = error == NULL;
        free (error);

	if ( !ok )
	  {
	    RETVAL = 0;
	    goto finish;
	  }
      }
    else
      {
	scalar_to_value (case_data_rw (c, v), sv, v);
      }
 }

 /* The remaining variables must be sysmis or blank string */
 while (i < dict_get_var_cnt (swi->dict->dict))
 {
   const struct variable *v = vv[i++];
   union value *val = case_data_rw (c, v);
   value_set_missing (val, var_get_width (v));
 }
 casewriter_write (swi->writer, c);
 RETVAL = 1;
 finish:
 free (vv);
OUTPUT:
 RETVAL




MODULE = PSPP		PACKAGE = PSPP::Reader

struct sysreader_info *
pxs_open_sysfile (name)
 char * name
CODE:
 struct casereader *reader;
 struct sysreader_info *sri = NULL;
 struct file_handle *fh =
 	 fh_create_file (NULL, name, fh_default_properties () );
 struct dictionary *dict;
 struct sfm_reader *r;

 sri = xmalloc (sizeof (*sri));
 r = sfm_open (fh);
 if (r)
   {
     sri->reader = sfm_decode (r, NULL, &dict, &sri->opts);
     if (sri->reader)
       sri->dict = create_pspp_dict (dict);
     else
       {
	 free (sri);
	 sri = NULL;
       }
   }
 else
   {
     free (sri);
     sri = NULL;
   } 

 RETVAL = sri;
 OUTPUT:
RETVAL


struct pspp_dict *
pxs_get_dict (reader)
 struct sysreader_info *reader;
CODE:
 RETVAL = reader->dict;
 OUTPUT:
RETVAL

SV *
get_case_cnt (sfr)
 struct sysreader_info *sfr;
CODE:
 SV *ret;
 casenumber n = casereader_get_case_cnt (sfr->reader);
 if (n == CASENUMBER_MAX)
  ret = &PL_sv_undef;
 else 
  ret = newSViv (n);
 RETVAL = ret;
 OUTPUT:
RETVAL



void
get_next_case (sfr)
 struct sysreader_info *sfr;
PPCODE:
 struct ccase *c;

 if ((c = casereader_read (sfr->reader)) != NULL)
 {
  int v;

  EXTEND (SP, dict_get_var_cnt (sfr->dict->dict));
  for (v = 0; v < dict_get_var_cnt (sfr->dict->dict); ++v )
    {
      const struct variable *var = dict_get_var (sfr->dict->dict, v);
      const union value *val = case_data (c, var);

      PUSHs (sv_2mortal (value_to_scalar (val, var)));
    }

  case_unref (c);
 }
