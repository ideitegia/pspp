/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2013  Free Software Foundation

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

/*
  A special purpose widget used in the means dialog.
*/

#ifndef __PSPPIRE_MEANS_LAYER_H__
#define __PSPPIRE_MEANS_LAYER_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "psppire-var-view.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_MEANS_LAYER            (psppire_means_layer_get_type ())

#define PSPPIRE_MEANS_LAYER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    PSPPIRE_TYPE_MEANS_LAYER, PsppireMeansLayer))

#define PSPPIRE_MEANS_LAYER_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_TYPE_MEANS_LAYER, PsppireMeansLayerClass))

#define PSPPIRE_IS_MEANS_LAYER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_TYPE_MEANS_LAYER))

#define PSPPIRE_IS_MEANS_LAYER_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_TYPE_MEANS_LAYER))


typedef struct _PsppireMeansLayer       PsppireMeansLayer;
typedef struct _PsppireMeansLayerClass  PsppireMeansLayerClass;


struct _PsppireMeansLayer
{
  GtkVBox parent;

  /* <private> */
  GtkWidget *var_view;
  int n_layers;
  int current_layer;
  GPtrArray *layer;
  gboolean dispose_has_run;

  GtkWidget *label;
  GtkWidget *back;
  GtkWidget *forward;
};


struct _PsppireMeansLayerClass
{
  GtkVBoxClass parent_class;
};

GType      psppire_means_layer_get_type        (void);
GType      psppire_means_layer_model_get_type        (void);
GtkWidget * psppire_means_layer_new (void);

void       psppire_means_layer_clear (PsppireMeansLayer *ml);
GtkTreeModel *psppire_means_layer_get_model_n (PsppireMeansLayer *ml, gint n);
GtkTreeModel *psppire_means_layer_get_model (PsppireMeansLayer *ml);

void psppire_means_layer_update (PsppireMeansLayer *ml);

G_END_DECLS

#endif /* __PSPPIRE_MEANS_LAYER_H__ */
