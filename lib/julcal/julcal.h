/* 
   Declarations for Julian date routines.

   Modified BLP 8/28/95, 9/26/95, 12/15/99 for PSPP.
 */

#if !julcal_h
#define julcal_h 1

long calendar_to_julian (int y, int m, int d);
void julian_to_calendar (long jd, int *y, int *m, int *d);
int julian_to_wday (long jd);
int julian_to_jday (long jd);

#endif /* !julcal_h */
