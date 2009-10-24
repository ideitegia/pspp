/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_ARGV_PARSER_H
#define LIBPSPP_ARGV_PARSER_H 1

/* Simple, modular command-line argument parser.

   glibc has two option parsers, but neither one of them feels
   quite right:

     - getopt_long is simple, but not modular, in that there is
       no easy way to make it accept multiple collections of
       options supported by different modules.

     - argp is more sophisticated and more complete, and hence
       more complex.  It still lacks one important feature for
       modularity: there is no straightforward way for option
       groups that are implemented independently to have separate
       auxiliary data.

   The parser implemented in this file is meant to be simple and
   modular.  It is based internally on getopt_long. */

#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>

struct argv_option
  {
    const char *long_name;  /* Long option name, NULL if none. */
    int short_name;         /* Short option character, 0 if none. */
    int has_arg;            /* no_argument, required_argument, or
                               optional_argument. */
    int id;                 /* Value passed to callback. */
  };

struct argv_parser *argv_parser_create (void);
void argv_parser_destroy (struct argv_parser *);

void argv_parser_add_options (struct argv_parser *,
                              const struct argv_option *options, size_t n,
                              void (*cb) (int id, void *aux), void *aux);
bool argv_parser_run (struct argv_parser *, int argc, char **argv);

#endif /* libpspp/argv-parser.h */
