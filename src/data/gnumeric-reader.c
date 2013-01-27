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

#include "c-xvasprintf.h"

#if !GNM_SUPPORT

struct casereader *
gnumeric_open_reader (struct spreadsheet_read_info *gri, struct spreadsheet_read_options *opts, struct dictionary **dict)
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
  xmlChar *name;

  int start_col;
  int stop_col;
  int start_row;
  int stop_row;

  int maxcol;
  int maxrow;

  z_off_t offset;
};


struct gnumeric_reader
{
  struct spreadsheet spreadsheet;

  xmlTextReaderPtr xtr;
  gzFile gz;

  enum reader_state state;

  int row;
  int col;
  int min_col;
  int node_type;
  int sheet_index;

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
};


const char *
gnumeric_get_sheet_name (struct spreadsheet *s, int n)
{
  struct gnumeric_reader *gr = (struct gnumeric_reader *) s;
  assert (n < s->sheets);

  return gr->sheets[n].name;
}


static void process_node (struct gnumeric_reader *r);


char *
gnumeric_get_sheet_range (struct spreadsheet *s, int n)
{
  int ret;
  struct gnumeric_reader *gr = (struct gnumeric_reader *) s;
  
  assert (n < s->sheets);

  while ( 
	 (gr->sheets[n].stop_col == -1)
	 && 
	 (1 == (ret = xmlTextReaderRead (gr->xtr)))
	  )
    {
      process_node (gr);
    }


  return create_cell_ref (
			  gr->sheets[n].start_col,
			  gr->sheets[n].start_row,
			  gr->sheets[n].stop_col,
			  gr->sheets[n].stop_row);
}


