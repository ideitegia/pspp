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
#include "error.h"
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
#include "copyleft.h"
#include "random.h"

#include "signal.h"

#if HAVE_LIBTERMCAP
#if HAVE_TERMCAP_H
#include <termcap.h>
#else /* !HAVE_TERMCAP_H */
int tgetent (char *, const char *);
int tgetnum (const char *);
#endif /* !HAVE_TERMCAP_H */
#endif /* !HAVE_LIBTERMCAP */

static int set_errors;
static int set_messages;
static int set_results;

static double set_blanks=SYSMIS;

static struct fmt_spec set_format={FMT_F,8,2};

static struct set_cust_currency set_cc[5];
  
static char *set_journal;
static int set_journaling;

static int set_listing=1;

#if !USE_INTERNAL_PAGER
static char *set_pager=0;
#endif /* !USE_INTERNAL_PAGER */

static unsigned long set_seed;
static int seed_flag=0;

static int long_view=0;
int set_testing_mode=0;
static int set_viewlength;
static int set_viewwidth;

void aux_show_warranty(void);
void aux_show_copying(void);

static const char *route_to_string(int routing);
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
     cpi=integer "x>0" "%s must be greater than 0";
     cprompt=string;
     decimal=dec:dot/comma;
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
     lpi=integer "x>0" "%s must be greater than 0";
     menus=menus:standard/extended;
     messages=messages:on/off/terminal/listing/both/none;
     mexpand=mexp:on/off;
     miterate=integer "x>0" "%s must be greater than 0";
     mnest=integer "x>0" "%s must be greater than 0";
     more=more:on/off;
     mprint=mprint:on/off;
     mxerrs=integer "x >= 1" "%s must be at least 1";
     mxloops=integer "x >=1" "%s must be at least 1";
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
     viewwidth=custom;
     width=custom;
     workdev=custom;
     workspace=integer "x>=1024" "%s must be at least 1 MB";
     xsort=xsort:yes/no.
*/

/* (declarations) */

/* (_functions) */

static int
aux_stc_custom_blanks(struct cmd_set *cmd UNUSED)
{
  if ( set_blanks == SYSMIS ) 
    msg(MM, "SYSMIS");
  else
    msg(MM, "%g", set_blanks);
  return 0;
}


static int
aux_stc_custom_color(struct cmd_set *cmd UNUSED)
{
  msg (MW, _("%s is obsolete."),"COLOR");
  return 0;
}

static int
aux_stc_custom_listing(struct cmd_set *cmd UNUSED)
{
  if ( set_listing ) 
    msg(MM, _("LISTING is ON"));
  else
    msg(MM, _("LISTING is OFF"));

  return 0;
}

static int
aux_stc_custom_disk(struct cmd_set *cmd UNUSED)
{
  return aux_stc_custom_listing(cmd);
}

static int
aux_stc_custom_format(struct cmd_set *cmd UNUSED)
{
  msg(MM, fmt_to_string(&set_format));
  return 0;
}



static int
aux_stc_custom_journal(struct cmd_set *cmd UNUSED)
{
  if (set_journaling) 
    msg(MM, set_journal);
  else
    msg(MM, _("Journalling is off") );
	
  return 0;
}

static int
aux_stc_custom_length(struct cmd_set *cmd UNUSED)
{
  msg(MM, "%d", set_viewlength);
  return 0;
}

static int
aux_stc_custom_log(struct cmd_set *cmd )
{
  return aux_stc_custom_journal (cmd);
}

static int
aux_stc_custom_pager(struct cmd_set *cmd UNUSED)
{
#if !USE_INTERNAL_PAGER 
  if ( set_pager ) 
    msg(MM, set_pager);
  else
    msg(MM, "No pager");
#else /* USE_INTERNAL_PAGER */
  msg (MM, "Internal pager.");
#endif /* USE_INTERNAL_PAGER */

  return 0;
}

static int
aux_stc_custom_rcolor(struct cmd_set *cmd UNUSED)
{
  msg (SW, _("%s is obsolete."),"RCOLOR");
  return 0;
}

