#ifndef CALENDAR_H
#define CALENDAR_H 1

double calendar_gregorian_to_offset (int y, int m, int d, char **errorp);
void calendar_offset_to_gregorian (int ofs, int *y, int *m, int *d, int *yd);
int calendar_offset_to_year (int ofs);
int calendar_offset_to_month (int ofs);
int calendar_offset_to_mday (int ofs);
int calendar_offset_to_yday (int ofs);
int calendar_offset_to_wday (int ofs);

int calendar_days_in_month (int y, int m);

#endif /* calendar.h */
