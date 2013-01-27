/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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


#include <glib-object.h>
#include <glib.h>

#include <gtk/gtk.h>

#ifndef __PSPPIRE_SPREADSHEET_MODEL_H__
#define __PSPPIRE_SPREADSHEET_MODEL_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_SPREADSHEET_MODEL (psppire_spreadsheet_model_get_type ())

#define PSPPIRE_SPREADSHEET_MODEL(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
		    PSPPIRE_TYPE_SPREADSHEET_MODEL, PsppireSpreadsheetModel))

#define PSPPIRE_SPREADSHEET_MODEL_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_SPREADSHEET_MODEL, \
                                 PsppireSpreadsheetModelClass))


#define PSPPIRE_IS_SPREADSHEET_MODEL(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_SPREADSHEET_MODEL))

#define PSPPIRE_IS_SPREADSHEET_MODEL_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_SPREADSHEET_MODEL))


#define PSPPIRE_SPREADSHEET_MODEL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_SPREADSHEET_MODEL, \
				   PsppireSpreadsheetModelClass))

typedef struct _PsppireSpreadsheetModel       PsppireSpreadsheetModel;
typedef struct _PsppireSpreadsheetModelClass  PsppireSpreadsheetModelClass;


struct spreadsheet;

struct _PsppireSpreadsheetModel
{
  GObject parent;


  /*< private >*/
  gint stamp;
  struct spreadsheet *spreadsheet;

  gboolean dispose_has_run ;
};


struct _PsppireSpreadsheetModelClass
{
  GObjectClass parent_class;
};


GType psppire_spreadsheet_model_get_type (void) G_GNUC_CONST;


GtkTreeModel * psppire_spreadsheet_model_new (struct spreadsheet *sp);


G_END_DECLS


enum
{
  PSPPIRE_SPREADSHEET_MODEL_COL_NAME,
  PSPPIRE_SPREADSHEET_MODEL_COL_RANGE,
  PSPPIRE_SPREADSHEET_MODEL_N_COLS
};

#endif /* __PSPPIRE_SPREADSHEET_MODEL_H__ */
