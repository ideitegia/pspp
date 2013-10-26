/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013  Free Software Foundation, Inc.

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

#include "libpspp/message.h"
#include "libpspp/misc.h"

#include "gl/minmax.h"
#include "gl/c-strtod.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

#include "spreadsheet-reader.h"

#if !GNM_READ_SUPPORT

struct casereader *
gnumeric_open_reader (const struct spreadsheet_read_options *opts, struct dictionary **dict)
{
  msg (ME, _("Support for %s files was not compiled into this installation of PSPP"), "Gnumeric");

  return NULL;
}

#else

#include "data/gnumeric-reader.h"

#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <zlib.h>

#include "data/format.h"
#include "data/data-in.h"
#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/dictionary.h"
#include "data/identifier.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"


/* Shamelessly lifted from the Gnumeric sources:
   https://git.gnome.org/browse/gnumeric/tree/src/value.h
 */
enum gnm_value_type
{
  VALUE_EMPTY   = 10,
  VALUE_BOOLEAN = 20,
  VALUE_INTEGER = 30, /* Note, this was removed from gnumeric in 2006 - old versions may of
			 course still be around. New ones are supposed to use float.*/
  VALUE_FLOAT   = 40,
  VALUE_ERROR   = 50,
  VALUE_STRING  = 60,
  VALUE_CELLRANGE  = 70,
  VALUE_ARRAY   = 80
};



static void gnm_file_casereader_destroy (struct casereader *, void *);

static struct ccase *gnm_file_casereader_read (struct casereader *, void *);


static const struct casereader_class gnm_file_casereader_class =
  {
    gnm_file_casereader_read,
    gnm_file_casereader_destroy,
    NULL,
    NULL,
  };

enum reader_state
  {
    STATE_PRE_INIT = 0,        /* Initial state */
    STATE_SHEET_COUNT,      /* Found the sheet index */
    STATE_INIT ,           /* Other Initial state */
    STATE_SHEET_START,     /* Found the start of a sheet */
    STATE_SHEET_NAME,      /* Found the sheet name */
    STATE_MAXROW,
    STATE_MAXCOL,
    STATE_SHEET_FOUND,     /* Found the sheet that we actually want */
    STATE_CELLS_START,     /* Found the start of the cell array */
    STATE_CELL             /* Found a cell */
  };

struct sheet_detail
{
  /* The name of the sheet (utf8 encoding) */
  char *name;

  int start_col;
  int stop_col;
  int start_row;
  int stop_row;

  int maxcol;
  int maxrow;
};

struct state_data 
{
  /* The libxml reader for this instance */
  xmlTextReaderPtr xtr;

  /* An internal state variable */
  enum reader_state state;

  int node_type;
  int current_sheet;

  int row;
  int col;

  int min_col;
};


static void
state_data_destroy (struct state_data *sd)
{
  xmlFreeTextReader (sd->xtr);
}


struct gnumeric_reader
{
  struct spreadsheet spreadsheet;
  int ref_cnt;

  struct state_data rsd;
  struct state_data msd;

  int start_col;
  int stop_col;
  int start_row;
  int stop_row;
  
  struct sheet_detail *sheets;

  const xmlChar *target_sheet;
  int target_sheet_index;

  struct caseproto *proto;
  struct dictionary *dict;
  struct ccase *first_case;
  bool used_first_case;

  enum gnm_value_type vtype;
};


void
gnumeric_destroy (struct spreadsheet *s)
{
  struct gnumeric_reader *r = (struct gnumeric_reader *) s;

  if (0 == --r->ref_cnt)
    {
      int i;

      for (i = 0; i < s->n_sheets; ++i)
	{
	  xmlFree (r->sheets[i].name);
	}
    
      free (r->sheets);
      state_data_destroy (&r->msd);

      free (r);
    }
}


const char *
gnumeric_get_sheet_name (struct spreadsheet *s, int n)
{
  struct gnumeric_reader *gr = (struct gnumeric_reader *) s;
  assert (n < s->n_sheets);

  return gr->sheets[n].name; 
}


static void process_node (struct gnumeric_reader *r, struct state_data *sd);



