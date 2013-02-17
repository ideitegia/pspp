/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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


#ifndef PAGE_FIRST_LINE_H
#define PAGE_FIRST_LINE_H

struct first_line_page ;
struct import_assistant;
struct string;

struct first_line_page *first_line_page_create (struct import_assistant *ia);
void first_line_append_syntax (const struct import_assistant *ia, struct string *s);

#endif
