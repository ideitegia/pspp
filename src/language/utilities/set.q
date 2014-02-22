/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <float.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "gl/vasnprintf.h"

#include "data/casereader.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/compiler.h"
#include "libpspp/copyleft.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "math/random.h"
#include "output/driver.h"
#include "output/journal.h"

#if HAVE_LIBTERMCAP
#if HAVE_TERMCAP_H
#include <termcap.h>
#else /* !HAVE_TERMCAP_H */
int tgetent (char *, const char *);
int tgetnum (const char *);
#endif /* !HAVE_TERMCAP_H */
#endif /* !HAVE_LIBTERMCAP */

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (specification)
   "SET" (stc_):
     blanks=custom;
     block=string;
     boxstring=string;
     case=size:upper/uplow;
     cca=string;
     ccb=string;
     ccc=string;
     ccd=string;
     cce=string;
     compression=compress:on/off;
     cpi=integer;
     decimal=dec:dot/comma;
     epoch=custom;
     errors=custom;
     format=custom;
     headers=headers:no/yes/blank;
     highres=hires:on/off;
     histogram=string;
     include=inc:on/off;
     journal=custom;
     log=custom;
     length=custom;
     locale=custom;
     lowres=lores:auto/on/off;
     lpi=integer;
     menus=menus:standard/extended;
     messages=custom;
     mexpand=mexp:on/off;
     miterate=integer;
     mnest=integer;
     mprint=mprint:on/off;
     mxerrs=integer;
     mxloops=integer;
     mxmemory=integer;
     mxwarns=integer;
     printback=custom;
     results=custom;
     rib=rib:msbfirst/lsbfirst/vax/native;
     rrb=rrb:native/isl/isb/idl/idb/vf/vd/vg/zs/zl;
     safer=safe:on;
     scompression=scompress:on/off;
     scripttab=string;
     seed=custom;
     tnumbers=custom;
     tvars=custom;
     tb1=string;
     tbfonts=string;
     undefined=undef:warn/nowarn;
     wib=wib:msbfirst/lsbfirst/vax/native;
     wrb=wrb:native/isl/isb/idl/idb/vf/vd/vg/zs/zl;
     width=custom;
     workspace=integer;
     xsort=xsort:yes/no.
*/

/* (headers) */

/* (declarations) */

/* (functions) */

static enum integer_format stc_to_integer_format (int stc);
static enum float_format stc_to_float_format (int stc);

