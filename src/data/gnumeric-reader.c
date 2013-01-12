/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#if !GNM_SUPPORT

struct casereader *
gnumeric_open_reader (struct spreadsheet_read_info *gri, struct dictionary **dict)
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

#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/dictionary.h"
#include "data/identifier.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

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
    STATE_INIT = 0,        /* Initial state */
    STATE_SHEET_START,     /* Found the start of a sheet */
    STATE_SHEET_NAME,      /* Found the sheet name */
    STATE_MAXROW,
    STATE_SHEET_FOUND,     /* Found the sheet that we actually want */
    STATE_CELLS_START,     /* Found the start of the cell array */
    STATE_CELL             /* Found a cell */
  };


struct gnumeric_reader
{
  xmlTextReaderPtr xtr;

  enum reader_state state;
  int row;
  int col;
  int node_type;
  int sheet_index;


  const xmlChar *target_sheet;
  int target_sheet_index;

  int start_row;
  int start_col;
  int stop_row;
  int stop_col;

  struct caseproto *proto;
  struct dictionary *dict;
  struct ccase *first_case;
  bool used_first_case;
};

static void process_node (struct gnumeric_reader *r);


static void
gnm_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct gnumeric_reader *r = r_;
  if ( r == NULL)
	return ;

  if ( r->xtr)
    xmlFreeTextReader (r->xtr);

  if ( ! r->used_first_case )
    case_unref (r->first_case);

  caseproto_unref (r->proto);

  free (r);
}

