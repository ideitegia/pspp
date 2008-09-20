#include <config.h>
#include "wx-mp-sr.h"

/*********************************************************************
* 
* Calculate the exact level of significance for a 
* Wilcoxon Matched-Pair Signed-Ranks Test using the sample's
* Sum of Ranks W and the sample size (i.e., number of pairs) N.
* This whole routine can be run as a stand-alone program.
*
* Use: 
* WX-MP-SR W N
*
* Copyright 1996, Rob van Son
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
* -------------------------------------------------------
*                 Rob van Son
* Institute of Phonetic Sciences & IFOTT 
* University of Amsterdam, Spuistraat 210 
* NL-1012VT Amsterdam, The Netherlands
* Tel.: (+31) 205252196	Fax.: (+31) 205252197
* Email: r.j.j.h.vanson@uva.nl
* WWW page: http://www.fon.hum.uva.nl/rob
* -------------------------------------------------------
*
* This is the actual routine that calculates the exact (two-tailed)
* level of significance for the Wilcoxon Matched-Pairs Signed-Ranks
* test. The inputs are the Sum of Ranks of either the positive of 
* negative samples (W) and the sample size (N).
* The Level of significance is calculated by checking for each
* possible outcome (2**N possibilities) whether the sum of ranks
* is larger than or equal to the observed Sum of Ranks (W).
*
* NOTE: The execution-time scales like ~ N*2**N, i.e., N*pow(2, N), 
* which is more than exponential. Adding a single pair to the sample 
* (i.e., increase N by 1) will more than double the time needed to 
* complete the calculations (apart from an additive constant).
* The execution-time of this program can easily outrun your 
* patience.
*
***********************************************************************/ 

double LevelOfSignificanceWXMPSR(double Winput, long int N)
{
  unsigned long int W, MaximalW, NumberOfPossibilities, CountLarger;
  unsigned long int i, RankSum, j;
  double p;

  /* Determine Wmax, i.e., work with the largest Rank Sum */
  MaximalW = N*(N+1)/2;
  if(Winput < MaximalW/2)Winput = MaximalW - Winput;
  W = Winput;    /* Convert to long int */
  if(W != Winput)++W;  /* Increase to next full integer */
  
  /* The total number of possible outcomes is 2**N  */
  NumberOfPossibilities = 1 << N;
  
  /* Initialize and loop. The loop-interior will be run 2**N times. */
  CountLarger = 0;
  /* Generate all distributions of sign over ranks as bit-patterns (i). */
  for(i=0; i < NumberOfPossibilities; ++i)
  { 
    RankSum = 0;
    /* 
       Shift "sign" bits out of i to determine the Sum of Ranks (j). 
    */
    for(j=0; j < N; ++j)
    { 
      if((i >> j) & 1)RankSum += j + 1;  
    };
    /*
    * Count the number of "samples" that have a Sum of Ranks larger than 
    * or equal to the one found (i.e., >= W).
    */
    if(RankSum >= W)++CountLarger;
  };
  /*****************************************************************
  * The level of significance is the number of outcomes with a
  * sum of ranks equal to or larger than the one found (W) 
  * divided by the total number of possible outcomes. 
  * The level is doubled to get the two-tailed result.
  ******************************************************************/
  p = 2*((double)CountLarger) / ((double)NumberOfPossibilities);

  return p;
}