int
cmd_set (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_set cmd;

  if (!parse_set (lexer, ds, &cmd, NULL))
    {
      free_set (&cmd);
      return CMD_FAILURE;
    }

  if (cmd.sbc_cca)
    settings_set_cc ( cmd.s_cca, FMT_CCA);
  if (cmd.sbc_ccb)
    settings_set_cc ( cmd.s_ccb, FMT_CCB);
  if (cmd.sbc_ccc)
    settings_set_cc ( cmd.s_ccc, FMT_CCC);
  if (cmd.sbc_ccd)
    settings_set_cc ( cmd.s_ccd, FMT_CCD);
  if (cmd.sbc_cce)
    settings_set_cc ( cmd.s_cce, FMT_CCE);

  if (cmd.sbc_decimal)
    settings_set_decimal_char (cmd.dec == STC_DOT ? '.' : ',');

  if (cmd.sbc_include)
    settings_set_include (cmd.inc == STC_ON);
  if (cmd.sbc_mxerrs)
    {
      if (cmd.n_mxerrs[0] >= 1)
        settings_set_max_messages (MSG_S_ERROR, cmd.n_mxerrs[0]);
      else
        msg (SE, _("%s must be at least 1."), "MXERRS");
    }
  if (cmd.sbc_mxloops)
    {
      if (cmd.n_mxloops[0] >= 1)
        settings_set_mxloops (cmd.n_mxloops[0]);
      else
        msg (SE, _("%s must be at least 1."), "MXLOOPS");
    }
  if (cmd.sbc_mxwarns)
    {
      if (cmd.n_mxwarns[0] >= 0)
        settings_set_max_messages (MSG_S_WARNING, cmd.n_mxwarns[0]);
      else
        msg (SE, _("%s must not be negative."), "MXWARNS");
    }
  if (cmd.sbc_rib)
    settings_set_input_integer_format (stc_to_integer_format (cmd.rib));
  if (cmd.sbc_rrb)
    settings_set_input_float_format (stc_to_float_format (cmd.rrb));
  if (cmd.sbc_safer)
    settings_set_safer_mode ();
  if (cmd.sbc_scompression)
    settings_set_scompression (cmd.scompress == STC_ON);
  if (cmd.sbc_undefined)
    settings_set_undefined (cmd.undef == STC_WARN);
  if (cmd.sbc_wib)
    settings_set_output_integer_format (stc_to_integer_format (cmd.wib));
  if (cmd.sbc_wrb)
    settings_set_output_float_format (stc_to_float_format (cmd.wrb));
  if (cmd.sbc_workspace)
    {
      if ( cmd.n_workspace[0] < 1024 && ! settings_get_testing_mode ())
	msg (SE, _("%s must be at least 1MB"), "WORKSPACE");
      else if (cmd.n_workspace[0] <= 0)
	msg (SE, _("%s must be positive"), "WORKSPACE");
      else
	settings_set_workspace (cmd.n_workspace[0] * 1024L);
    }

  if (cmd.sbc_block)
    msg (SW, _("%s is obsolete."), "BLOCK");
  if (cmd.sbc_boxstring)
    msg (SW, _("%s is obsolete."), "BOXSTRING");
  if (cmd.sbc_cpi)
    msg (SW, _("%s is obsolete."), "CPI");
  if (cmd.sbc_histogram)
    msg (SW, _("%s is obsolete."), "HISTOGRAM");
  if (cmd.sbc_lpi)
    msg (SW, _("%s is obsolete."), "LPI");
  if (cmd.sbc_menus)
    msg (SW, _("%s is obsolete."), "MENUS");
  if (cmd.sbc_xsort)
    msg (SW, _("%s is obsolete."), "XSORT");
  if (cmd.sbc_mxmemory)
    msg (SE, _("%s is obsolete."), "MXMEMORY");
  if (cmd.sbc_scripttab)
    msg (SE, _("%s is obsolete."), "SCRIPTTAB");
  if (cmd.sbc_tbfonts)
    msg (SW, _("%s is obsolete."), "TBFONTS");
  if (cmd.sbc_tb1 && cmd.s_tb1)
    msg (SW, _("%s is obsolete."), "TB1");

  if (cmd.sbc_case)
    msg (SW, _("%s is not yet implemented."), "CASE");

  if (cmd.sbc_compression)
    msg (SW, _("Active file compression is not implemented."));

  free_set (&cmd);

  return CMD_SUCCESS;
}

/* Returns the integer_format value corresponding to STC,
   which should be the value of cmd.rib or cmd.wib. */
static enum integer_format
stc_to_integer_format (int stc)
{
  return (stc == STC_MSBFIRST ? INTEGER_MSB_FIRST
          : stc == STC_LSBFIRST ? INTEGER_LSB_FIRST
          : stc == STC_VAX ? INTEGER_VAX
          : INTEGER_NATIVE);
}

/* Returns the float_format value corresponding to STC,
   which should be the value of cmd.rrb or cmd.wrb. */
static enum float_format
stc_to_float_format (int stc)
{
  switch (stc)
    {
    case STC_NATIVE:
      return FLOAT_NATIVE_DOUBLE;

    case STC_ISL:
      return FLOAT_IEEE_SINGLE_LE;
    case STC_ISB:
      return FLOAT_IEEE_SINGLE_BE;
    case STC_IDL:
      return FLOAT_IEEE_DOUBLE_LE;
    case STC_IDB:
      return FLOAT_IEEE_DOUBLE_BE;

    case STC_VF:
      return FLOAT_VAX_F;
    case STC_VD:
      return FLOAT_VAX_D;
    case STC_VG:
      return FLOAT_VAX_G;

    case STC_ZS:
      return FLOAT_Z_SHORT;
    case STC_ZL:
      return FLOAT_Z_LONG;
    }

  NOT_REACHED ();
}

static int
set_output_routing (struct lexer *lexer, enum settings_output_type type)
{
  enum settings_output_devices devices;

  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "ON") || lex_match_id (lexer, "BOTH"))
    devices = SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL;
  else if (lex_match_id (lexer, "TERMINAL"))
    devices = SETTINGS_DEVICE_TERMINAL;
  else if (lex_match_id (lexer, "LISTING"))
    devices = SETTINGS_DEVICE_LISTING;
  else if (lex_match_id (lexer, "OFF") || lex_match_id (lexer, "NONE"))
    devices = 0;
  else
    {
      lex_error (lexer, NULL);
      return 0;
    }

  settings_set_output_routing (type, devices);

  return 1;
}

