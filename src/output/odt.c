/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010 Free Software Foundation, Inc.

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

/* A driver for creating OpenDocument Format text files from PSPP's output */

#include <errno.h>
#include <libgen.h>
#include <libxml/xmlwriter.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/str.h"
#include "libpspp/version.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/text-item.h"

#include "gl/xalloc.h"
#include "gl/error.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define _xml(X) (const xmlChar *)(X)

struct odt_driver
{
  struct output_driver driver;

  char *file_name;            /* Output file name. */
  bool debug;

  /* The name of the temporary directory used to construct the ODF */
  char *dirname;

  /* Writer for the content.xml file */
  xmlTextWriterPtr content_wtr;

  /* Writer fot the manifest.xml file */
  xmlTextWriterPtr manifest_wtr;

  /* Number of tables so far. */
  int table_num;

  /* Name of current command. */
  char *command_name;
};

static const struct output_driver_class odt_driver_class;

static struct odt_driver *
odt_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &odt_driver_class);
  return UP_CAST (driver, struct odt_driver, driver);
}

/* Create the "mimetype" file needed by ODF */
static bool
create_mimetype (const char *dirname)
{
  FILE *fp;
  struct string filename;
  ds_init_cstr (&filename, dirname);
  ds_put_cstr (&filename, "/mimetype");
  fp = fopen (ds_cstr (&filename), "w");

  if (fp == NULL)
    {
      error (0, errno, _("error opening output file \"%s\""),
             ds_cstr (&filename));
      ds_destroy (&filename);
      return false;
    }
  ds_destroy (&filename);

  fprintf (fp, "application/vnd.oasis.opendocument.text");
  fclose (fp);

  return true;
}

/* Create a new XML file called FILENAME in the temp directory, and return a writer for it */
static xmlTextWriterPtr
create_writer (const struct odt_driver *driver, const char *filename)
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
register_file (struct odt_driver *odt, const char *filename)
{
  assert (odt->manifest_wtr);
  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:media-type"),  _xml("text/xml"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:full-path"),  _xml (filename));
  xmlTextWriterEndElement (odt->manifest_wtr);
}

static void
write_style_data (struct odt_driver *odt)
{
  xmlTextWriterPtr w = create_writer (odt, "styles.xml");
  register_file (odt, "styles.xml");

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
write_meta_data (struct odt_driver *odt)
{
  xmlTextWriterPtr w = create_writer (odt, "meta.xml");
  register_file (odt, "meta.xml");

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
    time_t t = time (NULL);
    struct tm *tm =  localtime (&t);

    strftime (buf, 30, "%Y-%m-%dT%H:%M:%S", tm);

    xmlTextWriterStartElement (w, _xml ("meta:creation-date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);

    xmlTextWriterStartElement (w, _xml ("dc:date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);
  }

#ifdef HAVE_PWD_H
  {
    struct passwd *pw = getpwuid (getuid ());
    if (pw != NULL)
      {
        xmlTextWriterStartElement (w, _xml ("meta:initial-creator"));
        xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));
        xmlTextWriterEndElement (w);

        xmlTextWriterStartElement (w, _xml ("dc:creator"));
        xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));
        xmlTextWriterEndElement (w);
      }
  }
#endif

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

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
odt_create (const char *file_name, enum settings_output_devices device_type,
            struct string_map *o)
{
  struct output_driver *d;
  struct odt_driver *odt;

  odt = xzalloc (sizeof *odt);
  d = &odt->driver;
  output_driver_init (d, &odt_driver_class, file_name, device_type);

  odt->file_name = xstrdup (file_name);
  odt->debug = parse_boolean (opt (d, o, "debug", "false"));

  odt->dirname = xstrdup ("odt-XXXXXX");
  mkdtemp (odt->dirname);

  if (!create_mimetype (odt->dirname))
    {
      output_driver_destroy (d);
      return NULL;
    }

  /* Create the manifest */
  odt->manifest_wtr = create_writer (odt, "META-INF/manifest.xml");

  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:manifest"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("xmlns:manifest"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"));


  /* Add a manifest entry for the document as a whole */
  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:media-type"),  _xml("application/vnd.oasis.opendocument.text"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:full-path"),  _xml("/"));
  xmlTextWriterEndElement (odt->manifest_wtr);


  write_meta_data (odt);
  write_style_data (odt);

  odt->content_wtr = create_writer (odt, "content.xml");
  register_file (odt, "content.xml");


  /* Some necessary junk at the start */
  xmlTextWriterStartElement (odt->content_wtr, _xml("office:document-content"));
  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:office"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:text"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:text:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:table"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:table:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("office:version"), _xml("1.1"));

  xmlTextWriterStartElement (odt->content_wtr, _xml("office:body"));
  xmlTextWriterStartElement (odt->content_wtr, _xml("office:text"));



  /* Close the manifest */
  xmlTextWriterEndElement (odt->manifest_wtr);
  xmlTextWriterEndDocument (odt->manifest_wtr);
  xmlFreeTextWriter (odt->manifest_wtr);

  return d;
}

