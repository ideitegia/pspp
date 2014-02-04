/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2014  Free Software Foundation

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

#include <float.h>
#include <gtk/gtk.h>
#include "dialog-common.h"
#include "psppire-val-chooser.h"

#include "libpspp/str.h"


#include "ui/syntax-gen.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_val_chooser_base_finalize (PsppireValChooserClass *, gpointer);
static void psppire_val_chooser_base_init     (PsppireValChooserClass *class);
static void psppire_val_chooser_class_init    (PsppireValChooserClass *class);
static void psppire_val_chooser_init          (PsppireValChooser      *vc);

static void psppire_val_chooser_realize       (GtkWidget *w);



GType
psppire_val_chooser_get_type (void)
{
  static GType psppire_val_chooser_type = 0;

  if (!psppire_val_chooser_type)
    {
      static const GTypeInfo psppire_val_chooser_info =
      {
	sizeof (PsppireValChooserClass),
	(GBaseInitFunc) psppire_val_chooser_base_init,
        (GBaseFinalizeFunc) psppire_val_chooser_base_finalize,
	(GClassInitFunc)psppire_val_chooser_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireValChooser),
	0,
	(GInstanceInitFunc) psppire_val_chooser_init,
      };

      psppire_val_chooser_type =
	g_type_register_static (GTK_TYPE_FRAME, "PsppireValChooser",
				&psppire_val_chooser_info, 0);
    }

  return psppire_val_chooser_type;
}


static void
psppire_val_chooser_finalize (GObject *object)
{

}

/* Properties */
enum
{
  PROP_0,
  PROP_IS_STRING,
  PROP_SHOW_ELSE
};


enum 
  {
    VC_VALUE,
    VC_SYSMIS,
    VC_MISSING,
    VC_RANGE,
    VC_LOW_UP,
    VC_HIGH_DOWN,
    VC_ELSE
  };

static void
psppire_val_chooser_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireValChooser *vr = PSPPIRE_VAL_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_SHOW_ELSE:
      {
	gboolean x = g_value_get_boolean (value);
	gtk_widget_set_visible (GTK_WIDGET (vr->rw[VC_ELSE].rb), x);
	gtk_widget_set_visible (GTK_WIDGET (vr->rw[VC_ELSE].label), x);
      }
      break;
    case PROP_IS_STRING:
      vr->input_var_is_string = g_value_get_boolean (value);
      gtk_widget_set_sensitive (GTK_WIDGET (vr->rw[VC_SYSMIS].rb), !vr->input_var_is_string);
      gtk_widget_set_sensitive (GTK_WIDGET (vr->rw[VC_MISSING].rb), !vr->input_var_is_string);
      gtk_widget_set_sensitive (GTK_WIDGET (vr->rw[VC_RANGE].rb), !vr->input_var_is_string);
      gtk_widget_set_sensitive (GTK_WIDGET (vr->rw[VC_LOW_UP].rb), !vr->input_var_is_string);      
      gtk_widget_set_sensitive (GTK_WIDGET (vr->rw[VC_HIGH_DOWN].rb), !vr->input_var_is_string);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_val_chooser_get_property (GObject         *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireValChooser *vr = PSPPIRE_VAL_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_SHOW_ELSE:
      {
	gboolean x =
	  gtk_widget_get_visible (GTK_WIDGET (vr->rw[VC_ELSE].rb));
	g_value_set_boolean (value, x);
      }
      break;
    case PROP_IS_STRING:
      g_value_set_boolean (value, vr->input_var_is_string);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static GObjectClass * parent_class = NULL;

static void
psppire_val_chooser_class_init (PsppireValChooserClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  GParamSpec *is_string_spec =
    g_param_spec_boolean ("is-string",
			  "String Value",
			  "Should the value range be a string value",
			  FALSE,
			  G_PARAM_READWRITE);

  GParamSpec *show_else_spec =
    g_param_spec_boolean ("show-else",
			  "Show Else",
			  "Should the \"All other values\" item be visible",
			  TRUE,
			  G_PARAM_READWRITE);


  parent_class = g_type_class_peek_parent (class);

  object_class->set_property = psppire_val_chooser_set_property;
  object_class->get_property = psppire_val_chooser_get_property;

  widget_class->realize = psppire_val_chooser_realize;

  g_object_class_install_property (object_class,
                                   PROP_IS_STRING,
                                   is_string_spec);

  g_object_class_install_property (object_class,
                                   PROP_SHOW_ELSE,
                                   show_else_spec);
}


static void
psppire_val_chooser_base_init (PsppireValChooserClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_val_chooser_finalize;
}



static void
psppire_val_chooser_base_finalize (PsppireValChooserClass *class,
				 gpointer class_data)
{

}


/* Set the focus of B to follow the sensitivity of A */
static void
focus_follows_sensitivity (GtkWidget *a, GParamSpec *pspec, GtkWidget *b)
{
  gboolean sens = gtk_widget_get_sensitive (a);

  g_object_set (b, "has-focus", sens, NULL);
}


struct layout;
typedef GtkWidget *filler_f (struct layout *, struct range_widgets *);
typedef void set_f (PsppireValChooser *, struct old_value *, const struct range_widgets *);

struct layout
{
  const gchar *label;
  filler_f *fill;
  set_f *set;
};



static void simple_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *rw)
{
  const gchar *text = gtk_entry_get_text (rw->e1);

  if ( vr->input_var_is_string)
    {
      ov->type = OV_STRING;
      ov->v.s = g_strdup (text);
    }
  else
    {
      ov->type = OV_NUMERIC;
      ov->v.v = g_strtod (text, 0);
    }
}