/* Parses the BLANKS subcommand, which controls the value that
   completely blank fields in numeric data imply.  X, Wnd: Syntax is
   SYSMIS or a numeric value. */
static int
stc_custom_blanks (struct lexer *lexer,
		   struct dataset *ds UNUSED,
		   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "SYSMIS"))
    {
      lex_get (lexer);
      settings_set_blanks (SYSMIS);
    }
  else
    {
      if (!lex_force_num (lexer))
	return 0;
      settings_set_blanks (lex_number (lexer));
      lex_get (lexer);
    }
  return 1;
}

static int
stc_custom_tnumbers (struct lexer *lexer,
		   struct dataset *ds UNUSED,
		   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);

  if (lex_match_id (lexer, "VALUES"))
    {
      settings_set_value_style (SETTINGS_VAL_STYLE_VALUES);
    }
  else if (lex_match_id (lexer, "LABELS"))
    {
      settings_set_value_style (SETTINGS_VAL_STYLE_LABELS);
    }
  else if (lex_match_id (lexer, "BOTH"))
    {
      settings_set_value_style (SETTINGS_VAL_STYLE_BOTH);
    }
  else
    {
      lex_error_expecting (lexer, "VALUES", "LABELS", "BOTH", NULL_SENTINEL);
      return 0;
    }

  return 1;
}


static int
stc_custom_tvars (struct lexer *lexer,
                  struct dataset *ds UNUSED,
                  struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);

  if (lex_match_id (lexer, "NAMES"))
    {
      settings_set_var_style (SETTINGS_VAR_STYLE_NAMES);
    }
  else if (lex_match_id (lexer, "LABELS"))
    {
      settings_set_var_style (SETTINGS_VAR_STYLE_LABELS);
    }
  else if (lex_match_id (lexer, "BOTH"))
    {
      settings_set_var_style (SETTINGS_VAR_STYLE_BOTH);
    }
  else
    {
      lex_error_expecting (lexer, "NAMES", "LABELS", "BOTH", NULL_SENTINEL);
      return 0;
    }

  return 1;
}


/* Parses the EPOCH subcommand, which controls the epoch used for
   parsing 2-digit years. */
static int
stc_custom_epoch (struct lexer *lexer,
		  struct dataset *ds UNUSED,
		  struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "AUTOMATIC"))
    settings_set_epoch (-1);
  else if (lex_is_integer (lexer))
    {
      int new_epoch = lex_integer (lexer);
      lex_get (lexer);
      if (new_epoch < 1500)
        {
          msg (SE, _("%s must be 1500 or later."), "EPOCH");
          return 0;
        }
      settings_set_epoch (new_epoch);
    }
  else
    {
      lex_error (lexer, _("expecting %s or year"), "AUTOMATIC");
      return 0;
    }

  return 1;
}

static int
stc_custom_errors (struct lexer *lexer, struct dataset *ds UNUSED,
                   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  return set_output_routing (lexer, SETTINGS_OUTPUT_ERROR);
}

static int
stc_custom_length (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  int page_length;

  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "NONE"))
    page_length = -1;
  else
    {
      if (!lex_force_int (lexer))
	return 0;
      if (lex_integer (lexer) < 1)
	{
	  msg (SE, _("%s must be at least %d."), "LENGTH", 1);
	  return 0;
	}
      page_length = lex_integer (lexer);
      lex_get (lexer);
    }

  if (page_length != -1)
    settings_set_viewlength (page_length);

  return 1;
}

static int
stc_custom_locale (struct lexer *lexer, struct dataset *ds UNUSED,
		   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  const char *s;

  lex_match (lexer, T_EQUALS);

  if ( !lex_force_string (lexer))
    return 0;

  s = lex_tokcstr (lexer);

  /* First try this string as an encoding name */
  if ( valid_encoding (s))
    set_default_encoding (s);

  /* Now try as a locale name (or alias) */
  else if (set_encoding_from_locale (s))
    {
    }
  else
    {
      msg (ME, _("%s is not a recognized encoding or locale name"), s);
      return 0;
    }

  lex_get (lexer);

  return 1;
}

static int
stc_custom_messages (struct lexer *lexer, struct dataset *ds UNUSED,
                   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  return set_output_routing (lexer, SETTINGS_OUTPUT_NOTE);
}

static int
stc_custom_printback (struct lexer *lexer, struct dataset *ds UNUSED,
                      struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  return set_output_routing (lexer, SETTINGS_OUTPUT_SYNTAX);
}

