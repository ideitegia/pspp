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


#include <config.h>
#include "command-line.h"
#include <argp.h>
#include <gl/xalloc.h>
#include <stdlib.h>
#include <string.h>
#include <libpspp/compiler.h>
#include <assert.h>


struct clp_child
{
  void *aux;
};

struct command_line_processor
{
  struct argp master_parser;

  struct clp_child *child_lookup_table;
  struct argp_child *children;
  int n_children;

  const char *doc;
  const char *args_doc;

  void *aux;
};


/* Convenience function for use in parsing functions.
   Returns the object for this parser */
struct command_line_processor *
get_subject (struct argp_state *state)
{
  const struct argp *root = state->root_argp;

  const struct argp_child *children = root->children;

  return  (struct command_line_processor *) children[0].argp;
}


/* Create a command line processor.
   DOC is typically the name of the program and short description.
   ARGS_DOC is a short description of the non option arguments.
   AUX is an arbitrary pointer.
 */
struct command_line_processor *
command_line_processor_create (const char *doc, const char *args_doc, void *aux)
{
  struct command_line_processor *clp = xzalloc (sizeof (*clp));

  clp->children = NULL;
  clp->child_lookup_table = NULL;

  clp->doc = doc;
  clp->args_doc = args_doc;
  clp->aux = aux;

  return clp;
}

/* Destroy a command line processor */
void
command_line_processor_destroy (struct command_line_processor *clp)
{
  free (clp->children);
  free (clp->child_lookup_table);
  free (clp);
}


/* Add a CHILD to the processor CLP, with the doc string DOC.
   AUX is an auxilliary pointer, specific to CHILD.
   If AUX is not known or not needed then it may be set to NULL
*/
void
command_line_processor_add_options (struct command_line_processor *clp, const struct argp *child,
			       const char *doc, void *aux)
{
  clp->n_children++;

  clp->children = xrealloc (clp->children, (clp->n_children + 1) * sizeof (*clp->children));
  memset (&clp->children[clp->n_children - 1], 0, sizeof (*clp->children));

  clp->child_lookup_table = xrealloc (clp->child_lookup_table,
				      clp->n_children * sizeof (*clp->child_lookup_table));

  clp->child_lookup_table [clp->n_children - 1].aux = aux;

  clp->children [clp->n_children - 1].argp = child;
  clp->children [clp->n_children - 1].header = doc;
  clp->children [clp->n_children].argp = NULL;
}


/* Set the aux paramter for CHILD in CLP to AUX.
   Any previous value will be overwritten.
 */
void
command_line_processor_replace_aux (struct command_line_processor *clp, const struct argp *child, void *aux)
{
  int i;
  for (i = 0 ; i < clp->n_children; ++i )
    {
      if (child->options == clp->children[i].argp->options)
	{
	  clp->child_lookup_table[i].aux = aux;
	  break;
	}
    }
  assert (i < clp->n_children);
}


static error_t
top_level_parser (int key UNUSED, char *arg UNUSED, struct argp_state *state)
{
  int i;
  struct command_line_processor *clp = state->input;

  if ( key == ARGP_KEY_INIT)
    {

      for (i = 0;  i < clp->n_children ; ++i)
	{
	  state->child_inputs[i] = clp->child_lookup_table[i].aux;
	}
    }

  return ARGP_ERR_UNKNOWN;
}


/* Parse the command line specified by (ARGC, ARGV) using CLP */
void
command_line_processor_parse (struct command_line_processor *clp, int argc, char **argv)
{
  clp->master_parser.parser = top_level_parser;
  clp->master_parser.args_doc = clp->args_doc;

  clp->master_parser.doc = clp->doc;

  clp->master_parser.children = clp->children;

  argp_parse (&clp->master_parser, argc, argv, 0, 0, clp);
}

