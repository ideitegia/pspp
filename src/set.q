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

/*
   Categories of SET subcommands:

   data input: BLANKS, DECIMAL, FORMAT.
   
   program input: ENDCMD, NULLINE.
   
   interaction: CPROMPT, DPROMPT, ERRORBREAK, MXERRS, MXWARNS, PROMPT.
   
   program execution: MEXPAND, MITERATE, MNEST, MPRINT,
   MXLOOPS, SEED, UNDEFINED.

   data output: CCA...CCE, DECIMAL, FORMAT, RESULTS-p.

   output routing: ECHO, ERRORS, INCLUDE, MESSAGES, PRINTBACK, ERRORS,
   RESULTS-rw.

   output activation: LISTING (on/off), SCREEN, PRINTER.

   output driver options: HEADERS, MORE, PAGER, VIEWLENGTH, VIEWWIDTH,
   LISTING (filename).

   logging: LOG, JOURNAL.

   system files: COMP/COMPRESSION, SCOMP/SCOMPRESSION.

   security: SAFER.
*/

/*
   FIXME

   These subcommands remain to be implemented:
     ECHO, PRINTBACK, INCLUDE
     MORE, PAGER, VIEWLENGTH, VIEWWIDTH, HEADERS

   These subcommands are not complete:
     MESSAGES, ERRORS, RESULTS
     LISTING/DISK, LOG/JOURNAL
*/     
   
#include <config.h>
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "log.h"
#include "output.h"
#include "var.h"
#include "format.h"

double set_blanks;
int set_compression;
struct set_cust_currency set_cc[5];
int set_cpi;
char *set_cprompt;
int set_decimal;
int set_grouping;
char *set_dprompt;
int set_echo;
int set_endcmd;
int set_errorbreak;
int set_errors, set_messages, set_results;
struct fmt_spec set_format;
int set_headers;
int set_include;
char *set_journal;
int set_journaling;
int set_lpi;
int set_messages;
int set_mexpand;
int set_miterate;
int set_mnest;
int set_more;
int set_mprint;
int set_mxerrs;
int set_mxloops;
int set_mxwarns;
int set_nullline;
int set_printback;
int set_output = 1;
#if !USE_INTERNAL_PAGER
char *set_pager;
#endif /* !USE_INTERNAL_PAGER */
int set_printer;
char *set_prompt;
char *set_results_file;
int set_safer;
int set_scompression;
int set_screen;
long set_seed;
int set_seed_used;
int set_testing_mode;
int set_undefined;
int set_viewlength;
int set_viewwidth;

static void set_routing (int q, int *setting);
static int set_ccx (const char *cc_string, struct set_cust_currency * cc,
		    int cc_name);

/* (specification)
   "SET" (stc_):
     automenu=automenu:on/off;
     beep=beep:on/off;
     blanks=custom;
     block=string "x==1" "one character long";
     boxstring=string "x==3 || x==11" "3 or 11 characters long";
     case=size:upper/uplow;
     cca=string;
     ccb=string;
     ccc=string;
     ccd=string;
     cce=string;
     color=custom;
     compression=compress:on/off;
     cpi=integer;
     cprompt=string;
     decimal=dec:dot/_comma;
     disk=custom;
     dprompt=string;
     echo=echo:on/off;
     eject=eject:on/off;
     endcmd=string "x==1" "one character long";
     errorbreak=errbrk:on/off;
     errors=errors:on/off/terminal/listing/both/none;
     format=custom;
     headers=headers:no/yes/blank;
     helpwindows=helpwin:on/off;
     highres=hires:on/off;
     histogram=string "x==1" "one character long";
     include=inc:on/off;
     journal=custom;
     length=custom;
     listing=custom;
     log=custom;
     lowres=lores:auto/on/off;
     lpi=integer;
     menus=menus:standard/extended;
     messages=messages:on/off/terminal/listing/both/none;
     mexpand=mexp:on/off;
     miterate=integer;
     mnest=integer;
     more=more:on/off;
     mprint=mprint:on/off;
     mxerrs=integer;
     mxloops=integer;
     mxmemory=integer;
     mxwarns=integer;
     nulline=null:on/off;
     pager=custom;
     printback=prtbck:on/off;
     printer=prtr:on/off;
     prompt=string;
     ptranslate=ptrans:on/off;
     rcolor=custom;
     results=custom;
     runreview=runrev:auto/manual;
     safer=safe:on;
     scompression=scompress:on/off;
     screen=scrn:on/off;
     scripttab=string "x==1" "one character long";
     seed=custom;
     tb1=string "x==3 || x==11" "3 or 11 characters long";
     tbfonts=string;
     undefined=undef:warn/nowarn;
     viewlength=custom;
     viewwidth=integer;
     width=custom;
     workdev=custom;
     workspace=integer;
     xsort=xsort:yes/no.
*/

