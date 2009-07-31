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


#include <libpspp/assertion.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>
#include <output/afm.h>
#include <output/chart-provider.h>
#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <libgen.h>

#include <libxml/xmlwriter.h>

#include <stdlib.h>

#include "error.h"
#include "intprops.h"
#include "minmax.h"
#include "xalloc.h"

#define _xml(X) (const xmlChar *)(X)

struct odt_driver_ext 
{
  /* The name of the temporary directory used to construct the ODF */
  char *dirname;

  /* Writer for the content.xml file */
  xmlTextWriterPtr content_wtr;

  /* Writer fot the manifest.xml file */
  xmlTextWriterPtr manifest_wtr;
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

  fprintf (fp, "application/vnd.oasis.opendocument.text");
  fclose (fp);
}

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

static bool
odt_open_driver (const char *name, int types, struct substring option_string)
{
  struct odt_driver_ext *x;
  struct outp_driver *this = outp_allocate_driver (&odt_class, name, types);

  this->ext = x = xmalloc (sizeof *x);

  outp_register_driver (this);

  x->dirname = xstrdup ("odt-XXXXXX");
  mkdtemp (x->dirname);

  printf ("Dir name is \"%s\"\n", x->dirname);

  create_mimetype (x->dirname);

  x->manifest_wtr = create_writer (x, "META-INF/manifest.xml");

  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:manifest"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("xmlns:manifest"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"));


  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:media-type"),  _xml("application/vnd.oasis.opendocument.text"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:full-path"),  _xml("/"));
  xmlTextWriterEndElement (x->manifest_wtr);


  x->content_wtr = create_writer (x, "content.xml");

  xmlTextWriterStartElement (x->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:media-type"),  _xml("text/xml"));
  xmlTextWriterWriteAttribute (x->manifest_wtr, _xml("manifest:full-path"),  _xml("content.xml"));
  xmlTextWriterEndElement (x->manifest_wtr);


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


  xmlTextWriterEndElement (x->manifest_wtr);
  xmlTextWriterEndDocument (x->manifest_wtr);
  xmlFreeTextWriter (x->manifest_wtr);

  return true;
}

static bool
odt_close_driver (struct outp_driver *this)
{
 struct odt_driver_ext *x = this->ext;

 xmlTextWriterEndElement (x->content_wtr); /* office:text */
 xmlTextWriterEndElement (x->content_wtr); /* office:body */
 xmlTextWriterEndElement (x->content_wtr); /* office:document-content */

 xmlTextWriterEndDocument (x->content_wtr);
 xmlFreeTextWriter (x->content_wtr);

 return true;
}

static void
odt_open_page (struct outp_driver *this)
{
 printf ("%s\n", __FUNCTION__);
}

static void
odt_close_page (struct outp_driver *this)
{
 printf ("%s\n", __FUNCTION__);
}

static void
odt_output_chart (struct outp_driver *this, const struct chart *chart)
{
 printf ("%s\n", __FUNCTION__);
}

static void
odt_submit (struct outp_driver *this, struct som_entity *e)
{
  int r, c;
  
  struct odt_driver_ext *x = this->ext;
  struct tab_table *tab = e->ext;

  xmlTextWriterStartElement (x->content_wtr, _xml("text:h"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("text:level"), "%d", e->subtable_num == 1 ? 2 : 3);
  xmlTextWriterWriteString (x->content_wtr, _xml (tab->title) );
  xmlTextWriterEndElement (x->content_wtr);

  xmlTextWriterStartElement (x->content_wtr, _xml("table:table"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("table:name"), 
				     "TABLE-%d.%d", e->table_num, e->subtable_num);


  /* Start column definitions */
  xmlTextWriterStartElement (x->content_wtr, _xml("table:table-column"));
  xmlTextWriterWriteFormatAttribute (x->content_wtr, _xml("table:number-columns-repeated"), "%d", tab->nc);
  xmlTextWriterEndElement (x->content_wtr);


  /* Deal with row headers */
  if ( tab->t > 0)
    xmlTextWriterStartElement (x->content_wtr, _xml("table:table-header-rows"));
    
  for (r = 0 ; r < tab->nr; ++r)
    {
      /* Start row definition */
      xmlTextWriterStartElement (x->content_wtr, _xml("table:table-row"));

      for (c = 0 ; c < tab->nc ; ++c)
	{
	  int opts = tab->ct[tab->nc * r + c];
	  xmlTextWriterStartElement (x->content_wtr, _xml("table:table-cell"));
	  xmlTextWriterWriteAttribute (x->content_wtr, _xml("office:value-type"), _xml("string"));

	  if (! (opts & TAB_EMPTY) ) 
	    {
	      char *s = ss_xstrdup (tab->cc[tab->nc * r + c]);
	      xmlTextWriterStartElement (x->content_wtr, _xml("text:p"));
	      if ( r < tab->t || c < tab->l )
		xmlTextWriterWriteAttribute (x->content_wtr, _xml("text:style-name"), _xml("Table_20_Heading"));
	      else
		xmlTextWriterWriteAttribute (x->content_wtr, _xml("text:style-name"), _xml("Table_20_Contents"));

	      xmlTextWriterWriteString (x->content_wtr, _xml (s));
	  
	      xmlTextWriterEndElement (x->content_wtr);
	      free (s);
	    }
	  xmlTextWriterEndElement (x->content_wtr);
	}
  
      xmlTextWriterEndElement (x->content_wtr); /* row */

      if ( tab->t > 0 && r == tab->t - 1)
	xmlTextWriterEndElement (x->content_wtr);
    }

  xmlTextWriterEndElement (x->content_wtr); /* table */
}


/* ODT driver class. */
const struct outp_class odt_class =
{
  "odt",
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
