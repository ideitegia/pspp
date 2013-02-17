/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include "data/data-in.h"

#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

#include "ods-reader.h"
#include "spreadsheet-reader.h"

#if !ODF_READ_SUPPORT

struct casereader *
ods_open_reader (const struct spreadsheet_read_options *opts, 
		 struct dictionary **dict)
{
  msg (ME, _("Support for %s files was not compiled into this installation of PSPP"), "OpenDocument");

  return NULL;
}

#else

#include "libpspp/zip-reader.h"


#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <zlib.h>

#include "data/format.h"
#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/dictionary.h"
#include "data/identifier.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

static void ods_file_casereader_destroy (struct casereader *, void *);

static struct ccase *ods_file_casereader_read (struct casereader *, void *);

static const struct casereader_class ods_file_casereader_class =
  {
    ods_file_casereader_read,
    ods_file_casereader_destroy,
    NULL,
    NULL,
  };

enum reader_state
  {
    STATE_INIT = 0,        /* Initial state */
    STATE_SPREADSHEET,     /* Found the start of the spreadsheet doc */
    STATE_TABLE,           /* Found the sheet that we actually want */
    STATE_ROW,             /* Found the start of the cell array */
    STATE_CELL,            /* Found a cell */
    STATE_CELL_CONTENT     /* Found a the text within a cell */
  };

struct ods_reader
{
  struct spreadsheet spreadsheet;

  xmlTextReaderPtr xtr;

  enum reader_state state;
  bool sheet_found;
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
  bool read_names;

  struct string ods_errs;
  int span;
};

static void process_node (struct ods_reader *r);

static void
ods_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct ods_reader *r = r_;
  if ( r == NULL)
    return ;

  if (r->xtr)
    xmlFreeTextReader (r->xtr);

  if ( ! ds_is_empty (&r->ods_errs))
    msg (ME, "%s", ds_cstr (&r->ods_errs));

  ds_destroy (&r->ods_errs);

  if ( ! r->used_first_case )
    case_unref (r->first_case);

  caseproto_unref (r->proto);

  free (r);
}

static void
process_node (struct ods_reader *r)
{
  xmlChar *name = xmlTextReaderName (r->xtr);
  if (name == NULL)
    name = xmlStrdup (_xml ("--"));

  r->node_type = xmlTextReaderNodeType (r->xtr);

  switch ( r->state)
    {
    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("office:spreadsheet")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SPREADSHEET;
	}
      break;
    case STATE_SPREADSHEET:
      if (0 == xmlStrcasecmp (name, _xml("table:table")))
	{
	  if (XML_READER_TYPE_ELEMENT == r->node_type)
	    {
	      r->col = -1;
	      r->row = -1;
	      ++r->sheet_index;
	      if ( r->target_sheet != NULL)
		{
		  xmlChar *value = xmlTextReaderGetAttribute (r->xtr, _xml ("table:name"));
		  if ( 0 == xmlStrcmp (value, r->target_sheet))
		    {
		      r->sheet_found = true;
		      r->state = STATE_TABLE;
		    }
		  free (value);
		}
	      else if (r->target_sheet_index == r->sheet_index)
		{
		  r->sheet_found = true;
		  r->state = STATE_TABLE;
		}
	      else if ( r->target_sheet_index == -1)
		r->state = STATE_TABLE;
	    }
	}
      else if (XML_READER_TYPE_END_ELEMENT  == r->node_type
		   && r->sheet_found)
	{
	  r->state = STATE_INIT;
	}
	break;
    case STATE_TABLE:
      if (0 == xmlStrcasecmp (name, _xml("table:table-row")) )
	{
	  if ( XML_READER_TYPE_ELEMENT  == r->node_type)
	    {
	      if (! xmlTextReaderIsEmptyElement (r->xtr))
		{
		  r->state = STATE_ROW;
		}
	      r->row++;
	      r->span = 1;
	    }
	}
      else if (XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SPREADSHEET;
	}
      break;
    case STATE_ROW:
      if (0 == xmlStrcasecmp (name, _xml ("table:table-cell")))
	{
	  if ( XML_READER_TYPE_ELEMENT  == r->node_type)
	    {
	      xmlChar *value =
		xmlTextReaderGetAttribute (r->xtr,
					   _xml ("table:number-columns-repeated"));
	      r->col += r->span;
	      r->span = value ? _xmlchar_to_int (value) : 1;
	      free (value);
	      if (! xmlTextReaderIsEmptyElement (r->xtr))
		{
		  r->state = STATE_CELL;
		}
	    }
	}
      else if (XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_TABLE;
	  r->col = -1;
	  /* Set the span back to the default */
	  r->span = 1;
	}
      break;
    case STATE_CELL:
      if (0 == xmlStrcasecmp (name, _xml("text:p")))
	{
	  if ( XML_READER_TYPE_ELEMENT  == r->node_type)
	    {
	      r->state = STATE_CELL_CONTENT;
	    }
	}
      else if (XML_READER_TYPE_END_ELEMENT  == r->node_type)
	{
	  r->state = STATE_ROW;
	}
      break;
    case STATE_CELL_CONTENT:
      if (XML_READER_TYPE_TEXT != r->node_type)
	r->state = STATE_CELL;
      break;
    default:
      break;
    };

  xmlFree (name);
}

