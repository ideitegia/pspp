/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012  Free Software Foundation

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

#include "psppire-dialog-action-indep-samps.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "t-test-options.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "psppire-dict.h"
#include "libpspp/str.h"

static void
psppire_dialog_action_indep_samps_class_init (PsppireDialogActionIndepSampsClass *class);

G_DEFINE_TYPE (PsppireDialogActionIndepSamps, psppire_dialog_action_indep_samps, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionIndepSamps *act = PSPPIRE_DIALOG_ACTION_INDEP_SAMPS (data);

  GtkTreeModel *vars = gtk_tree_view_get_model (GTK_TREE_VIEW (act->test_vars_tv));

  GtkTreeIter notused;

  if (NULL == act->grp_var)
    return FALSE;

  if ( 0 == gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  if ( act->group_defn == GROUPS_UNDEF)
    return FALSE;

  return TRUE;
}


static void
refresh (PsppireDialogAction *da)
{
  PsppireDialogActionIndepSamps *act = PSPPIRE_DIALOG_ACTION_INDEP_SAMPS (da);

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (act->test_vars_tv));

  act->group_defn = GROUPS_UNDEF;

  if (act->grp_var)
    {
      int width = var_get_width (act->grp_var);
      value_destroy (&act->cut_point, width);
      value_destroy (&act->grp_val[0], width);
      value_destroy (&act->grp_val[1], width);
      act->grp_var = NULL;
    }

  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[0]), NULL);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[1]), NULL);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_cut_point_entry), NULL);

  gtk_entry_set_text (GTK_ENTRY (act->group_var_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));

  gtk_widget_set_sensitive (act->define_groups_button, FALSE);
}

/* Return TRUE if VE contains a text which is not valid for VAR or if it
   contains the SYSMIS value */
static gboolean
value_entry_contains_invalid (PsppireValueEntry *ve, const struct variable *var)
{
  gboolean result = FALSE;

  if (var) 
    {
      union value val;
      const int width = var_get_width (var);
      value_init (&val, width);

      if ( psppire_value_entry_get_value (ve, &val, width))
	{
	  if (var_is_value_missing (var, &val, MV_SYSTEM))
	    {
	      result = TRUE;
	    }
	}
      else
	result = TRUE;

      value_destroy (&val, width);
    }

  return result;
}

/* Returns TRUE iff the define groups subdialog has a
   state which defines a valid group criterion */
static gboolean
define_groups_state_valid (gpointer data)
{
  PsppireDialogActionIndepSamps *act = data;

  if (gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON (act->dg_values_toggle_button)))
    {
      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[0]),
					act->grp_var))
        return FALSE;

      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[1]),
					act->grp_var))
        return FALSE;
    }
  else
    {
      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (act->dg_cut_point_entry),
					act->grp_var))
        return FALSE;
    }

  return TRUE;
}


static void
run_define_groups (PsppireDialogActionIndepSamps *act)
{
  gint response;
  PsppireDialogAction *da = PSPPIRE_DIALOG_ACTION (act);
  GtkWidget *parent1 = gtk_widget_get_parent (act->dg_table1);
  GtkWidget *parent2 = gtk_widget_get_parent (act->dg_table2);

  if (parent1)
    gtk_container_remove (GTK_CONTAINER (parent1), act->dg_table1);

  if (parent2)
    gtk_container_remove (GTK_CONTAINER (parent2), act->dg_table2);

  if ( var_is_numeric (act->grp_var))
    {
      gtk_table_attach_defaults (GTK_TABLE (act->dg_table1), act->dg_table2,
  				 1, 2, 1, 2);

      gtk_container_add (GTK_CONTAINER (act->dg_box), act->dg_table1);
    }
  else
    {
      gtk_container_add (GTK_CONTAINER (act->dg_box), act->dg_table2);
      act->group_defn = GROUPS_VALUES;
    }


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (act->dg_dialog),
  				      define_groups_state_valid, act);

  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[0]), act->grp_var);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[1]), act->grp_var);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (act->dg_cut_point_entry), act->grp_var);

  if ( act->group_defn != GROUPS_CUT_POINT )
    {
      gtk_toggle_button_set_active
  	(GTK_TOGGLE_BUTTON (act->dg_cut_point_toggle_button), TRUE);

      gtk_toggle_button_set_active
  	(GTK_TOGGLE_BUTTON (act->dg_values_toggle_button), TRUE);
    }
  else
    {
      gtk_toggle_button_set_active
  	(GTK_TOGGLE_BUTTON (act->dg_values_toggle_button), TRUE);

      gtk_toggle_button_set_active
  	(GTK_TOGGLE_BUTTON (act->dg_cut_point_toggle_button), TRUE);
    }

  g_signal_emit_by_name (act->dg_grp_entry[0], "changed");
  g_signal_emit_by_name (act->dg_grp_entry[1], "changed");
  g_signal_emit_by_name (act->dg_cut_point_entry, "changed");

  response = psppire_dialog_run (PSPPIRE_DIALOG (act->def_grps_dialog));

  if (response == PSPPIRE_RESPONSE_CONTINUE)
    {
      const int width = var_get_width (act->grp_var);

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->dg_values_toggle_button)))
	{
	  act->group_defn = GROUPS_VALUES;

          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[0]),
					 &act->grp_val[0], width);

          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (act->dg_grp_entry[1]),
					 &act->grp_val[1], width);
	}
      else
	{
	  act->group_defn = GROUPS_CUT_POINT;

          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (act->dg_cut_point_entry),
					 &act->cut_point, width);
	}

      psppire_dialog_notify_change (PSPPIRE_DIALOG (da->dialog));
    }
}

