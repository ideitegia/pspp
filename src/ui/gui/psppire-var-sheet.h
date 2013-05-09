/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2011, 2012 Free Software Foundation, Inc.

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

#ifndef __PSPPIRE_VAR_SHEET_H__
#define __PSPPIRE_VAR_SHEET_H__

/* PsppireVarSheet is a PsppSheetView that displays the variables in a
   dictionary, one variable per row.

   PsppireDataSheet is usually a child of PsppireDataEditor in the widget
   hierarchy.  Other widgets can also use it. */

#include <gtk/gtk.h>
#include "data/format.h"
#include "ui/gui/pspp-sheet-view.h"


G_BEGIN_DECLS

#define PSPPIRE_TYPE_FMT_USE (psppire_fmt_use_get_type ())

GType psppire_fmt_use_get_type (void) G_GNUC_CONST;

#define PSPPIRE_VAR_SHEET_TYPE            (psppire_var_sheet_get_type ())
#define PSPPIRE_VAR_SHEET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_VAR_SHEET_TYPE, PsppireVarSheet))
#define PSPPIRE_VAR_SHEET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_VAR_SHEET_TYPE, PsppireVarSheetClass))
#define PSPPIRE_IS_VAR_SHEET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_VAR_SHEET_TYPE))
#define PSPPIRE_IS_VAR_SHEET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_VAR_SHEET_TYPE))


typedef struct _PsppireVarSheet       PsppireVarSheet;
typedef struct _PsppireVarSheetClass  PsppireVarSheetClass;

enum
{
    PSPPIRE_VAR_SHEET_BACKEND_CHANGED,
    PSPPIRE_VAR_SHEET_VARIABLE_CHANGED,
    PSPPIRE_VAR_SHEET_VARIABLE_INSERTED,
    PSPPIRE_VAR_SHEET_VARIABLE_DELETED,
    PSPPIRE_VAR_SHEET_N_SIGNALS
 };

struct _PsppireVarSheet
{
  PsppSheetView parent;

  gboolean may_create_vars;
  gboolean may_delete_vars;
  enum fmt_use format_use;

  struct _PsppireDict *dict;

  gulong scroll_to_bottom_signal;
  gulong dict_signals[PSPPIRE_VAR_SHEET_N_SIGNALS];

  GtkBuilder *builder;

  GtkWidget *container;
  gulong on_switch_page_handler;

  GtkUIManager *uim;

  gboolean dispose_has_run;
};

struct _PsppireVarSheetClass
{
  PsppSheetViewClass parent_class;
};

GType          psppire_var_sheet_get_type        (void);
GtkWidget*     psppire_var_sheet_new             (void);

struct _PsppireDict *psppire_var_sheet_get_dictionary (PsppireVarSheet *);
void psppire_var_sheet_set_dictionary (PsppireVarSheet *,
                                       struct _PsppireDict *);

gboolean psppire_var_sheet_get_may_create_vars (PsppireVarSheet *);
void psppire_var_sheet_set_may_create_vars (PsppireVarSheet *, gboolean);

gboolean psppire_var_sheet_get_may_delete_vars (PsppireVarSheet *);
void psppire_var_sheet_set_may_delete_vars (PsppireVarSheet *, gboolean);

void psppire_var_sheet_goto_variable (PsppireVarSheet *, int dict_index);

GtkUIManager *psppire_var_sheet_get_ui_manager (PsppireVarSheet *);

G_END_DECLS

#endif /* __PSPPIRE_VAR_SHEET_H__ */
