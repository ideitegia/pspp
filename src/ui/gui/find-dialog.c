/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2011, 2012  Free Software Foundation

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


/* This module implements the "Find" dialog; a dialog box to locate cases
which match particular strings */

#include <config.h>

#include <ctype.h>
#include <gtk/gtk.h>
#include <regex.h>
#include <stdlib.h>
#include <sys/types.h>

#include "data/data-in.h"
#include "data/datasheet.h"
#include "data/format.h"
#include "data/value.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/dict-display.h"
#include "ui/gui/find-dialog.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-data-sheet.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-selector.h"

#include "gl/xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct find_dialog
{
  GtkBuilder *xml;
  PsppireDict *dict;
  struct datasheet *data;
  PsppireDataWindow *de;
  GtkWidget *variable_entry;
  GtkWidget *value_entry;
  GtkWidget *value_labels_checkbox;
  GtkWidget *match_regexp_checkbox;
  GtkWidget *match_substring_checkbox;
};

static void
find_value (const struct find_dialog *fd, casenumber current_row,
	   casenumber *row, int *column);


/* A callback which occurs whenever the "Refresh" button is clicked,
   and when the dialog pops up.
   It restores the dialog to its default state.
*/
static void
refresh (GObject *obj, const struct find_dialog *fd)
{
  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (get_widget_assert (fd->xml, "find-wrap")),
     FALSE);

  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (get_widget_assert (fd->xml, "find-backwards")),
     FALSE);

  gtk_entry_set_text (GTK_ENTRY (fd->variable_entry), "");
  gtk_entry_set_text (GTK_ENTRY (fd->value_entry), "");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->match_regexp_checkbox),
				FALSE);

  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (fd->match_substring_checkbox), FALSE);


  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (fd->match_substring_checkbox), FALSE);
}

/* Callback on the "Find" button */
static void
do_find (GObject *obj, const struct find_dialog *fd)
{
  PsppireDataSheet *data_sheet;
  casenumber x = -1;
  gint column = -1;
  glong row;

  data_sheet = psppire_data_editor_get_active_data_sheet (fd->de->data_editor);
  row = psppire_data_sheet_get_selected_case (data_sheet);
  if ( row < 0 )
    row = 0;

  find_value (fd, row, &x, &column);


  if ( x != -1)
    {
      gtk_notebook_set_current_page (GTK_NOTEBOOK (fd->de->data_editor),
				     PSPPIRE_DATA_EDITOR_DATA_VIEW);

      psppire_data_sheet_goto_case (data_sheet, x);
      psppire_data_sheet_goto_variable (data_sheet, column);
    }

}

/* Callback on the selector.
   It gets invoked whenever a variable is selected */
static void
on_select (GtkEntry *entry, gpointer data)
{
  struct find_dialog *fd = data;
  const char *var_name = gtk_entry_get_text (GTK_ENTRY (fd->variable_entry));
  struct variable *var = dict_lookup_var (fd->dict->dict, var_name);
  gboolean search_labels ;

  g_return_if_fail (var);

  gtk_widget_set_sensitive (fd->value_labels_checkbox,
			    var_has_value_labels (var));

  search_labels =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->value_labels_checkbox));

  gtk_widget_set_sensitive (fd->match_regexp_checkbox,
			    var_is_alpha (var) || search_labels);


  gtk_widget_set_sensitive (fd->match_substring_checkbox,
			    var_is_alpha (var) || search_labels);
}

/* Callback on the selector.
   It gets invoked whenever a variable is unselected */
static void
on_deselect (GtkEntry *entry, gpointer data)
{
  struct find_dialog *fd = data;

  gtk_widget_set_sensitive (fd->value_labels_checkbox, FALSE);
  gtk_widget_set_sensitive (fd->match_substring_checkbox, FALSE);
  gtk_widget_set_sensitive (fd->match_regexp_checkbox, FALSE);
}

static void
value_labels_toggled (GtkToggleButton *tb, gpointer data)
{
  struct find_dialog *fd = data;

  const char *var_name = gtk_entry_get_text (GTK_ENTRY (fd->variable_entry));
  const struct variable *var = dict_lookup_var (fd->dict->dict, var_name);

  gboolean active = gtk_toggle_button_get_active  (tb) ;

  gtk_widget_set_sensitive (fd->match_substring_checkbox,
			    active || (var && var_is_alpha (var)));

  gtk_widget_set_sensitive (fd->match_regexp_checkbox,
			      active || (var && var_is_alpha (var)));
}

/* Pops up the Find dialog box
 */