static void lo_up_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets  *rw)
{
  const gchar *text = gtk_entry_get_text (rw->e1);
  
  ov->type = OV_LOW_UP;
  ov->v.range[1] = g_strtod (text, 0);
}


static void hi_down_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *rw)
{
  const gchar *text = gtk_entry_get_text (rw->e1);
  
  ov->type = OV_HIGH_DOWN;
  ov->v.range[0] = g_strtod (text, 0);
}

static void missing_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *l)
{
  ov->type = OV_MISSING;
}


static void sysmis_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *l)
{
  ov->type = OV_SYSMIS;
}

static void else_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *l)
{
  ov->type = OV_ELSE;
}


static void range_set (PsppireValChooser *vr, struct old_value *ov, const struct range_widgets *rw)
{
  const gchar *text = gtk_entry_get_text (rw->e1);

  ov->type = OV_RANGE;
  ov->v.range[0] = g_strtod (text, 0);
  
  text = gtk_entry_get_text (rw->e2);
  ov->v.range[1] = g_strtod (text, 0);
}

static GtkWidget * range_entry (struct layout *l, struct range_widgets *rw)
{
  GtkWidget *vbox = gtk_vbox_new (3, FALSE);
  GtkWidget *entrylo = gtk_entry_new ();
  GtkWidget *label = gtk_label_new (_("through"));
  GtkWidget *entryhi = gtk_entry_new ();

  rw->e1 = GTK_ENTRY (entrylo);
  rw->e2 = GTK_ENTRY (entryhi);

  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  g_signal_connect (vbox, "notify::sensitive", G_CALLBACK (focus_follows_sensitivity), entrylo);

  gtk_box_pack_start (GTK_BOX (vbox), entrylo, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), entryhi, TRUE, TRUE, 0);
  return vbox;
}

static GtkWidget * simple_entry (struct layout *l, struct range_widgets *rw)
{
  GtkWidget *entry = gtk_entry_new ();

  rw->e1 = GTK_ENTRY (entry);

  g_signal_connect (entry, "notify::sensitive", G_CALLBACK (focus_follows_sensitivity), entry);
  return entry;
}


static struct layout range_opt[n_VAL_CHOOSER_BUTTONS]= 
  {
    {N_("_Value:"),                    simple_entry, simple_set },
    {N_("_System Missing"),            NULL,         sysmis_set },
    {N_("System _or User Missing"),    NULL,         missing_set},
    {N_("_Range:"),                    range_entry,  range_set  },
    {N_("Range, _LOWEST thru value"),  simple_entry, lo_up_set  },
    {N_("Range, value thru _HIGHEST"), simple_entry, hi_down_set},
    {N_("_All other values"),          NULL,         else_set   }
  };

