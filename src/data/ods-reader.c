/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013 Free Software Foundation, Inc.

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
#include "libpspp/assertion.h"

#include "data/data-in.h"

#include "gl/c-strtod.h"
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

struct sheet_detail
{
  /* The name of the sheet (utf8 encoding) */
  char *name;

  int start_col;
  int stop_col;
  int start_row;
  int stop_row;
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

struct state_data
{
  xmlTextReaderPtr xtr;
  int node_type;
  enum reader_state state;
  int row;
  int col;
  int current_sheet;
  xmlChar *current_sheet_name;

  int col_span;
};

static void
state_data_destroy (struct state_data *sd)
{
  xmlFree (sd->current_sheet_name);
  sd->current_sheet_name = NULL;

  xmlFreeTextReader (sd->xtr);
  sd->xtr = NULL;
}

struct ods_reader
{
  struct spreadsheet spreadsheet;
  struct zip_reader *zreader;
  int ref_cnt;
  int target_sheet_index;
  xmlChar *target_sheet_name;
  
  /* State data for the meta data */
  struct state_data msd;

  /* State data for the reader */
  struct state_data rsd;

  int start_row;
  int start_col;
  int stop_row;
  int stop_col;

  struct sheet_detail *sheets;
  int n_allocated_sheets;

  struct caseproto *proto;
  struct dictionary *dict;
  struct ccase *first_case;
  bool used_first_case;
  bool read_names;

  struct string ods_errs;

  struct string zip_errs;
};

void
ods_destroy (struct spreadsheet *s)
{
  struct ods_reader *r = (struct ods_reader *) s;

  if (--r->ref_cnt == 0)
    {
      int i;

      state_data_destroy (&r->msd);
      for (i = 0; i < r->n_allocated_sheets; ++i)
	{
	  xmlFree (r->sheets[i].name);
	}
	
      zip_reader_destroy (r->zreader);
      free (r->sheets);
	
      free (r);
    }
}



static bool
reading_target_sheet (const struct ods_reader *r, const struct state_data *msd)
{
  if (r->target_sheet_name != NULL)
    {
      if ( 0 == xmlStrcmp (r->target_sheet_name, msd->current_sheet_name))
	return true;
    }
  
  if (r->target_sheet_index == msd->current_sheet + 1)
    return true;

  return false;
}


static void process_node (struct ods_reader *or, struct state_data *r);


const char *
ods_get_sheet_name (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data *or = &r->msd;

  assert (n < s->n_sheets);

  while ( 
	  (r->n_allocated_sheets <= n)
	  || or->state != STATE_SPREADSHEET
	  )
    {
      int ret = xmlTextReaderRead (or->xtr);
      if ( ret != 1)
	break;

      process_node (r, or);
    }

  return r->sheets[n].name;
}

char *
ods_get_sheet_range (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data *or = &r->msd;
  
  assert (n < s->n_sheets);

  while ( 
	  (r->n_allocated_sheets <= n)
	  || (r->sheets[n].stop_row == -1) 
	  || or->state != STATE_SPREADSHEET
	  )
    {
      int ret = xmlTextReaderRead (or->xtr);
      if ( ret != 1)
	break;

      process_node (r, or);
    }

  return create_cell_range (
			  r->sheets[n].start_col,
			  r->sheets[n].start_row,
			  r->sheets[n].stop_col,
			  r->sheets[n].stop_row);
}


static void
ods_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct ods_reader *r = r_;
  if ( r == NULL)
    return ;

  state_data_destroy (&r->rsd);

  if ( ! ds_is_empty (&r->ods_errs))
    msg (ME, "%s", ds_cstr (&r->ods_errs));

  ds_destroy (&r->ods_errs);

  if ( r->first_case && ! r->used_first_case )
    case_unref (r->first_case);


  caseproto_unref (r->proto);
  r->proto = NULL;

  xmlFree (r->target_sheet_name);
  r->target_sheet_name = NULL;


  ods_destroy (&r->spreadsheet);
}





