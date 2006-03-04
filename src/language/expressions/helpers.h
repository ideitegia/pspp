#ifndef EXPRESSIONS_HELPERS_H 
#define EXPRESSIONS_HELPERS_H

#include <ctype.h>
#include <float.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include "case.h"
#include "compiler.h"
#include "data-in.h"
#include "dictionary.h"
#include "message.h"
#include "calendar.h"
#include "gsl-extras/gsl-extras.h"
#include "misc.h"
#include "moments.h"
#include "random.h"
#include "settings.h"
#include "str.h"
#include "value.h"
#include "variable.h"
#include "procedure.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static inline double check_errno (double x) 
{
  return errno == 0 ? x : SYSMIS;
}

#define check_errno(EXPRESSION) (errno = 0, check_errno (EXPRESSION))

#define DAY_S (60. * 60. * 24.)         /* Seconds per day. */
#define DAY_H 24.                       /* Hours per day. */
#define H_S (60 * 60.)                  /* Seconds per hour. */
#define H_MIN 60.                       /* Minutes per hour. */
#define MIN_S 60.                       /* Seconds per minute. */
#define WEEK_DAY 7.                     /* Days per week. */

extern const struct fixed_string empty_string;

int compare_string (const struct fixed_string *, const struct fixed_string *);

double expr_ymd_to_date (double year, double month, double day);
double expr_ymd_to_ofs (double year, double month, double day);
double expr_wkyr_to_date (double wk, double yr);
double expr_yrday_to_date (double yr, double day);
double expr_yrmoda (double year, double month, double day);

struct fixed_string alloc_string (struct expression *, size_t length);
struct fixed_string copy_string (struct expression *,
                                 const char *, size_t length);

static inline bool
is_valid (double d) 
{
  return finite (d) && d != SYSMIS;
}

size_t count_valid (double *, size_t);

double idf_beta (double P, double a, double b);
double ncdf_beta (double x, double a, double b, double lambda);
double npdf_beta (double x, double a, double b, double lambda);

double cdf_bvnor (double x0, double x1, double r);

double idf_fdist (double P, double a, double b);

#endif /* expressions/helpers.h */
