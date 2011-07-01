/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010  Free Software Foundation

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


#ifndef __PSPPIRE_SELECTOR_H__
#define __PSPPIRE_SELECTOR_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

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
			      GtkTreeModel *source_model,
			      gpointer data);

/* Function to determine if items may be selected */
typedef gboolean AllowSelectionFunc (GtkWidget *src, GtkWidget *dest);


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

  gboolean dispose_has_run;

  enum psppire_selector_dir direction;

  GtkWidget *source;
  GtkWidget *dest;

  /* A flag indicating that the object is in the process of
     updating its subjects.
     (not thread safe if two threads access the same object)
  */
  gboolean selecting;

  gint orientation;

  GtkTreeModelFilter *filtered_source;

  SelectItemsFunc *select_items;
  gpointer select_user_data;

  FilterItemsFunc *filter;

  AllowSelectionFunc *allow_selection;

  gulong row_activate_id ;

  gulong source_select_id ;

  gboolean primary_requested;
};

struct _PsppireSelectorClass
{
  GtkButtonClass parent_class;

  /* This is a hash of Lists of FilterItemsFunc pointers, keyed by address of
     the source widget */
  GHashTable *source_hash;

  /* A hash of SelectItemFuncs indexed by GType */
  GHashTable *default_selection_funcs;
};

GType      psppire_selector_get_type        (void);
GtkWidget* psppire_selector_new             (void);


/* Set FILTER_FUNC for this selector */
void psppire_selector_set_filter_func (PsppireSelector *selector,
				       FilterItemsFunc *filter_func);

/* Set SELECT_FUNC for this selector */
void psppire_selector_set_select_func (PsppireSelector *selector,
				       SelectItemsFunc *select_func,
				       gpointer user_data);


void psppire_selector_set_allow (PsppireSelector *, AllowSelectionFunc *);


GType psppire_selector_orientation_get_type (void) G_GNUC_CONST;


typedef enum {
  PSPPIRE_SELECT_SOURCE_BEFORE_DEST,
  PSPPIRE_SELECT_SOURCE_AFTER_DEST,
  PSPPIRE_SELECT_SOURCE_ABOVE_DEST,
  PSPPIRE_SELECT_SOURCE_BELOW_DEST
} PsppireSelectorOrientation;

#define PSPPIRE_TYPE_SELECTOR_ORIENTATION \
  (psppire_selector_orientation_get_type())


void psppire_selector_set_default_selection_func (GType type, SelectItemsFunc *);


G_END_DECLS

#endif /* __PSPPIRE_SELECTOR_H__ */