static void
process_node (struct ods_reader *or, struct state_data *r)
{
  xmlChar *name = xmlTextReaderName (r->xtr);
  if (name == NULL)
    name = xmlStrdup (_xml ("--"));


  r->node_type = xmlTextReaderNodeType (r->xtr);

  switch (r->state)
    {
    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("office:spreadsheet")) &&
	  XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_SPREADSHEET;
	  r->current_sheet = -1;
	  r->current_sheet_name = NULL;
	}
      break;
    case STATE_SPREADSHEET:
      if (0 == xmlStrcasecmp (name, _xml("table:table"))
	  && 
	  (XML_READER_TYPE_ELEMENT == r->node_type))
	{
	  xmlFree (r->current_sheet_name);
	  r->current_sheet_name = xmlTextReaderGetAttribute (r->xtr, _xml ("table:name"));

	  ++r->current_sheet;

	  if (r->current_sheet >= or->n_allocated_sheets)
	    {
	      assert (r->current_sheet == or->n_allocated_sheets);
	      or->sheets = xrealloc (or->sheets, sizeof (*or->sheets) * ++or->n_allocated_sheets);
	      or->sheets[or->n_allocated_sheets - 1].start_col = -1;
	      or->sheets[or->n_allocated_sheets - 1].stop_col = -1;
	      or->sheets[or->n_allocated_sheets - 1].start_row = -1;
	      or->sheets[or->n_allocated_sheets - 1].stop_row = -1;
	      or->sheets[or->n_allocated_sheets - 1].name = CHAR_CAST (char *, xmlStrdup (r->current_sheet_name));
	    }

	  r->col = 0;
	  r->row = 0;

	  r->state = STATE_TABLE;
	}
      else if (0 == xmlStrcasecmp (name, _xml("office:spreadsheet")) &&
	       XML_READER_TYPE_ELEMENT  == r->node_type)
	{
	  r->state = STATE_INIT;
	}
      break;
    case STATE_TABLE:
      if (0 == xmlStrcasecmp (name, _xml("table:table-row")) && 
	  (XML_READER_TYPE_ELEMENT  == r->node_type))
	{
	  xmlChar *value =
	    xmlTextReaderGetAttribute (r->xtr,
				       _xml ("table:number-rows-repeated"));
	  
	  int row_span = value ? _xmlchar_to_int (value) : 1;

	  r->row += row_span;
	  r->col = 0;
	  
	  if (! xmlTextReaderIsEmptyElement (r->xtr))
	    r->state = STATE_ROW;

	  xmlFree (value);
	}
      else if (0 == xmlStrcasecmp (name, _xml("table:table")) && 
	       (XML_READER_TYPE_END_ELEMENT  == r->node_type))
	{
	  r->state = STATE_SPREADSHEET;
	}
      break;
    case STATE_ROW:
      if ( (0 == xmlStrcasecmp (name, _xml ("table:table-cell")))
	   && 
	   (XML_READER_TYPE_ELEMENT  == r->node_type))
	{
	  xmlChar *value =
	    xmlTextReaderGetAttribute (r->xtr,
				       _xml ("table:number-columns-repeated"));
	  
	  r->col_span = value ? _xmlchar_to_int (value) : 1;
	  r->col += r->col_span;

	  if (! xmlTextReaderIsEmptyElement (r->xtr))
	    r->state = STATE_CELL;

	  xmlFree (value);
	}
      else if ( (0 == xmlStrcasecmp (name, _xml ("table:table-row")))
		&&
		(XML_READER_TYPE_END_ELEMENT  == r->node_type))
	{
	  r->state = STATE_TABLE;
	}
      break;
    case STATE_CELL:
      if ( (0 == xmlStrcasecmp (name, _xml("text:p")))
	    &&
	   ( XML_READER_TYPE_ELEMENT  == r->node_type))
	{
	  if (! xmlTextReaderIsEmptyElement (r->xtr))
	    r->state = STATE_CELL_CONTENT;
	}
      else if
	( (0 == xmlStrcasecmp (name, _xml("table:table-cell")))
	  &&
	  (XML_READER_TYPE_END_ELEMENT  == r->node_type)
	  )
	{
	  r->state = STATE_ROW;
	}
      break;
    case STATE_CELL_CONTENT:
      assert (r->current_sheet >= 0);
      assert (r->current_sheet < or->n_allocated_sheets);

      if (or->sheets[r->current_sheet].start_row == -1)
	or->sheets[r->current_sheet].start_row = r->row - 1;

      if ( 
	  (or->sheets[r->current_sheet].start_col == -1)
	  ||
	  (or->sheets[r->current_sheet].start_col >= r->col - 1)
	   )
	or->sheets[r->current_sheet].start_col = r->col - 1;

      or->sheets[r->current_sheet].stop_row = r->row - 1;

      if ( or->sheets[r->current_sheet].stop_col <  r->col - 1)
	or->sheets[r->current_sheet].stop_col = r->col - 1;

      if (XML_READER_TYPE_END_ELEMENT  == r->node_type)
	r->state = STATE_CELL;
      break;
    default:
      NOT_REACHED ();
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
		      const struct xml_value *xmv, int col, int row)
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
      const struct fmt_spec *fmt = var_get_write_format (var);
      enum fmt_category fc  = fmt_get_category (fmt->type);

      assert ( fc != FMT_CAT_STRING);

      if ( 0 == xmlStrcmp (xmv->type, _xml("float")))
	{
	  v->f = c_strtod (CHAR_CAST (const char *, xmv->value), NULL);
	}
      else
	{
	  const char *text = xmv->value ?
	    CHAR_CAST (const char *, xmv->value) : CHAR_CAST (const char *, xmv->text);

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
			 (xmlInputCloseCallback) NULL,
			 meta,   NULL, NULL, 0);

  while (1 == xmlTextReaderRead (mxtr))
    {
      xmlChar *name = xmlTextReaderName (mxtr);
      if ( 0 == xmlStrcmp (name, _xml("meta:document-statistic")))
	{
	  xmlChar *attr = xmlTextReaderGetAttribute (mxtr, _xml ("meta:table-count"));

	  if ( attr != NULL)
	    {
	      int s = _xmlchar_to_int (attr);
	      xmlFreeTextReader (mxtr);
	      xmlFree (name);
	      xmlFree (attr);      
	      return s;
	    }
	  xmlFree (attr);      
	}
      xmlFree (name);      
    }

  xmlFreeTextReader (mxtr);
  return -1;
}