static int
stc_custom_results (struct lexer *lexer, struct dataset *ds UNUSED,
                    struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  return set_output_routing (lexer, SETTINGS_OUTPUT_RESULT);
}

static int
stc_custom_seed (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "RANDOM"))
    set_rng (time (0));
  else
    {
      if (!lex_force_num (lexer))
	return 0;
      set_rng (lex_number (lexer));
      lex_get (lexer);
    }

  return 1;
}

static int
stc_custom_width (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "NARROW"))
    settings_set_viewwidth (79);
  else if (lex_match_id (lexer, "WIDE"))
    settings_set_viewwidth (131);
  else
    {
      if (!lex_force_int (lexer))
	return 0;
      if (lex_integer (lexer) < 40)
	{
	  msg (SE, _("%s must be at least %d."), "WIDTH", 40);
	  return 0;
	}
      settings_set_viewwidth (lex_integer (lexer));
      lex_get (lexer);
    }

  return 1;
}

/* Parses FORMAT subcommand, which consists of a numeric format
   specifier. */
static int
stc_custom_format (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  struct fmt_spec fmt;

  lex_match (lexer, T_EQUALS);
  if (!parse_format_specifier (lexer, &fmt))
    return 0;

  if (!fmt_check_output (&fmt))
    return 0;
  
  if (fmt_is_string (fmt.type))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("%s requires numeric output format as an argument.  "
		 "Specified format %s is of type string."),
	   "FORMAT",
	   fmt_to_string (&fmt, str));
      return 0;
    }

  settings_set_format (&fmt);
  return 1;
}

static int
stc_custom_journal (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "ON") || lex_match_id (lexer, "YES"))
    journal_enable ();
  else if (lex_match_id (lexer, "OFF") || lex_match_id (lexer, "NO"))
    journal_disable ();
  else if (lex_is_string (lexer) || lex_token (lexer) == T_ID)
    {
      char *filename = utf8_to_filename (lex_tokcstr (lexer));
      journal_set_file_name (filename);
      free (filename);

      lex_get (lexer);
    }
  else
    {
      lex_error (lexer, NULL);
      return 0;
    }
  return 1;
}

static int
stc_custom_log (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  return stc_custom_journal (lexer, ds, cmd, aux);
}

static char *
show_output_routing (enum settings_output_type type)
{
  enum settings_output_devices devices;
  const char *s;

  devices = settings_get_output_routing (type);
  if (devices & SETTINGS_DEVICE_LISTING)
    s = devices & SETTINGS_DEVICE_TERMINAL ? "BOTH" : "LISTING";
  else if (devices & SETTINGS_DEVICE_TERMINAL)
    s = "TERMINAL";
  else
    s = "NONE";

  return xstrdup (s);
}

static char *
show_blanks (const struct dataset *ds UNUSED)
{
  return (settings_get_blanks () == SYSMIS
          ? xstrdup ("SYSMIS")
          : xasprintf ("%.*g", DBL_DIG + 1, settings_get_blanks ()));
}

static void
format_cc (struct string *out, const char *in, char grouping)
{
  while (*in != '\0')
    {
      char c = *in++;
      if (c == grouping || c == '\'')
        ds_put_byte (out, '\'');
      else if (c == '"')
        ds_put_byte (out, '"');
      ds_put_byte (out, c);
    }
}

static char *
show_cc (enum fmt_type type)
{
  const struct fmt_number_style *cc = settings_get_style (type);
  struct string out;

  ds_init_empty (&out);
  format_cc (&out, cc->neg_prefix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->prefix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->suffix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->neg_suffix.s, cc->grouping);

  return ds_cstr (&out);
}

static char *
show_cca (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCA);
}

static char *
show_ccb (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCB);
}

static char *
show_ccc (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCC);
}

static char *
show_ccd (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCD);
}

static char *
show_cce (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCE);
}

static char *
show_decimals (const struct dataset *ds UNUSED)
{
  return xasprintf ("`%c'", settings_get_decimal_char (FMT_F));
}

static char *
show_errors (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_ERROR);
}

static char *
show_format (const struct dataset *ds UNUSED)
{
  char str[FMT_STRING_LEN_MAX + 1];
  return xstrdup (fmt_to_string (settings_get_format (), str));
}

static char *
show_journal (const struct dataset *ds UNUSED)
{
  return (journal_is_enabled ()
          ? xasprintf ("\"%s\"", journal_get_file_name ())
          : xstrdup ("disabled"));
}

