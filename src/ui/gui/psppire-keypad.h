/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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
 This module implements a keypad widget, similar to that dipicted
 below:

 +---+---+---+---+---+
 |   	       	     |
 | +   <   7   8   9 |
 +   	       	     +
 |   	       	     |
 | -   >   4   5   6 |
 +   	       	     +
 |   	       	     |
 | *  <=   1   2   3 |
 +   	       	     +
 |     	       	     |
 | /  >=     0 	   . |
 + 	       	     +
 | y  !=   =   (   ) |
 |x	       	     |
 +---+---+---+---+---+

 It's intended for dialog boxes which produce PSPP syntax.  Thus,
 a "insert-syntax" signal is emitted whenever a key is clicked.
 The signal supports the following callback:

 void insert_syntax (Keypad *kp, const char *syntax, gpointer user_data);

*/

#ifndef __PSPPIRE_KEYPAD_H__
#define __PSPPIRE_KEYPAD_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define PSPPIRE_KEYPAD_TYPE            (psppire_keypad_get_type ())
#define PSPPIRE_KEYPAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_KEYPAD_TYPE, Psppire_Keypad))
#define PSPPIRE_KEYPAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_KEYPAD_TYPE, Psppire_KeypadClass))
#define PSPPIRE_IS_KEYPAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_KEYPAD_TYPE))
#define PSPPIRE_IS_KEYPAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_KEYPAD_TYPE))


typedef struct _PsppireKeypad       PsppireKeypad;
typedef struct _PsppireKeypadClass  PsppireKeypadClass;

/* All members are private. */
struct _PsppireKeypad
{
  GtkEventBox parent;

  /* Hash of syntax fragments indexed by pointer to widget (button) */
  GHashTable *frag_table;
  GtkWidget *table;

  /* The order of everything here is important */
  GtkWidget *digit[10];
  GtkWidget *dot;
  GtkWidget *plus;
  GtkWidget *minus;
  GtkWidget *star;
  GtkWidget *star_star;
  GtkWidget *slash;
  GtkWidget *eq;
  GtkWidget *neq;
  GtkWidget *lt;
  GtkWidget *le;
  GtkWidget *gt;
  GtkWidget *ge;
  GtkWidget *and;
  GtkWidget *or;
  GtkWidget *not;


  GtkWidget *parentheses;
  GtkWidget *delete;


  gboolean dispose_has_run;
};


struct _PsppireKeypadClass
{
  GtkEventBoxClass parent_class;
  void (*keypad)(PsppireKeypad*);
};


GType          psppire_keypad_get_type        (void);
GtkWidget*     psppire_keypad_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_KEYPAD_H__ */
