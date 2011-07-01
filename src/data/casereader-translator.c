/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/casereader-provider.h"
#include "data/casereader.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

/* Casereader that applies a user-supplied function to translate
   each case into another in an arbitrary fashion. */

/* A translating casereader. */
struct casereader_translator
  {
    struct casereader *subreader; /* Source of input cases. */

    struct ccase *(*translate) (struct ccase *input, void *aux);
    bool (*destroy) (void *aux);
    void *aux;
  };

static const struct casereader_class casereader_translator_class;

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and passing through TRANSLATE, which
   must return the translated case, and populate it based on
   INPUT and auxiliary data AUX.  TRANSLATE must destroy its
   input case.

   TRANSLATE may be stateful, that is, the output for a given
   case may depend on previous cases.  If TRANSLATE is stateless,
   then you may want to use casereader_translate_stateless
   instead, since it sometimes performs better.

   The cases returned by TRANSLATE must match OUTPUT_PROTO.

   When the translating casereader is destroyed, DESTROY will be
   called to allow any state maintained by TRANSLATE to be freed.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casereader is destroyed. */
struct casereader *
casereader_create_translator (struct casereader *subreader,
                              const struct caseproto *output_proto,
                              struct ccase *(*translate) (struct ccase *input,
                                                          void *aux),
                              bool (*destroy) (void *aux),
                              void *aux)
{
  struct casereader_translator *ct = xmalloc (sizeof *ct);
  struct casereader *reader;
  ct->subreader = casereader_rename (subreader);
  ct->translate = translate;
  ct->destroy = destroy;
  ct->aux = aux;
  reader = casereader_create_sequential (
    NULL, output_proto, casereader_get_case_cnt (ct->subreader),
    &casereader_translator_class, ct);
  taint_propagate (casereader_get_taint (ct->subreader),
                   casereader_get_taint (reader));
  return reader;
}

/* Internal read function for translating casereader. */
static struct ccase *
casereader_translator_read (struct casereader *reader UNUSED,
                            void *ct_)
{
  struct casereader_translator *ct = ct_;
  struct ccase *tmp = casereader_read (ct->subreader);
  if (tmp)
    tmp = ct->translate (tmp, ct->aux);
  return tmp;
}

/* Internal destroy function for translating casereader. */
static void
casereader_translator_destroy (struct casereader *reader UNUSED, void *ct_)
{
  struct casereader_translator *ct = ct_;
  casereader_destroy (ct->subreader);
  ct->destroy (ct->aux);
  free (ct);
}

/* Casereader class for translating casereader. */
static const struct casereader_class casereader_translator_class =
  {
    casereader_translator_read,
    casereader_translator_destroy,
    NULL,
    NULL,
  };

/* Casereader that applies a user-supplied function to translate
   each case into another in a stateless fashion. */

/* A statelessly translating casereader. */
struct casereader_stateless_translator
  {
    struct casereader *subreader; /* Source of input cases. */

    casenumber case_offset;
    struct ccase *(*translate) (struct ccase *input, casenumber,
                                const void *aux);
    bool (*destroy) (void *aux);
    void *aux;
  };

static const struct casereader_random_class
casereader_stateless_translator_class;

/* Creates and returns a new casereader whose cases are produced by reading
   from SUBREADER and passing through the TRANSLATE function.  TRANSLATE must
   takes ownership of its input case and returns a translated case, populating
   the translated case based on INPUT and auxiliary data AUX.

   TRANSLATE must be stateless, that is, the output for a given case must not
   depend on previous cases.  This is because cases may be retrieved in
   arbitrary order, and some cases may be retrieved multiple times, and some
   cases may be skipped and never retrieved at all.  If TRANSLATE is stateful,
   use casereader_create_translator instead.

   The casenumber argument to the TRANSLATE function is the absolute case
   number in SUBREADER, that is, 0 when the first case in SUBREADER is being
   translated, 1 when the second case is being translated, and so on.

   The cases returned by TRANSLATE must match OUTPUT_PROTO.

   When the stateless translating casereader is destroyed, DESTROY will be
   called to allow any auxiliary data maintained by TRANSLATE to be freed.

   After this function is called, SUBREADER must not ever again be referenced
   directly.  It will be destroyed automatically when the translating
   casereader is destroyed. */
