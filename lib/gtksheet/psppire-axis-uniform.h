/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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


#ifndef PSPPIRE_AXIS_UNIFORM_H__
#define PSPPIRE_AXIS_UNIFORM_H__


#include <glib-object.h>
#include <glib.h>

#include "psppire-axis.h"

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_AXIS_UNIFORM              (psppire_axis_uniform_get_type ())
#define PSPPIRE_AXIS_UNIFORM(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_AXIS_UNIFORM, PsppireAxisUniform))
#define PSPPIRE_AXIS_UNIFORM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_AXIS_UNIFORM, PsppireAxisUniformClass))
#define PSPPIRE_IS_AXIS_UNIFORM(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_AXIS_UNIFORM))
#define PSPPIRE_IS_AXIS_UNIFORM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_AXIS_UNIFORM))
#define PSPPIRE_AXIS_UNIFORM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_AXIS_UNIFORM, PsppireAxisUniformClass))



/* --- typedefs & structures --- */
typedef struct _PsppireAxisUniform	   PsppireAxisUniform;
typedef struct _PsppireAxisUniformClass PsppireAxisUniformClass;

struct pool;

struct _PsppireAxisUniform
{
  PsppireAxis  parent;

  gint n_items;
};

struct _PsppireAxisUniformClass
{
  PsppireAxisClass parent_class;
};

GType          psppire_axis_uniform_get_type (void);

PsppireAxisUniform*   psppire_axis_uniform_new (void);


/* Interface between axis and model */


void psppire_axis_uniform_set_count (PsppireAxisUniform *axis, gint n);



G_END_DECLS

#endif /* PSPPIRE_AXIS_UNIFORM_H__ */
