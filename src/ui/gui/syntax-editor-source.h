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

#ifndef SYNTAX_EDITOR_SOURCE_H
#define SYNTAX_EDITOR_SOURCE_H

#include <gtk/gtk.h>
struct getl_interface;

struct syntax_editor;

struct getl_interface *
create_syntax_editor_source (const struct syntax_editor *se,
			     GtkTextIter start,
			     GtkTextIter stop
			     );



#endif
