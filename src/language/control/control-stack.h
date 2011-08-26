/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2011 Free Software Foundation, Inc.

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

#ifndef CTL_STACK_H
#define CTL_STACK_H 1

#include <stdbool.h>

/* The following #include avoids a potential problem when Gnulib substitutes
 * for close() by putting "#define close rpl_close" into <unistd.h>, by
 * ensuring that every source file that includes this one sees the #define..
 * (It would probably be better to rename the 'close' member of struct
 * ctl_class.)  */
#include <unistd.h>

struct ctl_class
  {
    const char *start_name;     /* e.g. LOOP. */
    const char *end_name;       /* e.g. END LOOP. */
    void (*close) (void *);     /* Closes the control structure. */
  };

void ctl_stack_clear (void);
void ctl_stack_push (const struct ctl_class *, void *private);
void *ctl_stack_top (const struct ctl_class *);
void *ctl_stack_search (const struct ctl_class *);
void ctl_stack_pop (void *);
bool ctl_stack_is_empty (void);

#endif /* ctl_stack.h */