void
find_dialog (PsppireDataWindow *de)
{
  struct find_dialog fd;

  GtkWidget *dialog ;
  GtkWidget *source ;
  GtkWidget *selector;
  GtkWidget *find_button;

  GtkWidget *buttonbox;

  PsppireDataStore *ds ;

  fd.xml = builder_new ("find.ui");
  fd.de = de;

  find_button = gtk_button_new_from_stock  (GTK_STOCK_FIND);
  gtk_widget_show (find_button);

  buttonbox = get_widget_assert (fd.xml, "find-buttonbox");

  psppire_box_pack_start_defaults (GTK_BOX (buttonbox), find_button);
  gtk_box_reorder_child (GTK_BOX (buttonbox), find_button, 0);

  dialog = get_widget_assert (fd.xml, "find-dialog");
  source = get_widget_assert (fd.xml, "find-variable-treeview");
  selector = get_widget_assert (fd.xml, "find-selector");

  g_object_get (de->data_editor,
		"dictionary", &fd.dict,
		"data-store", &ds,
		NULL);

  fd.data = ds->datasheet;

  fd.variable_entry        = get_widget_assert (fd.xml, "find-variable-entry");
  fd.value_entry           = get_widget_assert (fd.xml, "find-value-entry");
  fd.value_labels_checkbox =
    get_widget_assert (fd.xml,
		       "find-value-labels-checkbutton");

  fd.match_regexp_checkbox =
    get_widget_assert (fd.xml,
		       "find-match-regexp-checkbutton");

  fd.match_substring_checkbox =
    get_widget_assert (fd.xml,
		       "find-match-substring-checkbutton");



  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));


  g_object_set (source, "model", fd.dict,
	"selection-mode", GTK_SELECTION_SINGLE,
	NULL);


  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector),
				    is_currently_in_entry);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &fd);

  g_signal_connect (find_button, "clicked", G_CALLBACK (do_find),  &fd);

  g_signal_connect (selector, "selected",
		    G_CALLBACK (on_select),  &fd);

  g_signal_connect (selector, "de-selected",
		    G_CALLBACK (on_deselect),  &fd);

  g_signal_connect (fd.value_labels_checkbox, "toggled",
		    G_CALLBACK (value_labels_toggled),  &fd);


  psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  g_object_unref (fd.xml);
}


/* Iterators */

static void
forward (casenumber *i, struct datasheet *data UNUSED)
{
  ++*i;
}


static void
forward_wrap (casenumber *i, struct datasheet *data)
{
  if ( ++*i >=  datasheet_get_n_rows (data) ) *i = 0;
}

static void
backward (casenumber *i, struct datasheet *data UNUSED)
{
  --*i;
}


static void
backward_wrap (casenumber *i, struct datasheet *data)
{
  if ( --*i < 0 )
    *i = datasheet_get_n_rows (data) - 1;
}


/* Current plus one */
static casenumber
cp1 (casenumber current, struct datasheet *data)
{
  return current + 1;
}

/* Current plus one, circular */
static casenumber
cp1c (casenumber current, struct datasheet *data)
{
  casenumber next = current;

  forward_wrap (&next, data);

  return next;
}


/* Current minus one */
static casenumber
cm1 (casenumber current, struct datasheet *data)
{
  return current - 1;
}

/* Current minus one, circular */
static casenumber
cm1c (casenumber current, struct datasheet *data)
{
  casenumber next = current;

  backward_wrap (&next, data);

  return next;
}


static casenumber
last (casenumber current, struct datasheet *data)
{
  return datasheet_get_n_rows (data) ;
}

static casenumber
minus1 (casenumber current, struct datasheet *data)
{
  return -1;
}

/* An type to facilitate iterating through casenumbers */
struct casenum_iterator
{
  /* returns the first case to access */
  casenumber (*start) (casenumber, struct datasheet *);

  /* Returns one past the last case to access */
  casenumber (*end) (casenumber, struct datasheet *);

  /* Sets the first arg to the next case to access */
  void (*next) (casenumber *, struct datasheet *);
};

enum iteration_type{
  FORWARD = 0,
  FORWARD_WRAP,
  REVERSE,
  REVERSE_WRAP,
  n_iterators
};

static const struct casenum_iterator ip[n_iterators] =
  {
    {cp1, last, forward},
    {cp1c, cm1, forward_wrap},
    {cm1, minus1, backward},
    {cm1c, cp1, backward_wrap}
  };