/* (declarations) */
/* (functions) */

int internal_cmd_set (void);

int
cmd_set (void)
{
  struct cmd_set cmd;

  lex_match_id ("SET");

  if (!parse_set (&cmd))
    return CMD_FAILURE;

  if (cmd.sbc_block)
    msg (SW, _("%s is obsolete."),"BLOCK");

  if (cmd.sbc_boxstring)
    msg (SW, _("%s is obsolete."),"BOXSTRING");

  if (cmd.compress != -1)
    {
      msg (MW, _("Active file compression is not yet implemented "
		 "(and probably won't be)."));
      set_compression = cmd.compress == STC_OFF ? 0 : 1;
    }
  if (cmd.scompress != -1)
    set_scompression = cmd.scompress == STC_OFF ? 0 : 1;
  if (cmd.n_cpi != NOT_LONG)
    {
      if (cmd.n_cpi <= 0)
	msg (SE, _("CPI must be greater than 0."));
      else
	set_cpi = cmd.n_cpi;
    }
  if (cmd.sbc_histogram)
    msg (MW, _("%s is obsolete."),"HISTOGRAM");
  if (cmd.n_lpi != NOT_LONG)
    {
      if (cmd.n_lpi <= 0)
	msg (SE, _("LPI must be greater than 0."));
      else
	set_lpi = cmd.n_lpi;
    }
  
  /* Windows compatible syntax. */
  if (cmd.sbc_case)
    msg (SW, _("CASE is not implemented and probably won't be.  If you care, "
	       "complain about it."));
  if (cmd.sbc_cca)
    set_ccx (cmd.s_cca, &set_cc[0], 'A');
  if (cmd.sbc_ccb)
    set_ccx (cmd.s_ccb, &set_cc[1], 'B');
  if (cmd.sbc_ccc)
    set_ccx (cmd.s_ccc, &set_cc[2], 'C');
  if (cmd.sbc_ccd)
    set_ccx (cmd.s_ccd, &set_cc[3], 'D');
  if (cmd.sbc_cce)
    set_ccx (cmd.s_cce, &set_cc[4], 'E');
  if (cmd.dec != -1)
    {
      set_decimal = cmd.dec == STC_DOT ? '.' : ',';
      set_grouping = cmd.dec == STC_DOT ? ',' : '.';
    }
  if (cmd.errors != -1)
    set_routing (cmd.errors, &set_errors);
  if (cmd.headers != -1)
    set_headers = cmd.headers == STC_NO ? 0 : (cmd.headers == STC_YES ? 1 : 2);
  if (cmd.messages != -1)
    set_routing (cmd.messages, &set_messages);
  if (cmd.mexp != -1)
    set_mexpand = cmd.mexp == STC_OFF ? 0 : 1;
  if (cmd.n_miterate != NOT_LONG)
    {
      if (cmd.n_miterate > 0)
	set_miterate = cmd.n_miterate;
      else
	msg (SE, _("Value for MITERATE (%ld) must be greater than 0."),
	     cmd.n_miterate);
    }
  if (cmd.n_mnest != NOT_LONG)
    {
      if (cmd.n_mnest > 0)
	set_mnest = cmd.n_mnest;
      else
	msg (SE, _("Value for MNEST (%ld) must be greater than 0."),
	     cmd.n_mnest);
    }
  if (cmd.mprint != -1)
    set_mprint = cmd.mprint == STC_OFF ? 0 : 1;
  if (cmd.n_mxerrs != NOT_LONG)
    {
      if (set_mxerrs < 1)
	msg (SE, _("MXERRS must be at least 1."));
      else
	set_mxerrs = cmd.n_mxerrs;
    }
  if (cmd.n_mxloops != NOT_LONG)
    {
      if (set_mxloops < 1)
	msg (SE, _("MXLOOPS must be at least 1."));
      else
	set_mxloops = cmd.n_mxloops;
    }
  if (cmd.n_mxmemory != NOT_LONG)
    msg (SE, _("%s is obsolete."),"MXMEMORY");
  if (cmd.n_mxwarns != NOT_LONG)
    set_mxwarns = cmd.n_mxwarns;
  if (cmd.prtbck != -1)
    set_printback = cmd.prtbck == STC_OFF ? 0 : 1;
  if (cmd.s_scripttab)
    msg (SE, _("%s is obsolete."),"SCRIPTTAB");
  if (cmd.s_tbfonts)
    msg (SW, _("TBFONTS not implemented."));
  if (cmd.s_tb1)
    msg (SW, _("TB1 not implemented."));
  if (cmd.undef != -1)
    set_undefined = cmd.undef == STC_NOWARN ? 0 : 1;
  if (cmd.n_workspace != NOT_LONG)
    msg (SE, _("%s is obsolete."),"WORKSPACE");

  /* PC+ compatible syntax. */
  if (cmd.scrn != -1)
    outp_enable_device (cmd.scrn == STC_OFF ? 0 : 1, OUTP_DEV_SCREEN);

  if (cmd.automenu != -1)
    msg (SW, _("%s is obsolete."),"AUTOMENU");
  if (cmd.beep != -1)
    msg (SW, _("%s is obsolete."),"BEEP");

  if (cmd.s_cprompt)
    {
      free (set_cprompt);
      set_cprompt = cmd.s_cprompt;
      cmd.s_cprompt = NULL;
    }
  if (cmd.s_dprompt)
    {
      free (set_dprompt);
      set_dprompt = cmd.s_dprompt;
      cmd.s_dprompt = NULL;
    }
  if (cmd.echo != -1)
    set_echo = cmd.echo == STC_OFF ? 0 : 1;
  if (cmd.s_endcmd)
    set_endcmd = cmd.s_endcmd[0];
  if (cmd.eject != -1)
    msg (SW, _("%s is obsolete."),"EJECT");
  if (cmd.errbrk != -1)
    set_errorbreak = cmd.errbrk == STC_OFF ? 0 : 1;
  if (cmd.helpwin != -1)
    msg (SW, _("%s is obsolete."),"HELPWINDOWS");
  if (cmd.inc != -1)
    set_include = cmd.inc == STC_OFF ? 0 : 1;
  if (cmd.menus != -1)
    msg (MW, _("%s is obsolete."),"MENUS");
  if (cmd.null != -1)
    set_nullline = cmd.null == STC_OFF ? 0 : 1;
  if (cmd.more != -1)
    set_more = cmd.more == STC_OFF ? 0 : 1;
  if (cmd.prtr != -1)
    outp_enable_device (cmd.prtr == STC_OFF ? 0 : 1, OUTP_DEV_PRINTER);
  if (cmd.s_prompt)
    {
      free (set_prompt);
      set_prompt = cmd.s_prompt;
      cmd.s_prompt = NULL;
    }
  if (cmd.ptrans != -1)
    msg (SW, _("%s is obsolete."),"PTRANSLATE");
  if (cmd.runrev != -1)
    msg (SW, _("%s is obsolete."),"RUNREVIEW");
  if (cmd.safe == STC_ON)
    set_safer = 1;
  if (cmd.xsort != -1)
    msg (SW, _("%s is obsolete."),"XSORT");

  free_set (&cmd);

  return CMD_SUCCESS;
}

