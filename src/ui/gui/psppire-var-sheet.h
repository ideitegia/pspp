/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2012 Free Software Foundation, Inc.

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


#include <glib.h>
#include <glib-object.h>
#include <gtk-contrib/psppire-sheet.h>
#include "val-labs-dialog.h"
#include "missing-val-dialog.h"
#include "var-type-dialog.h"


G_BEGIN_DECLS

#define PSPPIRE_VAR_SHEET_TYPE            (psppire_var_sheet_get_type ())
#define PSPPIRE_VAR_SHEET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_VAR_SHEET_TYPE, PsppireVarSheet))
#define PSPPIRE_VAR_SHEET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_VAR_SHEET_TYPE, PsppireVarSheetClass))
#define PSPPIRE_IS_VAR_SHEET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_VAR_SHEET_TYPE))
#define PSPPIRE_IS_VAR_SHEET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_VAR_SHEET_TYPE))


typedef struct _PsppireVarSheet       PsppireVarSheet;
typedef struct _PsppireVarSheetClass  PsppireVarSheetClass;

struct _PsppireVarSheet
{
  PsppireSheet parent;

  gboolean dispose_has_run;
  gboolean may_create_vars;

  struct val_labs_dialog *val_labs_dialog ;
  struct missing_val_dialog *missing_val_dialog ;
};


struct _PsppireVarSheetClass
{
  PsppireSheetClass parent_class;

  GtkListStore *alignment_list;
  GtkListStore *measure_list;

  void (*var_sheet)(PsppireVarSheet*);
};


GType          psppire_var_sheet_get_type        (void);
GtkWidget*     psppire_var_sheet_new             (void);

G_END_DECLS




#endif /* __PSPPIRE_VAR_SHEET_H__ */
