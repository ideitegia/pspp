/* PSPP - computes sample statistics.			-*- C -*-
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

/* Included by frequencies.q. */

#if WEIGHTING
  #define WEIGHT w
  #define FUNCNAME calc_weighting
#else /* !WEIGHTING */
  #define WEIGHT 1.0
  #define FUNCNAME calc_no_weight
#endif /* !WEIGHTING */

static int
FUNCNAME (struct ccase *c)
{
  int i;
#if WEIGHTING
  double w;

  w = c->data[default_dict.var[default_dict.weight_index]->fv].f;
#endif

  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      union value *val = &c->data[v->fv];
      struct freq_tab *ft = &v->p.frq.tab;

      switch (v->p.frq.tab.mode)
	{
	  case FRQM_GENERAL:
	    {
	      /* General mode.  This declaration and initialization are
		 strictly conforming: see C89 section 6.5.2.1. */
	      struct freq *fp = avl_find (ft->tree, (struct freq *) val);
	  
	      if (fp)
		fp->c += WEIGHT;
	      else
		{
		  fp = pool_alloc (gen_pool, sizeof *fp);
		  fp->v = *val;
		  fp->c = WEIGHT;
		  avl_insert (ft->tree, fp);
		  if (is_missing (val, v))
		    v->p.frq.tab.n_missing++;
		}
	    }
	  break;
	case FRQM_INTEGER:
	  /* Integer mode. */
	  if (val->f == SYSMIS)
	    v->p.frq.tab.sysmis += WEIGHT;
	  else if (val->f > INT_MIN+1 && val->f < INT_MAX-1)
	    {
	      int i = val->f;
	      if (i >= v->p.frq.tab.min && i <= v->p.frq.tab.max)
		v->p.frq.tab.vector[i - v->p.frq.tab.min] += WEIGHT;
	    }
	  else
	    v->p.frq.tab.out_of_range += WEIGHT;
	  break;
	default:
	  assert (0);
	}
    }
  return 1;
}

#undef WEIGHT
#undef WEIGHTING
#undef FUNCNAME