/* 
   A struct containing the parameters of a cell's value 
   parsed from the xml
*/
struct xml_value
{
  xmlChar *type;
  xmlChar *value;
  xmlChar *text;
};

struct var_spec
{
  char *name;
  struct xml_value firstval;
};


/* Determine the width that a xmv should probably have */
static int
xmv_to_width (const struct xml_value *xmv, int fallback)
{
  int width = SPREADSHEET_DEFAULT_WIDTH;

  /* Non-strings always have zero width */
  if (xmv->type != NULL && 0 != xmlStrcmp (xmv->type, _xml("string")))
    return 0;

  if ( fallback != -1)
    return fallback;

  if ( xmv->value )
    width = ROUND_UP (xmlStrlen (xmv->value),
		      SPREADSHEET_DEFAULT_WIDTH);
  else if ( xmv->text)
    width = ROUND_UP (xmlStrlen (xmv->text),
		      SPREADSHEET_DEFAULT_WIDTH);

  return width;
}

/*
   Sets the VAR of case C, to the value corresponding to the xml data
 */
static void
convert_xml_to_value (struct ccase *c, const struct variable *var,
		      const struct xml_value *xmv)
{
  union value *v = case_data_rw (c, var);

  if (xmv->value == NULL && xmv->text == NULL)
    value_set_missing (v, var_get_width (var));
  else if ( var_is_alpha (var))
    /* Use the text field, because it seems that there is no
       value field for strings */
    value_copy_str_rpad (v, var_get_width (var), xmv->text, ' ');
  else
    {
      const char *text ;
      const struct fmt_spec *fmt = var_get_write_format (var);
      enum fmt_category fc  = fmt_get_category (fmt->type);

      assert ( fc != FMT_CAT_STRING);

      text =
        xmv->value ? CHAR_CAST (const char *, xmv->value) : CHAR_CAST (const char *, xmv->text);

      free (data_in (ss_cstr (text), "UTF-8",
                     fmt->type,
                     v,
                     var_get_width (var),
                     "UTF-8"));
    }
}


/* Try to find out how many sheets there are in the "workbook" */
static int
get_sheet_count (struct zip_reader *zreader)
{
  xmlTextReaderPtr mxtr;
  struct zip_member *meta = NULL;
  meta = zip_member_open (zreader, "meta.xml");

  if ( meta == NULL)
    return -1;

  mxtr = xmlReaderForIO ((xmlInputReadCallback) zip_member_read,
			 (xmlInputCloseCallback) zip_member_finish,
			 meta,   NULL, NULL, XML_PARSE_RECOVER);

  while (1 == xmlTextReaderRead (mxtr))
    {
      xmlChar *name = xmlTextReaderName (mxtr);
      if ( 0 == xmlStrcmp (name, _xml("meta:document-statistic")))
	{
	  xmlChar *attr = xmlTextReaderGetAttribute (mxtr, _xml ("meta:table-count"));
	    
	  if ( attr != NULL)
	    {
	      int s = _xmlchar_to_int (attr);
	      return s;
	    }
	}
    }
  return -1;
}

struct spreadsheet *ods_probe (const char *filename, bool report_errors)
{
  struct ods_reader *r;
  struct string errs;
  xmlTextReaderPtr xtr ;
  int sheet_count;
  struct zip_member *content = NULL;

  struct zip_reader *zreader = NULL;

  ds_init_empty (&errs);

  zreader = zip_reader_create (filename, &errs);

  if (zreader == NULL)
    return NULL;

