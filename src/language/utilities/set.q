/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <data/data-in.h>
#include <data/data-out.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/value.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <language/prompt.h>
#include <libpspp/compiler.h>
#include <libpspp/copyleft.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>
#include <libpspp/message.h>
#include <libpspp/i18n.h>
#include <math/random.h>
#include <output/journal.h>
#include <output/output.h>

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
     block=string "x==1" "one character long";
     boxstring=string "x==3 || x==11" "3 or 11 characters long";
     case=size:upper/uplow;
     cca=string;
     ccb=string;
     ccc=string;
     ccd=string;
     cce=string;
     compression=compress:on/off;
     cpi=integer "x>0" "%s must be greater than 0";
     cprompt=string;
     decimal=dec:dot/comma;
     disk=custom;
     dprompt=string;
     echo=echo:on/off;
     endcmd=string "x==1" "one character long";
     epoch=custom;
     errorbreak=errbrk:on/off;
     errors=errors:terminal/listing/both/on/none/off;
     format=custom;
     headers=headers:no/yes/blank;
     highres=hires:on/off;
     histogram=string "x==1" "one character long";
     include=inc:on/off;
     journal=custom;
     log=custom;
     length=custom;
     locale=custom;
     listing=custom;
     lowres=lores:auto/on/off;
     lpi=integer "x>0" "%s must be greater than 0";
     menus=menus:standard/extended;
     messages=messages:on/off/terminal/listing/both/on/none/off;
     mexpand=mexp:on/off;
     miterate=integer "x>0" "%s must be greater than 0";
     mnest=integer "x>0" "%s must be greater than 0";
     mprint=mprint:on/off;
     mxerrs=integer "x >= 1" "%s must be at least 1";
     mxloops=integer "x >=1" "%s must be at least 1";
     mxmemory=integer;
     mxwarns=integer;
     nulline=null:on/off;
     printback=prtbck:on/off;
     prompt=string;
     results=res:on/off/terminal/listing/both/on/none/off;
     rib=rib:msbfirst/lsbfirst/vax/native;
     rrb=rrb:native/isl/isb/idl/idb/vf/vd/vg/zs/zl;
     safer=safe:on;
     scompression=scompress:on/off;
     scripttab=string "x==1" "one character long";
     seed=custom;
     tb1=string "x==3 || x==11" "3 or 11 characters long";
     tbfonts=string;
     undefined=undef:warn/nowarn;
     wib=wib:msbfirst/lsbfirst/vax/native;
     wrb=wrb:native/isl/isb/idl/idb/vf/vd/vg/zs/zl;
     width=custom;
     workspace=integer "x>0" "%s must be positive";
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

  if (cmd.sbc_prompt)
    prompt_set (PROMPT_FIRST, cmd.s_prompt);
  if (cmd.sbc_cprompt)
    prompt_set (PROMPT_LATER, cmd.s_cprompt);
  if (cmd.sbc_dprompt)
    prompt_set (PROMPT_DATA, cmd.s_dprompt);

  if (cmd.sbc_decimal)
    settings_set_decimal_char (cmd.dec == STC_DOT ? '.' : ',');

  if (cmd.sbc_echo)
    settings_set_echo (cmd.echo == STC_ON);
  if (cmd.sbc_endcmd)
    settings_set_endcmd (cmd.s_endcmd[0]);
  if (cmd.sbc_errorbreak)
    settings_set_errorbreak (cmd.errbrk == STC_ON);
  if (cmd.sbc_errors)
    {
      bool both = cmd.errors == STC_BOTH || cmd.errors == STC_ON;
      settings_set_error_routing_to_terminal (cmd.errors == STC_TERMINAL || both);
      settings_set_error_routing_to_listing (cmd.errors == STC_LISTING || both);
    }
  if (cmd.sbc_include)
    settings_set_include (cmd.inc == STC_ON);
  if (cmd.sbc_mxerrs)
    settings_set_mxerrs (cmd.n_mxerrs[0]);
  if (cmd.sbc_mxwarns)
    settings_set_mxwarns (cmd.n_mxwarns[0]);
  if (cmd.sbc_nulline)
    settings_set_nulline (cmd.null == STC_ON);
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
	msg (SE, _("WORKSPACE must be at least 1MB"));
      else
	settings_set_workspace (cmd.n_workspace[0] * 1024L);
    }

  if (cmd.sbc_block)
    msg (SW, _("%s is obsolete."), "BLOCK");
  if (cmd.sbc_boxstring)
    msg (SW, _("%s is obsolete."), "BOXSTRING");
  if (cmd.sbc_histogram)
    msg (SW, _("%s is obsolete."), "HISTOGRAM");
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
    msg (SW, _("%s is not implemented."), "CASE");

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



