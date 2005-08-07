/* PSPP - A program for statistical analysis . -*-c-*-

Copyright (C) 2004 Free Software Foundation, Inc.
Author: John Darrington 2004

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA. */

#include <config.h>
#include "factor_stats.h"
#include "percentiles.h"
#include "misc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <assert.h>


struct ptile_params
{
  double g1, g1_star;
  double g2, g2_star;
  int k1, k2;
};


const char *ptile_alg_desc[] = {
  "",
  N_("HAverage"),
  N_("Weighted Average"),
  N_("Rounded"),
  N_("Empirical"),
  N_("Empirical with averaging")
};




/* Individual Percentile algorithms */

/* Closest observation to tc1 */
double ptile_round(const struct weighted_value **wv, 
		   const struct ptile_params *par);


/* Weighted average at y_tc2 */
double ptile_haverage(const struct weighted_value **wv, 
		   const struct ptile_params *par);


/* Weighted average at y_tc1 */
double ptile_waverage(const struct weighted_value **wv, 
		   const struct ptile_params *par);


/* Empirical distribution function */
double ptile_empirical(const struct weighted_value **wv, 
		   const struct ptile_params *par);


/* Empirical distribution function with averaging*/
double ptile_aempirical(const struct weighted_value **wv, 
		   const struct ptile_params *par);




/* Closest observation to tc1 */
double
ptile_round(const struct weighted_value **wv, 
	    const struct ptile_params *par)
{
  double x;
  double a=0;

  if ( par->k1 >= 0 ) 
    a = wv[par->k1]->v.f;

  if ( wv[par->k1 + 1]->w >= 1 )
    {
      if ( par->g1_star < 0.5 ) 
	x = a;
      else
	x = wv[par->k1 + 1]->v.f;
    }
  else
    {
      if ( par->g1 < 0.5 ) 
	x = a;
      else
	x = wv[par->k1 + 1]->v.f;

    }

  return x;
}

/* Weighted average at y_tc2 */
double
ptile_haverage(const struct weighted_value **wv, 
	       const struct ptile_params *par)
{

  double a=0;

  if ( par->g2_star >= 1.0 ) 
      return wv[par->k2 + 1]->v.f ;

  /* Special case  for k2 + 1 >= n_data 
     (actually it's not a special case, but just avoids indexing errors )
   */
  if ( par->g2_star == 0 ) 
    {
      assert(par->g2 == 0 );
      return wv[par->k2]->v.f;
    }

  /* Ditto for k2 < 0 */
  if ( par->k2 >= 0 ) 
    {
      a = wv[par->k2]->v.f;
    }

  if ( wv[par->k2 + 1]->w >= 1.0 ) 
    return ( (1 - par->g2_star) *  a   + 
	     par->g2_star * wv[par->k2 + 1]->v.f);
  else
    return ( (1 - par->g2) * a + 
	     par->g2 * wv[par->k2 + 1]->v.f);

}



/* Weighted average at y_tc1 */
double 
ptile_waverage(const struct weighted_value **wv, 
	       const struct ptile_params *par)
{
  double a=0;

  if ( par->g1_star >= 1.0 ) 
      return wv[par->k1 + 1]->v.f ;

  if ( par->k1 >= 0 ) 
    {
      a = wv[par->k1]->v.f;
    }

  if ( wv[par->k1 + 1]->w >= 1.0 ) 
    return ( (1 - par->g1_star) * a + 
	     par->g1_star * wv[par->k1 + 1]->v.f);
  else
    return ( (1 - par->g1) * a + 
	     par->g1 * wv[par->k1 + 1]->v.f);
}


/* Empirical distribution function */
double 
ptile_empirical(const struct weighted_value **wv, 
	       const struct ptile_params *par)
{
  if ( par->g1_star > 0 ) 
    return wv[par->k1 + 1]->v.f;
  else
    return wv[par->k1]->v.f;
}



/* Empirical distribution function with averageing */
double 
ptile_aempirical(const struct weighted_value **wv, 
	       const struct ptile_params *par)
{
  if ( par->g1_star > 0 ) 
    return wv[par->k1 + 1]->v.f;
  else
    return (wv[par->k1]->v.f + wv[par->k1 + 1]->v.f ) / 2.0 ;
}



/* Compute the percentile p */
double ptile(double p, 
	     const struct weighted_value **wv,
	     int n_data,
	     double w,
	     enum pc_alg algorithm);



double 
ptile(double p, 
      const struct weighted_value **wv,
      int n_data,
      double w,
      enum pc_alg algorithm)
{
  int i;
  double tc1, tc2;
  double result;

  struct ptile_params pp;

  assert( p <= 1.0);

  tc1 = w * p ;
  tc2 = (w + 1) * p ;

  pp.k1 = -1;
  pp.k2 = -1;

  for ( i = 0 ; i < n_data ; ++i ) 
    {
      if ( wv[i]->cc <= tc1 ) 
	pp.k1 = i;

      if ( wv[i]->cc <= tc2 ) 
	pp.k2 = i;
      
    }