static void
odt_destroy (struct output_driver *driver)
{
  struct odt_driver *odt = odt_driver_cast (driver);

  if (odt->content_wtr != NULL)
    {
      struct string zip_cmd;

      xmlTextWriterEndElement (odt->content_wtr); /* office:text */
      xmlTextWriterEndElement (odt->content_wtr); /* office:body */
      xmlTextWriterEndElement (odt->content_wtr); /* office:document-content */

      xmlTextWriterEndDocument (odt->content_wtr);
      xmlFreeTextWriter (odt->content_wtr);

      /* Zip up the directory */
      ds_init_empty (&zip_cmd);
      ds_put_format (&zip_cmd,
                     "cd %s ; rm -f ../%s; zip -q -X ../%s mimetype; zip -q -X -u -r ../%s .",
                     odt->dirname, odt->file_name, odt->file_name, odt->file_name);
      system (ds_cstr (&zip_cmd));
      ds_destroy (&zip_cmd);
    }

  if ( !odt->debug )
    {
      /* Remove the temp dir */
      struct string rm_cmd;

      ds_init_empty (&rm_cmd);
      ds_put_format (&rm_cmd, "rm -r %s", odt->dirname);
      system (ds_cstr (&rm_cmd));
      ds_destroy (&rm_cmd);
    }
  else
    fprintf (stderr, "Not removing directory %s\n", odt->dirname);

  free (odt->command_name);
  free (odt->dirname);
  free (odt);
}

static void
odt_submit_table (struct odt_driver *odt, struct table_item *item)
{
  const struct table *tab = table_item_get_table (item);
  const char *caption = table_item_get_caption (item);
  int r, c;

  /* Write a heading for the table */
  if (caption != NULL)
    {
      xmlTextWriterStartElement (odt->content_wtr, _xml("text:h"));
      xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("text:level"),
                                         "%d", 2);
      xmlTextWriterWriteString (odt->content_wtr,
                                _xml (table_item_get_caption (item)) );
      xmlTextWriterEndElement (odt->content_wtr);
    }

  /* Start table */
  xmlTextWriterStartElement (odt->content_wtr, _xml("table:table"));
  xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("table:name"), 
				     "TABLE-%d", odt->table_num++);


  /* Start column definitions */
  xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-column"));
  xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("table:number-columns-repeated"), "%d", table_nc (tab));
  xmlTextWriterEndElement (odt->content_wtr);


  /* Deal with row headers */
  if ( table_ht (tab) > 0)
    xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-header-rows"));
    

  /* Write all the rows */
  for (r = 0 ; r < table_nr (tab); ++r)
    {
      /* Start row definition */
      xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-row"));

      /* Write all the columns */
      for (c = 0 ; c < table_nc (tab) ; ++c)
	{
          struct table_cell cell;

          table_get_cell (tab, c, r, &cell);

          if (c == cell.d[TABLE_HORZ][0] && r == cell.d[TABLE_VERT][0])
            {
              int colspan = table_cell_colspan (&cell);
              int rowspan = table_cell_rowspan (&cell);

              xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-cell"));
              xmlTextWriterWriteAttribute (odt->content_wtr, _xml("office:value-type"), _xml("string"));

              if (colspan > 1)
                xmlTextWriterWriteFormatAttribute (
                  odt->content_wtr, _xml("table:number-columns-spanned"),
                  "%d", colspan);

              if (rowspan > 1)
                xmlTextWriterWriteFormatAttribute (
                  odt->content_wtr, _xml("table:number-rows-spanned"),
                  "%d", rowspan);

	      xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));

	      if ( r < table_ht (tab) || c < table_hl (tab) )
		xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Heading"));
	      else
		xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Contents"));

	      xmlTextWriterWriteString (odt->content_wtr, _xml(cell.contents));

	      xmlTextWriterEndElement (odt->content_wtr); /* text:p */
	      xmlTextWriterEndElement (odt->content_wtr); /* table:table-cell */
	    }
	  else
	    {
	      xmlTextWriterStartElement (odt->content_wtr, _xml("table:covered-table-cell"));
	      xmlTextWriterEndElement (odt->content_wtr);
	    }

          table_cell_free (&cell);
	}
  
      xmlTextWriterEndElement (odt->content_wtr); /* row */

      if ( table_ht (tab) > 0 && r == table_ht (tab) - 1)
	xmlTextWriterEndElement (odt->content_wtr); /* table-header-rows */
    }

  xmlTextWriterEndElement (odt->content_wtr); /* table */
}

static void
odt_output_text (struct odt_driver *odt, const char *text)
{
  xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));
  xmlTextWriterWriteString (odt->content_wtr, _xml(text));
  xmlTextWriterEndElement (odt->content_wtr);
}

/* Submit a table to the ODT driver */
static void
odt_submit (struct output_driver *driver,
            const struct output_item *output_item)
{
  struct odt_driver *odt = odt_driver_cast (driver);

  output_driver_track_current_command (output_item, &odt->command_name);

  if (is_table_item (output_item))
    odt_submit_table (odt, to_table_item (output_item));
  else if (is_text_item (output_item))
    {
      /* XXX apply different styles based on text_item's type.  */
      odt_output_text (odt, text_item_get_text (to_text_item (output_item)));
    }
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      const struct msg *msg = message_item_get_msg (message_item);
      char *s = msg_to_string (msg, odt->command_name);
      odt_output_text (odt, s);
      free (s);
    }
}

struct output_driver_factory odt_driver_factory = { "odt", odt_create };

static const struct output_driver_class odt_driver_class =
{
  "odf",
  odt_destroy,
  odt_submit,
  NULL,
};
