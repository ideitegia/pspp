/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011 Free Software Foundation, Inc.

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

#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeysyms-compat.h>
#include "psppire-keypad.h"

enum {
  INSERT_SYNTAX,
  ERASE,
  n_SIGNALS
};

static void psppire_keypad_class_init          (PsppireKeypadClass *klass);
static void psppire_keypad_init                (PsppireKeypad      *kp);

static guint keypad_signals [n_SIGNALS] = { 0 };

GType
psppire_keypad_get_type (void)
{
  static GType kp_type = 0;

  if (!kp_type)
    {
      static const GTypeInfo kp_info =
      {
	sizeof (PsppireKeypadClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_keypad_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireKeypad),
	0,
	(GInstanceInitFunc) psppire_keypad_init,
      };

      kp_type = g_type_register_static (GTK_TYPE_EVENT_BOX, "PsppireKeypad",
					&kp_info, 0);
    }

  return kp_type;
}

static GObjectClass * parent_class = NULL;

static void
psppire_keypad_dispose (GObject *obj)
{
  PsppireKeypad *kp = (PsppireKeypad *)obj;

  if (kp->dispose_has_run)
    return;

  /* Make sure dispose does not run twice. */
  kp->dispose_has_run = TRUE;

  g_hash_table_unref (kp->frag_table);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_keypad_finalize (GObject *obj)
{
   /* Chain up to the parent class */
   G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
psppire_keypad_class_init (PsppireKeypadClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = psppire_keypad_dispose;
  gobject_class->finalize = psppire_keypad_finalize;

  keypad_signals[INSERT_SYNTAX] = g_signal_new ("insert-syntax",
					 G_TYPE_FROM_CLASS (klass),
	                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
	                                 G_STRUCT_OFFSET (PsppireKeypadClass,
							  keypad),
                                         NULL,
                                         NULL,
					 g_cclosure_marshal_VOID__STRING,
                                         G_TYPE_NONE, 1,
					 G_TYPE_STRING);

  keypad_signals[ERASE] = g_signal_new ("erase",
					 G_TYPE_FROM_CLASS (klass),
	                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
	                                 G_STRUCT_OFFSET (PsppireKeypadClass,
							  keypad),
                                         NULL,
                                         NULL,
					 g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);
}


/*
   These are the strings that will be arguments to
   the emitted signals.
   The order of these must correspond
   to the order of the button declarations
*/
static const char * const keypad_insert_text[] = {
  "0",  "1",  "2", "3", "4", "5", "6", "7", "8", "9",
  ".", "+", "-", "*", "**", "/", "=", "<>", "<", "<=",
  ">", ">=", "&", "|", "~", "()", NULL
};


/* Callback for any button click.
   Emits the "insert-syntax" signal for the keypad,
   with the string corresponding to the clicked button.
*/
static void
button_click (GtkButton *b, PsppireKeypad *kp)
{
  const gchar *s = g_hash_table_lookup (kp->frag_table, b);


  if ( s )
    g_signal_emit (kp, keypad_signals [INSERT_SYNTAX], 0, s);
  else
    g_signal_emit (kp, keypad_signals [ERASE], 0);
}

static const gint cols = 6;
static const gint rows = 5;



/* Add BUTTON to KP.  The top-left corner at X1,Y1, the
   botton-right corner at X2,Y2 */
static void
add_button (PsppireKeypad *kp, GtkWidget **button,
	    gint x1, gint x2,
	    gint y1, gint y2)
{
  g_object_set (G_OBJECT (*button), "focus-on-click", FALSE, NULL);

  gtk_table_attach_defaults (GTK_TABLE (kp->table),
			     *button,
			     x1, x2,
			     y1, y2);

  gtk_widget_set_size_request (*button,
			       30 * rows / (float) cols,
			       30 * cols / (float) rows);

  g_hash_table_insert (kp->frag_table, *button,
		       (void *) keypad_insert_text[(button - &kp->digit[0])] );

  g_signal_connect (*button, "clicked",
		    G_CALLBACK (button_click), kp);

  gtk_widget_show (*button);
}


/* Return  a  new button with CODE as the unicode character for its label */
static inline GtkWidget *
button_new_from_unicode (gunichar code)
{
  char s[6] = {0,0,0,0,0,0};

  g_unichar_to_utf8 (code, s);

  return gtk_button_new_with_label (s);
}


/* Callback which occurs when the mouse enters the widget.
   It sets or unsets the focus.
*/
static gboolean
enter_leave_notify (GtkWidget   *widget,
      GdkEventCrossing *event,
      gpointer     user_data)
{
  /* Do nothing if we're just moving between the widget and
     its children */
 if (event->detail == GDK_NOTIFY_INFERIOR)
   return FALSE;

 if (event->type == GDK_ENTER_NOTIFY)
   gtk_widget_grab_focus (widget);

 return FALSE;
}

static gboolean
key_release_callback (GtkWidget   *widget,
		      GdkEventKey *event,
		      gpointer     user_data)
{
  if ( ! gtk_widget_has_focus (widget))
    return FALSE;

  switch (event->keyval)
    {
    case '(':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "(");
      break;
    case ')':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, ")");
      break;
    case '>':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, ">");
      break;
    case '<':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "<");
      break;
    case GDK_KP_Equal :
    case '=':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "=");
      break;
    case GDK_KP_Multiply :
    case '*':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "*");
      break;
    case GDK_KP_Add :
    case '+':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "+");
      break;
    case GDK_KP_Subtract :
    case '-':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "-");
      break;
    case GDK_KP_Decimal :
    case '.':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, ".");
      break;
    case GDK_KP_Divide :
    case '/':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "/");
      break;
    case GDK_KP_0 :
    case '0':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "0");
      break;
    case GDK_KP_1 :
    case '1':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "1");
      break;
    case GDK_KP_2 :
    case '2':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "2");
      break;
    case GDK_KP_3 :
    case '3':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "3");
      break;
    case GDK_KP_4 :
    case '4':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "4");
      break;
    case GDK_KP_5 :
    case '5':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "5");
      break;
    case GDK_KP_6 :
    case '6':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "6");
      break;
    case GDK_KP_7 :
    case '7':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "7");
      break;
    case GDK_KP_8 :
    case '8':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "8");
      break;
    case GDK_KP_9 :
    case '9':
      g_signal_emit (widget, keypad_signals [INSERT_SYNTAX], 0, "9");
      break;
     default:
       break;
    };

  return FALSE;
}


