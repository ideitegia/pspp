#include "widgets.h"

#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "psppire-vbuttonbox.h"
#include "psppire-hbuttonbox.h"
#include "psppire-keypad.h"
#include "psppire-acr.h"


/* Any custom widgets which are to be used in GtkBuilder ui files
   need to be preregistered, otherwise GtkBuilder refuses to 
   acknowledge their existence. */
void
preregister_widgets (void)
{
  psppire_dialog_get_type ();
  psppire_selector_get_type ();
  psppire_vbutton_box_get_type ();
  psppire_hbutton_box_get_type ();
  psppire_keypad_get_type ();
  psppire_acr_get_type ();
}
