/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2009, 2010, 2011, 2012  Free Software Foundation

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
#include <string.h>
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <libpspp/i18n.h>

#include <gobject/gvaluecollector.h>

#include "psppire-var-store.h"
#include "helper.h"

#include <data/dictionary.h>
#include <data/variable.h>
#include <data/format.h>
#include <data/missing-values.h>

#include "val-labs-dialog.h"
#include "missing-val-dialog.h"
#include <data/value-labels.h>

#include "var-display.h"

enum
  {
    PROP_0,
    PSPPIRE_VAR_STORE_DICT
  };

static void         psppire_var_store_init            (PsppireVarStore      *var_store);
static void         psppire_var_store_class_init      (PsppireVarStoreClass *class);
static void         psppire_var_store_finalize        (GObject           *object);
static void         psppire_var_store_dispose        (GObject           *object);


static GObjectClass *parent_class = NULL;

GType
psppire_var_store_get_type (void)
{
  static GType var_store_type = 0;

  if (!var_store_type)
    {
      static const GTypeInfo var_store_info =
      {
	sizeof (PsppireVarStoreClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_var_store_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireVarStore),
	0,
        (GInstanceInitFunc) psppire_var_store_init,
      };

      var_store_type = g_type_register_static (G_TYPE_OBJECT, "PsppireVarStore", &var_store_info, 0);
    }

  return var_store_type;
}

static void
psppire_var_store_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_DICT:
      if ( self->dictionary)
	g_object_unref (self->dictionary);
      self->dictionary = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
    }
}

static void
psppire_var_store_get_property (GObject      *object,
                        guint         property_id,
                        GValue       *value,
                        GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_DICT:
      g_value_take_object (value, self->dictionary);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
psppire_var_store_class_init (PsppireVarStoreClass *class)
{
  GObjectClass *object_class;
  GParamSpec *dict_pspec;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_var_store_finalize;
  object_class->dispose = psppire_var_store_dispose;
  object_class->set_property = psppire_var_store_set_property;
  object_class->get_property = psppire_var_store_get_property;

  dict_pspec = g_param_spec_object ("dictionary",
				    "Dictionary",
				    "The PsppireDict represented by this var store",
				    PSPPIRE_TYPE_DICT,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
				    
  g_object_class_install_property (object_class,
                                   PSPPIRE_VAR_STORE_DICT,
                                   dict_pspec);
}

static void
psppire_var_store_init (PsppireVarStore *var_store)
{
  var_store->dictionary = NULL;
}

struct variable *
psppire_var_store_get_var (PsppireVarStore *store, glong row)
{
  return psppire_dict_get_variable (store->dictionary, row);
}

/**
 * psppire_var_store_new:
 * @dict: The dictionary for this var_store.
 *
 *
 * Return value: a new #PsppireVarStore
 **/
PsppireVarStore *
psppire_var_store_new (PsppireDict *dict)
{
  PsppireVarStore *retval;

  retval = g_object_new (GTK_TYPE_VAR_STORE, "dictionary", dict, NULL);

  //  psppire_var_store_set_dictionary (retval, dict);

  return retval;
}

static void
psppire_var_store_finalize (GObject *object)
{
  /* must chain up */
  (* parent_class->finalize) (object);
}

static void
psppire_var_store_dispose (GObject *object)
{
  PsppireVarStore *self = PSPPIRE_VAR_STORE (object);

  if (self->dictionary)
    g_object_unref (self->dictionary);

  /* must chain up */
  (* parent_class->finalize) (object);
}


/* Return the number of variables */
gint
psppire_var_store_get_var_cnt (PsppireVarStore  *store)
{
  return psppire_dict_get_var_cnt (store->dictionary);
}
