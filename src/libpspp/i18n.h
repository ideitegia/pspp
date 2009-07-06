/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifndef I18N_H
#define I18N_H

#include <stdbool.h>

void  i18n_done (void);
void  i18n_init (void);

#define UTF8 "UTF-8"

struct pool;

char *recode_string_pool (const char *to, const char *from,
			  const char *text, int length, struct pool *pool);

char *recode_string (const char *to, const char *from,
		      const char *text, int len);


bool valid_encoding (const char *enc);

/* Return the decimal separator according to the
   system locale */
char get_system_decimal (void);

const char * get_default_encoding (void);
void set_default_encoding (const char *enc);

bool set_encoding_from_locale (const char *loc);


#endif /* i18n.h */