struct casereader *
casereader_translate_stateless (
  struct casereader *subreader,
  const struct caseproto *output_proto,
  struct ccase *(*translate) (struct ccase *input, casenumber,
                              const void *aux),
  bool (*destroy) (void *aux),
  void *aux)
{
  struct casereader_stateless_translator *cst = xmalloc (sizeof *cst);
  struct casereader *reader;
  cst->subreader = casereader_rename (subreader);
  cst->translate = translate;
  cst->destroy = destroy;
  cst->aux = aux;
  reader = casereader_create_random (
    output_proto, casereader_get_case_cnt (cst->subreader),
    &casereader_stateless_translator_class, cst);
  taint_propagate (casereader_get_taint (cst->subreader),
                   casereader_get_taint (reader));
  return reader;
}

/* Internal read function for stateless translating casereader. */
static struct ccase *
casereader_stateless_translator_read (struct casereader *reader UNUSED,
                                      void *cst_, casenumber idx)
{
  struct casereader_stateless_translator *cst = cst_;
  struct ccase *tmp = casereader_peek (cst->subreader, idx);
  if (tmp != NULL)
    tmp = cst->translate (tmp, cst->case_offset + idx, cst->aux);
  return tmp;
}

/* Internal destroy function for translating casereader. */
static void
casereader_stateless_translator_destroy (struct casereader *reader UNUSED,
                                         void *cst_)
{
  struct casereader_stateless_translator *cst = cst_;
  casereader_destroy (cst->subreader);
  cst->destroy (cst->aux);
  free (cst);
}

static void
casereader_stateless_translator_advance (struct casereader *reader UNUSED,
                                         void *cst_, casenumber cnt)
{
  struct casereader_stateless_translator *cst = cst_;
  cst->case_offset += casereader_advance (cst->subreader, cnt);
}

/* Casereader class for stateless translating casereader. */
static const struct casereader_random_class
casereader_stateless_translator_class =
  {
    casereader_stateless_translator_read,
    casereader_stateless_translator_destroy,
    casereader_stateless_translator_advance,
  };


struct casereader_append_numeric
{
  struct caseproto *proto;
  casenumber n;
  new_value_func *func;
  void *aux;
  void (*destroy) (void *aux);
};

static bool can_destroy (void *can_);

static struct ccase *can_translate (struct ccase *, void *can_);

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and appending an additional value,
   generated by FUNC.  AUX is an optional parameter which
   gets passed to FUNC. FUNC will also receive N as it, which is
   the ordinal number of the case in the reader.  DESTROY is an
   optional parameter used to destroy AUX.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casereader is destroyed. */
struct casereader *
casereader_create_append_numeric (struct casereader *subreader,
				  new_value_func func, void *aux,
				  void (*destroy) (void *aux))
{
  struct casereader_append_numeric *can = xmalloc (sizeof *can);
  can->proto = caseproto_ref (casereader_get_proto (subreader));
  can->proto = caseproto_add_width (can->proto, 0);
  can->n = 0;
  can->aux = aux;
  can->func = func;
  can->destroy = destroy;
  return casereader_create_translator (subreader, can->proto,
                                       can_translate, can_destroy, can);
}


static struct ccase *
can_translate (struct ccase *c, void *can_)
{
  struct casereader_append_numeric *can = can_;
  double new_value = can->func (c, can->n++, can->aux);
  c = case_unshare_and_resize (c, can->proto);
  case_data_rw_idx (c, caseproto_get_n_widths (can->proto) - 1)->f = new_value;
  return c;
}

static bool
can_destroy (void *can_)
{
  struct casereader_append_numeric *can = can_;
  if (can->destroy)
    can->destroy (can->aux);
  caseproto_unref (can->proto);
  free (can);
  return true;
}