char *
gnumeric_get_sheet_range (struct spreadsheet *s, int n)
{
  int ret;
  struct gnumeric_reader *gr = (struct gnumeric_reader *) s;
  
  assert (n < s->n_sheets);

  while ( 
	 (gr->sheets[n].stop_col == -1)
	 && 
	 (1 == (ret = xmlTextReaderRead (gr->msd.xtr)))
	  )
    {
      process_node (gr, &gr->msd);
    }

  return create_cell_range (
			  gr->sheets[n].start_col,
			  gr->sheets[n].start_row,
			  gr->sheets[n].stop_col,
			  gr->sheets[n].stop_row);
}


static void
gnm_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct gnumeric_reader *r = r_;

  if ( r == NULL)
	return ;

  state_data_destroy (&r->rsd);

  if (r->first_case &&  ! r->used_first_case )
    case_unref (r->first_case);

  if (r->proto) 
    caseproto_unref (r->proto);

  gnumeric_destroy (&r->spreadsheet);
}


static void
process_node (struct gnumeric_reader *r, struct state_data *sd)
{
  xmlChar *name = xmlTextReaderName (sd->xtr);
  if (name == NULL)
    name = xmlStrdup (_xml ("--"));

  sd->node_type = xmlTextReaderNodeType (sd->xtr);

  switch (sd->state)
    {
    case STATE_PRE_INIT:
      sd->current_sheet = -1;
      if (0 == xmlStrcasecmp (name, _xml("gnm:SheetNameIndex")) &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_SHEET_COUNT;
	}
      break;

    case STATE_SHEET_COUNT:
      if (0 == xmlStrcasecmp (name, _xml("gnm:SheetName")) &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  ++sd->current_sheet;
	  if (sd->current_sheet + 1 > r->spreadsheet.n_sheets)
	    {
	      struct sheet_detail *detail ;
	      r->sheets = xrealloc (r->sheets, (sd->current_sheet + 1) * sizeof *r->sheets);
	      detail = &r->sheets[sd->current_sheet];
	      detail->start_col = detail->stop_col = detail->start_row = detail->stop_row = -1;
	      detail->name = NULL;
	      r->spreadsheet.n_sheets = sd->current_sheet + 1;
	    }
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:SheetNameIndex")) &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_INIT;
	  sd->current_sheet = -1;
	}
      else if (XML_READER_TYPE_TEXT == sd->node_type)
	{
	  if ( r->sheets [r->spreadsheet.n_sheets - 1].name == NULL)
	    r->sheets [r->spreadsheet.n_sheets - 1].name = CHAR_CAST (char *, xmlTextReaderValue (sd->xtr));
	}
      break;

    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet")) &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  ++sd->current_sheet;
	  sd->state = STATE_SHEET_START;
	}
      break;
    case STATE_SHEET_START:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Name"))  &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_SHEET_NAME;
	}
      break;
    case STATE_SHEET_NAME:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Name"))  &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_INIT;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet"))  &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_INIT;
	}
      else if (XML_READER_TYPE_TEXT == sd->node_type)
	{
       	  if ( r->target_sheet != NULL)
	    {
	      xmlChar *value = xmlTextReaderValue (sd->xtr);
	      if ( 0 == xmlStrcmp (value, r->target_sheet))
		sd->state = STATE_SHEET_FOUND;
	      free (value);
	    }
	  else if (r->target_sheet_index == sd->current_sheet + 1)
	    {
	      sd->state = STATE_SHEET_FOUND;
	    }
	  else if (r->target_sheet_index == -1)
	    {
	      sd->state = STATE_SHEET_FOUND;
	    }
	}
      break;
    case STATE_SHEET_FOUND:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Cells"))  &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  sd->min_col = INT_MAX;
	  if (! xmlTextReaderIsEmptyElement (sd->xtr))
	    sd->state = STATE_CELLS_START;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:MaxRow"))  &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_MAXROW;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:MaxCol"))  &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_MAXCOL;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet"))  &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
      	  sd->state = STATE_INIT;
	}
      break;
    case STATE_MAXROW:
      if (0 == xmlStrcasecmp (name, _xml("gnm:MaxRow"))  &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_SHEET_FOUND;
	}
      else if (sd->node_type == XML_READER_TYPE_TEXT)
	{
	  xmlChar *value = xmlTextReaderValue (sd->xtr);
	  r->sheets[sd->current_sheet].maxrow = _xmlchar_to_int (value);
	  xmlFree (value);
	}
      break;
    case STATE_MAXCOL:
      if (0 == xmlStrcasecmp (name, _xml("gnm:MaxCol"))  &&
	  XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_SHEET_FOUND;
	}
      else if (sd->node_type == XML_READER_TYPE_TEXT)
	{
	  xmlChar *value = xmlTextReaderValue (sd->xtr);
	  r->sheets[sd->current_sheet].maxcol = _xmlchar_to_int (value);
	  xmlFree (value);
	}
      break;
    case STATE_CELLS_START:
      if (0 == xmlStrcasecmp (name, _xml ("gnm:Cell"))  &&
	  XML_READER_TYPE_ELEMENT  == sd->node_type)
	{
	  xmlChar *attr = NULL;

	  attr = xmlTextReaderGetAttribute (sd->xtr, _xml ("Col"));
	  sd->col =  _xmlchar_to_int (attr);
	  free (attr);

	  if (sd->col < sd->min_col)
	    sd->min_col = sd->col;

	  attr = xmlTextReaderGetAttribute (sd->xtr, _xml ("Row"));
	  sd->row = _xmlchar_to_int (attr);
	  free (attr);

	  if (r->sheets[sd->current_sheet].start_row == -1)
	    {
	      r->sheets[sd->current_sheet].start_row = sd->row;
	    }

	  if (r->sheets[sd->current_sheet].start_col == -1)
	    {
	      r->sheets[sd->current_sheet].start_col = sd->col;
	    }
	  if (! xmlTextReaderIsEmptyElement (sd->xtr))
	    sd->state = STATE_CELL;
	}
      else if ( (0 == xmlStrcasecmp (name, _xml("gnm:Cells")))  &&  (XML_READER_TYPE_END_ELEMENT  == sd->node_type) )
	{
	  r->sheets[sd->current_sheet].stop_col = sd->col;
	  r->sheets[sd->current_sheet].stop_row = sd->row;
	  sd->state = STATE_SHEET_NAME;
	}
      break;
    case STATE_CELL:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Cell"))  && XML_READER_TYPE_END_ELEMENT  == sd->node_type)
	{
	  sd->state = STATE_CELLS_START;
	}
      break;
    default:
      break;
    };

  xmlFree (name);
}


