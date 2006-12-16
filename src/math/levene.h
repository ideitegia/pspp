/* This file is part of GNU PSPP 
   Computes Levene test  statistic.

   Copyright (C) 2004 Free Software Foundation, Inc.

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

#if !levene_h
#define levene_h 1


#include <data/variable.h>
#include <data/casefile.h>

/* Calculate the Levene statistic 

The independent variable :   v_indep; 

Number of dependent variables :   n_dep;

The dependent variables :   v_dep;

*/


struct dictionary ;
struct casefilter ;

void  levene(const struct dictionary *dict, const struct casefile *cf, 
	     struct variable *v_indep, size_t n_dep, struct variable **v_dep,
	     struct casefilter *filter);



#endif /* levene_h */