static void
psppire_keypad_init (PsppireKeypad *kp)
{
  gint i;
  const int digit_voffset = 0;
  const int digit_hoffset = 3;

  gtk_widget_set_can_focus (GTK_WIDGET (kp), TRUE);

  kp->dispose_has_run = FALSE;

  g_signal_connect (kp, "enter-notify-event", G_CALLBACK (enter_leave_notify),
		    NULL);

  g_signal_connect (kp, "leave-notify-event", G_CALLBACK (enter_leave_notify),
		    NULL);

  g_signal_connect (kp, "key-release-event", G_CALLBACK (key_release_callback),
		    NULL);

  kp->frag_table = g_hash_table_new (g_direct_hash, g_direct_equal);

  kp->table = gtk_table_new (rows, cols, TRUE);

  /* Buttons for the digits */
  for (i = 0; i < 10; i++)
    {
      int j = i - 1;
      char buf[5];
      g_snprintf (buf, 5, "%d", i);
      kp->digit[i] = gtk_button_new_with_label (buf);

      if ( i == 0 )
	add_button (kp, &kp->digit[i],
		    digit_hoffset + 0, digit_hoffset + 2,
		    digit_voffset + 3, digit_voffset + 4);
      else
	add_button (kp, &kp->digit[i],
		    digit_hoffset + j % 3, digit_hoffset + j % 3 + 1,
		    digit_voffset + 2 - (j / 3),
		    digit_voffset + 2 - (j / 3) + 1);
    }

  /* ... all the other buttons */

  kp->dot = button_new_from_unicode (0xB7);     /* MIDDLE DOT */
  add_button (kp, &kp->dot, digit_hoffset + 2,
	      digit_hoffset + 3,
	      digit_voffset + 3,
	      digit_voffset + 4);

  kp->plus  = gtk_button_new_with_label ("+");
  add_button (kp, &kp->plus, 0, 1,
	      0,1);

  kp->minus = button_new_from_unicode (0x2212); /* MINUS SIGN */
  add_button (kp, &kp->minus, 0, 1,
	      1,2);

  kp->star  = button_new_from_unicode (0xD7);   /* MULTIPLICATION SIGN */
  add_button (kp, &kp->star, 0, 1,
	      2,3);

  kp->slash = button_new_from_unicode (0xF7);   /* DIVISION SIGN */
  add_button (kp, &kp->slash, 0, 1,
	      3,4);

  {
    GtkWidget *label;
    char *markup =
      g_markup_printf_escaped ("<span style=\"italic\">x<sup>y</sup></span>");

    label = gtk_label_new ("**");

    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);

    kp->star_star = gtk_button_new ();
    gtk_container_add (GTK_CONTAINER (kp->star_star), label);

    gtk_widget_show (label);

    add_button (kp, &kp->star_star,
		0, 1,
		4, 5);
  }


  kp->gt = button_new_from_unicode (0x3E); /* GREATER-THAN SIGN*/
  add_button (kp, &kp->gt, 2, 3,
	      0,1);

  kp->lt = button_new_from_unicode (0x3C); /* LESS-THAN SIGN*/
  add_button (kp, &kp->lt, 1, 2,
	      0,1);

  kp->ge = button_new_from_unicode (0x2265); /* GREATER-THAN OR EQUAL */
  add_button (kp, &kp->ge, 2, 3,
	      1,2);

  kp->le = button_new_from_unicode (0x2264); /* LESS-THAN OR EQUAL */
  add_button (kp, &kp->le, 1, 2,
	      1,2);

  kp->neq = button_new_from_unicode (0x2260); /* NOT EQUAL */
  add_button (kp, &kp->neq, 2, 3,
	      2,3);

  kp->eq = gtk_button_new_with_label ("=");
  add_button (kp, &kp->eq, 1, 2,
	      2,3);

  kp->parentheses = gtk_button_new_with_label ("()");
  add_button (kp, &kp->parentheses, 2, 3,
	      4,5);


  kp->delete = gtk_button_new_with_label ("Delete");
  add_button (kp, &kp->delete, 3, 6,
	      4,5);



  kp->and = button_new_from_unicode (0x2227); /* LOGICAL AND */
  add_button (kp, &kp->and, 1, 2,
	      3,4);


  kp->or = button_new_from_unicode (0x2228); /* LOGICAL OR */
  add_button (kp, &kp->or, 2, 3,
	      3,4);


  kp->not = button_new_from_unicode (0xAC); /* NOT SIGN */
  add_button (kp, &kp->not, 1, 2,
	      4,5);



  g_object_set (G_OBJECT (kp->table), "row-spacing", 5, NULL);
  g_object_set (G_OBJECT (kp->table), "column-spacing", 5, NULL);

  gtk_container_add (GTK_CONTAINER (kp), kp->table);
  gtk_widget_show (kp->table);

  gtk_widget_add_events (GTK_WIDGET (kp),
	GDK_KEY_RELEASE_MASK  |
	GDK_LEAVE_NOTIFY_MASK |
	GDK_ENTER_NOTIFY_MASK |
        GDK_FOCUS_CHANGE_MASK);

}


GtkWidget*
psppire_keypad_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_keypad_get_type (), NULL));
}