static void
process_node (struct gnumeric_reader *r)
{
  xmlChar *name = xmlTextReaderName (r->xtr);
  if (name == NULL)
    name = xmlStrdup (_xml ("--"));


  r->node_type = xmlTextReaderNodeType (r->xtr);

  switch ( r->state)
    {
    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  ++r->sheet_index;
	  r->state = STATE_SHEET_START;
	}
      break;
    case STATE_SHEET_START:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Name"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SHEET_NAME;
	}
      break;
    case STATE_SHEET_NAME:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Name"))  &&
	  XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_INIT;
	}
      else if (XML_READER_TYPE_TEXT == r->node_type)
	{
	  if ( r->target_sheet != NULL)
	    {
	      xmlChar *value = xmlTextReaderValue (r->xtr);
	      if ( 0 == xmlStrcmp (value, r->target_sheet))
		r->state = STATE_SHEET_FOUND;
	      free (value);
	    }
	  else if (r->target_sheet_index == r->sheet_index)
	    {
	      r->state = STATE_SHEET_FOUND;
	    }
	}
      break;
    case STATE_SHEET_FOUND:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Cells"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  if (! xmlTextReaderIsEmptyElement (r->xtr))
	    r->state = STATE_CELLS_START;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:MaxRow"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_MAXROW;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet"))  &&
	  XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_INIT;
	}
      break;
    case STATE_MAXROW:
      if (0 == xmlStrcasecmp (name, _xml("gnm:MaxRow"))  &&
	  XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SHEET_FOUND;
	}
    case STATE_CELLS_START:
      if (0 == xmlStrcasecmp (name, _xml ("gnm:Cell"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  xmlChar *attr = NULL;
	  r->state = STATE_CELL;

	  attr = xmlTextReaderGetAttribute (r->xtr, _xml ("Col"));
	  r->col =  _xmlchar_to_int (attr);
	  free (attr);

	  attr = xmlTextReaderGetAttribute (r->xtr, _xml ("Row"));
	  r->row = _xmlchar_to_int (attr);
	  free (attr);
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Cells"))  &&
	       XML_READER_TYPE_END_ELEMENT  == r->node_type)
	r->state = STATE_SHEET_NAME;

      break;
    case STATE_CELL:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Cell"))  &&
			      XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_CELLS_START;
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
			     const xmlChar *xv)
{
  union value *v = case_data_rw (c, var);

  if (xv == NULL)
    value_set_missing (v, var_get_width (var));
  else if ( var_is_alpha (var))
    value_copy_str_rpad (v, var_get_width (var), xv, ' ');
  else
    {
      const char *text = CHAR_CAST (const char *, xv);
      char *endptr;

      errno = 0;
      v->f = c_strtod (text, &endptr);
      if ( errno != 0 || endptr == text)
	v->f = SYSMIS;
    }
}

struct var_spec
{
  char *name;
  int width;
  xmlChar *first_value;
};

struct casereader *
gnumeric_open_reader (struct spreadsheet_read_info *gri, struct dictionary **dict)
{
  unsigned long int vstart = 0;
  int ret;
  casenumber n_cases = CASENUMBER_MAX;
  int i;
  struct var_spec *var_spec = NULL;
  int n_var_specs = 0;

  struct gnumeric_reader *r = NULL;

  gzFile gz = gzopen (gri->file_name, "r");

  if ( NULL == gz)
    {
      msg (ME, _("Error opening `%s' for reading as a Gnumeric file: %s."),
           gri->file_name, strerror (errno));

      goto error;
    }

  r = xzalloc (sizeof *r);

  r->xtr = xmlReaderForIO ((xmlInputReadCallback) gzread,
                           (xmlInputCloseCallback) gzclose, gz,
			   NULL, NULL, 0);

  if ( r->xtr == NULL )
    goto error;

  if ( gri->cell_range )
    {
      if ( ! convert_cell_ref (gri->cell_range,
			       &r->start_col, &r->start_row,
			       &r->stop_col, &r->stop_row))
	{
	  msg (SE, _("Invalid cell range `%s'"),
	       gri->cell_range);
	  goto error;
	}
    }
  else
    {
      r->start_col = 0;
      r->start_row = 0;
      r->stop_col = -1;
      r->stop_row = -1;
    }

  r->state = STATE_INIT;
  r->target_sheet = BAD_CAST gri->sheet_name;
  r->target_sheet_index = gri->sheet_index;
  r->row = r->col = -1;
  r->sheet_index = 0;

  /* Advance to the start of the cells for the target sheet */
  while ( (r->state != STATE_CELL || r->row < r->start_row )
	  && 1 == (ret = xmlTextReaderRead (r->xtr)))
    {
      xmlChar *value ;
      process_node (r);
      value = xmlTextReaderValue (r->xtr);

      if ( r->state == STATE_MAXROW  && r->node_type == XML_READER_TYPE_TEXT)
	{
	  n_cases = 1 + _xmlchar_to_int (value) ;
	}
      free (value);
    }


  /* If a range has been given, then  use that to calculate the number
     of cases */
  if ( gri->cell_range)
    {
      n_cases = MIN (n_cases, r->stop_row - r->start_row + 1);
    }

  if ( gri->read_names )
    {
      r->start_row++;
      n_cases --;
    }

  /* Read in the first row of cells,
     including the headers if read_names was set */
  while (
	 (( r->state == STATE_CELLS_START && r->row <= r->start_row) || r->state == STATE_CELL )
	 && (ret = xmlTextReaderRead (r->xtr))
	 )
    {
      int idx;
      process_node (r);

      if ( r->row > r->start_row ) break;

      if ( r->col < r->start_col ||
	   (r->stop_col != -1 && r->col > r->stop_col))
	continue;

      idx = r->col - r->start_col;

      if ( idx  >= n_var_specs )
	{
	  n_var_specs =  idx + 1 ;
	  var_spec = xrealloc (var_spec, sizeof (*var_spec) * n_var_specs);
	  var_spec [idx].name = NULL;
	  var_spec [idx].width = -1;
	  var_spec [idx].first_value = NULL;
	}

      if ( r->node_type == XML_READER_TYPE_TEXT )
	{
	  xmlChar *value = xmlTextReaderValue (r->xtr);
	  const char *text  = CHAR_CAST (const char *, value);

	  if ( r->row < r->start_row)
	    {
	      if ( gri->read_names )
		{
		  var_spec [idx].name = xstrdup (text);
		}
	    }
	  else
	    {
	      var_spec [idx].first_value = xmlStrdup (value);

	      if (-1 ==  var_spec [idx].width )
		var_spec [idx].width = (gri->asw == -1) ?
		  ROUND_UP (strlen(text), SPREADSHEET_DEFAULT_WIDTH) : gri->asw;
	    }

	  free (value);
	}
      else if ( r->node_type == XML_READER_TYPE_ELEMENT
		&& r->state == STATE_CELL)
	{
	  if ( r->row == r->start_row )
	    {
	      xmlChar *attr =
		xmlTextReaderGetAttribute (r->xtr, _xml ("ValueType"));

	      if ( NULL == attr || 60 !=  _xmlchar_to_int (attr))
		var_spec [idx].width = 0;

	      free (attr);
	    }
	}
    }

  {
    const xmlChar *enc = xmlTextReaderConstEncoding (r->xtr);
    if ( enc == NULL)
      goto error;
    /* Create the dictionary and populate it */
    *dict = r->dict = dict_create (CHAR_CAST (const char *, enc));
  }

  for (i = 0 ; i < n_var_specs ; ++i )
    {
      char *name;

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
           gri->file_name);
      goto error;
    }

  r->proto = caseproto_ref (dict_get_proto (r->dict));
  r->first_case = case_create (r->proto);
  case_set_missing (r->first_case);

  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      const struct variable *var = dict_get_var (r->dict, i);

      convert_xml_string_to_value (r->first_case, var,
				   var_spec[i].first_value);
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
  dict_destroy (*dict);
  *dict = NULL;

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
  int current_row = r->row;

  if ( !r->used_first_case )
    {
      r->used_first_case = true;
      return r->first_case;
    }

  c = case_create (r->proto);
  case_set_missing (c);

  while ((r->state == STATE_CELL || r->state == STATE_CELLS_START )
	 && r->row == current_row && (ret = xmlTextReaderRead (r->xtr)))
    {
      process_node (r);

      if ( r->col < r->start_col || (r->stop_col != -1 &&
				     r->col > r->stop_col))
	continue;

      if ( r->col - r->start_col >= caseproto_get_n_widths (r->proto))
	continue;

      if ( r->stop_row != -1 && r->row > r->stop_row)
	break;

      if ( r->node_type == XML_READER_TYPE_TEXT )
	{
	  xmlChar *value = xmlTextReaderValue (r->xtr);

	  const int idx = r->col - r->start_col;

	  const struct variable *var = dict_get_var (r->dict, idx);

	  convert_xml_string_to_value (c, var, value);

	  free (value);
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


#endif /* GNM_SUPPORT */