/* Called whenever the group variable entry widget's contents change */
static void
on_grp_var_change (GtkEntry *entry, PsppireDialogActionIndepSamps *act)
{
  PsppireDialogAction *da = PSPPIRE_DIALOG_ACTION (act);
  const gchar *text = gtk_entry_get_text (entry);

  const struct variable *v = psppire_dict_lookup_var (da->dict, text);

  gtk_widget_set_sensitive (act->define_groups_button, v != NULL);

  if (act->grp_var)
    {
      int width = var_get_width (act->grp_var);
      value_destroy (&act->cut_point, width);
      value_destroy (&act->grp_val[0], width);
      value_destroy (&act->grp_val[1], width);
    }

  if (v)
    {
      const int width = var_get_width (v);
      value_init (&act->cut_point, width);
      value_init (&act->grp_val[0], width);
      value_init (&act->grp_val[1], width);

      if (width == 0)
        {
          act->cut_point.f  = SYSMIS;
          act->grp_val[0].f = SYSMIS;
          act->grp_val[1].f = SYSMIS;
        }
      else
        {
          act->cut_point.short_string[0] = '\0';
          act->grp_val[0].short_string[0] = '\0';
          act->grp_val[1].short_string[0] = '\0';
        }
    }

  act->grp_var = v;
}

static void
set_group_criterion_type (GtkToggleButton *button,
			  PsppireDialogActionIndepSamps *act)
{
  gboolean by_values = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (act->dg_label, by_values);
  gtk_widget_set_sensitive (act->dg_table2, by_values);

  gtk_widget_set_sensitive (act->dg_hbox1, !by_values);
}


static void
psppire_dialog_action_indep_samps_activate (GtkAction *a)
{
  PsppireDialogActionIndepSamps *act = PSPPIRE_DIALOG_ACTION_INDEP_SAMPS (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("indep-samples.ui");

  pda->dialog = get_widget_assert (xml,"independent-samples-dialog"); 
  pda->source = get_widget_assert (xml, "indep-samples-treeview1");
  act->define_groups_button = get_widget_assert (xml, "define-groups-button");
  act->options_button = get_widget_assert (xml, "indep-samples-options-button");

  act->def_grps_dialog = get_widget_assert (xml, "define-groups-dialog");
  act->group_var_entry = get_widget_assert (xml, "indep-samples-entry");
  act->test_vars_tv = get_widget_assert (xml, "indep-samples-treeview2");

  act->dg_dialog = get_widget_assert (xml, "define-groups-dialog");
  act->dg_grp_entry[0] = get_widget_assert (xml, "group1-entry");
  act->dg_grp_entry[1] = get_widget_assert (xml, "group2-entry");
  act->dg_cut_point_entry = get_widget_assert (xml, "cut-point-entry");
  act->dg_box = get_widget_assert (xml, "dialog-hbox2");

  act->dg_table1 = get_widget_assert (xml, "table1");
  act->dg_table2 = get_widget_assert (xml, "table2");
  act->dg_label  = get_widget_assert (xml, "label4");
  act->dg_hbox1  = get_widget_assert (xml, "hbox1");
  act->dg_values_toggle_button = get_widget_assert (xml, "radiobutton3");
  act->dg_cut_point_toggle_button = get_widget_assert (xml, "radiobutton4");

  act->opts = tt_options_dialog_create (GTK_WINDOW (pda->toplevel));

  g_object_ref (act->dg_table1);
  g_object_ref (act->dg_table2);

  g_signal_connect (act->dg_values_toggle_button, "toggled",
		    G_CALLBACK (set_group_criterion_type), act);


  g_object_unref (xml);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

  g_signal_connect_swapped (act->define_groups_button, "clicked",
			    G_CALLBACK (run_define_groups), act);

  g_signal_connect_swapped (act->options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), act->opts);


  g_signal_connect (act->group_var_entry, "changed",
		    G_CALLBACK (on_grp_var_change), act);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_indep_samps_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_indep_samps_parent_class)->activate (pda);
}



static char *
generate_syntax (PsppireDialogAction *a)
{
  PsppireDialogActionIndepSamps *act = PSPPIRE_DIALOG_ACTION_INDEP_SAMPS (a);
  gchar *text;

  GString *str = g_string_new ("T-TEST /VARIABLES=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (act->test_vars_tv), 0, str);

  g_string_append (str, "\n\t/GROUPS=");

  g_string_append (str, var_get_name (act->grp_var));

  if (act->group_defn != GROUPS_UNDEF)
    {
      g_string_append (str, "(");

      {
        const union value *val = 
          (act->group_defn == GROUPS_VALUES) ?
          &act->grp_val[0] :
          &act->cut_point;

        struct string strx;        
        ds_init_empty (&strx);
        syntax_gen_value (&strx, val, var_get_width (act->grp_var),
                          var_get_print_format (act->grp_var));
      
        g_string_append (str, ds_cstr (&strx));
        ds_destroy (&strx);
      }

      if (act->group_defn == GROUPS_VALUES)
	{
	  g_string_append (str, ",");

          {
            struct string strx;
            ds_init_empty (&strx);
            
            syntax_gen_value (&strx, &act->grp_val[1], var_get_width (act->grp_var),
                              var_get_print_format (act->grp_var));
            
            g_string_append (str, ds_cstr (&strx));
            ds_destroy (&strx);
          }
	}

      g_string_append (str, ")");
    }

  tt_options_dialog_append_syntax (act->opts, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

static void
psppire_dialog_action_indep_samps_class_init (PsppireDialogActionIndepSampsClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_indep_samps_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_indep_samps_init (PsppireDialogActionIndepSamps *act)
{
  act->grp_var = NULL;
  act->group_defn = GROUPS_UNDEF;
}