  if ( pp.k1 >= 0 ) 
    {
      pp.g1 = ( tc1 - wv[pp.k1]->cc ) / wv[pp.k1 + 1]->w;
      pp.g1_star = tc1 -  wv[pp.k1]->cc ; 
    }
  else
    {
      pp.g1 = tc1 / wv[pp.k1 + 1]->w;
      pp.g1_star = tc1 ;
    }


  if ( pp.k2  + 1 >= n_data ) 
    {
      pp.g2 = 0 ;
      pp.g2_star = 0;
    }
  else 
    {
      if ( pp.k2 >= 0 ) 
	{
	  pp.g2 = ( tc2 - wv[pp.k2]->cc ) / wv[pp.k2 + 1]->w;
	  pp.g2_star = tc2 -  wv[pp.k2]->cc ; 
	}
      else
	{
	  pp.g2 = tc2 / wv[pp.k2 + 1]->w;
	  pp.g2_star = tc2 ;
	}
    }

  switch ( algorithm ) 
    {
    case PC_HAVERAGE:
      result = ptile_haverage(wv, &pp);
      break;
    case PC_WAVERAGE:
      result = ptile_waverage(wv, &pp);
      break;
    case PC_ROUND:
      result = ptile_round(wv, &pp);
      break;
    case PC_EMPIRICAL:
      result = ptile_empirical(wv, &pp);
      break;
    case PC_AEMPIRICAL:
      result = ptile_aempirical(wv, &pp);
      break;
    default:
      result = SYSMIS;
    }

  return result;
}


/* 
   Calculate the values of the percentiles in pc_hash.
   wv is  a sorted array of weighted values of the data set.
*/
void 
ptiles(struct hsh_table *pc_hash,
       const struct weighted_value **wv,
       int n_data,
       double w,
       enum pc_alg algorithm)
{
  struct hsh_iterator hi;
  struct percentile *p;

  if ( !pc_hash ) 
    return ;
  for ( p = hsh_first(pc_hash, &hi);
	p != 0 ;
	p = hsh_next(pc_hash, &hi))
    {
      p->v = ptile(p->p/100.0 , wv, n_data, w, algorithm);
    }
  
}


/* Calculate Tukey's Hinges */
void
tukey_hinges(const struct weighted_value **wv,
	     int n_data, 
	     double w,
	     double hinge[3]
	     )
{
  int i;
  double c_star = DBL_MAX;
  double d;
  double l[3];
  int h[3];
  double a, a_star;
  
  for ( i = 0 ; i < n_data ; ++i ) 
    {
      c_star = min(c_star, wv[i]->w);
    }

  if ( c_star > 1 ) c_star = 1;

  d = floor((w/c_star + 3 ) / 2.0)/ 2.0;

  l[0] = d*c_star;
  l[1] = w/2.0 + c_star/2.0;
  l[2] = w + c_star - d*c_star;

  h[0]=-1;
  h[1]=-1;
  h[2]=-1;

  for ( i = 0 ; i < n_data ; ++i ) 
    {
      if ( l[0] >= wv[i]->cc ) h[0] = i ;
      if ( l[1] >= wv[i]->cc ) h[1] = i ;
      if ( l[2] >= wv[i]->cc ) h[2] = i ;
    }

  for ( i = 0 ; i < 3 ; i++ )
    {

      if ( h[i] >= 0 ) 
	a_star = l[i] - wv[h[i]]->cc ;
      else
	a_star = l[i];

      if ( h[i] + 1 >= n_data )
      {
	      assert( a_star < 1 ) ;
	      hinge[i] = (1 - a_star) * wv[h[i]]->v.f;
	      continue;
      }
      else 
      {
	      a = a_star / ( wv[h[i] + 1]->cc ) ; 
      }

      if ( a_star >= 1.0 ) 
	{
	  hinge[i] = wv[h[i] + 1]->v.f ;
	  continue;
	}

      if ( wv[h[i] + 1]->w >= 1)
	{
	  hinge[i] = ( 1 - a_star) * wv[h[i]]->v.f
	    + a_star * wv[h[i] + 1]->v.f;

	  continue;
	}

      hinge[i] = (1 - a) * wv[h[i]]->v.f + a * wv[h[i] + 1]->v.f;
      
    }

  assert(hinge[0] <= hinge[1]);
  assert(hinge[1] <= hinge[2]);

}


int
ptile_compare(const struct percentile *p1, 
		   const struct percentile *p2, 
		   void *aux UNUSED)
{

  int cmp;
  
  if ( p1->p == p2->p) 
    cmp = 0 ;
  else if (p1->p < p2->p)
    cmp = -1 ; 
  else 
    cmp = +1;

  return cmp;
}

unsigned
ptile_hash(const struct percentile *p, void *aux UNUSED)
{
  return hsh_hash_double(p->p);
}


