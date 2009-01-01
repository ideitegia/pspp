/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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

#include "psppire-window-register.h"

static void psppire_window_register_init            (PsppireWindowRegister      *window_register);
static void psppire_window_register_class_init      (PsppireWindowRegisterClass *class);

static void psppire_window_register_finalize        (GObject   *object);
static void psppire_window_register_dispose        (GObject   *object);

static GObjectClass *parent_class = NULL;


enum  {
  INSERTED,
  REMOVED,
  n_SIGNALS
};

static guint signals [n_SIGNALS];

GType
psppire_window_register_get_type (void)
{
  static GType window_register_type = 0;

  if (!window_register_type)
    {
      static const GTypeInfo window_register_info =
      {
	sizeof (PsppireWindowRegisterClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_window_register_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireWindowRegister),
	0,
        (GInstanceInitFunc) psppire_window_register_init,
      };

      window_register_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireWindowRegister",
						&window_register_info, 0);
    }

  return window_register_type;
}


static void
psppire_window_register_finalize (GObject *object)
{
}

static void
psppire_window_register_dispose  (GObject *object)
{
}

static PsppireWindowRegister *the_instance = NULL;

static GObject*
psppire_window_register_construct   (GType                  type,
				     guint                  n_construct_params,
				     GObjectConstructParam *construct_params)
{
  GObject *object;
  
  if (!the_instance)
    {
      object = G_OBJECT_CLASS (parent_class)->constructor (type,
                                                           n_construct_params,
                                                           construct_params);
      the_instance = PSPPIRE_WINDOW_REGISTER (object);
    }
  else
    object = g_object_ref (G_OBJECT (the_instance));

  return object;
}

static void
psppire_window_register_class_init (PsppireWindowRegisterClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_window_register_finalize;
  object_class->dispose = psppire_window_register_dispose;
  object_class->constructor = psppire_window_register_construct;

  signals [INSERTED] =
    g_signal_new ("inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__POINTER,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_POINTER);

  signals [REMOVED] =
    g_signal_new ("removed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__POINTER,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_POINTER);
}

static void
psppire_window_register_init (PsppireWindowRegister *window_register)
{
  window_register->dispose_has_run = FALSE;
  window_register->name_table = g_hash_table_new (g_str_hash, g_str_equal);
}

void
psppire_window_register_insert (PsppireWindowRegister *wr, PsppireWindow *window, const gchar *name)
{
  g_hash_table_insert (wr->name_table, (gpointer) name, window);

  g_signal_emit (wr, signals[INSERTED], 0, name);
}


void
psppire_window_register_remove (PsppireWindowRegister *wr, const gchar *name)
{
  g_signal_emit (wr, signals[REMOVED], 0, name);

  g_hash_table_remove (wr->name_table, (gpointer) name);
}

PsppireWindow *
psppire_window_register_lookup (PsppireWindowRegister *wr, const gchar *name)
{
  return g_hash_table_lookup (wr->name_table, name);
}

void
psppire_window_register_foreach (PsppireWindowRegister *wr, GHFunc func, PsppireWindow *win)
{
  g_hash_table_foreach (wr->name_table, func, win);
}

static void
minimise_window (gpointer key, gpointer value, gpointer data)
{
  gtk_window_iconify (GTK_WINDOW (value));
}


void
psppire_window_register_minimise_all (PsppireWindowRegister *wr)
{
  g_hash_table_foreach (wr->name_table, minimise_window, wr);
}



PsppireWindowRegister *
psppire_window_register_new (void)
{
  return g_object_new (psppire_window_register_get_type (), NULL);
}
