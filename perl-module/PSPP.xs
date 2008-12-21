/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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


#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include <config.h>

#include "ppport.h"

#include "minmax.h"
#include <libpspp/message.h>
#include <libpspp/version.h>
#include <gl/xalloc.h>
#include <data/dictionary.h>
#include <data/case.h>
#include <data/variable.h>
#include <data/file-handle-def.h>
#include <data/sys-file-writer.h>
#include <data/value.h>
#include <data/format.h>
#include <data/data-in.h>
#include <string.h>

typedef struct fmt_spec input_format ;
typedef struct fmt_spec output_format ;


/*  A thin wrapper around sfm_writer */
struct sysfile_info
{
  bool opened; 

  /* A pointer to the writer. The writer is owned by the struct */
  struct casewriter *writer;

  /* A pointer to the dictionary. Owned externally */
  const struct dictionary *dict;
};


/*  A message handler which writes messages to PSPP::errstr */
static void
message_handler (const struct msg *m)
{
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, m->text);
}

static int
sysfile_close (struct sysfile_info *sfi)
{
  int retval ;
  if ( ! sfi->opened )
    return 0;

  retval = casewriter_destroy (sfi->writer);
  if (retval > 0 )
    sfi->opened = false;

  return retval;
}

static void
scalar_to_value (union value *val, SV *scalar)
{
  if ( looks_like_number (scalar))
    {
	val->f = SvNV (scalar);
    }
  else
    {
	STRLEN len;
	char *p = SvPV (scalar, len);
	memset (val->s, ' ', MAX_SHORT_STRING);
	memcpy (val->s, p, len);
    }
}


MODULE = PSPP

BOOT:
 msg_init (NULL, message_handler);
 settings_init (0, 0);
 fh_init ();


MODULE = PSPP		PACKAGE = PSPP::Dict

struct dictionary *
_dict_new()
CODE:
 RETVAL = dict_create ();
OUTPUT:
 RETVAL


void
DESTROY (dict)
 struct dictionary *dict
CODE:
 dict_destroy (dict);


void
set_label (dict, label)
 struct dictionary *dict
 char *label
CODE:
 dict_set_label (dict, label);

void
set_documents (dict, docs)
 struct dictionary *dict
 char *docs
CODE:
 dict_set_documents (dict, docs);


void
add_document (dict, doc)
 struct dictionary *dict
 char *doc
CODE:
 dict_add_document_line (dict, doc);


void
clear_documents (dict)
 struct dictionary *dict
CODE:
 dict_clear_documents (dict);


void
set_weight (dict, var)
 struct dictionary *dict
 struct variable *var
CODE:
 dict_set_weight (dict, var);



MODULE = PSPP		PACKAGE = PSPP::Var

struct variable *
_dict_create_var (dict, name, ip_fmt)
 struct dictionary * dict
 char *name
 input_format ip_fmt
INIT:
 SV *errstr = get_sv("PSPP::errstr", TRUE);
 sv_setpv (errstr, "");
 if ( ! var_is_plausible_name (name, false))
  {
    sv_setpv (errstr, "The variable name is not valid.");
    XSRETURN_UNDEF;
  }
CODE:
 struct fmt_spec op_fmt;
 struct fmt_spec *if_copy;
 struct variable *v;
 op_fmt = fmt_for_output_from_input (&ip_fmt);
 v = dict_create_var (dict, name,
	fmt_is_string (op_fmt.type) ? op_fmt.w : 0);
 if ( NULL == v )
  {
    sv_setpv (errstr, "The variable could not be created (probably already exists).");
    XSRETURN_UNDEF;
  }
 var_set_both_formats (v, &op_fmt);
 if_copy = malloc (sizeof (*if_copy));
 memcpy (if_copy, &ip_fmt, sizeof (ip_fmt));
 var_attach_aux (v, if_copy, var_dtor_free);
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
   scalar_to_value (&val[i], ST(i+1));
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
  var_set_label (var, label);
 

void
clear_value_labels (var)
 struct variable *var;
CODE:
 var_clear_value_labels (var);

void
_set_write_format (var, fmt)
 struct variable *var
 output_format fmt
CODE:
 var_set_write_format (var, &fmt);


void
_set_print_format (var, fmt)
 struct variable *var
 output_format fmt
CODE:
 var_set_print_format (var, &fmt);

void
_set_output_format (var, fmt)
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

 if ( var_is_numeric (var))
 {
  if ( ! looks_like_number (key))
    {
      sv_setpv (errstr, "Cannot add label with string key to a numeric variable");
      XSRETURN_IV (0);
    }
  the_value.f = SvNV (key);
 }
 else
 {
   if ( var_is_long_string (var) )
     {
      sv_setpv (errstr, "Cannot add label to a long string variable");
      XSRETURN_IV (0);
     }
  strncpy (the_value.s, SvPV_nolen(key), MAX_SHORT_STRING);
 }
 if (! var_add_value_label (var, &the_value, label) )
 {
   sv_setpv (errstr, "Something went wrong");
   XSRETURN_IV (0);
 }
 XSRETURN_IV (1);



MODULE = PSPP		PACKAGE = PSPP::Sysfile


struct sysfile_info *
_create_sysfile (name, dict, opts_hr)
 char * name
 struct dictionary * dict
 SV *opts_hr
INIT:
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
    opts.compress = compress ? SvIV (*compress) : false;
    opts.version = version ? SvIV (*version) : 3 ;
  }
CODE:
 struct file_handle *fh =
  fh_create_file (NULL, name, fh_default_properties () );
 struct sysfile_info *sfi = xmalloc (sizeof (*sfi));
 sfi->writer = sfm_open_writer (fh, dict, opts);
 sfi->dict = dict;
 sfi->opened = true;
 RETVAL = sfi;
 OUTPUT:
RETVAL

int
close (sfi)
 struct sysfile_info *sfi
CODE:
 RETVAL = sysfile_close (sfi);
OUTPUT:
 RETVAL

void
DESTROY (sfi)
 struct sysfile_info *sfi
CODE:
 sysfile_close (sfi);
 free (sfi);

int
append_case (sfi, ccase)
 struct sysfile_info *sfi
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
 struct ccase c;

 if ( av_len (av_case) >= dict_get_var_cnt (sfi->dict))
   XSRETURN_UNDEF;

 case_create (&c, dict_get_next_value_idx (sfi->dict));

 dict_get_vars (sfi->dict, &vv, &nv, 1u << DC_ORDINARY | 1u << DC_SYSTEM);

 SV *sv ;
 for (sv = av_shift (av_case); SvOK (sv);  sv = av_shift (av_case))
 {
    const struct variable *v = vv[i++];
    struct substring ss = ss_cstr (SvPV_nolen (sv));
    struct fmt_spec *ifmt = var_get_aux (v);

    if ( ! data_in (ss, LEGACY_NATIVE, ifmt->type, 0, 0, 0, case_data_rw (&c, v),
	var_get_width (v)) )
     {
	RETVAL = 0;
	goto finish;
     }
 }
 /* The remaining variables must be sysmis or blank string */
 while (i < dict_get_var_cnt (sfi->dict))
 {
   const struct variable *v = vv[i++];
   union value *val = case_data_rw (&c, v);
   if ( var_is_numeric (v))
	val->f = SYSMIS;
   else
	memset (val->s, ' ', var_get_width (v));
 }
 RETVAL = casewriter_write (sfi->writer, &c);
 finish:
 case_destroy (&c);
 free (vv);
OUTPUT:
 RETVAL
