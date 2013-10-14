/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010  Free Software Foundation

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

#include <gtk/gtk.h>

#ifndef __PSPPIRE_CONF_H__
#define __PSPPIRE_CONF_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_CONF (psppire_conf_get_type ())

#define PSPPIRE_CONF(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
		    PSPPIRE_TYPE_CONF, PsppireConf))

#define PSPPIRE_CONF_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_CONF, \
                                 PsppireConfClass))


#define PSPPIRE_IS_CONF(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_CONF))

#define PSPPIRE_IS_CONF_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_CONF))


#define PSPPIRE_CONF_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_CONF, \
				   PsppireConfClass))

typedef struct _PsppireConf       PsppireConf;
typedef struct _PsppireConfClass  PsppireConfClass;


struct _PsppireConf
{
  GObject parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GKeyFile *keyfile;
  gchar *filename;
  guint idle;
};


struct _PsppireConfClass
{
  GObjectClass parent_class;
};


GType psppire_conf_get_type (void) G_GNUC_CONST;

PsppireConf * psppire_conf_new (void);

gboolean psppire_conf_get_int (PsppireConf *,
			       const gchar *, const gchar *, int *);

gboolean psppire_conf_get_string (PsppireConf *,
			       const gchar *, const gchar *, gchar **);

gboolean psppire_conf_get_boolean (PsppireConf *,
				   const gchar *, const gchar *, gboolean *);

void psppire_conf_set_int (PsppireConf *conf,
			   const gchar *base, const gchar *name,
			   gint value);

void psppire_conf_set_boolean (PsppireConf *conf,
			       const gchar *base, const gchar *name,
			       gboolean value);

void psppire_conf_set_string (PsppireConf *conf,
			       const gchar *base, const gchar *name,
			      const gchar *value);

void psppire_conf_set_window_geometry (PsppireConf *conf,
				       const gchar *base,
				       GtkWindow *window);

void psppire_conf_save_window_geometry (PsppireConf *,
					const gchar *,
					GtkWindow *);


G_END_DECLS

#endif /* __PSPPIRE_CONF_H__ */