/* A factory returning an iterator according to the dialog box's settings */
static const struct casenum_iterator *
get_iteration_params (const struct find_dialog *fd)
{
  gboolean wrap = gtk_toggle_button_get_active
    (GTK_TOGGLE_BUTTON (get_widget_assert (fd->xml, "find-wrap")));

  gboolean reverse = gtk_toggle_button_get_active
    (GTK_TOGGLE_BUTTON (get_widget_assert (fd->xml, "find-backwards")));

  if ( wrap )
    {
      if ( reverse )
	return &ip[REVERSE_WRAP];
      else
	return &ip[FORWARD_WRAP];
    }
  else
    {
      if ( reverse )
	return &ip[REVERSE];
      else
	return &ip[FORWARD];
    }
}


enum string_cmp_flags
  {
    STR_CMP_SUBSTR = 0x01, /* Find strings which are substrings of the
			      values */
    STR_CMP_REGEXP = 0x02, /* Match against a regular expression */

    STR_CMP_LABELS = 0x04  /* Match against the values' labels instead
			      of the data */
  };


/* An abstract base type for comparing union values against a reference */
struct comparator
{
  const struct variable *var;
  enum string_cmp_flags flags;

  bool (*compare) (const struct comparator *,
		   const union value *);

  void (*destroy) (struct comparator *);
};


/* A comparator which operates on the unadulterated union values */
struct value_comparator
{
  struct comparator parent;
  union value pattern;
};

/* A comparator which matches string values or parts thereof */
struct string_comparator
{
  struct comparator parent;
  const char *pattern;
};

/* A comparator to match string values against a POSIX.2 regular expression */
struct regexp_comparator
{
  struct comparator parent;
  regex_t re;
};


static bool
value_compare (const struct comparator *cmptr,
	       const union value *v)
{
  const struct value_comparator *vc = (const struct value_comparator *) cmptr;
  return 0 == value_compare_3way (v, &vc->pattern, var_get_width (cmptr->var));
}


/* Return true if the label of VAL matches the reference string*/
static bool
string_label_compare (const struct comparator *cmptr,
		const union value *val)
{
  const struct string_comparator *ssc =
    (const struct string_comparator *) cmptr;

  int width;

  const char *text = var_lookup_value_label (cmptr->var, val);
  if (text == NULL)
    return false;

  width = strlen (text);

  assert ( cmptr->flags & STR_CMP_LABELS);

  g_return_val_if_fail (width > 0, false);

  if ( cmptr->flags & STR_CMP_SUBSTR)
    return (NULL != g_strstr_len (text, width, ssc->pattern));
  else
    return (0 == strncmp (text, ssc->pattern, width));
}

/* Return true if VAL matches the reference string*/
static bool
string_value_compare (const struct comparator *cmptr,
		      const union value *val)
{
  bool found;
  char *text;
  const struct string_comparator *ssc =
    (const struct string_comparator *) cmptr;

  int width = var_get_width (cmptr->var);
  g_return_val_if_fail (width > 0, false);
  assert ( ! (cmptr->flags & STR_CMP_LABELS));

  text = value_to_text (*val, cmptr->var);

  if ( cmptr->flags & STR_CMP_SUBSTR)
    found =  (NULL != g_strstr_len (text, width, ssc->pattern));
  else
    found = (0 == strncmp (text, ssc->pattern, width));

  free (text);
  return found;
}



/* Return true if VAL matched the regexp */
static bool
regexp_value_compare (const struct comparator *cmptr,
		const union value *val)
{
  char *text;
  bool retval;
  const struct regexp_comparator *rec =
    (const struct regexp_comparator *) cmptr;

  int width = var_get_width (cmptr->var);

  assert  ( ! (cmptr->flags & STR_CMP_LABELS) );

  g_return_val_if_fail (width > 0, false);

  text = value_to_text (*val, cmptr->var);
  /* We must remove trailing whitespace, otherwise $ will not match where
     one would expect */
  g_strchomp (text);

  retval = (0 == regexec (&rec->re, text, 0, 0, 0));

  g_free (text);

  return retval;
}

/* Return true if the label of VAL matched the regexp */
static bool
regexp_label_compare (const struct comparator *cmptr,
		      const union value *val)
{
  const char *text;
  const struct regexp_comparator *rec =
    (const struct regexp_comparator *) cmptr;

  int width ;

  assert ( cmptr->flags & STR_CMP_LABELS);

  text = var_lookup_value_label (cmptr->var, val);
  width = strlen (text);

  g_return_val_if_fail (width > 0, false);

  return (0 == regexec (&rec->re, text, 0, 0, 0));
}



static void
regexp_destroy (struct comparator *cmptr)
{
  struct regexp_comparator *rec
    = UP_CAST (cmptr, struct regexp_comparator, parent);

  regfree (&rec->re);
}

static void
cmptr_value_destroy (struct comparator *cmptr)
{
  struct value_comparator *vc
    = UP_CAST (cmptr, struct value_comparator, parent);
  value_destroy (&vc->pattern, var_get_width (cmptr->var));
}