  content = zip_member_open (zreader, "content.xml");

  if ( content == NULL)
    goto error;

  zip_member_ref (content);

  sheet_count = get_sheet_count (zreader);

  xtr = xmlReaderForIO ((xmlInputReadCallback) zip_member_read,
			   (xmlInputCloseCallback) zip_member_finish,
			   content,   NULL, NULL, XML_PARSE_RECOVER);

  if ( xtr == NULL)
    goto error;

  r = xzalloc (sizeof *r);
  r->xtr = xtr;
  r->spreadsheet.type = SPREADSHEET_ODS;
  r->spreadsheet.n_sheets = sheet_count;

  ds_destroy (&errs);

  r->spreadsheet.file_name = filename;
  return &r->spreadsheet;

 error:
  zip_reader_destroy (zreader);
  ds_destroy (&errs);
  return NULL;
}

struct casereader *
ods_make_reader (struct spreadsheet *spreadsheet, 
		 const struct spreadsheet_read_options *opts)
{
  intf ret = 0;
  xmlChar *type = NULL;
  unsigned long int vstart = 0;
  casenumber n_cases = CASENUMBER_MAX;
  int i;
  struct var_spec *var_spec = NULL;
  int n_var_specs = 0;

  struct ods_reader *r = (struct ods_reader *) spreadsheet;
  xmlChar *val_string = NULL;

  assert (r);
  r->read_names = opts->read_names;
  ds_init_empty (&r->ods_errs);

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
      r->start_col = 0;
      r->start_row = 0;
      r->stop_col = -1;
      r->stop_row = -1;
    }

  r->state = STATE_INIT;
  r->target_sheet = BAD_CAST opts->sheet_name;
  r->target_sheet_index = opts->sheet_index;
  r->row = r->col = -1;
  r->sheet_index = 0;


  /* If CELLRANGE was given, then we know how many variables should be read */
  if ( r->stop_col != -1 )
    {
      assert (var_spec == NULL);
      n_var_specs =  r->stop_col - r->start_col + 1;
      var_spec = xrealloc (var_spec, sizeof (*var_spec) * n_var_specs);
      memset (var_spec, '\0', sizeof (*var_spec) * n_var_specs);
    }


  /* Advance to the start of the cells for the target sheet */
  while ( (r->row < r->start_row ))
    {
      if (1 != (ret = xmlTextReaderRead (r->xtr)))
	   break;

      process_node (r);
    }

  if (ret < 1)
    {
      msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
           spreadsheet->file_name);
      goto error;
    }

  if ( opts->read_names)
    {
      while (1 == (ret = xmlTextReaderRead (r->xtr)))
	{
	  int idx;
	  process_node (r);
	  if ( r->row > r->start_row)
	    break;

	  if (r->col == -1 && r->row == r->start_row)
	    break;

	  if ( r->col < r->start_col)
	    continue;

	  idx = r->col - r->start_col;

	  if (r->state == STATE_CELL_CONTENT 
	      &&
	      XML_READER_TYPE_TEXT  == r->node_type)
	    {
	      xmlChar *value = xmlTextReaderValue (r->xtr);
	      if ( idx >= n_var_specs)
		{

		  var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));

		  /* xrealloc (unlike realloc) doesn't initialise its memory to 0 */
		  memset (var_spec + n_var_specs,
			  0, 
			  (n_var_specs - idx + 1) * sizeof (*var_spec));
		  n_var_specs = idx + 1;
		}
	      var_spec[idx].firstval.text = 0;
	      var_spec[idx].firstval.value = 0;
	      var_spec[idx].firstval.type = 0;

	      var_spec [idx].name = strdup (CHAR_CAST (const char *, value));
	      free (value);
	      value = NULL;
	    }
	}
    }

  /* Read in the first row of data */
  while (1 == xmlTextReaderRead (r->xtr))
    {
      int idx;
      process_node (r);
      if ( r->row >= r->start_row + 1 + opts->read_names)
	break;

      if ( r->col < r->start_col)
	continue;

      if ( r->col - r->start_col + 1 > n_var_specs)
	continue;

      idx = r->col - r->start_col;

      if ( r->state == STATE_CELL &&
	   XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  type = xmlTextReaderGetAttribute (r->xtr, _xml ("office:value-type"));
	  val_string = xmlTextReaderGetAttribute (r->xtr, _xml ("office:value"));
	}

      if ( r->state == STATE_CELL_CONTENT &&
	   XML_READER_TYPE_TEXT  == r->node_type)
	{
	  var_spec [idx].firstval.type = type;
	  var_spec [idx].firstval.text = xmlTextReaderValue (r->xtr);
	  var_spec [idx].firstval.value = val_string;
	  val_string = NULL;
	  type = NULL;
	}
    }

  /* Create the dictionary and populate it */
  r->spreadsheet.dict = r->dict = dict_create (
    CHAR_CAST (const char *, xmlTextReaderConstEncoding (r->xtr)));

  for (i = 0 ; i < n_var_specs ; ++i )
    {
      struct fmt_spec fmt;
      struct variable *var = NULL;
      char *name = dict_make_unique_var_name (r->dict, var_spec[i].name, &vstart);
      int width  = xmv_to_width (&var_spec[i].firstval, opts->asw);
      dict_create_var (r->dict, name, width);
      free (name);

      var = dict_get_var (r->dict, i);

      if ( 0 == xmlStrcmp (var_spec[i].firstval.type, _xml("date")))
	{
	  fmt.type = FMT_DATE;
	  fmt.d = 0;
	  fmt.w = 20;
	}
      else
	fmt = fmt_default_for_width (width);

      var_set_both_formats (var, &fmt);
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
      const struct variable *var = dict_get_var (r->dict, i);

      convert_xml_to_value (r->first_case, var,  &var_spec[i].firstval);
    }

  //  zip_reader_destroy (zreader);

  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      free (var_spec[i].firstval.type);
      free (var_spec[i].firstval.value);
      free (var_spec[i].firstval.text);
      free (var_spec[i].name);
    }

  free (var_spec);

  return casereader_create_sequential
    (NULL,
     r->proto,
     n_cases,
     &ods_file_casereader_class, r);

 error:
  
  // zip_reader_destroy (zreader);

  for ( i = 0 ; i < n_var_specs ; ++i )
    {
      free (var_spec[i].firstval.type);
      free (var_spec[i].firstval.value);
      free (var_spec[i].firstval.text);
      free (var_spec[i].name);
    }

  free (var_spec);

  dict_destroy (r->spreadsheet.dict);
  r->spreadsheet.dict = NULL;
  ods_file_casereader_destroy (NULL, r);


  return NULL;
}


