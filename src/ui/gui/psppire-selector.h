/*
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2007  Free Software Foundation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/


#ifndef __PSPPIRE_SELECTOR_H__
#define __PSPPIRE_SELECTOR_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtktreemodelfilter.h>

G_BEGIN_DECLS

#define PSPPIRE_SELECTOR_TYPE            (psppire_selector_get_type ())
#define PSPPIRE_SELECTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_SELECTOR_TYPE, PsppireSelector))
#define PSPPIRE_SELECTOR_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_SELECTOR_TYPE, PsppireSelectorClass))
#define PSPPIRE_IS_SELECTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_SELECTOR_TYPE))
#define PSPPIRE_IS_SELECTOR_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_SELECTOR_TYPE))


typedef struct _PsppireSelector       PsppireSelector;
typedef struct _PsppireSelectorClass  PsppireSelectorClass;


/* Function for appending selected items to the destination widget */
typedef void SelectItemsFunc (GtkTreeIter iter,
			      GtkWidget *dest,
			      GtkTreeModel *source_model);


/* Function to determine whether an item in MODEL, pointed to by ITER
   is currently selected.

   Returns TRUE if the item is currently selected, FALSE otherwise.
 */
typedef gboolean FilterItemsFunc (GtkTreeModel *model,
				  GtkTreeIter *iter,
				  PsppireSelector *selector);

enum psppire_selector_dir
  {
    PSPPIRE_SELECTOR_SOURCE_TO_DEST,
    PSPPIRE_SELECTOR_DEST_TO_SOURCE
  };


struct _PsppireSelector
{
  GtkButton parent;

  /* <private> */
  GtkWidget *arrow;

  enum psppire_selector_dir direction;
  GtkWidget *source;
  GtkWidget *dest;


  gint orientation;

  GtkTreeModelFilter *filtered_source;

  SelectItemsFunc *select_items;
  FilterItemsFunc *filter;
};

struct _PsppireSelectorClass
{
  GtkButtonClass parent_class;

  /* This is a hash of Lists of FilterItemsFunc pointers, keyed by address of
     the source widget */
  GHashTable *source_hash;
};

GType      psppire_selector_get_type        (void);
GtkWidget* psppire_selector_new             (void);
void       psppire_selector_set_subjects    (PsppireSelector *,
					     GtkWidget *,
					     GtkWidget *,
					     SelectItemsFunc *,
					     FilterItemsFunc * );

GType psppire_selector_orientation_get_type (void) G_GNUC_CONST;


typedef enum {
  PSPPIRE_SELECT_SOURCE_BEFORE_DEST,
  PSPPIRE_SELECT_SOURCE_AFTER_DEST,
  PSPPIRE_SELECT_SOURCE_ABOVE_DEST,
  PSPPIRE_SELECT_SOURCE_BELOW_DEST
} PsppireSelectorOrientation;

#define G_TYPE_PSPPIRE_SELECTOR_ORIENTATION \
  (psppire_selector_orientation_get_type())



G_END_DECLS

#endif /* __PSPPIRE_SELECTOR_H__ */