static char *
show_length (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewlength ());
}

static char *
show_locale (const struct dataset *ds UNUSED)
{
  return xstrdup (get_default_encoding ());
}

static char *
show_messages (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_NOTE);
}

static char *
show_printback (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_SYNTAX);
}

static char *
show_results (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_RESULT);
}

static char *
show_mxerrs (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_ERROR));
}

static char *
show_mxloops (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_mxloops ());
}

static char *
show_mxwarns (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_WARNING));
}

/* Returns a name for the given INTEGER_FORMAT value. */
static char *
show_integer_format (enum integer_format integer_format)
{
  return xasprintf ("%s (%s)",
                    (integer_format == INTEGER_MSB_FIRST ? "MSBFIRST"
                     : integer_format == INTEGER_LSB_FIRST ? "LSBFIRST"
                     : "VAX"),
                    integer_format == INTEGER_NATIVE ? "NATIVE" : "nonnative");
}

/* Returns a name for the given FLOAT_FORMAT value. */
static char *
show_float_format (enum float_format float_format)
{
  const char *format_name = "";

  switch (float_format)
    {
    case FLOAT_IEEE_SINGLE_LE:
      format_name = _("ISL (32-bit IEEE 754 single, little-endian)");
      break;
    case FLOAT_IEEE_SINGLE_BE:
      format_name = _("ISB (32-bit IEEE 754 single, big-endian)");
      break;
    case FLOAT_IEEE_DOUBLE_LE:
      format_name = _("IDL (64-bit IEEE 754 double, little-endian)");
      break;
    case FLOAT_IEEE_DOUBLE_BE:
      format_name = _("IDB (64-bit IEEE 754 double, big-endian)");
      break;

    case FLOAT_VAX_F:
      format_name = _("VF (32-bit VAX F, VAX-endian)");
      break;
    case FLOAT_VAX_D:
      format_name = _("VD (64-bit VAX D, VAX-endian)");
      break;
    case FLOAT_VAX_G:
      format_name = _("VG (64-bit VAX G, VAX-endian)");
      break;

    case FLOAT_Z_SHORT:
      format_name = _("ZS (32-bit IBM Z hexadecimal short, big-endian)");
      break;
    case FLOAT_Z_LONG:
      format_name = _("ZL (64-bit IBM Z hexadecimal long, big-endian)");
      break;

    case FLOAT_FP:
    case FLOAT_HEX:
      NOT_REACHED ();
    }

  return xasprintf ("%s (%s)", format_name,
                    (float_format == FLOAT_NATIVE_DOUBLE
                     ? "NATIVE" : "nonnative"));
}

static char *
show_rib (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_input_integer_format ());
}

static char *
show_rrb (const struct dataset *ds UNUSED)
{
  return show_float_format (settings_get_input_float_format ());
}

static char *
show_scompression (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_scompression () ? "ON" : "OFF");
}

static char *
show_undefined (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_undefined () ? "WARN" : "NOWARN");
}

static char *
show_weight (const struct dataset *ds)
{
  const struct variable *var = dict_get_weight (dataset_dict (ds));
  return xstrdup (var != NULL ? var_get_name (var) : "OFF");
}

static char *
show_wib (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_output_integer_format ());
}

static char *
show_wrb (const struct dataset *ds UNUSED)
{
  return show_float_format (settings_get_output_float_format ());
}

static char *
show_width (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewwidth ());
}

static char *
show_workspace (const struct dataset *ds UNUSED)
{
  size_t ws = settings_get_workspace () / 1024L;
  return xasprintf ("%zu", ws);
}

static char *
show_current_directory (const struct dataset *ds UNUSED)
{
  char *buf = NULL;
  char *wd = NULL;
  size_t len = 256;

  do
    {
      len <<= 1;
      buf = xrealloc (buf, len);
    } 
  while (NULL == (wd = getcwd (buf, len)));

  return wd;
}

static char *
show_tempdir (const struct dataset *ds UNUSED)
{
  return strdup (temp_dir_name ());
}

static char *
show_version (const struct dataset *ds UNUSED)
{
  return strdup (version);
}

static char *
show_system (const struct dataset *ds UNUSED)
{
  return strdup (host_system);
}

static char *
show_n (const struct dataset *ds)
{
  casenumber n;
  size_t l;

  const struct casereader *reader = dataset_source (ds);

  if (reader == NULL)
    return strdup (_("Unknown"));

  n =  casereader_count_cases (reader);

  return  asnprintf (NULL, &l, "%ld", n);
}


