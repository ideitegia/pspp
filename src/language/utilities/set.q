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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/line-buffer.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/copyleft.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <math/random.h>
#include <output/output.h>

#if HAVE_LIBTERMCAP
#if HAVE_TERMCAP_H
#include <termcap.h>
#else /* !HAVE_TERMCAP_H */
int tgetent (char *, const char *);
int tgetnum (const char *);
#endif /* !HAVE_TERMCAP_H */
#endif /* !HAVE_LIBTERMCAP */

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
     errors=errors:on/off/terminal/listing/both/none;
     format=custom;
     headers=headers:no/yes/blank;
     highres=hires:on/off;
     histogram=string "x==1" "one character long";
     include=inc:on/off;
     journal=custom;
     length=custom;
     listing=custom;
     lowres=lores:auto/on/off;
     lpi=integer "x>0" "%s must be greater than 0";
     menus=menus:standard/extended;
     messages=messages:on/off/terminal/listing/both/none;
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
     results=res:on/off/terminal/listing/both/none;
     safer=safe:on;
     scompression=scompress:on/off;
     scripttab=string "x==1" "one character long";
     seed=custom;
     tb1=string "x==3 || x==11" "3 or 11 characters long";
     tbfonts=string;
     undefined=undef:warn/nowarn;
     width=custom;
     workspace=integer "x>=1024" "%s must be at least 1 MB";
     xsort=xsort:yes/no.
*/

/* (headers) */

/* (declarations) */

/* (_functions) */

static bool do_cc (const char *cc_string, int idx);

int
cmd_set (void)
{
  struct cmd_set cmd;
  bool ok = true;

  if (!parse_set (&cmd, NULL))
    return CMD_FAILURE;

  if (cmd.sbc_cca)
    ok = ok && do_cc (cmd.s_cca, 0);
  if (cmd.sbc_ccb)
    ok = ok && do_cc (cmd.s_ccb, 1);
  if (cmd.sbc_ccc)
    ok = ok && do_cc (cmd.s_ccc, 2);
  if (cmd.sbc_ccd)
    ok = ok && do_cc (cmd.s_ccd, 3);
  if (cmd.sbc_cce)
    ok = ok && do_cc (cmd.s_cce, 4);

  if (cmd.sbc_prompt)
    getl_set_prompt (GETL_PROMPT_FIRST, cmd.s_prompt);
  if (cmd.sbc_cprompt)
    getl_set_prompt (GETL_PROMPT_LATER, cmd.s_cprompt);
  if (cmd.sbc_dprompt)
    getl_set_prompt (GETL_PROMPT_DATA, cmd.s_dprompt);

  if (cmd.sbc_decimal)
    set_decimal (cmd.dec == STC_DOT ? '.' : ',');
  if (cmd.sbc_echo)
    set_echo (cmd.echo == STC_ON);
  if (cmd.sbc_endcmd)
    set_endcmd (cmd.s_endcmd[0]);
  if (cmd.sbc_errorbreak)
    set_errorbreak (cmd.errbrk == STC_ON);
  if (cmd.sbc_include)
    set_include (cmd.inc == STC_ON);
  if (cmd.sbc_mxerrs)
    set_mxerrs (cmd.n_mxerrs[0]);
  if (cmd.sbc_mxwarns)
    set_mxwarns (cmd.n_mxwarns[0]);
  if (cmd.sbc_nulline)
    set_nulline (cmd.null == STC_ON);
  if (cmd.sbc_safer)
    set_safer_mode ();
  if (cmd.sbc_scompression)
    set_scompression (cmd.scompress == STC_ON);
  if (cmd.sbc_undefined)
    set_undefined (cmd.undef == STC_WARN);
  if (cmd.sbc_workspace)
    set_workspace (cmd.n_workspace[0] * 1024L);

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

  return CMD_SUCCESS;
}

/* Find the grouping characters in CC_STRING and set CC's
   grouping and decimal members appropriately.  Returns true if
   successful, false otherwise. */
static bool
find_cc_separators (const char *cc_string, struct custom_currency *cc)
{
  const char *sp;
  int comma_cnt, dot_cnt;
  
  /* Count commas and periods.  There must be exactly three of
     one or the other, except that an apostrophe acts escapes a
     following comma or period. */
  comma_cnt = dot_cnt = 0;
  for (sp = cc_string; *sp; sp++)
    if (*sp == ',')
      comma_cnt++;
    else if (*sp == '.')
      dot_cnt++;
    else if (*sp == '\'' && (sp[1] == '.' || sp[1] == ',' || sp[1] == '\''))
      sp++;
  
  if ((comma_cnt == 3) == (dot_cnt == 3))
    return false;

  if (comma_cnt == 3)
    {
      cc->decimal = '.';
      cc->grouping = ',';
    }
  else
    {
      cc->decimal = ',';
      cc->grouping = '.';
    }
  return true;
}

/* Extracts a token from IN into TOKEn.  Tokens are delimited by
   GROUPING.  The token is truncated to at most CC_WIDTH
   characters (including null terminator).  Returns the first
   character following the token. */
