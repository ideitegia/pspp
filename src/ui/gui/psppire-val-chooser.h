/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011  Free Software Foundation

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


#ifndef __PSPPIRE_VAL_CHOOSER_H__
#define __PSPPIRE_VAL_CHOOSER_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define PSPPIRE_VAL_CHOOSER_TYPE            (psppire_val_chooser_get_type ())
#define PSPPIRE_VAL_CHOOSER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_VAL_CHOOSER_TYPE, PsppireValChooser))
#define PSPPIRE_VAL_CHOOSER_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_VAL_CHOOSER_TYPE, PsppireValChooserClass))
#define PSPPIRE_IS_VAL_CHOOSER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_VAL_CHOOSER_TYPE))
#define PSPPIRE_IS_VAL_CHOOSER_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_VAL_CHOOSER_TYPE))


typedef struct _PsppireValChooser       PsppireValChooser;
typedef struct _PsppireValChooserClass  PsppireValChooserClass;

#define n_VAL_CHOOSER_BUTTONS 7

struct range_widgets
{
  GtkLabel *label;
  GtkToggleButton *rb;
  GtkEntry *e1;
  GtkEntry *e2;
};

struct _PsppireValChooser
{
  GtkFrame parent;

  struct range_widgets rw [n_VAL_CHOOSER_BUTTONS];
  gboolean input_var_is_string;
};

struct _PsppireValChooserClass
{
  GtkTreeViewClass parent_class;

};

GType      psppire_val_chooser_get_type        (void);


G_END_DECLS




enum old_value_type
 {
   OV_NUMERIC,
   OV_STRING,
   OV_SYSMIS,
   OV_MISSING,
   OV_RANGE,
   OV_LOW_UP,
   OV_HIGH_DOWN,
   OV_ELSE
 };

struct old_value
 {
   enum old_value_type type;
   union {
     double v;
     gchar *s;
     double range[2];
   } v;
 };

GType old_value_get_type (void);

struct string;
void old_value_append_syntax (struct string *str, const struct old_value *ov);

void psppire_val_chooser_get_status (PsppireValChooser *vr, struct old_value *ov);
void psppire_val_chooser_set_status (PsppireValChooser *vr, const struct old_value *ov);



#endif /* __PSPPIRE_VAL_CHOOSER_H__ */
