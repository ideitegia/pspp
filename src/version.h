/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !version_h
#define version_h 1

/* "A.B.C" */
extern const char bare_version[];

/* "GNU PSPP A.B.C" */
extern const char version[];

/* "GNU PSPP version A.B (date), Copyright (C) XXXX Free Software
   Foundation, Inc." */
extern const char stat_version[];

/* Canonical name of host system type. */
extern const char host_system[];

/* Canonical name of build system type. */
extern const char build_system[];

/* Configuration path at build time. */
extern const char default_config_path[];

/* Include path. */
extern const char include_path[];

/* Font path. */
extern const char groff_font_path[];

/* Locale directory. */
extern const char locale_dir[];

#endif /* !version_h */