/* Reads and returns one case from READER's file.  Returns a null
   pointer on failure. */
static struct ccase *
ods_file_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct ccase *c = NULL;
  xmlChar *val_string = NULL;
  struct ods_reader *r = r_;
  int current_row = r->row;

  if ( r->row == -1)
    return NULL;

  if ( !r->used_first_case )
    {
      r->used_first_case = true;
      return r->first_case;
    }

  if ( r->state > STATE_INIT)
    {
      c = case_create (r->proto);
      case_set_missing (c);
    }

  while (1 == xmlTextReaderRead (r->xtr))
    {
      process_node (r);
      if ( r->row > current_row)
	{
	  break;
	}
      if ( r->col < r->start_col || (r->stop_col != -1 && r->col > r->stop_col))
	{
	  continue;
	}
      if ( r->col - r->start_col >= caseproto_get_n_widths (r->proto))
	{
	  continue;
	}
      if ( r->stop_row != -1 && r->row > r->stop_row)
	{
	  continue;
	}
      if ( r->state == STATE_CELL &&
	   r->node_type == XML_READER_TYPE_ELEMENT )
	{
	  val_string = xmlTextReaderGetAttribute (r->xtr, _xml ("office:value"));
	}

      if ( r->state == STATE_CELL_CONTENT && r->node_type == XML_READER_TYPE_TEXT )
	{
	  int col;
	  struct xml_value *xmv = xzalloc (sizeof *xmv);
	  xmv->text = xmlTextReaderValue (r->xtr);
	  xmv->value = val_string;
	  val_string = NULL;

	  for (col = 0; col < r->span ; ++col)
	    {
	      const int idx = r->col + col - r->start_col;

	      const struct variable *var = dict_get_var (r->dict, idx);

	      convert_xml_to_value (c, var, xmv);
	    }
	  free (xmv->text);
	  free (xmv->value);
	  free (xmv);
	}

      if ( r->state < STATE_TABLE)
	break;
    }

  if (NULL == c || (r->stop_row != -1 && r->row > r->stop_row + 1))
    {
      case_unref (c);
      return NULL;
    }
  else
    {
      return c;
    }
}
#endif