static struct comparator *
value_comparator_create (const struct variable *var, const char *target)
{
  struct value_comparator *vc = xzalloc (sizeof (*vc));
  struct comparator *cmptr = &vc->parent;

  cmptr->flags = 0;
  cmptr->var = var;
  cmptr->compare  = value_compare ;
  cmptr->destroy = cmptr_value_destroy;

  text_to_value (target, var, &vc->pattern);

  return cmptr;
}

static struct comparator *
string_comparator_create (const struct variable *var, const char *target,
			  enum string_cmp_flags flags)
{
  struct string_comparator *ssc = xzalloc (sizeof (*ssc));
  struct comparator *cmptr = &ssc->parent;

  cmptr->flags = flags;
  cmptr->var = var;

  if ( flags & STR_CMP_LABELS)
    cmptr->compare = string_label_compare;
  else
    cmptr->compare = string_value_compare;

  ssc->pattern = target;

  return cmptr;
}


static struct comparator *
regexp_comparator_create (const struct variable *var, const char *target,
			  enum string_cmp_flags flags)
{
  int code;
  struct regexp_comparator *rec = xzalloc (sizeof (*rec));
  struct comparator *cmptr = &rec->parent;

  cmptr->flags = flags;
  cmptr->var = var;
  cmptr->compare  = (flags & STR_CMP_LABELS)
    ? regexp_label_compare : regexp_value_compare ;

  cmptr->destroy  = regexp_destroy;

  code = regcomp (&rec->re, target, 0);
  if ( code != 0 )
    {
      char *errbuf = NULL;
      size_t errbuf_size = regerror (code, &rec->re, errbuf,  0);

      errbuf = xmalloc (errbuf_size);

      regerror (code, &rec->re, errbuf, errbuf_size);

      msg (ME, _("Bad regular expression: %s"), errbuf);

      free ( cmptr);
      free (errbuf);
      return NULL;
    }

  return cmptr;
}


/* Compare V against CMPTR's reference */
static bool
comparator_compare (const struct comparator *cmptr,
		    const union value *v)
{
  return cmptr->compare (cmptr, v);
}

/* Destroy CMPTR */
static void
comparator_destroy (struct comparator *cmptr)
{
  if ( ! cmptr )
    return ;

  if ( cmptr->destroy )
    cmptr->destroy (cmptr);

  free (cmptr);
}


static struct comparator *
comparator_factory (const struct variable *var, const char *str,
		    enum string_cmp_flags flags)
{
  if ( flags & STR_CMP_REGEXP )
    return regexp_comparator_create (var, str, flags);

  if ( flags & (STR_CMP_SUBSTR | STR_CMP_LABELS) )
    return string_comparator_create (var, str, flags);

  return value_comparator_create (var, str);
}


/* Find the row and column specified by the dialog FD, starting at CURRENT_ROW.
   After the function returns, *ROW contains the row and *COLUMN the column.
   If no such case is found, then *ROW will be set to -1
 */
static void
find_value (const struct find_dialog *fd, casenumber current_row,
	   casenumber *row, int *column)
{
  int width;
  const struct variable *var;
  const char *var_name = gtk_entry_get_text (GTK_ENTRY (fd->variable_entry));
  const char *target_string = gtk_entry_get_text (GTK_ENTRY (fd->value_entry));

  enum string_cmp_flags flags = 0;
  g_assert (current_row >= 0);

  var = dict_lookup_var (fd->dict->dict, var_name);
  if ( ! var )
    return ;

  width = var_get_width (var);

  *column = var_get_dict_index (var);
  *row = -1;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (fd->match_substring_checkbox)))
    flags |= STR_CMP_SUBSTR;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (fd->match_regexp_checkbox)))
    flags |= STR_CMP_REGEXP;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (fd->value_labels_checkbox)))
    flags |= STR_CMP_LABELS;

  {
    union value val;
    casenumber i;
    const struct casenum_iterator *ip = get_iteration_params (fd);
    struct comparator *cmptr =
      comparator_factory (var, target_string, flags);

    value_init (&val, width);
    if ( ! cmptr)
      goto finish;

    for (i = ip->start (current_row, fd->data);
	 i != ip->end (current_row, fd->data);
	 ip->next (&i, fd->data))
      {
	datasheet_get_value (fd->data, i, var_get_case_index (var), &val);

	if ( comparator_compare (cmptr, &val))
	  {
	    *row = i;
	    break;
	  }
      }

  finish:
    comparator_destroy (cmptr);
    value_destroy (&val, width);
  }
}
