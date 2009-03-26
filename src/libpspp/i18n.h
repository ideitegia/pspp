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

const char * get_pspp_locale (void);
void set_pspp_locale (const char *locale);
const char * get_pspp_charset (void);

void  i18n_done (void);
void  i18n_init (void);

enum conv_id
  {
    CONV_PSPP_TO_UTF8,
    CONV_UTF8_TO_PSPP,
    n_CONV
  };


char * recode_string (enum conv_id how,  const char *text, int len);


/* Return the decimal separator according to the
   system locale */
char get_system_decimal (void);

#endif /* i18n.h */
