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


#ifndef PSPPIRE_AXIS_IMPL_H__
#define PSPPIRE_AXIS_IMPL_H__


#include <glib-object.h>
#include <glib.h>

#include "psppire-axis.h"
#include <libpspp/tower.h>

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_AXIS_IMPL              (psppire_axis_impl_get_type ())
#define PSPPIRE_AXIS_IMPL(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_AXIS_IMPL, PsppireAxisImpl))
#define PSPPIRE_AXIS_IMPL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_AXIS_IMPL, PsppireAxisImplClass))
#define PSPPIRE_IS_AXIS_IMPL(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_AXIS_IMPL))
#define PSPPIRE_IS_AXIS_IMPL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_AXIS_IMPL))
#define PSPPIRE_AXIS_IMPL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_AXIS_IMPL, PsppireAxisImplClass))



/* --- typedefs & structures --- */
typedef struct _PsppireAxisImpl	   PsppireAxisImpl;
typedef struct _PsppireAxisImplClass PsppireAxisImplClass;

struct pool;

struct _PsppireAxisImpl
{
  PsppireAxis  parent;

  struct tower pixel_tower;
  struct tower unit_tower;

  struct pool *pool;
};

struct _PsppireAxisImplClass
{
  PsppireAxisClass parent_class;
};

GType          psppire_axis_impl_get_type (void);

PsppireAxisImpl*   psppire_axis_impl_new (void);


/* Interface between axis and model */



void psppire_axis_impl_insert (PsppireAxisImpl *a, gint posn, gint size);

void psppire_axis_impl_append (PsppireAxisImpl *a, gint size);


void psppire_axis_impl_append_n (PsppireAxisImpl *a, gint n_units, gint size);

void psppire_axis_impl_resize (PsppireAxisImpl *a, gint posn, gint size);

void psppire_axis_impl_clear (PsppireAxisImpl *);


void psppire_axis_impl_delete (PsppireAxisImpl *, gint first, gint n_cases);



G_END_DECLS

#endif /* PSPPIRE_AXIS_IMPL_H__ */
