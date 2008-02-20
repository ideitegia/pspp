/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006  Free Software Foundation

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


#ifndef VAR_SHEET_H
#define VAR_SHEET_H

#include <gtksheet/gtksheet.h>

#include "psppire-dict.h"
#include "psppire-var-store.h"

#define PSPPIRE_TYPE_VAR_SHEET                  (psppire_var_sheet_get_type ())
#define PSPPIRE_VAR_SHEET(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_TYPE_VAR_SHEET, PsppireVarSheet))
#define PSPPIRE_VAR_SHEET_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_TYPE_VAR_SHEET, PsppireVarSheetClass))
#define PSPPIRE_IS_VAR_SHEET(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_VAR_SHEET))
#define PSPPIRE_IS_VAR_SHEET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_VAR_SHEET))
#define PSPPIRE_VAR_SHEET_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), PSPPIRE_TYPE_VAR_SHEET, PsppireVarSheetClass))

typedef struct _PsppireVarSheet PsppireVarSheet;
typedef struct _PsppireVarSheetClass PsppireVarSheetClass;

enum {COL_NAME,
      COL_TYPE,
      COL_WIDTH,
      COL_DECIMALS,
      COL_LABEL,
      COL_VALUES,
      COL_MISSING,
      COL_COLUMNS,
      COL_ALIGN,
      COL_MEASURE,
      n_COLS};

struct _PsppireVarSheet {
  GtkSheet parent;
  gboolean may_create_vars;
};

struct _PsppireVarSheetClass {
  GtkSheetClass parent;
};

GType psppire_var_sheet_get_type (void);

/* Create the var sheet */
GtkWidget* psppire_var_sheet_new_with_var_store (PsppireVarStore *);

/* Glade interface for creating a var sheet. */
GtkWidget* psppire_variable_sheet_create (gchar *widget_name,
                                          gchar *string1,
                                          gchar *string2,
                                          gint int1, gint int2);

void psppire_var_sheet_range_set_editable (PsppireVarSheet *sheet,
                                           const GtkSheetRange *urange,
                                           gboolean editable);

void psppire_var_sheet_set_may_create_vars (PsppireVarSheet *sheet,
                                            gboolean may_create_vars);


#define n_ALIGNMENTS 3

extern const gchar *const alignments[n_ALIGNMENTS + 1];

#define n_MEASURES 3

extern const gchar *const measures[n_MEASURES + 1];


#endif