static int
aux_stc_custom_results(struct cmd_set *cmd UNUSED)
{
  
  msg(MM, route_to_string(set_results) );

  return 0;
}

static int
aux_stc_custom_seed(struct cmd_set *cmd UNUSED)
{
  msg(MM, "%ld",set_seed);
  return 0;
}

static int
aux_stc_custom_viewlength(struct cmd_set *cmd UNUSED)
{
  msg(MM, "%d", set_viewlength);
  return 0;
}

static int
aux_stc_custom_viewwidth(struct cmd_set *cmd UNUSED)
{
  msg(MM, "%d", set_viewwidth);
  return 0;
}

static int
aux_stc_custom_width(struct cmd_set *cmd UNUSED)
{
  msg(MM, "%d", set_viewwidth);
  return 0;
}

static int
aux_stc_custom_workdev(struct cmd_set *cmd UNUSED)
{
  msg (SW, _("%s is obsolete."),"WORKDEV");
  return 0;
}



/* (aux_functions) 
     warranty=show_warranty;
     copying=show_copying.
*/


static struct cmd_set cmd;

int
cmd_show (void)
{
  lex_match_id ("SHOW");

  if (!aux_parse_set (&cmd))
    return CMD_FAILURE;

  return CMD_SUCCESS;
}

int
cmd_set (void)
{

  if (!parse_set (&cmd))
    return CMD_FAILURE;

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

  if (cmd.sbc_errors)
    set_routing (cmd.errors, &set_errors);
  if (cmd.sbc_messages)
    set_routing (cmd.messages, &set_messages);

  /* PC+ compatible syntax. */
  if (cmd.sbc_screen)
    outp_enable_device (cmd.scrn == STC_OFF ? 0 : 1, OUTP_DEV_SCREEN);
  if (cmd.sbc_printer)
    outp_enable_device (cmd.prtr == STC_OFF ? 0 : 1, OUTP_DEV_PRINTER);

  if (cmd.sbc_automenu )
    msg (SW, _("%s is obsolete."),"AUTOMENU");
  if (cmd.sbc_beep )
    msg (SW, _("%s is obsolete."),"BEEP");
  if (cmd.sbc_block)
    msg (SW, _("%s is obsolete."),"BLOCK");
  if (cmd.sbc_boxstring)
    msg (SW, _("%s is obsolete."),"BOXSTRING");
  if (cmd.sbc_eject )
    msg (SW, _("%s is obsolete."),"EJECT");
  if (cmd.sbc_helpwindows )
    msg (SW, _("%s is obsolete."),"HELPWINDOWS");
  if (cmd.sbc_histogram)
    msg (MW, _("%s is obsolete."),"HISTOGRAM");
  if (cmd.sbc_menus )
    msg (MW, _("%s is obsolete."),"MENUS");
  if (cmd.sbc_ptranslate )
    msg (SW, _("%s is obsolete."),"PTRANSLATE");
  if (cmd.sbc_runreview )
    msg (SW, _("%s is obsolete."),"RUNREVIEW");
  if (cmd.sbc_xsort )
    msg (SW, _("%s is obsolete."),"XSORT");
  if (cmd.sbc_mxmemory )
    msg (SE, _("%s is obsolete."),"MXMEMORY");
  if (cmd.sbc_scripttab)
    msg (SE, _("%s is obsolete."),"SCRIPTTAB");

  if (cmd.sbc_tbfonts)
    msg (SW, _("%s is not yet implemented."),"TBFONTS");
  if (cmd.sbc_tb1 && cmd.s_tb1)
    msg (SW, _("%s is not yet implemented."),"TB1");

  /* Windows compatible syntax. */
  if (cmd.sbc_case)
    msg (SW, _("CASE is not implemented and probably won't be.  "
	"If you care, complain about it."));

  if (cmd.sbc_compression)
    {
      msg (MW, _("Active file compression is not yet implemented "
		 "(and probably won't be)."));
    }

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


const char *
route_to_string(int routing)
{
  static char s[255];
  
  s[0]='\0';

  if ( routing == 0 )
    {
      strcpy(s, _("None"));
      return s;
    }

  if (routing & SET_ROUTE_DISABLE ) 
    {
    strcpy(s, _("Disabled") );
    return s;
    }

  if (routing & SET_ROUTE_SCREEN)
    strcat(s, _("Screen") );
  
  if (routing & SET_ROUTE_LISTING)
    {
      if(s[0] != '\0') 
	strcat(s,", ");
	
      strcat(s, _("Listing") );
    }

  if (routing & SET_ROUTE_OTHER)
    {
      if(s[0] != '\0') 
	strcat(s,", ");
      strcat(s, _("Other") );
    }
 
    
  return s;
  
    
}

/* Sets *SETTING, which is a combination of SET_ROUTE_* bits that
   indicates what to do with some sort of output, to the value
   indicated by Q, which is a value provided by the input parser. */
static void
set_routing (int q, int *setting)
{
  switch (q)
    {
    case STC_OFF:
      *setting |= SET_ROUTE_DISABLE;
      break;
    case STC_ON:
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
stc_custom_pager (struct cmd_set *cmd UNUSED)
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
      set_pager = xstrdup (ds_c_str (&tokstr));
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
stc_custom_blanks (struct cmd_set *cmd UNUSED)
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
stc_custom_length (struct cmd_set *cmd UNUSED)
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

  if ( page_length != -1 ) 
    set_viewlength = page_length;

  return 1;
}

static int
stc_custom_results (struct cmd_set *cmd UNUSED)
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
stc_custom_seed (struct cmd_set *cmd UNUSED)
{
  lex_match ('=');
  if (lex_match_id ("RANDOM"))
    set_seed = random_seed();
  else
    {
      if (!lex_force_num ())
	return 0;
      set_seed = tokval;
      lex_get ();
    }
  seed_flag = 1;

  return 1;
}

static int
stc_custom_width (struct cmd_set *cmd UNUSED)
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

  set_viewwidth = page_width;
  return 1;
}

/* Parses FORMAT subcommand, which consists of a numeric format
   specifier. */
static int
stc_custom_format (struct cmd_set *cmd UNUSED)
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
stc_custom_journal (struct cmd_set *cmd UNUSED)
{
  lex_match ('=');
  if (lex_match_id ("ON"))
    set_journaling = 1;
  else if (lex_match_id ("OFF"))
    set_journaling = 0;
  if (token == T_STRING)
    {
      set_journal = xstrdup (ds_c_str (&tokstr));
      lex_get ();
    }
  return 1;
}

/* Parses COLOR subcommand.  PC+: either ON or OFF or two or three
   comma-delimited numbers inside parentheses. */
static int
stc_custom_color (struct cmd_set *cmd UNUSED)
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
stc_custom_listing (struct cmd_set *cmd UNUSED)
{
  lex_match ('=');
  if (lex_match_id ("ON") || lex_match_id ("YES"))
    set_listing = 1;
  else if (lex_match_id ("OFF") || lex_match_id ("NO"))
    set_listing = 0;
  else
    {
      /* FIXME */
      return 0;
    }
  outp_enable_device (set_listing, OUTP_DEV_LISTING);

  return 1;
}

static int
stc_custom_disk (struct cmd_set *cmd UNUSED)
{
  return stc_custom_listing (cmd);
}

static int
stc_custom_log (struct cmd_set *cmd UNUSED)
{ 
  return stc_custom_journal (cmd);
}

static int
stc_custom_rcolor (struct cmd_set *cmd UNUSED)
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
stc_custom_viewwidth (struct cmd_set *cmd UNUSED)
{
  lex_match ('=');

  if ( !lex_force_int() ) 
    return 0;

  set_viewwidth = lex_integer();
  lex_get();
  
  return 1;
}

static int
stc_custom_viewlength (struct cmd_set *cmd UNUSED)
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
#ifdef __MSDOS__
      if (lex_integer () >= (43 + 25) / 2)
	set_viewlength = 43;
      else
	set_viewlength = 25;
#else /* not dos */
      set_viewlength = lex_integer ();
#endif /* not dos */
      lex_get ();
    }

