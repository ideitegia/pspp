/* PSPPIRE - a graphical user interface for PSPP.
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

#ifndef PSPPIRE_DATA_SHEET_H
#define PSPPIRE_DATA_SHEET_H 1

/* PsppireDataSheet is a PsppSheetView that displays the data in a dataset,
   with one column per variable and one row per case.

   PsppireDataSheet is usually a child of PsppireDataEditor in the widget
   hierarchy.  Other widgets can also use it. */

#include <gtk/gtk.h>
#include "ui/gui/pspp-sheet-view.h"

G_BEGIN_DECLS

#define PSPP_TYPE_DATA_SHEET              (psppire_data_sheet_get_type())
#define PSPPIRE_DATA_SHEET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPP_TYPE_DATA_SHEET,PsppireDataSheet))
#define PSPPIRE_DATA_SHEET_CLASS(class)   (G_TYPE_CHECK_CLASS_CAST ((class),PSPP_TYPE_DATA_SHEET,PsppireDataSheetClass))
#define PSPP_IS_DATA_SHEET(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPP_TYPE_DATA_SHEET))
#define PSPP_IS_DATA_SHEET_CLASS(class)   (G_TYPE_CHECK_CLASS_TYPE ((class),PSPP_TYPE_DATA_SHEET))
#define PSPPIRE_DATA_SHEET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPP_TYPE_DATA_SHEET,PsppireDataSheetClass))

typedef struct _PsppireDataSheet      PsppireDataSheet;
typedef struct _PsppireDataSheetClass PsppireDataSheetClass;

struct _PsppireDataSheet
{
  PsppSheetView parent;

  struct _PsppireDataStore *data_store;
  gboolean show_value_labels;
  gboolean show_case_numbers;
  gboolean may_create_vars;
  gboolean may_delete_vars;

  gboolean owns_primary_selection;

  guint scroll_to_bottom_signal;
  guint scroll_to_right_signal;

  GtkClipboard *clip;
  guint on_owner_change_signal;

  PsppSheetViewColumn *new_variable_column;

  GtkBuilder *builder;

  GtkWidget *container;
  GtkUIManager *uim;
  gboolean dispose_has_run;
};

struct _PsppireDataSheetClass 
{
  PsppSheetViewClass parent_class;
};

GType psppire_data_sheet_get_type (void) G_GNUC_CONST;
GtkWidget *psppire_data_sheet_new (void);

struct _PsppireDataStore *psppire_data_sheet_get_data_store (PsppireDataSheet *);
void psppire_data_sheet_set_data_store (PsppireDataSheet *,
                                        struct _PsppireDataStore *);

gboolean psppire_data_sheet_get_value_labels (const PsppireDataSheet *);
void psppire_data_sheet_set_value_labels (PsppireDataSheet *,
                                          gboolean show_value_labels);

gboolean psppire_data_sheet_get_case_numbers (const PsppireDataSheet *);
void psppire_data_sheet_set_case_numbers (PsppireDataSheet *,
                                          gboolean show_case_numbers);

gboolean psppire_data_sheet_get_may_create_vars (PsppireDataSheet *);
void psppire_data_sheet_set_may_create_vars (PsppireDataSheet *, gboolean);

gboolean psppire_data_sheet_get_may_delete_vars (PsppireDataSheet *);
void psppire_data_sheet_set_may_delete_vars (PsppireDataSheet *, gboolean);

void psppire_data_sheet_goto_variable (PsppireDataSheet *, gint dict_index);
struct variable *psppire_data_sheet_get_current_variable (const PsppireDataSheet *);

void psppire_data_sheet_goto_case (PsppireDataSheet *, gint case_index);
gint psppire_data_sheet_get_selected_case (const PsppireDataSheet *);
gint psppire_data_sheet_get_current_case (const PsppireDataSheet *);

GtkUIManager *psppire_data_sheet_get_ui_manager (PsppireDataSheet *);

G_END_DECLS

#endif /* PSPPIRE_DATA_SHEET_H */