static void
psppire_val_chooser_init (PsppireValChooser *vr)
{
  gint i;
  GtkWidget *aln = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  GtkWidget *table = gtk_table_new (11, 2, FALSE);
  GSList *group = NULL;
  gint row = 0;

  gtk_alignment_set_padding (GTK_ALIGNMENT (aln), 0, 0, 5, 5);

  vr->input_var_is_string = FALSE;

  for (i = 0; i < n_VAL_CHOOSER_BUTTONS; ++i)
    {
      struct layout *l = &range_opt[i];
      vr->rw[i].label = GTK_LABEL (gtk_label_new (gettext (l->label)));
      gtk_label_set_use_underline (vr->rw[i].label, TRUE);
      vr->rw[i].rb = GTK_TOGGLE_BUTTON (gtk_radio_button_new (group));
      gtk_label_set_mnemonic_widget (vr->rw[i].label, GTK_WIDGET (vr->rw[i].rb));

      gtk_misc_set_alignment (GTK_MISC (vr->rw[i].label), 0, 0.5);

      group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (vr->rw[i].rb));

      /* Attach the buttons */
      gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (vr->rw[i].rb),
			0, 1,	row, row + 1,
			0, GTK_EXPAND | GTK_FILL,
			0, 0);

      /* Attach the labels */
      gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (vr->rw[i].label),
			1, 2,   row, row + 1,
			GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL,
			0, 0);
      ++row;

      if (l->fill)
	{
	  GtkWidget *fill = l->fill (l, &vr->rw[i]);

	  gtk_widget_set_sensitive (fill, FALSE);

	  gtk_table_attach_defaults (GTK_TABLE (table), fill, 1, 2,
				 row, row + 1);
	  ++row;

      	  g_signal_connect (vr->rw[i].rb, "toggled", G_CALLBACK (set_sensitivity_from_toggle), fill);
	}
    }

  gtk_frame_set_shadow_type (GTK_FRAME (vr), GTK_SHADOW_ETCHED_IN);

  gtk_container_add (GTK_CONTAINER (aln), table);
  gtk_container_add (GTK_CONTAINER (vr), aln);

  gtk_widget_show_all (aln);
}


GtkWidget*
psppire_val_chooser_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_val_chooser_get_type (), NULL));
}



static void
psppire_val_chooser_realize (GtkWidget *w)
{
  PsppireValChooser *vr = PSPPIRE_VAL_CHOOSER (w);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(vr->rw[0].rb), TRUE);
  gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (vr->rw[0].rb));

  /* Chain up to the parent class */
  GTK_WIDGET_CLASS (parent_class)->realize (w);
}




/* A boxed type representing a value, or a range of values which may
   potentially be replaced by something */


static struct old_value *
old_value_copy (struct old_value *ov)
{
  struct old_value *copy = g_memdup (ov, sizeof (*copy));

  if ( ov->type == OV_STRING )
    copy->v.s = g_strdup (ov->v.s);

  return copy;
}


static void
old_value_free (struct old_value *ov)
{
  if (ov->type == OV_STRING)
    g_free (ov->v.s);
  g_free (ov);
}

static void
old_value_to_string (const GValue *src, GValue *dest)
{
  const struct old_value *ov = g_value_get_boxed (src);

  switch (ov->type)
    {
    case OV_NUMERIC:
      {
	gchar *text = g_strdup_printf ("%.*g", DBL_DIG + 1, ov->v.v);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_STRING:
      g_value_set_string (dest, ov->v.s);
      break;
    case OV_MISSING:
      g_value_set_string (dest, "MISSING");
      break;
    case OV_SYSMIS:
      g_value_set_string (dest, "SYSMIS");
      break;
    case OV_ELSE:
      g_value_set_string (dest, "ELSE");
      break;
    case OV_RANGE:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("%.*g %s %.*g",
                                DBL_DIG + 1, ov->v.range[0],
                                en_dash,
                                DBL_DIG + 1, ov->v.range[1]);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_LOW_UP:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("LOWEST %s %.*g",
				en_dash,
				DBL_DIG + 1, ov->v.range[1]);

	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_HIGH_DOWN:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("%.*g %s HIGHEST",
				DBL_DIG + 1, ov->v.range[0],
				en_dash);

	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    default:
      g_warning ("Invalid type in old recode value");
      g_value_set_string (dest, "???");
      break;
    };
}

GType
old_value_get_type (void)
{
  static GType t = 0;

  if (t == 0 )
    {
      t = g_boxed_type_register_static  ("psppire-recode-old-values",
					 (GBoxedCopyFunc) old_value_copy,
					 (GBoxedFreeFunc) old_value_free);

      g_value_register_transform_func     (t, G_TYPE_STRING,
					   old_value_to_string);
    }

  return t;
}



/* Generate a syntax fragment for NV and append it to STR */
void
old_value_append_syntax (struct string *str, const struct old_value *ov)
{
  switch (ov->type)
    {
    case OV_NUMERIC:
      ds_put_c_format (str, "%.*g", DBL_DIG + 1, ov->v.v);
      break;
    case OV_STRING:
      {
	struct string ds = DS_EMPTY_INITIALIZER;
	syntax_gen_string (&ds, ss_cstr (ov->v.s));
	ds_put_cstr (str, ds_cstr (&ds));
	ds_destroy (&ds);
      }
      break;
    case OV_MISSING:
      ds_put_cstr (str, "MISSING");
      break;
    case OV_SYSMIS:
      ds_put_cstr (str, "SYSMIS");
      break;
    case OV_ELSE:
      ds_put_cstr (str, "ELSE");
      break;
    case OV_RANGE:
      ds_put_c_format (str, "%.*g THRU %.*g",
                       DBL_DIG + 1, ov->v.range[0],
                       DBL_DIG + 1, ov->v.range[1]);
      break;
    case OV_LOW_UP:
      ds_put_c_format (str, "LOWEST THRU %*gg",
                       DBL_DIG + 1, ov->v.range[1]);
      break;
    case OV_HIGH_DOWN:
      ds_put_c_format (str, "%.*g THRU HIGHEST",
                       DBL_DIG + 1, ov->v.range[0]);
      break;
    default:
      g_warning ("Invalid type in old recode value");
      ds_put_cstr (str, "???");
      break;
    };
}



/* Set OV according to the current state of VR */
void
psppire_val_chooser_get_status (PsppireValChooser *vr, struct old_value *ov)
{
  int i;

  for (i = 0; i < n_VAL_CHOOSER_BUTTONS; ++i)
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (vr->rw[i].rb)))
	{
	  break;
	}
    }

  range_opt[i].set (vr, ov, &vr->rw[i]);
}

