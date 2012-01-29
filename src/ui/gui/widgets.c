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
#include "psppire-dialog-action-kmeans.h"
#include "psppire-dialog-action-roc.h"
#include "psppire-dialog-action-var-info.h"


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

  psppire_dialog_action_correlation_get_type ();
  psppire_dialog_action_descriptives_get_type ();
  psppire_dialog_action_kmeans_get_type ();
  psppire_dialog_action_var_info_get_type ();
  psppire_dialog_action_roc_get_type ();
}