/*
   Sets the VAR of case C, to the value corresponding to the xml string XV
 */
static void
convert_xml_string_to_value (struct ccase *c, const struct variable *var,
			     const xmlChar *xv, enum gnm_value_type type, int col, int row)
{
  union value *v = case_data_rw (c, var);

  if (xv == NULL)
    value_set_missing (v, var_get_width (var));
  else if ( var_is_alpha (var))
    value_copy_str_rpad (v, var_get_width (var), xv, ' ');
  else if (type == VALUE_FLOAT || type == VALUE_INTEGER)
    {
      const char *text = CHAR_CAST (const char *, xv);
      char *endptr;

      errno = 0;
      v->f = c_strtod (text, &endptr);
      if ( errno != 0 || endptr == text)
	v->f = SYSMIS;
    }
  else
    {
      const char *text = CHAR_CAST (const char *, xv);

      const struct fmt_spec *fmt = var_get_write_format (var);

      char *m = data_in (ss_cstr (text), "UTF-8",
			 fmt->type,
			 v,
			 var_get_width (var),
			 "UTF-8");
      
      if (m)
	{
	  char buf [FMT_STRING_LEN_MAX + 1];
	  char *cell = create_cell_ref (col, row);
	  
	  msg (MW, _("Cannot convert the value in the spreadsheet cell %s to format (%s): %s"), 
	       cell, fmt_to_string (fmt, buf), m);
	  free (cell);
	}
      free (m);
    }
}

struct var_spec
{
  char *name;
  int width;
  xmlChar *first_value;
  int first_type;
};


