/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012 Free Software Foundation, Inc.

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
#include "psppire-value-entry.h"
#include "data/data-in.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-format.h"

static void psppire_value_entry_finalize (GObject *);

G_DEFINE_TYPE (PsppireValueEntry,
               psppire_value_entry,
               GTK_TYPE_COMBO_BOX);

enum
  {
    COL_LABEL,                  /* Value label string. */
    COL_VALUE,                  /* union value *. */
    N_COLUMNS
  };

enum
  {
    PROP_0,
    PROP_SHOW_VALUE_LABEL,
    PROP_VARIABLE,
    PROP_VALUE_LABELS,
    PROP_FORMAT,
    PROP_ENCODING,
    PROP_WIDTH
  };

static void
psppire_value_entry_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PsppireValueEntry *obj = PSPPIRE_VALUE_ENTRY (object);

  switch (prop_id)
    {
    case PROP_SHOW_VALUE_LABEL:
      psppire_value_entry_set_show_value_label (obj,
                                                g_value_get_boolean (value));
      break;

    case PROP_VARIABLE:
      psppire_value_entry_set_variable (obj, g_value_get_pointer (value));
      break;

    case PROP_VALUE_LABELS:
      psppire_value_entry_set_value_labels (obj, g_value_get_pointer (value));
      break;

    case PROP_FORMAT:
      psppire_value_entry_set_format (obj, g_value_get_boxed (value));
      break;

    case PROP_ENCODING:
      psppire_value_entry_set_encoding (obj, g_value_get_string (value));
      break;

    case PROP_WIDTH:
      psppire_value_entry_set_width (obj, g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_value_entry_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  PsppireValueEntry *obj = PSPPIRE_VALUE_ENTRY (object);

  switch (prop_id)
    {
    case PROP_SHOW_VALUE_LABEL:
      g_value_set_boolean (value,
                           psppire_value_entry_get_show_value_label (obj));
      break;

    case PROP_VARIABLE:
      g_return_if_reached ();

    case PROP_VALUE_LABELS:
      g_value_set_pointer (value, obj->val_labs);
      break;

    case PROP_FORMAT:
      g_value_set_boxed (value, &obj->format);
      break;

    case PROP_ENCODING:
      g_value_set_string (value, psppire_value_entry_get_encoding (obj));
      break;

    case PROP_WIDTH:
      g_value_set_int (value, psppire_value_entry_get_width (obj));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_value_entry_text_changed (GtkEntryBuffer *buffer,
                                  GParamSpec *pspec,
                                  PsppireValueEntry *obj)
{
  obj->cur_value = NULL;
}


static void
on_realize (GtkWidget *w)
{
  GtkEntry *entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (w)));
  GtkEntryBuffer *buffer = gtk_entry_get_buffer (entry);

  gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (w), COL_LABEL);
  
  g_signal_connect (buffer, "notify::text",
                    G_CALLBACK (psppire_value_entry_text_changed), w);

  GTK_WIDGET_CLASS (psppire_value_entry_parent_class)->realize (w);
}

