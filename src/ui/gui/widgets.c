#include <config.h>

#include "widgets.h"


#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "psppire-vbuttonbox.h"
#include "psppire-hbuttonbox.h"
#include "psppire-keypad.h"
#include "psppire-acr.h"
#include "psppire-dictview.h"
#include "psppire-var-view.h"
#include "psppire-val-chooser.h"

#include "psppire-dialog-action-correlation.h"
#include "psppire-dialog-action-descriptives.h"
#include "psppire-dialog-action-examine.h"
#include "psppire-dialog-action-factor.h"
#include "psppire-dialog-action-indep-samps.h"
#include "psppire-dialog-action-kmeans.h"
#include "psppire-dialog-action-means.h"
#include "psppire-means-layer.h"
#include "psppire-dialog-action-rank.h"
#include "psppire-dialog-action-regression.h"
#include "psppire-dialog-action-reliability.h"
#include "psppire-dialog-action-roc.h"
#include "psppire-dialog-action-sort.h"
#include "psppire-dialog-action-var-info.h"
#include "psppire-value-entry.h"


/* Any custom widgets which are to be used in GtkBuilder ui files
   need to be preregistered, otherwise GtkBuilder refuses to 
   acknowledge their existence. */
void
preregister_widgets (void)
{
  psppire_val_chooser_get_type ();
  psppire_dialog_get_type ();
  psppire_selector_get_type ();
  psppire_vbutton_box_get_type ();
  psppire_hbutton_box_get_type ();
  psppire_keypad_get_type ();
  psppire_acr_get_type ();
  psppire_dict_view_get_type ();
  psppire_var_view_get_type ();
  psppire_value_entry_get_type ();

  psppire_dialog_action_correlation_get_type ();
  psppire_dialog_action_descriptives_get_type ();
  psppire_dialog_action_examine_get_type ();
  psppire_dialog_action_factor_get_type ();
  psppire_dialog_action_kmeans_get_type ();
  psppire_dialog_action_means_get_type ();
  psppire_dialog_action_indep_samps_get_type ();
  psppire_means_layer_get_type ();
  psppire_dialog_action_var_info_get_type ();
  psppire_dialog_action_rank_get_type ();
  psppire_dialog_action_reliability_get_type ();
  psppire_dialog_action_regression_get_type ();
  psppire_dialog_action_roc_get_type ();
  psppire_dialog_action_sort_get_type ();
}