static void
ods_error_handler (void *ctx, const char *mesg,
			UNUSED xmlParserSeverities sev, xmlTextReaderLocatorPtr loc)
{
  struct ods_reader *r = ctx;
       
  msg (MW, _("There was a problem whilst reading the %s file `%s' (near line %d): `%s'"),
       "ODF",
       r->spreadsheet.file_name,
       xmlTextReaderLocatorLineNumber (loc),
       mesg);
}


static xmlTextReaderPtr
init_reader (struct ods_reader *r, bool report_errors)
{
  struct zip_member *content = zip_member_open (r->zreader, "content.xml");
  xmlTextReaderPtr xtr;

  if ( content == NULL)
    return NULL;

  xtr = xmlReaderForIO ((xmlInputReadCallback) zip_member_read,
			(xmlInputCloseCallback) NULL,
			content,   NULL, NULL,
			report_errors ? 0 : (XML_PARSE_NOERROR | XML_PARSE_NOWARNING) );

  if ( xtr == NULL)
    return false;


  r->spreadsheet.type = SPREADSHEET_ODS;

  if (report_errors) 
    xmlTextReaderSetErrorHandler (xtr, ods_error_handler, r);

  return xtr;
}



struct spreadsheet *
ods_probe (const char *filename, bool report_errors)
{
  int sheet_count;
  struct ods_reader *r = xzalloc (sizeof *r);
  xmlTextReaderPtr xtr;  
  struct zip_reader *zr;

  ds_init_empty (&r->zip_errs);

  zr = zip_reader_create (filename, &r->zip_errs);

  if (zr == NULL)
    {
      if (report_errors)
	{
	  msg (ME, _("Cannot open %s as a OpenDocument file: %s"),
	       filename, ds_cstr (&r->zip_errs));
	}
      ds_destroy (&r->zip_errs);
      free (r);
      return NULL;
    }

  sheet_count = get_sheet_count (zr);

  r->zreader = zr;
  r->ref_cnt = 1;

  xtr = init_reader (r, report_errors);
  if (xtr == NULL)
    {
      goto error;
    }
  r->msd.xtr = xtr;
  r->msd.row = 0;
  r->msd.col = 0;
  r->msd.current_sheet = 0;
  r->msd.state = STATE_INIT;


  r->spreadsheet.n_sheets = sheet_count;
  r->n_allocated_sheets = 0;
  r->sheets = NULL;

  r->spreadsheet.file_name = filename;
  return &r->spreadsheet;

 error:
  ds_destroy (&r->zip_errs);
  zip_reader_destroy (r->zreader);
  free (r);
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
  xmlTextReaderPtr xtr;

  struct ods_reader *r = (struct ods_reader *) spreadsheet;
  xmlChar *val_string = NULL;

  assert (r);
  r->read_names = opts->read_names;
  ds_init_empty (&r->ods_errs);
  ++r->ref_cnt;

  xtr = init_reader (r, true);
  if ( xtr == NULL)
    goto error;

  r->rsd.xtr = xtr;
  r->rsd.row = 0;
  r->rsd.col = 0;
  r->rsd.current_sheet = 0;
  r->rsd.state = STATE_INIT;

  r->used_first_case = false;
  r->first_case = NULL;

  if (opts->cell_range)
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

  r->target_sheet_name = xmlStrdup (BAD_CAST opts->sheet_name);
  r->target_sheet_index = opts->sheet_index;

  /* Advance to the start of the cells for the target sheet */
  while ( ! reading_target_sheet (r, &r->rsd)  
	  || r->rsd.state != STATE_ROW || r->rsd.row <= r->start_row )
    {
      if (1 != (ret = xmlTextReaderRead (r->rsd.xtr)))
	   break;

      process_node (r, &r->rsd);
    }

  if (ret < 1)
    {
      msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
           spreadsheet->file_name);
      goto error;
    }

  if ( opts->read_names)
    {
      while (1 == xmlTextReaderRead (r->rsd.xtr))
	{
	  int idx;

	  process_node (r, &r->rsd);

	  /* If the row is finished then stop for now */
	  if (r->rsd.state == STATE_TABLE && r->rsd.row > r->start_row)
	    break;

	  idx = r->rsd.col - r->start_col -1 ;

	  if ( idx < 0)
	    continue;

	  if (r->stop_col != -1 && idx > r->stop_col - r->start_col)
	    continue;

	  if (r->rsd.state == STATE_CELL_CONTENT 
	      &&
	      XML_READER_TYPE_TEXT  == r->rsd.node_type)
	    {
	      xmlChar *value = xmlTextReaderValue (r->rsd.xtr);

	      if ( idx >= n_var_specs)
		{
		  var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));

		  /* xrealloc (unlike realloc) doesn't initialise its memory to 0 */
		  memset (var_spec + n_var_specs,
			  0, 
			  (idx - n_var_specs + 1) * sizeof (*var_spec));
		  n_var_specs = idx + 1;
		}
	      var_spec[idx].firstval.text = 0;
	      var_spec[idx].firstval.value = 0;
	      var_spec[idx].firstval.type = 0;

	      var_spec [idx].name = strdup (CHAR_CAST (const char *, value));

	      xmlFree (value);
	    }
	}
    }

  /* Read in the first row of data */
  while (1 == xmlTextReaderRead (r->rsd.xtr))
    {
      int idx;
      process_node (r, &r->rsd);

      if ( ! reading_target_sheet (r, &r->rsd) )
	break;

      /* If the row is finished then stop for now */
      if (r->rsd.state == STATE_TABLE &&
	  r->rsd.row > r->start_row + (opts->read_names ? 1 : 0))
	break;

      idx = r->rsd.col - r->start_col - 1;
      if (idx < 0)
	continue;

      if (r->stop_col != -1 && idx > r->stop_col - r->start_col)
	continue;

      if ( r->rsd.state == STATE_CELL &&
	   XML_READER_TYPE_ELEMENT  == r->rsd.node_type)
	{
	  type = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value-type"));
	  val_string = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value"));
	}

      if ( r->rsd.state == STATE_CELL_CONTENT &&
	   XML_READER_TYPE_TEXT  == r->rsd.node_type)
	{
	  if (idx >= n_var_specs)
	    {
	      var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));
	      memset (var_spec + n_var_specs,
		      0, 
		      (idx - n_var_specs + 1) * sizeof (*var_spec));

	      var_spec [idx].name = NULL;
	      n_var_specs = idx + 1;
	    }

	  var_spec [idx].firstval.type = type;
	  var_spec [idx].firstval.text = xmlTextReaderValue (r->rsd.xtr);
	  var_spec [idx].firstval.value = val_string;

	  val_string = NULL;
	  type = NULL;
	}
    }


  /* Create the dictionary and populate it */
  r->spreadsheet.dict = r->dict = dict_create (
    CHAR_CAST (const char *, xmlTextReaderConstEncoding (r->rsd.xtr)));

  for (i = 0; i < n_var_specs ; ++i )
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

  if ( n_var_specs ==  0 )
    {
      msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
           spreadsheet->file_name);
      goto error;
    }

  /* Create the first case, and cache it */
  r->proto = caseproto_ref (dict_get_proto (r->dict));
  r->first_case = case_create (r->proto);
  case_set_missing (r->first_case);

  for (i = 0 ; i < n_var_specs; ++i)
    {
      const struct variable *var = dict_get_var (r->dict, i);

      convert_xml_to_value (r->first_case, var,  &var_spec[i].firstval,
			    r->rsd.col - n_var_specs + i,
			    r->rsd.row - 1);
    }

  /* Read in the first row of data */
  while (1 == xmlTextReaderRead (r->rsd.xtr))
    {
      process_node (r, &r->rsd);

      if (r->rsd.state == STATE_ROW)
	break;
    }


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
  struct ods_reader *r = r_;

  xmlChar *val_string = NULL;
  xmlChar *type = NULL;

  if (!r->used_first_case)
    {
      r->used_first_case = true;
      return r->first_case;
    }


  /* Advance to the start of a row. (If there is one) */
  while (r->rsd.state != STATE_ROW 
	 && 1 == xmlTextReaderRead (r->rsd.xtr)
	 )
    {
      process_node (r, &r->rsd);
    }


  if ( ! reading_target_sheet (r, &r->rsd)  
       ||  r->rsd.state < STATE_TABLE
       ||  (r->stop_row != -1 && r->rsd.row > r->stop_row + 1)
       )
    {
      return NULL;
    }

  c = case_create (r->proto);
  case_set_missing (c);
  
  while (1 == xmlTextReaderRead (r->rsd.xtr))
    {
      process_node (r, &r->rsd);

      if ( r->stop_row != -1 && r->rsd.row > r->stop_row + 1)
	break;

      if (r->rsd.state == STATE_CELL &&
	   r->rsd.node_type == XML_READER_TYPE_ELEMENT)
	{
	  type = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value-type"));
	  val_string = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value"));
	}

      if (r->rsd.state == STATE_CELL_CONTENT && 
	   r->rsd.node_type == XML_READER_TYPE_TEXT)
	{
	  int col;
	  struct xml_value *xmv = xzalloc (sizeof *xmv);
	  xmv->text = xmlTextReaderValue (r->rsd.xtr);
	  xmv->value = val_string;	 
	  val_string = NULL;
	  xmv->type = type;
	  type = NULL;

	  for (col = 0; col < r->rsd.col_span; ++col)
	    {
	      const struct variable *var;
	      const int idx = r->rsd.col - col - r->start_col - 1;
	      if (idx < 0)
		continue;
	      if (r->stop_col != -1 && idx > r->stop_col - r->start_col )
		break;
	      if (idx >= dict_get_var_cnt (r->dict))
		break;

              var = dict_get_var (r->dict, idx);
	      convert_xml_to_value (c, var, xmv, idx + r->start_col, r->rsd.row - 1);
	    }

	  xmlFree (xmv->text);
	  xmlFree (xmv->value);
	  xmlFree (xmv->type);
	  free (xmv);
	}
      if ( r->rsd.state <= STATE_TABLE)
	break;
    }

  return c;
}
#endif
