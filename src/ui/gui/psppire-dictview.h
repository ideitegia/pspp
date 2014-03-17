/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2013  Free Software Foundation

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


#ifndef __PSPPIRE_DICT_VIEW_H__
#define __PSPPIRE_DICT_VIEW_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "data/format.h"
#include "psppire-dict.h"
#include "dict-display.h"

G_BEGIN_DECLS

#define PSPPIRE_DICT_VIEW_TYPE            (psppire_dict_view_get_type ())
#define PSPPIRE_DICT_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_DICT_VIEW_TYPE, PsppireDictView))
#define PSPPIRE_DICT_VIEW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_DICT_VIEW_TYPE, PsppireDictViewClass))
#define PSPPIRE_IS_DICT_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_DICT_VIEW_TYPE))
#define PSPPIRE_IS_DICT_VIEW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_DICT_VIEW_TYPE))


typedef struct _PsppireDictView       PsppireDictView;
typedef struct _PsppireDictViewClass  PsppireDictViewClass;



struct _PsppireDictView
{
  GtkTreeView parent;

  PsppireDict *dict;
  var_predicate_func *predicate;
  GtkWidget *menu;
  gboolean prefer_labels;
  GtkTreeModel *sorted_model;
};

struct _PsppireDictViewClass
{
  GtkTreeViewClass parent_class;

};

GType      psppire_dict_view_get_type        (void);
void psppire_dict_view_get_selected_variables (PsppireDictView *,
                                               struct variable ***vars,
                                               size_t *n_varsp);
struct variable * psppire_dict_view_get_selected_variable (PsppireDictView *);

const char *get_var_measurement_stock_id (enum fmt_type, enum measure);

G_END_DECLS

#endif /* __PSPPIRE_DICT_VIEW_H__ */