#ifdef __MSDOS__
  msg (SW, _("%s is not yet implemented."),"VIEWLENGTH");
#endif /* dos */
  return 1;
}

static int
stc_custom_workdev (struct cmd_set *cmd UNUSED)
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



static void 
set_viewport(int sig_num UNUSED)
{
#if HAVE_LIBTERMCAP
  static char term_buffer[16384];
#endif

  set_viewwidth = -1;
  set_viewlength = -1;

#if __DJGPP__ || __BORLANDC__
  {
    struct text_info ti;

    gettextinfo (&ti);
    set_viewlength = max (ti.screenheight, 25);
    set_viewwidth = max (ti.screenwidth, 79);
  }
#elif HAVE_LIBTERMCAP
  {
    char *termtype;
    int success;

    /* This code stolen from termcap.info, though modified. */
    termtype = getenv ("TERM");
    if (!termtype)
      msg (FE, _("Specify a terminal type with the TERM environment variable."));

    success = tgetent (term_buffer, termtype);
    if (success <= 0)
      {
	if (success < 0)
	  msg (IE, _("Could not access the termcap data base."));
	else
	  msg (IE, _("Terminal type `%s' is not defined."), termtype);
      }
    else
      {
	/* NOTE: Do not rely upon tgetnum returning -1 if the value is 
	   not available. It's supposed to do it, but not all platforms 
	   do (eg Cygwin) .
	*/
        if ( -1 != tgetnum("li")) 
	  set_viewlength = tgetnum ("li");

        if ( -1 != tgetnum("co")) 
	  set_viewwidth = tgetnum ("co") - 1;
      }
  }
#endif /* HAVE_LIBTERMCAP */

  /* Try the environment variables */
  if ( -1 ==  set_viewwidth ) 
    { 
      char *s = getenv("COLUMNS");
      if ( s )  set_viewwidth = atoi(s);
    }

  if ( -1 ==  set_viewwidth ) 
    {
      char *s = getenv("LINES");
      if ( s )  set_viewlength = atoi(s);
    }


  /* Last resort.  Use hard coded values */
  if ( 0  >  set_viewwidth ) set_viewwidth = 79;
  if ( 0  >  set_viewlength ) set_viewlength = 24;

}