static void
psppire_value_entry_class_init (PsppireValueEntryClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);


  gobject_class->finalize = psppire_value_entry_finalize;
  gobject_class->set_property = psppire_value_entry_set_property;
  gobject_class->get_property = psppire_value_entry_get_property;
  widget_class->realize = on_realize;

  g_object_class_install_property (
    gobject_class, PROP_SHOW_VALUE_LABEL,
    g_param_spec_boolean ("show-value-label",
                          "Show Value Label",
                          "If true, a value that has a value label is shown "
                          "as the label.  If false, all values are shown "
                          "literally.",
                          TRUE,
                          G_PARAM_WRITABLE | G_PARAM_READABLE));

  g_object_class_install_property (
    gobject_class, PROP_VARIABLE,
    g_param_spec_pointer ("variable",
                          "Variable",
                          "Set to configure the PsppireValueEntry according "
                          "to the specified variable's value labels, format, "
                          "width, and encoding.",
                          G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_VALUE_LABELS,
    g_param_spec_pointer ("value-labels",
                          "Value Labels",
                          "The set of value labels from which the user may "
                          "choose and which is used to display the value (if "
                          "value labels are to be displayed)",
                          G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_FORMAT,
    g_param_spec_boxed ("format",
                        "Format",
                        "The format used to display values (that are not "
                        "displayed as value labels) and to interpret values "
                        "entered.",
                        PSPPIRE_TYPE_FORMAT,
                        G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_ENCODING,
    g_param_spec_string ("encoding",
                         "Encoding",
                         "The encoding (e.g. \"UTF-8\") for string values.  "
                         "For numeric values this setting has no effect.",
                         "UTF-8",
                         G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_WIDTH,
    g_param_spec_int ("width",
                      "Width",
                      "Width of the value, either 0 for a numeric value or "
                      "a positive integer count of bytes for string values.",
                      0, MAX_STRING,
                      0,
                      G_PARAM_READABLE | G_PARAM_WRITABLE));
}

static void
psppire_value_entry_init (PsppireValueEntry *obj)
{
  obj->show_value_label = true;
  obj->val_labs = NULL;
  obj->format = F_8_0;
  obj->encoding = NULL;
  obj->cur_value = NULL;
}

static void
psppire_value_entry_finalize (GObject *gobject)
{
  PsppireValueEntry *obj = PSPPIRE_VALUE_ENTRY (gobject);

  val_labs_destroy (obj->val_labs);
  g_free (obj->encoding);

  G_OBJECT_CLASS (psppire_value_entry_parent_class)->finalize (gobject);
}

GtkWidget *
psppire_value_entry_new (void)
{
  return GTK_WIDGET (g_object_new (PSPPIRE_TYPE_VALUE_ENTRY, "has-entry", TRUE, NULL));
}

static void
psppire_value_entry_refresh_model (PsppireValueEntry *obj)
{
  GtkTreeModel *model;
  GtkTreeModel *old_model;

  if (val_labs_count (obj->val_labs) > 0)
    {
      const struct val_lab **vls = val_labs_sorted (obj->val_labs);
      size_t n_vls = val_labs_count (obj->val_labs);

      GtkListStore *list_store;
      size_t i;

      list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
      model = GTK_TREE_MODEL (list_store);
      for (i = 0; i < n_vls; i++)
        {
          const struct val_lab *vl = vls[i];
          GtkTreeIter iter;

          gtk_list_store_append (list_store, &iter);
          gtk_list_store_set (list_store, &iter,
                              COL_LABEL, val_lab_get_label (vl),
                              COL_VALUE, val_lab_get_value (vl),
                              -1);
        }
      free (vls);
    }
  else
    model = NULL;

  old_model = gtk_combo_box_get_model (GTK_COMBO_BOX (obj));

  if (old_model != model)
    {
      GtkEntry *entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (obj)));
      gtk_entry_set_text (entry, "");
    }

  gtk_combo_box_set_model (GTK_COMBO_BOX (obj), model);
  if (model != NULL)
    g_object_unref (model);
}

void
psppire_value_entry_set_show_value_label (PsppireValueEntry *obj,
                                          gboolean show_value_label)
{
  if (obj->show_value_label != show_value_label)
    {
      obj->show_value_label = show_value_label;
      g_object_notify (G_OBJECT (obj), "show-value-label");
    }
}

gboolean
psppire_value_entry_get_show_value_label (const PsppireValueEntry *obj)
{
  return obj->show_value_label;
}

void
psppire_value_entry_set_variable (PsppireValueEntry *obj,
                                  const struct variable *var)
{
  if (var != NULL)
    {
      psppire_value_entry_set_value_labels (obj, var_get_value_labels (var));
      obj->format = *var_get_print_format (var);
      psppire_value_entry_set_encoding (obj, var_get_encoding (var));
    }
  else
    psppire_value_entry_set_value_labels (obj, NULL);
}

void
psppire_value_entry_set_value_labels (PsppireValueEntry *obj,
                                      const struct val_labs *val_labs)
{
  if (!val_labs_equal (obj->val_labs, val_labs))
    {
      obj->cur_value = NULL;

      val_labs_destroy (obj->val_labs);
      obj->val_labs = val_labs_clone (val_labs);

      if (val_labs != NULL)
        {
          int width = val_labs_get_width (val_labs);
          if (width != fmt_var_width (&obj->format))
            obj->format = fmt_default_for_width (width);
        }

      psppire_value_entry_refresh_model (obj);

      g_object_notify (G_OBJECT (obj), "value-labels");
    }
}

const struct val_labs *
psppire_value_entry_get_value_labels (const PsppireValueEntry *obj)
{
  return obj->val_labs;
}

void
psppire_value_entry_set_format (PsppireValueEntry *obj,
                                const struct fmt_spec *format)
{
  if (!fmt_equal (format, &obj->format))
    {
      obj->cur_value = NULL;
      obj->format = *format;

      if (obj->val_labs
          && val_labs_get_width (obj->val_labs) != fmt_var_width (format))
        psppire_value_entry_set_value_labels (obj, NULL);

      g_object_notify (G_OBJECT (obj), "format");
    }
}

const struct fmt_spec *
psppire_value_entry_get_format (const PsppireValueEntry *obj)
{
  return &obj->format;
}

void
psppire_value_entry_set_encoding (PsppireValueEntry *obj,
                                  const gchar *encoding)
{
  g_free (obj->encoding);
  obj->encoding = encoding != NULL ? g_strdup (encoding) : NULL;

  g_object_notify (G_OBJECT (obj), "encoding");
}

const gchar *
psppire_value_entry_get_encoding (const PsppireValueEntry *obj)
{
  return obj->encoding ? obj->encoding : UTF8;
}

void
psppire_value_entry_set_width (PsppireValueEntry *obj, int width)
{
  if (width != fmt_var_width (&obj->format))
    {
      struct fmt_spec format = fmt_default_for_width (width);
      psppire_value_entry_set_format (obj, &format);
    }
}

int
psppire_value_entry_get_width (const PsppireValueEntry *obj)
{
  return fmt_var_width (&obj->format);
}

void
psppire_value_entry_set_value (PsppireValueEntry *obj,
                               const union value *value,
                               int width)
{
  GtkEntry *entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (obj)));
  gchar *string;

  obj->cur_value = NULL;
  if (obj->show_value_label)
    {
      struct val_lab *vl = val_labs_lookup (obj->val_labs, value);
      if (vl != NULL)
        {
          gtk_entry_set_text (entry, val_lab_get_label (vl));
          obj->cur_value = val_lab_get_value (vl);
          return;
        }
    }

  string = value_to_text__ (*value, &obj->format, obj->encoding);
  gtk_entry_set_text (entry, string);
  g_free (string);
}

gboolean
psppire_value_entry_get_value (PsppireValueEntry *obj,
                               union value *value,
                               int width)
{
  GtkEntry *entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (obj)));
  GtkTreeIter iter;

  g_return_val_if_fail (fmt_var_width (&obj->format) == width, FALSE);

  if (obj->cur_value)
    {
      value_copy (value, obj->cur_value, width);
      return TRUE;
    }
  else if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (obj), &iter))
    {
      union value *v;

      gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (obj)), &iter,
                          COL_VALUE, &v,
                          -1);
      value_copy (value, v, width);
      return TRUE;
    }
  else
    {
      const gchar *new_text;

      new_text = gtk_entry_get_text (entry);
      return data_in_msg (ss_cstr (new_text), UTF8,
                          obj->format.type,
                          value, width, obj->encoding);
    }
}