struct show_sbc
  {
    const char *name;
    char *(*function) (const struct dataset *);
  };

const struct show_sbc show_table[] =
  {
    {"BLANKS", show_blanks},
    {"CCA", show_cca},
    {"CCB", show_ccb},
    {"CCC", show_ccc},
    {"CCD", show_ccd},
    {"CCE", show_cce},
    {"DECIMALS", show_decimals},
    {"DIRECTORY", show_current_directory},
    {"ENVIRONMENT", show_system},
    {"ERRORS", show_errors},
    {"FORMAT", show_format},
    {"JOURNAL", show_journal},
    {"LENGTH", show_length},
    {"LOCALE", show_locale},
    {"MESSAGES", show_messages},
    {"MXERRS", show_mxerrs},
    {"MXLOOPS", show_mxloops},
    {"MXWARNS", show_mxwarns},
    {"N", show_n},
    {"PRINTBACk", show_printback},
    {"RESULTS", show_results},
    {"RIB", show_rib},
    {"RRB", show_rrb},
    {"SCOMPRESSION", show_scompression},
    {"TEMPDIR", show_tempdir},
    {"UNDEFINED", show_undefined},
    {"VERSION", show_version},
    {"WEIGHT", show_weight},
    {"WIB", show_wib},
    {"WRB", show_wrb},
    {"WIDTH", show_width},
    {"WORKSPACE", show_workspace},
  };

static void
do_show (const struct dataset *ds, const struct show_sbc *sbc)
{
  char *value = sbc->function (ds);
  msg (SN, _("%s is %s."), sbc->name, value);
  free (value);
}

static void
show_all (const struct dataset *ds)
{
  size_t i;

  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    do_show (ds, &show_table[i]);
}

static void
show_all_cc (const struct dataset *ds)
{
  int i;

  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    {
      const struct show_sbc *sbc = &show_table[i];
      if (!strncmp (sbc->name, "CC", 2))
        do_show (ds, sbc);
    }
}

static void
show_warranty (const struct dataset *ds UNUSED)
{
  fputs (lack_of_warranty, stdout);
}

static void
show_copying (const struct dataset *ds UNUSED)
{
  fputs (copyleft, stdout);
}


int
cmd_show (struct lexer *lexer, struct dataset *ds)
{
  if (lex_token (lexer) == T_ENDCMD)
    {
      show_all (ds);
      return CMD_SUCCESS;
    }

  do
    {
      if (lex_match (lexer, T_ALL))
        show_all (ds);
      else if (lex_match_id (lexer, "CC"))
        show_all_cc (ds);
      else if (lex_match_id (lexer, "WARRANTY"))
        show_warranty (ds);
      else if (lex_match_id (lexer, "COPYING") || lex_match_id (lexer, "LICENSE"))
        show_copying (ds);
      else if (lex_token (lexer) == T_ID)
        {
          int i;

          for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
            {
              const struct show_sbc *sbc = &show_table[i];
              if (lex_match_id (lexer, sbc->name))
                {
                  do_show (ds, sbc);
                  goto found;
                }
              }
          lex_error (lexer, NULL);
          return CMD_FAILURE;

        found: ;
        }
      else
        {
          lex_error (lexer, NULL);
          return CMD_FAILURE;
        }

      lex_match (lexer, T_SLASH);
    }
  while (lex_token (lexer) != T_ENDCMD);

  return CMD_SUCCESS;
}

#define MAX_SAVED_SETTINGS 5

static struct settings *saved_settings[MAX_SAVED_SETTINGS];
static int n_saved_settings;

int
cmd_preserve (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  if (n_saved_settings < MAX_SAVED_SETTINGS)
    {
      saved_settings[n_saved_settings++] = settings_get ();
      return CMD_SUCCESS;
    }
  else
    {
      msg (SE, _("Too many %s commands without a %s: at most "
                 "%d levels of saved settings are allowed."),
	   "PRESERVE", "RESTORE",
           MAX_SAVED_SETTINGS);
      return CMD_CASCADING_FAILURE;
    }
}

int
cmd_restore (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  if (n_saved_settings > 0)
    {
      struct settings *s = saved_settings[--n_saved_settings];
      settings_set (s);
      settings_destroy (s);
      return CMD_SUCCESS;
    }
  else
    {
      msg (SE, _("%s without matching %s."), "RESTORE", "PRESERVE");
      return CMD_FAILURE;
    }
}

/*
   Local Variables:
   mode: c
   End:
*/