/* This might need to be changed to something less naive.
   In particular, what happends with dates, etc?
 */
static gchar *
num_to_string (gdouble x)
{
  return g_strdup_printf ("%.*g", DBL_DIG + 1, x);
}


/* Set VR according to the value of OV */
void
psppire_val_chooser_set_status (PsppireValChooser *vr, const struct old_value *ov)
{
  gint i;
  if ( !ov )
    return;

  for (i = 0; i < n_VAL_CHOOSER_BUTTONS; ++i)
    {
      if (vr->rw[i].e1)
	gtk_entry_set_text (vr->rw[i].e1, "");

      if (vr->rw[i].e2)
	gtk_entry_set_text (vr->rw[i].e2, "");
    }

  switch (ov->type)
    {
    case OV_STRING:
      gtk_toggle_button_set_active (vr->rw[0].rb, TRUE);
      gtk_entry_set_text (vr->rw[0].e1, ov->v.s);
      break;
      
    case OV_NUMERIC:
      {
	gchar *str;
	gtk_toggle_button_set_active (vr->rw[0].rb, TRUE);
	
	str = num_to_string (ov->v.v);
	
	gtk_entry_set_text (vr->rw[0].e1, str);
	g_free (str);
      }
      break;

      case OV_SYSMIS:
	gtk_toggle_button_set_active (vr->rw[VC_SYSMIS].rb, TRUE);
	break;

      case OV_MISSING:
	gtk_toggle_button_set_active (vr->rw[VC_MISSING].rb, TRUE);
	break;

      case OV_RANGE:
	{
	  gchar *str = num_to_string (ov->v.range[0]);
	  gtk_toggle_button_set_active (vr->rw[VC_RANGE].rb, TRUE);
	  gtk_entry_set_text (vr->rw[VC_RANGE].e1, str);

	  g_free (str);

	  str = num_to_string (ov->v.range[1]);
	  gtk_entry_set_text (vr->rw[VC_RANGE].e2, str);
	  g_free (str);
	}
	break;

      case OV_LOW_UP:
	{
	  gchar *str = num_to_string (ov->v.range[1]);

	  gtk_toggle_button_set_active (vr->rw[VC_LOW_UP].rb, TRUE);

	  gtk_entry_set_text (vr->rw[VC_LOW_UP].e1, str);

	  g_free (str);
	}
	break;


      case OV_HIGH_DOWN:
	{
	  gchar *str = num_to_string (ov->v.range[0]);

	  gtk_toggle_button_set_active (vr->rw[VC_HIGH_DOWN].rb, TRUE);

	  gtk_entry_set_text (vr->rw[VC_HIGH_DOWN].e1, str);

	  g_free (str);
	}
	break;

      case OV_ELSE:
	gtk_toggle_button_set_active (vr->rw[VC_ELSE].rb, TRUE);
	break;

    default:
      g_warning ("Unknown old value type");
      break;
    };
}
