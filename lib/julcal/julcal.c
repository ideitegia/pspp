/*
   Modified BLP 8/28/95, 12/15/99 for PSPP.

   Original sources for julcal.c and julcal.h can be found at
   ftp.cdrom.com in /pub/algorithms/c/julcal10/{julcal.c,julcal.h}.
 */

/*
   Translated from Pascal to C by Jim Van Zandt, July 1992.

   Error-free translation based on error-free PL/I source

   Based on Pascal code copyright 1985 by Michael A. Covington,
   published in P.C. Tech Journal, December 1985, based on formulae
   appearing in Astronomical Formulae for Calculators by Jean Meeus
 */

/*#include <config.h>*/
#include <time.h>
#include <assert.h>
#include "julcal.h"

#define JUL_OFFSET 2299160L

/* Takes Y, M, and D, and returns the corresponding Julian date as an
   offset in days from the midnight separating 8 Oct 1582 and 9 Oct
   1582.  (Y,M,D) = (1999,10,1) corresponds to 1 Oct 1999. */
long
calendar_to_julian (int y, int m, int d)
{
  m--;
  y += m / 12;
  m -= m / 12 * 12;

  assert (m > -12 && m < 12);
  if (m < 0)
    {
      m += 12;
      y--;
    }

  assert (m >= 0 && m < 12);
  if (m < 2)
    {
      m += 13;
      y--;
    }
  else
    m++;
    
  return ((1461 * (y + 4716L) / 4)
	  + (153 * (m + 1) / 5)
	  + (d - 1)
	  - 1524
	  + 3
	  - y / 100
	  + y / 400
	  - y / 4000
	  - JUL_OFFSET);
}

/* Takes a Julian date JD and sets *Y0, *M0, and *D0 to the
   corresponding year, month, and day, respectively, where
   (*Y0,*M0,*D0) = (1999,10,1) would be 1 Oct 1999. */
void
julian_to_calendar (long jd, int *y0, int *m0, int *d0)
{
  int a, ay, em;

  jd += JUL_OFFSET;
  
  {
    long aa, ab;
    
    aa = jd - 1721120L;
    ab = 31 * (aa / 1460969L);
    aa %= 1460969L;
    ab += 3 * (aa / 146097L);
    aa = aa % 146097L;
    if (aa == 146096L)
      ab += 3;
    else
      ab += aa / 36524L;
    a = jd + (ab - 2);
  }
  
  {
    long ee, b, d;
    
    b = a + 1524;
    ay = (20 * b - 2442) / 7305;
    d = 1461L * ay / 4;
    ee = b - d;
    em = 10000 * ee / 306001;
    if (d0 != NULL)
      *d0 = ee - 306001L * em / 10000L;
  }

  if (y0 != NULL || m0 != NULL)
    {
      int m = em - 1;
      if (m > 12)
	m -= 12;
      if (m0 != NULL)
	*m0 = m;

      if (y0 != NULL)
	{
	  if (m > 2)
	    *y0 = ay - 4716;
	  else
	    *y0 = ay - 4715;
	}
      
    }
}

/* Takes a julian date JD and returns the corresponding year-relative
   Julian date, with 1=Jan 1. */
int
julian_to_jday (long jd)
{
  int year;

  julian_to_calendar (jd, &year, NULL, NULL);
  return jd - calendar_to_julian (year, 1, 1) + 1;
}


/* Takes a julian date JD and returns the corresponding weekday 1...7,
   with 1=Sunday. */
int
julian_to_wday (long jd)
{
  return (jd - 3) % 7 + 1;
}

#if STANDALONE
#include <stdio.h>

int
main (void)
{
  {
    long julian[] = 
      {
	1, 50000, 102, 1157, 14288, 87365, 109623, 153211, 152371, 144623,
      };
    size_t j;

    for (j = 0; j < sizeof julian / sizeof *julian; j++)
      {
	int y, m, d;
	long jd;
	julian_to_calendar (julian[j], &y, &m, &d);
	jd = calendar_to_julian (y, m, d);
	printf ("%ld => %d/%d/%d => %ld\n", julian[j], y, m, d, jd);
      }
  }
  
  {
    int date[][3] = 
      {
	{1582,10,15}, {1719,9,6}, {1583,1,24}, {1585,12,14},
	{1621,11,26}, {1821,12,25}, {1882,12,3}, {2002,4,6},
	{1999,12,19}, {1978,10,1},
      };
    size_t j;

    for (j = 0; j < sizeof date / sizeof *date; j++)
      {
	int y = date[j][0], m = date[j][1], d = date[j][2];
	long jd = calendar_to_julian (y, m, d);
	int y2, m2, d2;
	julian_to_calendar (jd, &y2, &m2, &d2);
	printf ("%d/%d/%d => %ld => %d/%d/%d\n",
		y, m, d, jd, y2, m2, d2);
      }
  }
    
  return 0;
}
#endif
