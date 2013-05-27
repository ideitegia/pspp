/* Wrapper for <gtk/gtk.h>.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef PSPP_GTK_GTK_H
#define PSPP_GTK_GTK_H

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

#@INCLUDE_NEXT@ @NEXT_GTK_GTK_H@


#ifndef G_CONST_RETURN
#define G_CONST_RETURN const
#endif


/* Like GSEAL but only used in PSPP */
#define PSEAL(X) X

#endif /* PSPP_GTK_GTK_H */