/* Public functions */

void
init_settings(void)
{
  cmd.s_dprompt = xstrdup (_("data> "));
  cmd.s_cprompt = xstrdup ("    > ");  
  cmd.s_prompt = xstrdup ("PSPP> ");
  cmd.s_endcmd = xstrdup (".");

  assert(cmd.safe == 0 );
  cmd.safe = STC_OFF;

  cmd.dec = STC_DOT;
  cmd.n_cpi[0] = 6;
  cmd.n_lpi[0] = 10;
  cmd.echo = STC_OFF;
  cmd.more = STC_ON;
  cmd.headers = STC_YES;
  cmd.errbrk = STC_OFF;

  cmd.scompress = STC_OFF;
  cmd.undef = STC_WARN;
  cmd.mprint = STC_ON ;
  cmd.prtbck = STC_ON ;
  cmd.null = STC_ON ;
  cmd.inc = STC_ON ;

  set_journal = xstrdup ("pspp.jnl");
  set_journaling = 1;

  cmd.n_mxwarns[0] = 100;
  cmd.n_mxerrs[0] = 100;
  cmd.n_mxloops[0] = 1;
  cmd.n_workspace[0] = 4L * 1024 * 1024;


#if !USE_INTERNAL_PAGER
  {
    char *pager;

    pager = getenv ("STAT_PAGER");
    if (!pager)  set_pager = getenv ("PAGER");

    if (pager)  
      set_pager = xstrdup (pager);
#if DEFAULT_PAGER
    else
      set_pager = xstrdup (DEFAULT_PAGER);
#endif /* DEFAULT_PAGER */
  }
#endif /* !USE_INTERNAL_PAGER */


  {
    int i;
    
    for (i = 0; i < 5; i++)
      {
	struct set_cust_currency *cc = &set_cc[i];
	strcpy (cc->buf, "-");
	cc->neg_prefix = cc->buf;
	cc->prefix = &cc->buf[1];
	cc->suffix = &cc->buf[1];
	cc->neg_suffix = &cc->buf[1];
	cc->decimal = '.';
	cc->grouping = ',';
      }
  }

  if ( ! long_view )
    {
      set_viewport (0);
      signal (SIGWINCH, set_viewport);
    }

}