static const char *
extract_cc_token (const char *in, int grouping, char token[CC_WIDTH]) 
{
  char *out = token;
  
  for (; *in != '\0' && *in != grouping; in++) 
    {
      if (*in == '\'' && in[1] == grouping)
        in++;
      if (out < &token[CC_WIDTH - 1])
        *out++ = *in;
    }
  *out = '\0';

  if (*in == grouping)
    in++;
  return in;
}

/* Sets custom currency specifier CC having name CC_NAME ('A' through
   'E') to correspond to the settings in CC_STRING. */
static bool
do_cc (const char *cc_string, int idx)
{
  struct custom_currency cc;
  
  /* Determine separators. */
  if (!find_cc_separators (cc_string, &cc)) 
    {
      msg (SE, _("CC%c: Custom currency string `%s' does not contain "
                 "exactly three periods or commas (not both)."),
           "ABCDE"[idx], cc_string);
      return false;
    }
  
  cc_string = extract_cc_token (cc_string, cc.grouping, cc.neg_prefix);
  cc_string = extract_cc_token (cc_string, cc.grouping, cc.prefix);
  cc_string = extract_cc_token (cc_string, cc.grouping, cc.suffix);
  cc_string = extract_cc_token (cc_string, cc.grouping, cc.neg_suffix);

  set_cc (idx, &cc);
  
  return true;
}

/* Parses the BLANKS subcommand, which controls the value that
   completely blank fields in numeric data imply.  X, Wnd: Syntax is
   SYSMIS or a numeric value. */
static int
stc_custom_blanks (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match ('=');
  if ((token == T_ID && lex_id_match ("SYSMIS", tokid)))
    {
      lex_get ();
      set_blanks (SYSMIS);
    }
  else
    {
      if (!lex_force_num ())
	return 0;
      set_blanks (lex_number ());
      lex_get ();
    }
  return 1;
}

/* Parses the EPOCH subcommand, which controls the epoch used for
   parsing 2-digit years. */
static int
stc_custom_epoch (struct cmd_set *cmd UNUSED, void *aux UNUSED) 
{
  lex_match ('=');
  if (lex_match_id ("AUTOMATIC"))
    set_epoch (-1);
  else if (lex_is_integer ()) 
    {
      int new_epoch = lex_integer ();
      lex_get ();
      if (new_epoch < 1500) 
        {
          msg (SE, _("EPOCH must be 1500 or later."));
          return 0;
        }
      set_epoch (new_epoch);
    }
  else 
    {
      lex_error (_("expecting AUTOMATIC or year"));
      return 0;
    }

  return 1;
}

static int
stc_custom_length (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  int page_length;

  lex_match ('=');
  if (lex_match_id ("NONE"))
    page_length = -1;
  else
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 1)
	{
	  msg (SE, _("LENGTH must be at least 1."));
	  return 0;
	}
      page_length = lex_integer ();
      lex_get ();
    }

  if (page_length != -1) 
    set_viewlength (page_length);

  return 1;
}

static int
stc_custom_seed (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match ('=');
  if (lex_match_id ("RANDOM"))
    set_rng (time (0));
  else
    {
      if (!lex_force_num ())
	return 0;
      set_rng (lex_number ());
      lex_get ();
    }

  return 1;
}

static int
stc_custom_width (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match ('=');
  if (lex_match_id ("NARROW"))
    set_viewwidth (79);
  else if (lex_match_id ("WIDE"))
    set_viewwidth (131);
  else
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 40)
	{
	  msg (SE, _("WIDTH must be at least 40."));
	  return 0;
	}
      set_viewwidth (lex_integer ());
      lex_get ();
    }

  return 1;
}

/* Parses FORMAT subcommand, which consists of a numeric format
   specifier. */
static int
stc_custom_format (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  struct fmt_spec fmt;

  lex_match ('=');
  if (!parse_format_specifier (&fmt, 0))
    return 0;
  if ((formats[fmt.type].cat & FCAT_STRING) != 0)
    {
      msg (SE, _("FORMAT requires numeric output format as an argument.  "
		 "Specified format %s is of type string."),
	   fmt_to_string (&fmt));
      return 0;
    }

  set_format (&fmt);
  return 1;
}

static int
stc_custom_journal (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  lex_match ('=');
  if (!lex_match_id ("ON") && !lex_match_id ("OFF")) 
    {
      if (token == T_STRING)
        lex_get ();
      else
        {
          lex_error (NULL);
          return 0;
        }
    }
  return 1;
}

