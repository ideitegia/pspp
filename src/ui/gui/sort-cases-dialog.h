/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */

#ifndef __SORT_CASES_DIALOG_H
#define __SORT_CASES_DIALOG_H


#include <gtk/gtk.h>
#include <glade/glade.h>
#include "psppire-dict.h"

struct sort_criteria;

struct sort_cases_dialog
{
  GtkWidget *window;
  GMainLoop *loop;

  GtkTreeView *dict_view;


  GtkTreeView *criteria_view;
  GtkTreeViewColumn *crit_col;
  GtkCellRenderer *crit_renderer;

  GtkListStore *criteria_list;

  struct sort_criteria *sc;

  GtkArrow *arrow;
  GtkButton *button;

  GtkToggleButton *ascending_button;

  /* FIXME: Could this be done better with a GtkToggleAction ?? */
  enum {VAR_SELECT, VAR_DESELECT} button_state;

  gint response;
};

struct sort_cases_dialog * sort_cases_dialog_create(GladeXML *xml);


gint sort_cases_dialog_run(struct sort_cases_dialog *dialog,
			   PsppireDict *dict,
			   struct sort_criteria *criteria
			   );

#endif