static void
gnumeric_error_handler (void *ctx, const char *mesg,
			UNUSED xmlParserSeverities sev, xmlTextReaderLocatorPtr loc)
{
  struct gnumeric_reader *r = ctx;
       
  msg (MW, _("There was a problem whilst reading the %s file `%s' (near line %d): `%s'"),
       "Gnumeric",
       r->spreadsheet.file_name,
       xmlTextReaderLocatorLineNumber (loc),
       mesg);
}

static struct gnumeric_reader *
gnumeric_reopen (struct gnumeric_reader *r, const char *filename, bool show_errors)
{  
  int ret = -1;
  struct state_data *sd;

  xmlTextReaderPtr xtr;
  gzFile gz;

  assert (r == NULL || filename == NULL);

  if (filename)
    {
      gz = gzopen (filename, "r");
    }
  else
    {
      gz = gzopen (r->spreadsheet.file_name, "r");
    }

  if (NULL == gz)
    return NULL;


  xtr = xmlReaderForIO ((xmlInputReadCallback) gzread,
			(xmlInputCloseCallback) gzclose, gz,
			NULL, NULL,
			show_errors ? 0 : (XML_PARSE_NOERROR | XML_PARSE_NOWARNING) );

  if (xtr == NULL)
    {
      gzclose (gz);
      return NULL;
    }

  if (r == NULL)
    {
      r = xzalloc (sizeof *r);
      r->spreadsheet.n_sheets = -1;
      r->spreadsheet.file_name = filename;
      sd = &r->msd;
    }
  else
    {
      sd = &r->rsd;
    }
  
  if (show_errors) 
    xmlTextReaderSetErrorHandler (xtr, gnumeric_error_handler, r);

  r->target_sheet = NULL;
  r->target_sheet_index = -1;

  sd->row = sd->col = -1;
  sd->state = STATE_PRE_INIT;
  sd->xtr = xtr;
  r->ref_cnt++;

  /* Advance to the start of the workbook.
     This gives us some confidence that we are actually dealing with a gnumeric
     spreadsheet.
   */
  while ( (sd->state != STATE_INIT )
	  && 1 == (ret = xmlTextReaderRead (sd->xtr)))
    {
      process_node (r, sd);
    }


  if ( ret != 1)
    {
      /* Does not seem to be a gnumeric file */
      xmlFreeTextReader (sd->xtr);
      free (r);
      return NULL;
    }

  r->spreadsheet.type = SPREADSHEET_GNUMERIC;

  if (show_errors)
    {
      const xmlChar *enc = xmlTextReaderConstEncoding (sd->xtr);
      xmlCharEncoding xce = xmlParseCharEncoding (CHAR_CAST (const char *, enc));

      if ( XML_CHAR_ENCODING_UTF8 != xce)
	{
	  /* I have been told that ALL gnumeric files are UTF8 encoded.  If that is correct, this 
	     can never happen. */
	  msg (MW, _("The gnumeric file `%s' is encoded as %s instead of the usual UTF-8 encoding. "
		     "Any non-ascii characters will be incorrectly imported."),
	       r->spreadsheet.file_name,
	       enc);
	}
    }

  return r;
}


struct spreadsheet *
gnumeric_probe (const char *filename, bool report_errors)
{
  struct gnumeric_reader *r = gnumeric_reopen (NULL, filename, report_errors);

  return &r->spreadsheet;
}


