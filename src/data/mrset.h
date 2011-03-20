/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifndef DATA_MRSET_H
#define DATA_MRSET_H 1

/* Multiple response set data structure.

   A multiple response set (mrset) is a set of variables that represent
   multiple responses to a single survey question in one of the two following
   ways:

     - A multiple dichotomy set represents a survey question with a set of
       checkboxes.  Each variable in the set is treated in a Boolean fashion:
       one value (the "counted value") means that the box was checked, and any
       other value means that it was not.

     - A multiple category set represents a survey question where the
       respondent is instructed to "list up to N choices".  Each variable
       represents one of the responses.

   The set of functions provided here are skeletal.  Undoubtedly they will grow
   as PSPP begins to make use of multiple response sets, as opposed to merely
   maintaining them as part of the dictionary.
 */

#include <stdbool.h>
#include <stddef.h>

#include "data/value.h"

struct dictionary;

/* Type of a multiple response set. */
enum mrset_type
  {
    MRSET_MD,                   /* Multiple dichotomy group. */
    MRSET_MC                    /* Multiple category group. */
  };

/* Source of category labels for a multiple dichotomy group. */
enum mrset_md_cat_source
  {
    MRSET_VARLABELS,            /* Variable labels. */
    MRSET_COUNTEDVALUES         /* Value labels for the counted value. */
  };

/* A multiple response set. */
struct mrset
  {
    char *name;                 /* UTF-8 encoded name beginning with "$". */
    char *label;                /* Human-readable UTF-8 label for group. */
    enum mrset_type type;       /* Group type. */
    struct variable **vars;     /* Constituent variables. */
    size_t n_vars;              /* Number of constituent variables. */

    /* MRSET_MD only. */
    enum mrset_md_cat_source cat_source; /* Source of category labels. */
    bool label_from_var_label;  /* 'label' taken from variable label? */
    union value counted;        /* Counted value. */
    int width;                  /* Width of 'counted'. */
  };

struct mrset *mrset_clone (const struct mrset *);
void mrset_destroy (struct mrset *);

bool mrset_is_valid_name (const char *name, const char *dict_encoding,
                          bool issue_error);

bool mrset_ok (const struct mrset *, const struct dictionary *);

#endif /* data/mrset.h */