static int
stc_custom_listing (struct cmd_set *cmd UNUSED, void *aux UNUSED)
{
  bool listing;

  lex_match ('=');
  if (lex_match_id ("ON") || lex_match_id ("YES"))
    listing = true;
  else if (lex_match_id ("OFF") || lex_match_id ("NO"))
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
stc_custom_disk (struct cmd_set *cmd UNUSED, void *aux)
{
  return stc_custom_listing (cmd, aux);
}

static void
show_blanks (void) 
{
  if (get_blanks () == SYSMIS)
    msg (SN, _("BLANKS is SYSMIS."));
  else
    msg (SN, _("BLANKS is %g."), get_blanks ());

}

static char *
format_cc (const char *in, char grouping, char *out) 
{
  while (*in != '\0') 
    {
      if (*in == grouping || *in == '\'')
        *out++ = '\'';
      *out++ = *in++;
    }
  return out;
}

static void
show_cc (int idx) 
{
  const struct custom_currency *cc = get_cc (idx);
  char cc_string[CC_WIDTH * 4 * 2 + 3 + 1];
  char *out;

  out = format_cc (cc->neg_prefix, cc->grouping, cc_string);
  *out++ = cc->grouping;
  out = format_cc (cc->prefix, cc->grouping, out);
  *out++ = cc->grouping;
  out = format_cc (cc->suffix, cc->grouping, out);
  *out++ = cc->grouping;
  out = format_cc (cc->neg_suffix, cc->grouping, out);
  *out = '\0';
  
  msg (SN, _("CC%c is \"%s\"."), "ABCDE"[idx], cc_string);
}


static void
show_cca (void) 
{
  show_cc (0);
}

static void
show_ccb (void) 
{
  show_cc (1);
}

static void
show_ccc (void) 
{
  show_cc (2);
}

static void
show_ccd (void) 
{
  show_cc (3);
}

static void
show_cce (void) 
{
  show_cc (4);
}

static void
show_decimals (void) 
{
  msg (SN, _("DECIMAL is \"%c\"."), get_decimal ());
}

static void
show_endcmd (void) 
{
  msg (SN, _("ENDCMD is \"%c\"."), get_endcmd ());
}

static void
show_format (void) 
{
  msg (SN, _("FORMAT is %s."), fmt_to_string (get_format ()));
}

static void
show_length (void) 
{
  msg (SN, _("LENGTH is %d."), get_viewlength ());
}

static void
show_mxerrs (void) 
{
  msg (SN, _("MXERRS is %d."), get_mxerrs ());
}

static void
show_mxloops (void) 
{
  msg (SN, _("MXLOOPS is %d."), get_mxloops ());
}

static void
show_mxwarns (void) 
{
  msg (SN, _("MXWARNS is %d."), get_mxwarns ());
}

static void
show_scompression (void) 
{
  if (get_scompression ())
    msg (SN, _("SCOMPRESSION is ON."));
  else
    msg (SN, _("SCOMPRESSION is OFF."));
}

static void
show_undefined (void) 
{
  if (get_undefined ())
    msg (SN, _("UNDEFINED is WARN."));
  else
    msg (SN, _("UNDEFINED is NOWARN."));
}

static void
show_weight (void) 
{
  struct variable *var = dict_get_weight (default_dict);
  if (var == NULL)
    msg (SN, _("WEIGHT is off."));
  else
    msg (SN, _("WEIGHT is variable %s."), var->name);
}

static void
show_width (void) 
{
  msg (SN, _("WIDTH is %d."), get_viewwidth ());
}

struct show_sbc 
  {
    const char *name;
    void (*function) (void);
  };

struct show_sbc show_table[] = 
  {
    {"BLANKS", show_blanks},
    {"CCA", show_cca},
    {"CCB", show_ccb},
    {"CCC", show_ccc},
    {"CCD", show_ccd},
    {"CCE", show_cce},
    {"DECIMALS", show_decimals},
    {"ENDCMD", show_endcmd},
    {"FORMAT", show_format},
    {"LENGTH", show_length},
    {"MXERRS", show_mxerrs},
    {"MXLOOPS", show_mxloops},
    {"MXWARNS", show_mxwarns},
    {"SCOMPRESSION", show_scompression},
    {"UNDEFINED", show_undefined},
    {"WEIGHT", show_weight},
    {"WIDTH", show_width},
  };

static void
show_all (void) 
{
  size_t i;
  
  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    show_table[i].function ();
}

static void
show_all_cc (void) 
{
  int i;

  for (i = 0; i < 5; i++)
    show_cc (i);
}

static void
show_warranty (void) 
{
  msg (MN, lack_of_warranty);
}

static void
show_copying (void) 
{
  msg (MN, copyleft);
}

int
cmd_show (void) 
{
  if (token == '.') 
    {
      show_all ();
      return CMD_SUCCESS;
    }

  do 
    {
      if (lex_match (T_ALL))
        show_all ();
      else if (lex_match_id ("CC")) 
        show_all_cc ();
      else if (lex_match_id ("WARRANTY"))
        show_warranty ();
      else if (lex_match_id ("COPYING"))
        show_copying ();
      else if (token == T_ID)
        {
          int i;

          for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
            if (lex_match_id (show_table[i].name)) 
              {
                show_table[i].function ();
                goto found;
              }
          lex_error (NULL);
          return CMD_FAILURE;

        found: ;
        }
      else 
        {
          lex_error (NULL);
          return CMD_FAILURE;
        }

      lex_match ('/');
    }
  while (token != '.');

  return CMD_SUCCESS;
}

/*
   Local Variables:
   mode: c
   End:
*/