struct casereader *
gnumeric_make_reader (struct spreadsheet *spreadsheet,
		      const struct spreadsheet_read_options *opts)
{
  int type = 0;
  int x = 0;
  struct gnumeric_reader *r = NULL;
  unsigned long int vstart = 0;
  int ret;
  casenumber n_cases = CASENUMBER_MAX;
  int i;
  struct var_spec *var_spec = NULL;
  int n_var_specs = 0;

  r = (struct gnumeric_reader *) (spreadsheet);

  r = gnumeric_reopen (r, NULL, true);

  if ( opts->cell_range )
    {
      if ( ! convert_cell_ref (opts->cell_range,
			       &r->start_col, &r->start_row,
			       &r->stop_col, &r->stop_row))
	{
	  msg (SE, _("Invalid cell range `%s'"),
	       opts->cell_range);
	  goto error;
	}
    }
  else
    {
      r->start_col = -1;
      r->start_row = 0;
      r->stop_col = -1;
      r->stop_row = -1;
    }

  r->target_sheet = BAD_CAST opts->sheet_name;
  r->target_sheet_index = opts->sheet_index;
  r->rsd.row = r->rsd.col = -1;
  r->rsd.current_sheet = -1;
  r->first_case = NULL;
  r->proto = NULL;

  /* Advance to the start of the cells for the target sheet */
  while ( (r->rsd.state != STATE_CELL || r->rsd.row < r->start_row )
	  && 1 == (ret = xmlTextReaderRead (r->rsd.xtr)))
    {
      xmlChar *value ;
      process_node (r, &r->rsd);
      value = xmlTextReaderValue (r->rsd.xtr);

      if ( r->rsd.state == STATE_MAXROW  && r->rsd.node_type == XML_READER_TYPE_TEXT)
	{
	  n_cases = 1 + _xmlchar_to_int (value) ;
	}
      free (value);
    }

  /* If a range has been given, then  use that to calculate the number
     of cases */
  if ( opts->cell_range)
    {
      n_cases = MIN (n_cases, r->stop_row - r->start_row + 1);
    }

  if ( opts->read_names )
    {
      r->start_row++;
      n_cases --;
    }


  /* Read in the first row of cells,
     including the headers if read_names was set */
  while (
	 (( r->rsd.state == STATE_CELLS_START && r->rsd.row <= r->start_row) || r->rsd.state == STATE_CELL )
	 && (ret = xmlTextReaderRead (r->rsd.xtr))
	 )
    {
      int idx;

      if (r->rsd.state == STATE_CELL && r->rsd.node_type == XML_READER_TYPE_TEXT)
	{
	  xmlChar *attr =
	    xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("ValueType"));

	  type  =  _xmlchar_to_int (attr);

	  xmlFree (attr);
	}

      process_node (r, &r->rsd);

      if ( r->rsd.row > r->start_row ) 
	{
	  xmlChar *attr =
	    xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("ValueType"));
	  
	  r->vtype  =  _xmlchar_to_int (attr);
	  
	  xmlFree (attr);
	  break;
	}

      if ( r->rsd.col < r->start_col ||
	   (r->stop_col != -1 && r->rsd.col > r->stop_col))
	continue;

      idx = r->rsd.col - r->start_col;

      if ( idx  >= n_var_specs )
	{
	  int i;
	  var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));
	  for (i = n_var_specs; i <= idx; ++i)
	  {
	    var_spec [i].name = NULL;
	    var_spec [i].width = -1;
	    var_spec [i].first_value = NULL;
	    var_spec [i].first_type = -1;
	  }
	  n_var_specs =  idx + 1 ;
	}

      var_spec [idx].first_type = type;

      if ( r->rsd.node_type == XML_READER_TYPE_TEXT )
	{
	  xmlChar *value = xmlTextReaderValue (r->rsd.xtr);
	  const char *text  = CHAR_CAST (const char *, value);

	  if ( r->rsd.row < r->start_row)
	    {
	      if ( opts->read_names )
		{
		  var_spec [idx].name = xstrdup (text);
		}
	    }
	  else
	    {
	      var_spec [idx].first_value = xmlStrdup (value);

	      if (-1 ==  var_spec [idx].width )
		var_spec [idx].width = (opts->asw == -1) ?
		  ROUND_UP (strlen(text), SPREADSHEET_DEFAULT_WIDTH) : opts->asw;
	    }

	  free (value);
	}
      else if ( r->rsd.node_type == XML_READER_TYPE_ELEMENT
		&& r->rsd.state == STATE_CELL)
	{
	  if ( r->rsd.row == r->start_row )
	    {
	      xmlChar *attr =
		xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("ValueType"));

	      if ( NULL == attr || VALUE_STRING !=  _xmlchar_to_int (attr))
		var_spec [idx].width = 0;

	      free (attr);
	    }
	}
    }

  {
    const xmlChar *enc = xmlTextReaderConstEncoding (r->rsd.xtr);
    if ( enc == NULL)
      goto error;
    /* Create the dictionary and populate it */
    spreadsheet->dict = r->dict = dict_create (CHAR_CAST (const char *, enc));
  }

  for (i = 0 ; i < n_var_specs ; ++i )
    {
      char *name;

      if ( (var_spec[i].name == NULL) && (var_spec[i].first_value == NULL))
	continue;

      /* Probably no data exists for this variable, so allocate a
	 default width */
      if ( var_spec[i].width == -1 )
	var_spec[i].width = SPREADSHEET_DEFAULT_WIDTH;

      name = dict_make_unique_var_name (r->dict, var_spec[i].name, &vstart);
      dict_create_var (r->dict, name, var_spec[i].width);
      free (name);
    }

  /* Create the first case, and cache it */
  r->used_first_case = false;

  if ( n_var_specs ==  0 )
    {
      msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
           spreadsheet->file_name);
      goto error;
    }

  r->proto = caseproto_ref (dict_get_proto (r->dict));
  r->first_case = case_create (r->proto);
  case_set_missing (r->first_case);


  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      const struct variable *var;

      if ( (var_spec[i].name == NULL) && (var_spec[i].first_value == NULL))
	continue;

      var = dict_get_var (r->dict, x++);
      
      convert_xml_string_to_value (r->first_case, var,
				   var_spec[i].first_value, 
				   var_spec[i].first_type,
				   r->rsd.col + i - 1,
				   r->rsd.row - 1);
    }

  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      free (var_spec[i].first_value);
      free (var_spec[i].name);
    }

  free (var_spec);
  

  return casereader_create_sequential
    (NULL,
     r->proto,
     n_cases,
     &gnm_file_casereader_class, r);


 error:
  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      free (var_spec[i].first_value);
      free (var_spec[i].name);
    }

  free (var_spec);
  dict_destroy (spreadsheet->dict);
  spreadsheet->dict = NULL;

  gnm_file_casereader_destroy (NULL, r);

  return NULL;
};