void
force_long_view(void)
{
  long_view = 1;
  set_viewwidth=9999;
}

int 
safer_mode(void)
{
  return !(cmd.safe != STC_ON) ;
}


/* Set safer mode */
void
make_safe(void)
{
  cmd.safe = STC_ON;
}


char 
get_decimal(void)
{
  return (cmd.dec == STC_DOT ? '.' : ',');
}


char
get_grouping(void)
{
  return (cmd.dec == STC_DOT ? ',' : '.');
}
 

char * 
get_prompt(void)
{
  return cmd.s_prompt;
}

char * 
get_dprompt(void)
{
  return cmd.s_dprompt;
}

char * 
get_cprompt(void)
{
  return cmd.s_cprompt;
}


int
get_echo(void)
{
    return (cmd.echo != STC_OFF );
}


int 
get_errorbreak(void)
{
  return (cmd.errbrk != STC_OFF);
}


int 
get_scompression(void)
{
  return (cmd.scompress != STC_OFF );
}

int
get_undefined(void)
{
  return (cmd.undef != STC_NOWARN);
}

int
get_mxwarns(void)
{  
  return cmd.n_mxwarns[0];
}

int
get_mxerrs(void)
{
  return cmd.n_mxerrs[0];
}

int
get_mprint(void)
{
  return ( cmd.mprint != STC_OFF );
}

int
get_printback(void)
{
  return (cmd.prtbck != STC_OFF );
}

int
get_mxloops(void)
{
  return cmd.n_mxloops[0];
}

int
get_nullline(void)
{
  return (cmd.null != STC_OFF );
}

int
get_include(void)
{
 return (cmd.inc != STC_OFF );
}

unsigned char
get_endcmd(void)
{
  return cmd.s_endcmd[0];
}


size_t
get_max_workspace(void)
{
  return cmd.n_workspace[0];
}

double
get_blanks(void)
{
  return set_blanks;
}

struct fmt_spec 
get_format(void)
{ 
  return set_format;
}

/* CCA through CCE. */
const struct set_cust_currency *
get_cc(int i)
{
  return &set_cc[i];
}

void
aux_show_warranty(void)
{
  msg(MM,lack_of_warranty);
}

void
aux_show_copying(void)
{
  msg(MM,copyleft);
}


int
get_viewlength(void)
{
  return set_viewlength;
}

int
get_viewwidth(void)
{
  return set_viewwidth;
}

const char *
get_pager(void)
{
  return set_pager;
}

/* Return 1 if the seed has been set since the last time this function
   was called.
   Fill the value pointed to by seed with the seed .
*/
int
seed_is_set(unsigned long *seed)
{
  int result = 0;

  *seed = set_seed ;

  if ( seed_flag ) 
    result = 1;
  
  seed_flag = 0;

  return result;
    
}


static int global_algorithm = ENHANCED;
static int cmd_algorithm = ENHANCED;
static int *algorithm = &global_algorithm;

static int syntax = ENHANCED;

/* Set the algorithm option globally */
void 
set_algorithm(int x)
{
  global_algorithm = x;
}

/* Set the algorithm option for this command only */
void 
set_cmd_algorithm(int x)
{
  cmd_algorithm = x; 
  algorithm = &cmd_algorithm;
}

/* Unset the algorithm option for this command */
void
unset_cmd_algorithm(void)
{
  algorithm = &global_algorithm;
}

/* Return the current algorithm setting */
int
get_algorithm(void)
{
  return *algorithm;
}

/* Set the syntax option */
void 
set_syntax(int x)
{
  syntax = x;
}

/* Get the current syntax setting */
int
get_syntax(void)
{
  return syntax;
}


/*
   Local Variables:
   mode: c
   End:
*/