/* Sets custom currency specifier CC having name CC_NAME ('A' through
   'E') to correspond to the settings in CC_STRING. */
static int
set_ccx (const char *cc_string, struct set_cust_currency * cc, int cc_name)
{
  if (strlen (cc_string) > 16)
    {
      msg (SE, _("CC%c: Length of custom currency string `%s' (%d) "
		 "exceeds maximum length of 16."),
	   cc_name, cc_string, strlen (cc_string));
      return 0;
    }

  /* Determine separators. */
  {
    const char *sp;
    int n_commas, n_periods;
  
    /* Count the number of commas and periods.  There must be exactly
       three of one or the other. */
    n_commas = n_periods = 0;
    for (sp = cc_string; *sp; sp++)
      if (*sp == ',')
	n_commas++;
      else if (*sp == '.')
	n_periods++;
  
    if (!((n_commas == 3) ^ (n_periods == 3)))
      {
	msg (SE, _("CC%c: Custom currency string `%s' does not contain "
		   "exactly three periods or commas (not both)."),
	     cc_name, cc_string);
	return 0;
      }
    else if (n_commas == 3)
      {
	cc->decimal = '.';
	cc->grouping = ',';
      }
    else
      {
	cc->decimal = ',';
	cc->grouping = '.';
      }
  }
  
  /* Copy cc_string to cc, changing separators to nulls. */
  {
    char *cp;
    
    strcpy (cc->buf, cc_string);
    cp = cc->neg_prefix = cc->buf;

    while (*cp++ != cc->grouping)
      ;
    cp[-1] = '\0';
    cc->prefix = cp;

    while (*cp++ != cc->grouping)
      ;
    cp[-1] = '\0';
    cc->suffix = cp;

    while (*cp++ != cc->grouping)
      ;
    cp[-1] = '\0';
    cc->neg_suffix = cp;
  }
  
  return 1;
}