struct arithmetic_sequence
{
  double first;
  double increment;
};

static double
next_arithmetic (const struct ccase *c UNUSED,
		 casenumber n,
		 void *aux)
{
  struct arithmetic_sequence *as = aux;
  return n * as->increment + as->first;
}

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and appending an additional value,
   which takes the value FIRST in the first case, FIRST +
   INCREMENT in the second case, FIRST + INCREMENT * 2 in the
   third case, and so on.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casereader is destroyed. */
struct casereader *
casereader_create_arithmetic_sequence (struct casereader *subreader,
                                       double first, double increment)
{
  struct arithmetic_sequence *as = xzalloc (sizeof *as);
  as->first = first;
  as->increment = increment;
  return casereader_create_append_numeric (subreader, next_arithmetic,
					   as, free);
}




struct casereader_append_rank
{
  struct casereader *clone;
  casenumber n;
  const struct variable *var;
  const struct variable *weight;
  struct caseproto *proto;
  casenumber n_common;
  double mean_rank;
  double cc;
  distinct_func *distinct;
  void *aux;
  enum rank_error *err;
  double prev_value;
};

static bool car_destroy (void *car_);

static struct ccase *car_translate (struct ccase *input, void *car_);

/* Creates and returns a new casereader whose cases are produced
   by reading from SUBREADER and appending an additional value,
   which is the rank of the observation.   W is the weight variable
   of the dictionary containing V, or NULL if there is no weight
   variable.

   The following preconditions must be met:

   1.    SUBREADER must be sorted on V.

   2.    The weight variables, must be non-negative.

   If either of these preconditions are not satisfied, then the rank
   variables may not be correct.  In this case, if ERR is non-null,
   it will be set according to the erroneous conditions encountered.

   If DISTINCT_CALLBACK is non-null, then  it will be called exactly
   once for every case containing a distinct value of V.  AUX is
   an auxilliary pointer passed to DISTINCT_CALLBACK.

   After this function is called, SUBREADER must not ever again
   be referenced directly.  It will be destroyed automatically
   when the translating casereader is destroyed. */
struct casereader *
casereader_create_append_rank (struct casereader *subreader,
			       const struct variable *v,
			       const struct variable *w,
			       enum rank_error *err,
			       distinct_func *distinct_callback,
			       void *aux
			       )
{
  struct casereader_append_rank *car = xmalloc (sizeof *car);
  car->proto = caseproto_ref (casereader_get_proto (subreader));
  car->proto = caseproto_add_width (car->proto, 0);
  car->weight = w;
  car->var = v;
  car->n = 0;
  car->n_common = 1;
  car->cc = 0.0;
  car->clone = casereader_clone (subreader);
  car->distinct = distinct_callback;
  car->aux = aux;
  car->err = err;
  car->prev_value = SYSMIS;

  return casereader_create_translator (subreader, car->proto,
                                       car_translate, car_destroy, car);
}


static bool
car_destroy (void *car_)
{
  struct casereader_append_rank *car = car_;
  casereader_destroy (car->clone);
  caseproto_unref (car->proto);
  free (car);
  return true;
}

static struct ccase *
car_translate (struct ccase *input, void *car_)
{
  struct casereader_append_rank *car = car_;

  const double value = case_data (input, car->var)->f;

  if ( car->prev_value != SYSMIS)
    {
      if (car->err && value < car->prev_value)
	*car->err |= RANK_ERR_UNSORTED;
    }

  if ( car->n_common == 1)
    {
      double vxx = SYSMIS;
      casenumber k = 0;
      double weight = 1.0;
      if (car->weight)
	{
	  weight = case_data (input, car->weight)->f;
	  if ( car->err && weight < 0 )
	    *car->err |= RANK_ERR_NEGATIVE_WEIGHT;
	}

      do
	{
	  struct ccase *c = casereader_peek (car->clone, car->n + ++k);
	  if (c == NULL)
	    break;
	  vxx = case_data (c, car->var)->f;

	  if ( vxx == value)
	    {
	      if (car->weight)
		{
		  double w = case_data (c, car->weight)->f;

		  if ( car->err && w < 0 )
		    *car->err |= RANK_ERR_NEGATIVE_WEIGHT;

		  weight += w;
		}
	      else
		weight += 1.0;
	      car->n_common++;
	    }
          case_unref (c);
	}
      while (vxx == value);
      car->mean_rank = car->cc + (weight + 1) / 2.0;
      car->cc += weight;

      if (car->distinct)
	car->distinct (value, car->n_common, weight, car->aux);
    }
  else
    car->n_common--;

  car->n++;

  input = case_unshare_and_resize (input, car->proto);
  case_data_rw_idx (input, caseproto_get_n_widths (car->proto) - 1)->f
    = car->mean_rank;
  car->prev_value = value;
  return input;
}




