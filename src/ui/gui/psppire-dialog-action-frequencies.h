/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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


#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

#ifndef __PSPPIRE_DIALOG_ACTION_FREQUENCIES_H__
#define __PSPPIRE_DIALOG_ACTION_FREQUENCIES_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES (psppire_dialog_action_frequencies_get_type ())

#define PSPPIRE_DIALOG_ACTION_FREQUENCIES(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES, \
                                                  PsppireDialogActionFrequencies))

#define PSPPIRE_DIALOG_ACTION_FREQUENCIES_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES, \
                                 PsppireDialogActionFrequenciesClass))


#define PSPPIRE_IS_DIALOG_ACTION_FREQUENCIES(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES))

#define PSPPIRE_IS_DIALOG_ACTION_FREQUENCIES_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES))


#define PSPPIRE_DIALOG_ACTION_FREQUENCIES_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES, \
				   PsppireDialogActionFrequenciesClass))

typedef struct _PsppireDialogActionFrequencies       PsppireDialogActionFrequencies;
typedef struct _PsppireDialogActionFrequenciesClass  PsppireDialogActionFrequenciesClass;


enum frq_scale
  {
    FRQ_FREQ,
    FRQ_PERCENT
  };

enum frq_order
  {
    FRQ_AVALUE,
    FRQ_DVALUE,
    FRQ_ACOUNT,
    FRQ_DCOUNT
  };

enum frq_table
  {
    FRQ_TABLE,
    FRQ_NOTABLE,
    FRQ_LIMIT
  };


struct _PsppireDialogActionFrequencies
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *stat_vars;
  GtkTreeModel *stats;

  GtkWidget *include_missing;

  enum frq_order tables_opts_order;
  enum frq_table tables_opts_table;
  gint tables_opts_limit;

  GtkWidget * always;
  GtkWidget * never;
  GtkWidget * limit;
  GtkWidget * limit_spinbutton;

  GtkWidget * avalue;
  GtkWidget * dvalue;
  GtkWidget * afreq;
  GtkWidget * dfreq;  

  GtkWidget *tables_dialog;

  /* Charts dialog */

  GtkWidget *min;
  GtkWidget *min_spin;
  GtkWidget *max;
  GtkWidget *max_spin;

  GtkWidget *hist;
  GtkWidget *normal;

  gboolean charts_opts_use_min;
  gdouble charts_opts_min;

  gboolean charts_opts_use_max;
  gdouble charts_opts_max;

  gboolean charts_opts_draw_hist;
  gboolean charts_opts_draw_normal;

  gboolean charts_opts_draw_pie;
  gboolean charts_opts_pie_include_missing;


  enum frq_scale charts_opts_scale;

  GtkWidget *freqs;
  GtkWidget *percents;
  GtkWidget *pie;
  GtkWidget *pie_include_missing;

  GtkWidget *charts_dialog;
};


struct _PsppireDialogActionFrequenciesClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_frequencies_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_FREQUENCIES_H__ */