/* Sets *SETTING, which is a combination of SET_ROUTE_* bits that
   indicates what to do with some sort of output, to the value
   indicated by Q, which is a value provided by the input parser. */
static void
set_routing (int q, int *setting)
{
  switch (q)
    {
    case STC_ON:
      *setting |= SET_ROUTE_DISABLE;
      break;
    case STC_OFF:
      *setting &= ~SET_ROUTE_DISABLE;
      break;
    case STC_TERMINAL:
      *setting &= ~(SET_ROUTE_LISTING | SET_ROUTE_OTHER);
      *setting |= SET_ROUTE_SCREEN;
      break;
    case STC_LISTING:
      *setting &= ~SET_ROUTE_SCREEN;
      *setting |= SET_ROUTE_LISTING | SET_ROUTE_OTHER;
      break;
    case STC_BOTH:
      *setting |= SET_ROUTE_SCREEN | SET_ROUTE_LISTING | SET_ROUTE_OTHER;
      break;
    case STC_NONE:
      *setting &= ~(SET_ROUTE_SCREEN | SET_ROUTE_LISTING | SET_ROUTE_OTHER);
      break;
    default:
      assert (0);
    }
}

static int
stc_custom_pager (struct cmd_set *cmd unused)
{
  lex_match ('=');
#if !USE_INTERNAL_PAGER
  if (lex_match_id ("OFF"))
    {
      if (set_pager)
	free (set_pager);
      set_pager = NULL;
    }
  else
    {
      if (!lex_force_string ())
	return 0;
      if (set_pager)
	free (set_pager);
      set_pager = xstrdup (ds_value (&tokstr));
      lex_get ();
    }
  return 1;
#else /* USE_INTERNAL_PAGER */
  if (match_id (OFF))
    return 1;
  msg (SW, "External pagers not supported.");
  return 0;
#endif /* USE_INTERNAL_PAGER */
}