/* Reads and returns one case from READER's file.  Returns a null
   pointer on failure. */
static struct ccase *
gnm_file_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct ccase *c;
  int ret = 0;

  struct gnumeric_reader *r = r_;
  int current_row = r->rsd.row;

  if ( !r->used_first_case )
    {
      r->used_first_case = true;
      return r->first_case;
    }

  c = case_create (r->proto);
  case_set_missing (c);

  if (r->start_col == -1)
    r->start_col = r->rsd.min_col;


  while ((r->rsd.state == STATE_CELL || r->rsd.state == STATE_CELLS_START )
	 && r->rsd.row == current_row && (ret = xmlTextReaderRead (r->rsd.xtr)))
    {
      process_node (r, &r->rsd);

      if (r->rsd.state == STATE_CELL && r->rsd.node_type == XML_READER_TYPE_ELEMENT)
	{
	  xmlChar *attr =
	    xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("ValueType"));

	  r->vtype  = _xmlchar_to_int (attr);

	  xmlFree (attr);
	}

      if ( r->rsd.col < r->start_col || (r->stop_col != -1 &&
				     r->rsd.col > r->stop_col))
	continue;

      if ( r->rsd.col - r->start_col >= caseproto_get_n_widths (r->proto))
	continue;

      if ( r->stop_row != -1 && r->rsd.row > r->stop_row)
	break;


      if ( r->rsd.node_type == XML_READER_TYPE_TEXT )
	{
	  xmlChar *value = xmlTextReaderValue (r->rsd.xtr);
	  const int idx = r->rsd.col - r->start_col;
	  const struct variable *var = dict_get_var (r->dict, idx);

	  convert_xml_string_to_value (c, var, value, r->vtype, 
				       r->rsd.col, r->rsd.row);

	  xmlFree (value);
	}
    }

  if (ret == 1)
    return c;
  else
    {
      case_unref (c);
      return NULL;
    }
}


#endif /* GNM_READ_SUPPORT */