static void
gnm_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  int i;
  struct gnumeric_reader *r = r_;
  if ( r == NULL)
	return ;

  if ( r->xtr)
    xmlFreeTextReader (r->xtr);

  if ( ! r->used_first_case )
    case_unref (r->first_case);

  caseproto_unref (r->proto);

  for (i = 0; i < r->spreadsheet.sheets; ++i)
    {
      xmlFree (r->sheets[i].name);
    }
    
  free (r->sheets);

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
    case STATE_PRE_INIT:
      r->sheet_index = -1;
      if (0 == xmlStrcasecmp (name, _xml("gnm:SheetNameIndex")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SHEET_COUNT;
	  r->spreadsheet.sheets = 0;
	}
      break;

    case STATE_SHEET_COUNT:
      if (0 == xmlStrcasecmp (name, _xml("gnm:SheetName")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  struct sheet_detail *sd ;
	  r->spreadsheet.sheets++;
	  r->sheets = xrealloc (r->sheets, r->spreadsheet.sheets * sizeof *r->sheets);
	  sd = &r->sheets[r->spreadsheet.sheets - 1];
	  sd->start_col = sd->stop_col = sd->start_row = sd->stop_row = -1;
	  sd->offset = -1;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:SheetNameIndex")) &&
	  XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_INIT;
	}
      else if (XML_READER_TYPE_TEXT == r->node_type)
	{
	  r->sheets [r->spreadsheet.sheets - 1].name = xmlTextReaderValue (r->xtr);
	}
      break;

    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  ++r->sheet_index;
	  r->state = STATE_SHEET_START;
	  r->sheets[r->sheet_index].offset = gztell (r->gz);
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
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Sheet"))  &&
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
	  else if (r->target_sheet_index == r->sheet_index + 1)
	    {
	      r->state = STATE_SHEET_FOUND;
	    }
	  else if (r->target_sheet_index == -1)
	    {
	      r->state = STATE_SHEET_FOUND;
	    }
	}
      break;
    case STATE_SHEET_FOUND:
      if (0 == xmlStrcasecmp (name, _xml("gnm:Cells"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->min_col = INT_MAX;
	  if (! xmlTextReaderIsEmptyElement (r->xtr))
	    r->state = STATE_CELLS_START;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:MaxRow"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_MAXROW;
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:MaxCol"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_MAXCOL;
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
      else if (r->node_type == XML_READER_TYPE_TEXT)
	{
	  xmlChar *value = xmlTextReaderValue (r->xtr);
	  r->sheets[r->sheet_index].maxrow = _xmlchar_to_int (value);
	  xmlFree (value);
	}
      break;
    case STATE_MAXCOL:
      if (0 == xmlStrcasecmp (name, _xml("gnm:MaxCol"))  &&
	  XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SHEET_FOUND;
	}
      else if (r->node_type == XML_READER_TYPE_TEXT)
	{
	  xmlChar *value = xmlTextReaderValue (r->xtr);
	  r->sheets[r->sheet_index].maxcol = _xmlchar_to_int (value);
	  xmlFree (value);
	}
      break;
    case STATE_CELLS_START:
      if (0 == xmlStrcasecmp (name, _xml ("gnm:Cell"))  &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  xmlChar *attr = NULL;
	  r->state = STATE_CELL;

	  attr = xmlTextReaderGetAttribute (r->xtr, _xml ("Col"));
	  r->col =  _xmlchar_to_int (attr);
	  free (attr);

	  if (r->col < r->min_col)
	    r->min_col = r->col;

	  attr = xmlTextReaderGetAttribute (r->xtr, _xml ("Row"));
	  r->row = _xmlchar_to_int (attr);
	  free (attr);
	  if (r->sheets[r->sheet_index].start_row == -1)
	    {
	      r->sheets[r->sheet_index].start_row = r->row;
	    }

	  if (r->sheets[r->sheet_index].start_col == -1)
	    {
	      r->sheets[r->sheet_index].start_col = r->col;
	    }
	}
      else if (0 == xmlStrcasecmp (name, _xml("gnm:Cells"))  &&
	       XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->sheets[r->sheet_index].stop_col = r->col;
	  r->sheets[r->sheet_index].stop_row = r->row;
	  r->state = STATE_SHEET_NAME;
	}
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


void 
gnumeric_destroy (struct spreadsheet *s)
{
  struct gnumeric_reader *r = (struct gnumeric *) s;
  gnm_file_casereader_destroy (NULL, s);
}

struct spreadsheet *
gnumeric_probe (const char *filename)
{
  int ret;
  struct gnumeric_reader *r = NULL;
  xmlTextReaderPtr xtr;

  gzFile gz;
  gz = gzopen (filename, "r");

  if (NULL == gz)
    return NULL;

  xtr = xmlReaderForIO ((xmlInputReadCallback) gzread,
                           (xmlInputCloseCallback) gzclose, gz,
			   NULL, NULL, 0);

  if (xtr == NULL)
    {
      gzclose (gz);
      return NULL;
    }

  r = xzalloc (sizeof *r);
  
  r->gz = gz;
  r->xtr = xtr;
  r->spreadsheet.sheets = -1;
  r->state = STATE_PRE_INIT;

  r->target_sheet = NULL;
  r->target_sheet_index = -1;


  /* Advance to the start of the workbook.
     This gives us some confidence that we are actually dealing with a gnumeric
     spreadsheet.
   */
  while ( (r->state != STATE_INIT )
	  && 1 == (ret = xmlTextReaderRead (r->xtr)))
    {
      process_node (r);
    }

  if (ret != 1)
    {
      /* Not a gnumeric spreadsheet */
      xmlFreeTextReader (r->xtr);
      free (r);
      return NULL;
    }
    
  r->spreadsheet.type = SPREADSHEET_GNUMERIC;
  r->spreadsheet.file_name = filename;
  
  return &r->spreadsheet;
}


struct casereader *
gnumeric_make_reader (struct spreadsheet *spreadsheet,
		      const struct spreadsheet_read_info *gri, 
		      struct spreadsheet_read_options *opts)
{
  struct gnumeric_reader *r = NULL;
  unsigned long int vstart = 0;
  int ret;
  casenumber n_cases = CASENUMBER_MAX;
  int i;
  struct var_spec *var_spec = NULL;
  int n_var_specs = 0;

  r = (struct gnumeric_reader *) (spreadsheet);

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
  r->row = r->col = -1;
  r->sheet_index = -1;

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
  if ( opts->cell_range)
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
	  int i;
	  var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));
	  for (i = n_var_specs; i <= idx; ++i)
	  {
	    var_spec [i].name = NULL;
	    var_spec [i].width = -1;
	    var_spec [i].first_value = NULL;
	  }
	  n_var_specs =  idx + 1 ;
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

  int x = 0;
  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      if ( (var_spec[i].name == NULL) && (var_spec[i].first_value == NULL))
	continue;

      const struct variable *var = dict_get_var (r->dict, x++);

      convert_xml_string_to_value (r->first_case, var,
				   var_spec[i].first_value);
    }

  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      free (var_spec[i].first_value);
      free (var_spec[i].name);
    }

  free (var_spec);
  
  
  if (opts->cell_range == NULL)
    {
      opts->cell_range = c_xasprintf ("%c%d:%c%ld", 
				       r->start_col + 'A',
				       r->start_row,
				       r->stop_col + 'A' + caseproto_get_n_widths (r->proto),
				       r->start_row + n_cases);
    }
  
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
  int current_row = r->row;

  if ( !r->used_first_case )
    {
      r->used_first_case = true;
      return r->first_case;
    }

  c = case_create (r->proto);
  case_set_missing (c);

  if (r->start_col == -1)
    r->start_col = r->min_col;

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