/* Parses the BLANKS subcommand, which controls the value that
   completely blank fields in numeric data imply.  X, Wnd: Syntax is
   SYSMIS or a numeric value; PC+: Syntax is '.', which is equivalent
   to SYSMIS, or a numeric value. */
static int
stc_custom_blanks (struct cmd_set *cmd unused)
{
  lex_match ('=');
  if ((token == T_ID && lex_id_match ("SYSMIS", tokid))
      || (token == T_STRING && !strcmp (tokid, ".")))
    {
      lex_get ();
      set_blanks = SYSMIS;
    }
  else
    {
      if (!lex_force_num ())
	return 0;
      set_blanks = tokval;
      lex_get ();
    }
  return 1;
}

static int
stc_custom_length (struct cmd_set *cmd unused)
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

  /* FIXME: Set page length. */
  return 1;
}

static int
stc_custom_results (struct cmd_set *cmd unused)
{
  struct tuple
    {	
      const char *s;	
      int v;
    };

  static struct tuple tab[] =
    {
      {"ON", STC_ON},
      {"OFF", STC_OFF},
      {"TERMINAL", STC_TERMINAL},
      {"LISTING", STC_LISTING},
      {"BOTH", STC_BOTH},
      {"NONE", STC_NONE},
      {NULL, 0},
    };

  struct tuple *t;

  lex_match ('=');

  if (token != T_ID)
    {
      msg (SE, _("Missing identifier in RESULTS subcommand."));
      return 0;
    }
  
  for (t = tab; t->s; t++)
    if (lex_id_match (t->s, tokid))
      {
	lex_get ();
	set_routing (t->v, &set_results);
	return 1;
      }
  msg (SE, _("Unrecognized identifier in RESULTS subcommand."));
  return 0;
}

static int
stc_custom_seed (struct cmd_set *cmd unused)
{
  lex_match ('=');
  if (lex_match_id ("RANDOM"))
    set_seed = NOT_LONG;
  else
    {
      if (!lex_force_num ())
	return 0;
      set_seed = tokval;
      lex_get ();
    }
  set_seed_used=1;
  return 1;
}

static int
stc_custom_width (struct cmd_set *cmd unused)
{
  int page_width;

  lex_match ('=');
  if (lex_match_id ("NARROW"))
    page_width = 79;
  else if (lex_match_id ("WIDE"))
    page_width = 131;
  else
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 1)
	{
	  msg (SE, _("WIDTH must be at least 1."));
	  return 0;
	}
      page_width = lex_integer ();
      lex_get ();
    }

  /* FIXME: Set page width. */
  return 1;
}

/* Parses FORMAT subcommand, which consists of a numeric format
   specifier. */
static int
stc_custom_format (struct cmd_set *cmd unused)
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

  set_format = fmt;
  return 1;
}

static int
stc_custom_journal (struct cmd_set *cmd unused)
{
  lex_match ('=');
  if (lex_match_id ("ON"))
    set_journaling = 1;
  else if (lex_match_id ("OFF"))
    set_journaling = 0;
  if (token == T_STRING)
    {
      set_journal = xstrdup (ds_value (&tokstr));
      lex_get ();
    }
  return 1;
}

/* Parses COLOR subcommand.  PC+: either ON or OFF or two or three
   comma-delimited numbers inside parentheses. */
