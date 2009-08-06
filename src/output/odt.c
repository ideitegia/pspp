/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A driver for creating OpenDocument Format text files from PSPP's output */

#include <libpspp/assertion.h>
#include <libpspp/version.h>

#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>

#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libgen.h>

#include <libxml/xmlwriter.h>

#include "xalloc.h"

#include "error.h"

#define _xml(X) (const xmlChar *)(X)


struct odf_driver_options
{
  struct outp_driver *driver;
  
  char *file_name;            /* Output file name. */
  bool debug;
};


struct odt_driver_ext 
{
  /* The name of the temporary directory used to construct the ODF */
  char *dirname;

  /* Writer for the content.xml file */
  xmlTextWriterPtr content_wtr;

  /* Writer fot the manifest.xml file */
  xmlTextWriterPtr manifest_wtr;

  struct odf_driver_options opts;
};



/* Create the "mimetype" file needed by ODF */
static void
create_mimetype (const char *dirname)
{
  FILE *fp;
  struct string filename;
  ds_init_cstr (&filename, dirname);
  ds_put_cstr (&filename, "/mimetype");
  fp = fopen (ds_cstr (&filename), "w");
  ds_destroy (&filename);

  assert (fp);
  fprintf (fp, "application/vnd.oasis.opendocument.text");
  fclose (fp);
}

/* Create a new XML file called FILENAME in the temp directory, and return a writer for it */
static xmlTextWriterPtr
create_writer (const struct odt_driver_ext *driver, const char *filename)
{
  char *copy = NULL;
  xmlTextWriterPtr w;
  struct string str;
  ds_init_cstr (&str, driver->dirname);
  ds_put_cstr (&str, "/");
  ds_put_cstr (&str, filename);

  /* dirname modifies its argument, so we must copy it */
  copy = xstrdup (ds_cstr (&str));
  mkdir (dirname (copy), 0700);
  free (copy);

  w = xmlNewTextWriterFilename (ds_cstr (&str), 0);

  ds_destroy (&str);

  xmlTextWriterStartDocument (w, NULL, "UTF-8", NULL);

  return w;
}


static void
register_file (struct odt_driver_ext *x, const char *filename)
{
  assert (x->manifest_wtr);
  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:media-type"),  _xml("text/xml"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:full-path"),  _xml (filename));
  xmlTextWriterEndElement (x->manifest_wtr);
}