struct consolidator
{
  const struct variable *key;
  const struct variable *weight;
  double cc;
  double prev_cc;

  casenumber n;
  struct casereader *clone;
  struct caseproto *proto;
  int direction;
};

static bool
uniquify (const struct ccase *c, void *aux)
{
  struct consolidator *cdr = aux;
  const union value *current_value = case_data (c, cdr->key);
  const int key_width = var_get_width (cdr->key);
  const double weight = cdr->weight ? case_data (c, cdr->weight)->f : 1.0;
  struct ccase *next_case = casereader_peek (cdr->clone, cdr->n + 1);
  int dir = 0;

  cdr->n ++;
  cdr->cc += weight;

  if ( NULL == next_case)
      goto end;
  
  dir = value_compare_3way (case_data (next_case, cdr->key),
			    current_value, key_width);
  case_unref (next_case);
  if ( dir != 0 )
    {
      /* Insist that the data are sorted */
      assert (cdr->direction == 0 || dir == cdr->direction);
      cdr->direction = dir;
      goto end;
    }
  
  return false;

 end:
  cdr->prev_cc = cdr->cc;
  cdr->cc = 0;
  return true;
}



static struct ccase *
consolodate_weight (struct ccase *input, void *aux)
{
  struct consolidator *cdr = aux;
  struct ccase *c;

  if (cdr->weight)
    {
      c = case_unshare (input);
      case_data_rw (c, cdr->weight)->f = cdr->prev_cc;
    }
  else
    {
      c = case_unshare_and_resize (input, cdr->proto);
      case_data_rw_idx (c, caseproto_get_n_widths (cdr->proto) - 1)->f = cdr->prev_cc;    
    }

  return c;
}


static bool
uniquify_destroy (void *aux)
{
  struct consolidator *cdr = aux;

  casereader_destroy (cdr->clone);
  caseproto_unref (cdr->proto);
  free (cdr);

  return true;
}



/* Returns a new casereader which is based upon INPUT, but which contains a maximum 
   of one case for each distinct value of KEY.
   If WEIGHT is non-null, then the new casereader's values for this variable
   will be the sum of all values matching KEY.
   IF WEIGHT is null, then the new casereader will have an additional numeric
   value appended, which will contain the total number of cases containing
   KEY.
   INPUT must be sorted on KEY
*/
struct casereader *
casereader_create_distinct (struct casereader *input,
					       const struct variable *key,
					       const struct variable *weight)
{
  struct casereader *u ;
  struct casereader *ud ;
  struct caseproto *output_proto = caseproto_ref (casereader_get_proto (input));

  struct consolidator *cdr = xmalloc (sizeof (*cdr));
  cdr->n = 0;
  cdr->key = key;
  cdr->weight = weight;
  cdr->cc = 0;
  cdr->clone = casereader_clone (input);
  cdr->direction = 0;

  if ( NULL == cdr->weight )
    output_proto = caseproto_add_width (output_proto, 0);

  cdr->proto = output_proto;

  u = casereader_create_filter_func (input, uniquify,
				     NULL, cdr, NULL);

  ud = casereader_create_translator (u,
				     output_proto,
				     consolodate_weight,
				     uniquify_destroy,
				     cdr);

  return ud;
}