static int
stc_custom_color (struct cmd_set *cmd unused)
{
  msg (MW, _("%s is obsolete."),"COLOR");

  lex_match ('=');
  if (!lex_match_id ("ON") && !lex_match_id ("YES") && !lex_match_id ("OFF") && !lex_match_id ("NO"))
    {
      if (!lex_force_match ('('))
	return 0;
      if (!lex_match ('*'))
	{
	  if (!lex_force_int ())
	    return 0;
	  if (lex_integer () < 0 || lex_integer () > 15)
	    {
	      msg (SE, _("Text color must be in range 0-15."));
	      return 0;
	    }
	  lex_get ();
	}
      if (!lex_force_match (','))
	return 0;
      if (!lex_match ('*'))
	{
	  if (!lex_force_int ())
	    return 0;
	  if (lex_integer () < 0 || lex_integer () > 7)
	    {
	      msg (SE, _("Background color must be in range 0-7."));
	      return 0;
	    }
	  lex_get ();
	}
      if (lex_match (',') && !lex_match ('*'))
	{
	  if (!lex_force_int ())
	    return 0;
	  if (lex_integer () < 0 || lex_integer () > 7)
	    {
	      msg (SE, _("Border color must be in range 0-7."));
	      return 0;
	    }
	  lex_get ();
	}
      if (!lex_force_match (')'))
	return 0;
    }
  return 1;
}

static int
stc_custom_listing (struct cmd_set *cmd unused)
{
  lex_match ('=');
  if (lex_match_id ("ON") || lex_match_id ("YES"))
    outp_enable_device (1, OUTP_DEV_LISTING);
  else if (lex_match_id ("OFF") || lex_match_id ("NO"))
    outp_enable_device (0, OUTP_DEV_LISTING);
  else
    {
      /* FIXME */
    }

  return 0;
}

static int
stc_custom_disk (struct cmd_set *cmd unused)
{
  stc_custom_listing (cmd);
  return 0;
}

static int
stc_custom_log (struct cmd_set *cmd unused)
{ 
  stc_custom_journal (cmd);
  return 0;
}

static int
stc_custom_rcolor (struct cmd_set *cmd unused)
{
  msg (SW, _("%s is obsolete."),"RCOLOR");

  lex_match ('=');
  if (!lex_force_match ('('))
    return 0;

  if (!lex_match ('*'))
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 0 || lex_integer () > 6)
	{
	  msg (SE, _("Lower window color must be between 0 and 6."));
	  return 0;
	}
      lex_get ();
    }
  if (!lex_force_match (','))
    return 0;

  if (!lex_match ('*'))
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 0 || lex_integer () > 6)
	{
	  msg (SE, _("Upper window color must be between 0 and 6."));
	  return 0;
	}
      lex_get ();
    }

  if (lex_match (',') && !lex_match ('*'))
    {
      if (!lex_force_int ())
	return 0;
      if (lex_integer () < 0 || lex_integer () > 6)
	{
	  msg (SE, _("Frame color must be between 0 and 6."));
	  return 0;
	}
      lex_get ();
    }
  return 1;
}

static int
stc_custom_viewlength (struct cmd_set *cmd unused)
{
  if (lex_match_id ("MINIMUM"))
    set_viewlength = 25;
  else if (lex_match_id ("MEDIAN"))
    set_viewlength = 43;	/* This is not correct for VGA displays. */
  else if (lex_match_id ("MAXIMUM"))
    set_viewlength = 43;
  else
    {
      if (!lex_force_int ())
	return 0;
#if __MSDOS__
      if (lex_integer () >= (43 + 25) / 2)
	set_viewlength = 43;
      else
	set_viewlength = 25;
#else /* not dos */
      set_viewlength = lex_integer ();
#endif /* not dos */
      lex_get ();
    }

#if __MSDOS__
  msg (SW, _("VIEWLENGTH not implemented."));
#endif /* dos */
  return 1;
}

static int
stc_custom_workdev (struct cmd_set *cmd unused)
{
  char c[2];

  msg (SW, _("%s is obsolete."),"WORKDEV");

  c[1] = 0;
  for (*c = 'A'; *c <= 'Z'; (*c)++)
    if (token == T_ID && lex_id_match (c, tokid))
      {
	lex_get ();
	return 1;
      }
  msg (SE, _("Drive letter expected in WORKDEV subcommand."));
  return 0;
}

/*
   Local Variables:
   mode: c
   End:
*/