static void
write_style_data (struct odt_driver_ext *x)
{
  xmlTextWriterPtr w = create_writer (x, "styles.xml");
  register_file (x, "styles.xml");

  xmlTextWriterStartElement (w, _xml ("office:document-styles"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:office"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));

  xmlTextWriterWriteAttribute (w, _xml ("xmlns:style"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:style:1.0"));

  xmlTextWriterWriteAttribute (w, _xml ("xmlns:fo"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0") );

  xmlTextWriterWriteAttribute (w, _xml ("office:version"),  _xml ("1.1"));
			       


  xmlTextWriterStartElement (w, _xml ("office:styles"));


  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Standard"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("text"));

    xmlTextWriterEndElement (w); /* style:style */
  }

  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Table_20_Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:display-name"),
				 _xml ("Table Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:parent-style-name"),
				 _xml ("Standard"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("extra"));

    xmlTextWriterEndElement (w); /* style:style */
  }

  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Table_20_Heading"));

    xmlTextWriterWriteAttribute (w, _xml ("style:display-name"),
				 _xml ("Table Heading"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:parent-style-name"),
				 _xml ("Table_20_Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("extra"));


    xmlTextWriterStartElement (w, _xml ("style:text-properties"));
    xmlTextWriterWriteAttribute (w, _xml ("fo:font-weight"), _xml ("bold"));
    xmlTextWriterWriteAttribute (w, _xml ("style:font-weight-asian"), _xml ("bold"));
    xmlTextWriterWriteAttribute (w, _xml ("style:font-weight-complex"), _xml ("bold"));
    xmlTextWriterEndElement (w); /* style:text-properties */

    xmlTextWriterEndElement (w); /* style:style */
  }


  xmlTextWriterEndElement (w); /* office:styles */
  xmlTextWriterEndElement (w); /* office:document-styles */

  xmlTextWriterEndDocument (w);
  xmlFreeTextWriter (w);
}

static void
write_meta_data (struct odt_driver_ext *x)
{
  xmlTextWriterPtr w = create_writer (x, "meta.xml");
  register_file (x, "meta.xml");

  xmlTextWriterStartElement (w, _xml ("office:document-meta"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:office"), _xml ("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:dc"),  _xml ("http://purl.org/dc/elements/1.1/"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:meta"), _xml ("urn:oasis:names:tc:opendocument:xmlns:meta:1.0"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:ooo"), _xml("http://openoffice.org/2004/office"));
  xmlTextWriterWriteAttribute (w, _xml ("office:version"),  _xml("1.1"));

  xmlTextWriterStartElement (w, _xml ("office:meta"));
  {
    xmlTextWriterStartElement (w, _xml ("meta:generator"));
    xmlTextWriterWriteString (w, _xml (stat_version));
    xmlTextWriterEndElement (w);
  }


  {
    char buf[30];
    struct passwd *pw = getpwuid (getuid ());
    time_t t = time (NULL);
    struct tm *tm =  localtime (&t);

    strftime (buf, 30, "%Y-%m-%dT%H:%M:%S", tm);

    xmlTextWriterStartElement (w, _xml ("meta:initial-creator"));
    xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));
    xmlTextWriterEndElement (w);

    xmlTextWriterStartElement (w, _xml ("meta:creation-date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);

    xmlTextWriterStartElement (w, _xml ("dc:creator"));
    xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));

    xmlTextWriterEndElement (w);

    xmlTextWriterStartElement (w, _xml ("dc:date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);
  }

  xmlTextWriterEndElement (w);
  xmlTextWriterEndElement (w);
  xmlTextWriterEndDocument (w);
  xmlFreeTextWriter (w);
}

enum
{
  output_file_arg,
  boolean_arg,
};

static const struct outp_option option_tab[] =
{
  {"output-file",		output_file_arg,0},

  {"debug",			boolean_arg,	1},

  {NULL, 0, 0},
};

static bool
handle_option (void *options_, const char *key, const struct string *val)
{
  struct odf_driver_options *options = options_;
  struct outp_driver *this = options->driver;
  int subcat;
  char *value = ds_cstr (val);

  switch (outp_match_keyword (key, option_tab, &subcat))
    {
    case -1:
      error (0, 0,
             _("unknown configuration parameter `%s' for %s device "
               "driver"), key, this->class->name);
      break;
    case output_file_arg:
      free (options->file_name);
      options->file_name = xstrdup (value);
      break;
    case boolean_arg:
      if (!strcmp (value, "on") || !strcmp (value, "true")
          || !strcmp (value, "yes") || atoi (value))
        options->debug = true;
      else if (!strcmp (value, "off") || !strcmp (value, "false")
               || !strcmp (value, "no") || !strcmp (value, "0"))
        options->debug = false;
      else
        {
          error (0, 0, _("boolean value expected for %s"), key);
          return false;
        }
      break;

    default:
      NOT_REACHED ();
    }

  return true;
}


static bool
odt_open_driver (const char *name, int types, struct substring option_string)
{
  struct odt_driver_ext *x;
  struct outp_driver *this = outp_allocate_driver (&odt_class, name, types);

  this->ext = x = xmalloc (sizeof *x);

  x->opts.driver = this;
  x->opts.file_name = xstrdup ("pspp.pdt");
  x->opts.debug = false;

  outp_parse_options (this->name, option_string, handle_option, &x->opts);

  outp_register_driver (this);

  x->dirname = xstrdup ("odt-XXXXXX");
  mkdtemp (x->dirname);

  create_mimetype (x->dirname);

  /* Create the manifest */
  x->manifest_wtr = create_writer (x, "META-INF/manifest.xml");

  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:manifest"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("xmlns:manifest"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"));


  /* Add a manifest entry for the document as a whole */
  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:media-type"),  _xml("application/vnd.oasis.opendocument.text"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:full-path"),  _xml("/"));
  xmlTextWriterEndElement (x->manifest_wtr);


  write_meta_data (x);
  write_style_data (x);

  x->content_wtr = create_writer (x, "content.xml");
  register_file (x, "content.xml");


  /* Some necessary junk at the start */
  xmlTextWriterStartElement (x->content_wtr, _xml("office:document-content"));
  xmlTextWriterWriteAttribute (x->content_wtr, _xml("xmlns:office"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));

  xmlTextWriterWriteAttribute (x->content_wtr, _xml("xmlns:text"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:text:1.0"));

  xmlTextWriterWriteAttribute (x->content_wtr, _xml("xmlns:table"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:table:1.0"));

  xmlTextWriterWriteAttribute (x->content_wtr, _xml("office:version"), _xml("1.1"));

  xmlTextWriterStartElement (x->content_wtr, _xml("office:body"));
  xmlTextWriterStartElement (x->content_wtr, _xml("office:text"));



  /* Close the manifest */
  xmlTextWriterEndElement (x->manifest_wtr);
  xmlTextWriterEndDocument (x->manifest_wtr);
  xmlFreeTextWriter (x->manifest_wtr);

  return true;
}

static bool
odt_close_driver (struct outp_driver *this)
{
  struct string zip_cmd;
  struct string rm_cmd;
  struct odt_driver_ext *x = this->ext;

  xmlTextWriterEndElement (x->content_wtr); /* office:text */
  xmlTextWriterEndElement (x->content_wtr); /* office:body */
  xmlTextWriterEndElement (x->content_wtr); /* office:document-content */

  xmlTextWriterEndDocument (x->content_wtr);
  xmlFreeTextWriter (x->content_wtr);

  /* Zip up the directory */
  ds_init_empty (&zip_cmd);
  ds_put_format (&zip_cmd,
		 "cd %s ; rm -f ../%s; zip -q -X ../%s mimetype; zip -q -X -u -r ../pspp.odt .",
		 x->dirname, x->opts.file_name, x->opts.file_name);
  system (ds_cstr (&zip_cmd));
  ds_destroy (&zip_cmd);


  if ( !x->opts.debug )
    {
      /* Remove the temp dir */
      ds_init_empty (&rm_cmd);
      ds_put_format (&rm_cmd, "rm -r %s", x->dirname);
      system (ds_cstr (&rm_cmd));
      ds_destroy (&rm_cmd);
    }
  else
    fprintf (stderr, "Not removing directory %s\n", x->dirname);

  free (x->dirname);
  free (x);

  return true;
}

static void
odt_open_page (struct outp_driver *this UNUSED)
{
}

static void
odt_close_page (struct outp_driver *this UNUSED)
{
}

static void
odt_output_chart (struct outp_driver *this UNUSED, const struct chart *chart UNUSED)
{
 printf ("%s\n", __FUNCTION__);
}


/* Submit a table to the ODT driver */
static void
odt_submit (struct outp_driver *this, struct som_entity *e)
{
  int r, c;
  
  struct odt_driver_ext *x = this->ext;
  struct tab_table *tab = e->ext;


  /* Write a heading for the table */
  xmlTextWriterStartElement (x->content_wtr, _xml("text:h"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("text:level"), "%d", e->subtable_num == 1 ? 2 : 3);
  xmlTextWriterWriteString (x->content_wtr, _xml (tab->title) );
  xmlTextWriterEndElement (x->content_wtr);

  /* Start table */
  xmlTextWriterStartElement (x->content_wtr, _xml("table:table"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("table:name"), 
				     "TABLE-%d.%d", e->table_num, e->subtable_num);


  /* Start column definitions */
  xmlTextWriterStartElement (x->content_wtr, _xml("table:table-column"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("table:number-columns-repeated"), "%d", tab_nc (tab));
  xmlTextWriterEndElement (x->content_wtr);


  /* Deal with row headers */
  if ( tab_t (tab) > 0)
    xmlTextWriterStartElement (x->content_wtr, _xml("table:table-header-rows"));
    

  /* Write all the rows */
  for (r = 0 ; r < tab_nr (tab); ++r)
    {
      int spanned_columns = 0;
      /* Start row definition */
      xmlTextWriterStartElement (x->content_wtr, _xml("table:table-row"));

      /* Write all the columns */
      for (c = 0 ; c < tab_nc (tab) ; ++c)
	{
	  char *s = NULL;
	  unsigned int opts = tab->ct[tab_nc (tab) * r + c];
	  struct substring ss = tab->cc[tab_nc (tab) * r + c];

	  if (opts & TAB_EMPTY)
	    {
	      xmlTextWriterStartElement (x->content_wtr, _xml("table:table-cell"));
	      xmlTextWriterEndElement (x->content_wtr);
	      continue;
	    }

	  if ( opts & TAB_JOIN)
	    {
	      if ( spanned_columns == 0)
		{
		  struct tab_joined_cell *j = (struct tab_joined_cell*) ss_data (ss);
		  s = ss_xstrdup (j->contents);
		}
	    }
	  else
	    s = ss_xstrdup (ss);

	  if ( spanned_columns == 0 )
	    {
	      xmlTextWriterStartElement (x->content_wtr, _xml("table:table-cell"));
	      xmlTextWriterWriteAttribute (x->content_wtr, _xml("office:value-type"), _xml("string"));

	      if ( opts & TAB_JOIN )
		{
		  struct tab_joined_cell *j = (struct tab_joined_cell*) ss_data (ss);
		  spanned_columns = j->x2 - j->x1;

		  xmlTextWriterWriteFormatAttribute (x->content_wtr,
						     _xml("table:number-columns-spanned"),
						     "%d", spanned_columns);
		}

	      xmlTextWriterStartElement (x->content_wtr, _xml("text:p"));

	      if ( r < tab_t (tab) || c < tab_l (tab) )
		xmlTextWriterWriteAttribute (x->content_wtr, _xml("text:style-name"), _xml("Table_20_Heading"));
	      else
		xmlTextWriterWriteAttribute (x->content_wtr, _xml("text:style-name"), _xml("Table_20_Contents"));

	      xmlTextWriterWriteString (x->content_wtr, _xml (s));
	  
	      xmlTextWriterEndElement (x->content_wtr); /* text:p */
	      xmlTextWriterEndElement (x->content_wtr); /* table:table-cell */
	    }
	  else
	    {
	      xmlTextWriterStartElement (x->content_wtr, _xml("table:covered-table-cell"));
	      xmlTextWriterEndElement (x->content_wtr);
	    }
	  if ( opts & TAB_JOIN )
	    spanned_columns --;

	  free (s);
	}
  
      xmlTextWriterEndElement (x->content_wtr); /* row */

      if ( tab_t (tab) > 0 && r == tab_t (tab) - 1)
	xmlTextWriterEndElement (x->content_wtr); /* table-header-rows */
    }

  xmlTextWriterEndElement (x->content_wtr); /* table */
}


/* ODT driver class. */
const struct outp_class odt_class =
{
  "odf",
  1,

  odt_open_driver,
  odt_close_driver,

  odt_open_page,
  odt_close_page,
  NULL,

  odt_output_chart,
  odt_submit,

  NULL,
  NULL,
  NULL,
};
