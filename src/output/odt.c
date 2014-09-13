/* PSPP - a program for statistical analysis.
   Copyright (C) 2009-2014 Free Software Foundation, Inc.

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
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/zip-writer.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define _xml(X) (CHAR_CAST (const xmlChar *, X))

struct odt_driver
{
  struct output_driver driver;

  struct zip_writer *zip;     /* ZIP file writer. */
  char *file_name;            /* Output file name. */

  /* content.xml */
  xmlTextWriterPtr content_wtr; /* XML writer. */
  FILE *content_file;           /* Temporary file. */

  /* manifest.xml */
  xmlTextWriterPtr manifest_wtr; /* XML writer. */
  FILE *manifest_file;           /* Temporary file. */

  /* Number of tables so far. */
  int table_num;

  /* Name of current command. */
  char *command_name;

  /* Number of footnotes so far. */
  int n_footnotes;
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
create_mimetype (struct zip_writer *zip)
{
  FILE *fp;

  fp = create_temp_file ();
  if (fp == NULL)
    {
      msg_error (errno, _("error creating temporary file"));
      return false;
    }

  fprintf (fp, "application/vnd.oasis.opendocument.text");
  zip_writer_add (zip, fp, "mimetype");
  close_temp_file (fp);

  return true;
}

/* Creates a new temporary file and stores it in *FILE, then creates an XML
   writer for it and stores it in *W. */
static void
create_writer (FILE **file, xmlTextWriterPtr *w)
{
  /* XXX this can fail */
  *file = create_temp_file ();
  *w = xmlNewTextWriter (xmlOutputBufferCreateFile (*file, NULL));

  xmlTextWriterStartDocument (*w, NULL, "UTF-8", NULL);
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
  xmlTextWriterPtr w;
  FILE *file;

  create_writer (&file, &w);
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
  zip_writer_add (odt->zip, file, "styles.xml");
  close_temp_file (file);
}

static void
write_meta_data (struct odt_driver *odt)
{
  xmlTextWriterPtr w;
  FILE *file;

  create_writer (&file, &w);
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
  zip_writer_add (odt->zip, file, "meta.xml");
  close_temp_file (file);
}

static struct output_driver *
odt_create (const char *file_name, enum settings_output_devices device_type,
            struct string_map *o UNUSED)
{
  struct output_driver *d;
  struct odt_driver *odt;
  struct zip_writer *zip;

  zip = zip_writer_create (file_name);
  if (zip == NULL)
    return NULL;

  odt = xzalloc (sizeof *odt);
  d = &odt->driver;
  output_driver_init (d, &odt_driver_class, file_name, device_type);

  odt->zip = zip;
  odt->file_name = xstrdup (file_name);

  if (!create_mimetype (zip))
    {
      output_driver_destroy (d);
      return NULL;
    }

  /* Create the manifest */
  create_writer (&odt->manifest_file, &odt->manifest_wtr);

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

  create_writer (&odt->content_file, &odt->content_wtr);
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
  zip_writer_add (odt->zip, odt->manifest_file, "META-INF/manifest.xml");
  close_temp_file (odt->manifest_file);

  return d;
}

static void
odt_destroy (struct output_driver *driver)
{
  struct odt_driver *odt = odt_driver_cast (driver);

  if (odt->content_wtr != NULL)
    {
      xmlTextWriterEndElement (odt->content_wtr); /* office:text */
      xmlTextWriterEndElement (odt->content_wtr); /* office:body */
      xmlTextWriterEndElement (odt->content_wtr); /* office:document-content */

      xmlTextWriterEndDocument (odt->content_wtr);
      xmlFreeTextWriter (odt->content_wtr);
      zip_writer_add (odt->zip, odt->content_file, "content.xml");
      close_temp_file (odt->content_file);

      zip_writer_close (odt->zip);
    }
  
  free (odt->file_name);
  free (odt->command_name);
  free (odt);
}

static void
write_xml_with_line_breaks (struct odt_driver *odt, const char *line_)
{
  xmlTextWriterPtr writer = odt->content_wtr;

  if (!strchr (line_, '\n'))
    xmlTextWriterWriteString (writer, _xml(line_));
  else
    {
      char *line = xstrdup (line_);
      char *newline;
      char *p;

      for (p = line; *p; p = newline + 1)
        {
          newline = strchr (p, '\n');

          if (!newline)
            {
              xmlTextWriterWriteString (writer, _xml(p));
              free (line);
              return;
            }

          if (newline > p && newline[-1] == '\r')
            newline[-1] = '\0';
          else
            *newline = '\0';
          xmlTextWriterWriteString (writer, _xml(p));
          xmlTextWriterWriteElement (writer, _xml("text:line-break"), _xml(""));
        }
    }
}

static void
write_footnote (struct odt_driver *odt, const char *footnote)
{
  char marker[16];

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note"));
  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:note-class"),
                               _xml("footnote"));

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note-citation"));
  str_format_26adic (++odt->n_footnotes, false, marker, sizeof marker);
  if (strlen (marker) > 1)
    xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("text:label"),
                                       "(%s)", marker);
  else
    xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:label"),
                                 _xml(marker));
  xmlTextWriterEndElement (odt->content_wtr);

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note-body"));
  xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));
  write_xml_with_line_breaks (odt, footnote);
  xmlTextWriterEndElement (odt->content_wtr);
  xmlTextWriterEndElement (odt->content_wtr);

  xmlTextWriterEndElement (odt->content_wtr);
}

static void
write_table (struct odt_driver *odt, const struct table_item *item)
{
  const struct table *tab = table_item_get_table (item);
  const char *title = table_item_get_title (item);
  int r, c;

  /* Write a heading for the table */
  if (title != NULL)
    {
      xmlTextWriterStartElement (odt->content_wtr, _xml("text:h"));
      xmlTextWriterWriteFormatAttribute (odt->content_wtr,
                                         _xml("text:outline-level"), "%d", 2);
      xmlTextWriterWriteString (odt->content_wtr,
                                _xml (table_item_get_title (item)) );
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
          size_t i;

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

              for (i = 0; i < cell.n_contents; i++)
                {
                  const struct cell_contents *contents = &cell.contents[i];
                  int j;

                  if (contents->text)
                    {
                      xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));

                      if ( r < table_ht (tab) || c < table_hl (tab) )
                        xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Heading"));
                      else
                        xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Contents"));

                      write_xml_with_line_breaks (odt, contents->text);

                      for (j = 0; j < contents->n_footnotes; j++)
                        write_footnote (odt, contents->footnotes[j]);

                      xmlTextWriterEndElement (odt->content_wtr); /* text:p */
                    }
                  else if (contents->table)
                    write_table (odt, contents->table);

                }
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
    write_table (odt, to_table_item (output_item));
  else if (is_text_item (output_item))
    {
      struct text_item *text_item = to_text_item (output_item);

      if (text_item_get_type (text_item) != TEXT_ITEM_COMMAND_CLOSE)
        odt_output_text (odt, text_item_get_text (text_item));
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

struct output_driver_factory odt_driver_factory =
  { "odt", "pspp.odf", odt_create };

static const struct output_driver_class odt_driver_class =
{
  "odf",
  odt_destroy,
  odt_submit,
  NULL,
};