/* Parses the BLANKS subcommand, which controls the value that
   completely blank fields in numeric data imply.  X, Wnd: Syntax is
   SYSMIS or a numeric value. */
static int
stc_custom_blanks (struct lexer *lexer,
		   struct dataset *ds UNUSED,
		   struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');
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

/* Parses the EPOCH subcommand, which controls the epoch used for
   parsing 2-digit years. */
static int
stc_custom_epoch (struct lexer *lexer,
		  struct dataset *ds UNUSED,
		  struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');
  if (lex_match_id (lexer, "AUTOMATIC"))
    settings_set_epoch (-1);
  else if (lex_is_integer (lexer))
    {
      int new_epoch = lex_integer (lexer);
      lex_get (lexer);
      if (new_epoch < 1500)
        {
          msg (SE, _("EPOCH must be 1500 or later."));
          return 0;
        }
      settings_set_epoch (new_epoch);
    }
  else
    {
      lex_error (lexer, _("expecting AUTOMATIC or year"));
      return 0;
    }

  return 1;
}

static int
stc_custom_length (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  int page_length;

  lex_match (lexer, '=');
  if (lex_match_id (lexer, "NONE"))
    page_length = -1;
  else
    {
      if (!lex_force_int (lexer))
	return 0;
      if (lex_integer (lexer) < 1)
	{
	  msg (SE, _("LENGTH must be at least 1."));
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
  const struct string *s;

  lex_match (lexer, '=');

  if ( !lex_force_string (lexer))
    return 0;

  s = lex_tokstr (lexer);

  lex_get (lexer);

  /* First try this string as an encoding name */
  if ( valid_encoding (ds_cstr (s)))
    set_default_encoding (ds_cstr (s));

  /* Now try as a locale name (or alias) */
  else if (set_encoding_from_locale (ds_cstr (s)))
    {
    }
  else
    {
      msg (ME, _("%s is not a recognised encoding or locale name"),
	   ds_cstr (s));
      return 0;
    }

  return 1;
}



static int
stc_custom_seed (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');
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
  lex_match (lexer, '=');
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
	  msg (SE, _("WIDTH must be at least 40."));
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

  lex_match (lexer, '=');
  if (!parse_format_specifier (lexer, &fmt))
    return 0;
  if (fmt_is_string (fmt.type))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("FORMAT requires numeric output format as an argument.  "
		 "Specified format %s is of type string."),
	   fmt_to_string (&fmt, str));
      return 0;
    }

  settings_set_format (&fmt);
  return 1;
}

static int
stc_custom_journal (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');
  if (lex_match_id (lexer, "ON") || lex_match_id (lexer, "YES"))
    journal_enable ();
  else if (lex_match_id (lexer, "OFF") || lex_match_id (lexer, "NO"))
    journal_disable ();
  else if (lex_token (lexer) == T_STRING || lex_token (lexer) == T_ID)
    {
      journal_set_file_name (ds_cstr (lex_tokstr (lexer)));
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

static int
stc_custom_listing (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  bool listing;

  lex_match (lexer, '=');
  if (lex_match_id (lexer, "ON") || lex_match_id (lexer, "YES"))
    listing = true;
  else if (lex_match_id (lexer, "OFF") || lex_match_id (lexer, "NO"))
    listing = false;
  else
    {
      /* FIXME */
      return 0;
    }
  outp_enable_device (listing, OUTP_DEV_LISTING);

  return 1;
}

static int
stc_custom_disk (struct lexer *lexer, struct dataset *ds, struct cmd_set *cmd UNUSED, void *aux)
{
  return stc_custom_listing (lexer, ds, cmd, aux);
}

static void
show_blanks (const struct dataset *ds UNUSED)
{
  if (settings_get_blanks () == SYSMIS)
    msg (SN, _("BLANKS is SYSMIS."));
  else
    msg (SN, _("BLANKS is %g."), settings_get_blanks ());

}

static char *
format_cc (struct substring in, char grouping, char *out)
{
  while (!ss_is_empty (in))
    {
      char c = ss_get_char (&in);
      if (c == grouping || c == '\'')
        *out++ = '\'';
      else if (c == '"')
        *out++ = '"';
      *out++ = c;
    }
  return out;
}

static void
show_cc (enum fmt_type type)
{
  const struct fmt_number_style *cc = settings_get_style (type);
  char cc_string[FMT_STYLE_AFFIX_MAX * 4 * 2 + 3 + 1];
  char *out;

  out = format_cc (cc->neg_prefix, cc->grouping, cc_string);
  *out++ = cc->grouping;
  out = format_cc (cc->prefix, cc->grouping, out);
  *out++ = cc->grouping;
  out = format_cc (cc->suffix, cc->grouping, out);
  *out++ = cc->grouping;
  out = format_cc (cc->neg_suffix, cc->grouping, out);
  *out = '\0';

  msg (SN, _("%s is \"%s\"."), fmt_name (type), cc_string);
}

static void
show_cca (const struct dataset *ds UNUSED)
{
  show_cc (FMT_CCA);
}

static void
show_ccb (const struct dataset *ds UNUSED)
{
  show_cc (FMT_CCB);
}

static void
show_ccc (const struct dataset *ds UNUSED)
{
  show_cc (FMT_CCC);
}

static void
show_ccd (const struct dataset *ds UNUSED)
{
  show_cc (FMT_CCD);
}

static void
show_cce (const struct dataset *ds UNUSED)
{
  show_cc (FMT_CCE);
}

static void
show_decimals (const struct dataset *ds UNUSED)
{
  msg (SN, _("DECIMAL is \"%c\"."), settings_get_decimal_char (FMT_F));
}

static void
show_endcmd (const struct dataset *ds UNUSED)
{
  msg (SN, _("ENDCMD is \"%c\"."), settings_get_endcmd ());
}

static void
show_errors (const struct dataset *ds UNUSED)
{
  bool terminal = settings_get_error_routing_to_terminal ();
  bool listing = settings_get_error_routing_to_listing ();
  msg (SN, _("ERRORS is \"%s\"."),
       terminal && listing ? "BOTH"
       : terminal ? "TERMINAL"
       : listing ? "LISTING"
       : "NONE");
}

static void
show_format (const struct dataset *ds UNUSED)
{
  char str[FMT_STRING_LEN_MAX + 1];
  msg (SN, _("FORMAT is %s."), fmt_to_string (settings_get_format (), str));
}

static void
show_length (const struct dataset *ds UNUSED)
{
  msg (SN, _("LENGTH is %d."), settings_get_viewlength ());
}

static void
show_locale (const struct dataset *ds UNUSED)
{
  msg (SN, _("LOCALE is %s"), get_default_encoding ());
}

static void
show_mxerrs (const struct dataset *ds UNUSED)
{
  msg (SN, _("MXERRS is %d."), settings_get_mxerrs ());
}

static void
show_mxloops (const struct dataset *ds UNUSED)
{
  msg (SN, _("MXLOOPS is %d."), settings_get_mxloops ());
}

static void
show_mxwarns (const struct dataset *ds UNUSED)
{
  msg (SN, _("MXWARNS is %d."), settings_get_mxwarns ());
}

/* Outputs that SETTING has the given INTEGER_FORMAT value. */
static void
show_integer_format (const char *setting, enum integer_format integer_format)
{
  msg (SN, _("%s is %s (%s)."),
       setting,
       (integer_format == INTEGER_MSB_FIRST ? "MSBFIRST"
        : integer_format == INTEGER_LSB_FIRST ? "LSBFIRST"
        : "VAX"),
       integer_format == INTEGER_NATIVE ? "NATIVE" : "nonnative");
}

/* Outputs that SETTING has the given FLOAT_FORMAT value. */
static void
show_float_format (const char *setting, enum float_format float_format)
{
  const char *format_name = "";

  switch (float_format)
    {
    case FLOAT_IEEE_SINGLE_LE:
      format_name = "ISL (32-bit IEEE 754 single, little-endian)";
      break;
    case FLOAT_IEEE_SINGLE_BE:
      format_name = "ISB (32-bit IEEE 754 single, big-endian)";
      break;
    case FLOAT_IEEE_DOUBLE_LE:
      format_name = "IDL (64-bit IEEE 754 double, little-endian)";
      break;
    case FLOAT_IEEE_DOUBLE_BE:
      format_name = "IDB (64-bit IEEE 754 double, big-endian)";
      break;

    case FLOAT_VAX_F:
      format_name = "VF (32-bit VAX F, VAX-endian)";
      break;
    case FLOAT_VAX_D:
      format_name = "VD (64-bit VAX D, VAX-endian)";
      break;
    case FLOAT_VAX_G:
      format_name = "VG (64-bit VAX G, VAX-endian)";
      break;

    case FLOAT_Z_SHORT:
      format_name = "ZS (32-bit IBM Z hexadecimal short, big-endian)";
      break;
    case FLOAT_Z_LONG:
      format_name = "ZL (64-bit IBM Z hexadecimal long, big-endian)";
      break;

    case FLOAT_FP:
    case FLOAT_HEX:
      NOT_REACHED ();
    }

  msg (SN, _("%s is %s (%s)."),
       setting, format_name,
       float_format == FLOAT_NATIVE_DOUBLE ? "NATIVE" : "nonnative");
}

static void
show_rib (const struct dataset *ds UNUSED)
{
  show_integer_format ("RIB", settings_get_input_integer_format ());
}

static void
show_rrb (const struct dataset *ds UNUSED)
{
  show_float_format ("RRB", settings_get_input_float_format ());
}

static void
show_scompression (const struct dataset *ds UNUSED)
{
  if (settings_get_scompression ())
    msg (SN, _("SCOMPRESSION is ON."));
  else
    msg (SN, _("SCOMPRESSION is OFF."));
}

static void
show_undefined (const struct dataset *ds UNUSED)
{
  if (settings_get_undefined ())
    msg (SN, _("UNDEFINED is WARN."));
  else
    msg (SN, _("UNDEFINED is NOWARN."));
}

static void
show_weight (const struct dataset *ds)
{
  const struct variable *var = dict_get_weight (dataset_dict (ds));
  if (var == NULL)
    msg (SN, _("WEIGHT is off."));
  else
    msg (SN, _("WEIGHT is variable %s."), var_get_name (var));
}

static void
show_wib (const struct dataset *ds UNUSED)
{
  show_integer_format ("WIB", settings_get_output_integer_format ());
}

static void
show_wrb (const struct dataset *ds UNUSED)
{
  show_float_format ("WRB", settings_get_output_float_format ());
}

static void
show_width (const struct dataset *ds UNUSED)
{
  msg (SN, _("WIDTH is %d."), settings_get_viewwidth ());
}

struct show_sbc
  {
    const char *name;
    void (*function) (const struct dataset *);
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
    {"ENDCMD", show_endcmd},
    {"ERRORS", show_errors},
    {"FORMAT", show_format},
    {"LENGTH", show_length},
    {"LOCALE", show_locale},
    {"MXERRS", show_mxerrs},
    {"MXLOOPS", show_mxloops},
    {"MXWARNS", show_mxwarns},
    {"RIB", show_rib},
    {"RRB", show_rrb},
    {"SCOMPRESSION", show_scompression},
    {"UNDEFINED", show_undefined},
    {"WEIGHT", show_weight},
    {"WIB", show_wib},
    {"WRB", show_wrb},
    {"WIDTH", show_width},
  };

static void
show_all (const struct dataset *ds)
{
  size_t i;

  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    show_table[i].function (ds);
}

static void
show_all_cc (void)
{
  int i;

  for (i = 0; i < 5; i++)
    show_cc (i);
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
  if (lex_token (lexer) == '.')
    {
      show_all (ds);
      return CMD_SUCCESS;
    }

  do
    {
      if (lex_match (lexer, T_ALL))
        show_all (ds);
      else if (lex_match_id (lexer, "CC"))
        show_all_cc ();
      else if (lex_match_id (lexer, "WARRANTY"))
        show_warranty (ds);
      else if (lex_match_id (lexer, "COPYING"))
        show_copying (ds);
      else if (lex_token (lexer) == T_ID)
        {
          int i;

          for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
            if (lex_match_id (lexer, show_table[i].name))
              {
                show_table[i].function (ds);
                goto found;
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

      lex_match (lexer, '/');
    }
  while (lex_token (lexer) != '.');

  return CMD_SUCCESS;
}

/*
   Local Variables:
   mode: c
   End:
*/
