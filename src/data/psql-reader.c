/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

#include <data/casereader-provider.h>
#include <libpspp/message.h>
#include <gl/xalloc.h>
#include <data/dictionary.h>
#include <stdlib.h>

#include "psql-reader.h"
#include "variable.h"
#include "format.h"
#include "calendar.h"

#include <inttypes.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)


#if !PSQL_SUPPORT
struct casereader *
psql_open_reader (struct psql_read_info *info, struct dictionary **dict)
{
  msg (ME, _("Support for reading postgres databases was not compiled into this installation of PSPP"));

  return NULL;
}

#else

#include <stdint.h>
#include <libpq-fe.h>


/* These macros  must be the same as in catalog/pg_types.h from the postgres source */
#define BOOLOID            16
#define BYTEAOID           17
#define CHAROID            18
#define NAMEOID            19
#define INT8OID            20
#define INT2OID            21
#define INT4OID            23
#define TEXTOID            25
#define OIDOID             26
#define FLOAT4OID          700
#define FLOAT8OID          701
#define CASHOID            790
#define BPCHAROID          1042
#define VARCHAROID         1043
#define DATEOID            1082
#define TIMEOID            1083
#define TIMESTAMPOID       1114
#define TIMESTAMPTZOID     1184
#define INTERVALOID        1186
#define TIMETZOID          1266
#define NUMERICOID         1700

static void psql_casereader_destroy (struct casereader *reader UNUSED, void *r_);

static bool psql_casereader_read (struct casereader *, void *,
				  struct ccase *);

static const struct casereader_class psql_casereader_class =
  {
    psql_casereader_read,
    psql_casereader_destroy,
    NULL,
    NULL,
  };

struct psql_reader
{
  PGconn *conn;

  bool integer_datetimes;

  double postgres_epoch;

  size_t value_cnt;
  struct dictionary *dict;

  bool used_first_case;
  struct ccase first_case;

  /* An array of ints, which maps psql column numbers into
     pspp variable numbers */
  int *vmap;
  size_t vmapsize;
};


static void set_value (const struct psql_reader *r,
		       PGresult *res, struct ccase *c);



#if WORDS_BIGENDIAN
static void
data_to_native (const void *in_, void *out_, int len)
{
  int i;
  const unsigned char *in = in_;
  unsigned char *out = out_;
  for (i = 0 ; i < len ; ++i )
    out[i] = in[i];
}
#else
static void
data_to_native (const void *in_, void *out_, int len)
{
  int i;
  const unsigned char *in = in_;
  unsigned char *out = out_;
  for (i = 0 ; i < len ; ++i )
    out[len - i - 1] = in[i];
}
#endif


#define GET_VALUE(IN, OUT) do { \
    size_t sz = sizeof (OUT); \
    data_to_native (*(IN), &(OUT), sz) ; \
    (*IN) += sz; \
} while (false)


#if 0
static void
dump (const unsigned char *x, int l)
{
  int i;

  for (i = 0; i < l ; ++i)
    {
      printf ("%02x ", x[i]);
    }

  putchar ('\n');

  for (i = 0; i < l ; ++i)
    {
      if ( isprint (x[i]))
	printf ("%c ", x[i]);
      else
	printf ("   ");
    }

  putchar ('\n');
}
#endif

static struct variable *
create_var (struct psql_reader *r, const struct fmt_spec *fmt,
	    int width, const char *suggested_name, int col)
{
  unsigned long int vx = 0;
  int vidx;
  struct variable *var;
  char name[VAR_NAME_LEN + 1];

  r->value_cnt += value_cnt_from_width (width);

  if ( ! dict_make_unique_var_name (r->dict, suggested_name, &vx, name))
    {
      msg (ME, _("Cannot create variable name from %s"), suggested_name);
      return NULL;
    }

  var = dict_create_var (r->dict, name, width);
  var_set_both_formats (var, fmt);

  vidx = var_get_dict_index (var);

  if ( col != -1)
    {
      r->vmap = xrealloc (r->vmap, (col + 1) * sizeof (int));

      r->vmap[col] = vidx;
      r->vmapsize = col + 1;
    }

  return var;
}

struct casereader *
psql_open_reader (struct psql_read_info *info, struct dictionary **dict)
{
  int i;
  int n_fields;
  PGresult *res = NULL;

  struct psql_reader *r = xzalloc (sizeof *r);
  struct string query ;


  r->conn = PQconnectdb (info->conninfo);
  if ( NULL == r->conn)
    {
      msg (ME, _("Memory error whilst opening psql source"));
      goto error;
    }

  if ( PQstatus (r->conn) != CONNECTION_OK )
    {
      msg (ME, _("Error opening psql source: %s."),
	   PQerrorMessage (r->conn));

      goto error;
    }

  {
    int v1;
    const char *vers = PQparameterStatus (r->conn, "server_version");

    sscanf (vers, "%d", &v1);

    if ( v1 < 8)
      {
	msg (ME,
	     _("Postgres server is version %s."
	       " Reading from versions earlier than 8.0 is not supported."),
	     vers);

	goto error;
      }
  }

  {
    const char *dt =  PQparameterStatus (r->conn, "integer_datetimes");

    r->integer_datetimes = ( 0 == strcasecmp (dt, "on"));
  }

#if USE_SSL
  if ( PQgetssl (r->conn) == NULL)
#endif
    {
      if (! info->allow_clear)
	{
	  msg (ME, _("Connection is unencrypted, "
		     "but unencrypted connections have not been permitted."));
	  goto error;
	}
    }

  r->postgres_epoch =
    calendar_gregorian_to_offset (2000, 1, 1, NULL, NULL);


  /* Create the dictionary and populate it */
  *dict = r->dict = dict_create ();

  ds_init_cstr (&query, "BEGIN READ ONLY ISOLATION LEVEL SERIALIZABLE; DECLARE  pspp BINARY CURSOR FOR ");
  ds_put_substring (&query, info->sql.ss);

  res = PQexec (r->conn, ds_cstr (&query));
  ds_destroy (&query);
  if ( PQresultStatus (res) != PGRES_COMMAND_OK )
    {
      msg (ME, _("Error from psql source: %s."),
	   PQresultErrorMessage (res));
      goto error;
    }

  PQclear (res);

  res = PQexec (r->conn, "FETCH FIRST FROM pspp");
  if ( PQresultStatus (res) != PGRES_TUPLES_OK )
    {
      msg (ME, _("Error from psql source: %s."),
	   PQresultErrorMessage (res));
      goto error;
    }

  n_fields = PQnfields (res);

  r->value_cnt = 0;
  r->vmap = NULL;
  r->vmapsize = 0;

  for (i = 0 ; i < n_fields ; ++i )
    {
      struct variable *var;
      struct fmt_spec fmt = {FMT_F, 8, 2};
      Oid type = PQftype (res, i);
      int width = 0;
      int length = PQgetlength (res, 0, i);

      switch (type)
	{
	case BOOLOID:
        case OIDOID:
	case INT2OID:
	case INT4OID:
        case INT8OID:
        case FLOAT4OID:
	case FLOAT8OID:
	  fmt.type = FMT_F;
	  break;
	case CASHOID:
	  fmt.type = FMT_DOLLAR;
	  break;
        case CHAROID:
	  fmt.type = FMT_A;
	  width = length > 0 ? length : 1;
	  fmt.d = 0;
	  fmt.w = 1;
	  break;
        case TEXTOID:
	case VARCHAROID:
	case BPCHAROID:
	  fmt.type = FMT_A;
	  width = (info->str_width == -1) ?
	    ROUND_UP (length, MAX_SHORT_STRING) : info->str_width;
	  fmt.w = width;
	  fmt.d = 0;
          break;
	case BYTEAOID:
	  fmt.type = FMT_AHEX;
	  width = length > 0 ? length : MAX_SHORT_STRING;
	  fmt.w = width * 2;
	  fmt.d = 0;
	  break;
	case INTERVALOID:
	  fmt.type = FMT_DTIME;
	  width = 0;
	  fmt.d = 0;
	  fmt.w = 13;
	  break;
	case DATEOID:
	  fmt.type = FMT_DATE;
	  width = 0;
	  fmt.w = 11;
	  fmt.d = 0;
	  break;
	case TIMEOID:
	case TIMETZOID:
	  fmt.type = FMT_TIME;
	  width = 0;
	  fmt.w = 11;
	  fmt.d = 0;
	  break;
	case TIMESTAMPOID:
	case TIMESTAMPTZOID:
	  fmt.type = FMT_DATETIME;
	  fmt.d = 0;
	  fmt.w = 22;
	  width = 0;
	  break;
	case NUMERICOID:
	  fmt.type = FMT_E;
	  fmt.d = 2;
	  fmt.w = 40;
	  width = 0;
	  break;
	default:
          msg (MW, _("Unsupported OID %d.  SYSMIS values will be inserted."), type);
	  fmt.type = FMT_A;
	  width = length > 0 ? length : MAX_SHORT_STRING;
	  fmt.w = width ;
	  fmt.d = 0;

	  break;
	}

      var = create_var (r, &fmt, width, PQfname (res, i), i);

      /* Timezones need an extra variable */
      switch (type)
	{
	case TIMETZOID:
	  {
	    struct string name;
	    ds_init_cstr (&name, var_get_name (var));
	    ds_put_cstr (&name, "-zone");
	    fmt.type = FMT_F;
	    fmt.w = 8;
	    fmt.d = 2;

	    create_var (r, &fmt, 0, ds_cstr (&name), -1);

	    ds_destroy (&name);
	  }
	  break;

	case INTERVALOID:
	  {
	    struct string name;
	    ds_init_cstr (&name, var_get_name (var));
	    ds_put_cstr (&name, "-months");
	    fmt.type = FMT_F;
	    fmt.w = 3;
	    fmt.d = 0;

	    create_var (r, &fmt, 0, ds_cstr (&name), -1);

	    ds_destroy (&name);
	  }
	default:
	  break;
	}

    }

  /* Create the first case, and cache it */
  r->used_first_case = false;


  case_create (&r->first_case, r->value_cnt);
  memset (case_data_rw_idx (&r->first_case, 0)->s,
	  ' ', MAX_SHORT_STRING * r->value_cnt);

  set_value (r, res, &r->first_case);

  PQclear (res);

  return casereader_create_sequential
    (NULL,
     r->value_cnt,
     CASENUMBER_MAX,
     &psql_casereader_class, r);

 error:
  PQclear (res);
  dict_destroy (*dict);

  psql_casereader_destroy (NULL, r);
  return NULL;
}


static void
psql_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct psql_reader *r = r_;
  if (r == NULL)
    return ;

  free (r->vmap);
  PQfinish (r->conn);

  free (r);
}

static bool
psql_casereader_read (struct casereader *reader UNUSED, void *r_,
		      struct ccase *cc)
{
  PGresult *res;

  struct psql_reader *r = r_;

  if ( !r->used_first_case )
    {
      *cc = r->first_case;
      r->used_first_case = true;
      return true;
    }

  case_create (cc, r->value_cnt);
  memset (case_data_rw_idx (cc, 0)->s, ' ', MAX_SHORT_STRING * r->value_cnt);

  res = PQexec (r->conn, "FETCH NEXT FROM pspp");
  if ( PQresultStatus (res) != PGRES_TUPLES_OK || PQntuples (res) < 1)
    {
      PQclear (res);
      case_destroy (cc);
      return false;
    }

  set_value (r, res, cc);

  PQclear (res);

  return true;
}

static void
set_value (const struct psql_reader *r,
	   PGresult *res, struct ccase *c)
{
  int i;
  int n_vars = PQnfields (res);

  for (i = 0 ; i < n_vars ; ++i )
    {
      Oid type = PQftype (res, i);
      struct variable *v = dict_get_var (r->dict, r->vmap[i]);
      union value *val = case_data_rw (c, v);
      const struct variable *v1 = NULL;
      union value *val1 = NULL;

      if (i < r->vmapsize && r->vmap[i] + 1 < dict_get_var_cnt (r->dict))
	{
	  v1 = dict_get_var (r->dict, r->vmap[i] + 1);

	  val1 = case_data_rw (c, v1);
	}


      if (PQgetisnull (res, 0, i))
	{
	  value_set_missing (val, var_get_width (v));

	  switch (type)
	    {
	    case INTERVALOID:
	    case TIMESTAMPTZOID:
	    case TIMETZOID:
	      val1->f = SYSMIS;
	      break;
	    default:
	      break;
	    }
	}
      else
	{
	  const uint8_t *vptr = (const uint8_t *) PQgetvalue (res, 0, i);
	  int length = PQgetlength (res, 0, i);

	  int var_width = var_get_width (v);
	  switch (type)
	    {
	    case BOOLOID:
	      {
		int8_t x;
		GET_VALUE (&vptr, x);
		val->f = x;
	      }
	      break;

	    case OIDOID:
	    case INT2OID:
	      {
		int16_t x;
		GET_VALUE (&vptr, x);
		val->f = x;
	      }
	      break;

	    case INT4OID:
	      {
		int32_t x;
		GET_VALUE (&vptr, x);
		val->f = x;
	      }
	      break;

	    case INT8OID:
	      {
		int64_t x;
		GET_VALUE (&vptr, x);
		val->f = x;
	      }
	      break;

	    case FLOAT4OID:
	      {
		float n;
		GET_VALUE (&vptr, n);
		val->f = n;
	      }
	      break;

	    case FLOAT8OID:
	      {
		double n;
		GET_VALUE (&vptr, n);
		val->f = n;
	      }
	      break;

	    case CASHOID:
	      {
		int32_t x;
		GET_VALUE (&vptr, x);
		val->f = x / 100.0;
	      }
	      break;

	    case INTERVALOID:
	      {
		if ( r->integer_datetimes )
		  {
		    uint32_t months;
		    uint32_t days;
		    uint32_t us;
		    uint32_t things;

		    GET_VALUE (&vptr, things);
		    GET_VALUE (&vptr, us);
		    GET_VALUE (&vptr, days);
		    GET_VALUE (&vptr, months);

		    val->f = us / 1000000.0;
		    val->f += days * 24 * 3600;

		    val1->f = months;
		  }
		else
		  {
		    uint32_t days, months;
		    double seconds;

		    GET_VALUE (&vptr, seconds);
		    GET_VALUE (&vptr, days);
		    GET_VALUE (&vptr, months);

		    val->f = seconds;
		    val->f += days * 24 * 3600;

		    val1->f = months;
		  }
	      }
	      break;

	    case DATEOID:
	      {
		int32_t x;

		GET_VALUE (&vptr, x);

		val->f = (x + r->postgres_epoch) * 24 * 3600 ;
	      }
	      break;

	    case TIMEOID:
	      {
		if ( r->integer_datetimes)
		  {
		    uint64_t x;
		    GET_VALUE (&vptr, x);
		    val->f = x / 1000000.0;
		  }
		else
		  {
		    double x;
		    GET_VALUE (&vptr, x);
		    val->f = x;
		  }
	      }
	      break;

	    case TIMETZOID:
	      {
		int32_t zone;
		if ( r->integer_datetimes)
		  {
		    uint64_t x;


		    GET_VALUE (&vptr, x);
		    val->f = x / 1000000.0;
		  }
		else
		  {
		    double x;

		    GET_VALUE (&vptr, x);
		    val->f = x ;
		  }

		GET_VALUE (&vptr, zone);
		val1->f = zone / 3600.0;
	      }
	      break;

	    case TIMESTAMPOID:
	    case TIMESTAMPTZOID:
	      {
		if ( r->integer_datetimes)
		  {
		    int64_t x;

		    GET_VALUE (&vptr, x);

		    x /= 1000000;

		    val->f = (x + r->postgres_epoch * 24 * 3600 );
		  }
		else
		  {
		    double x;

		    GET_VALUE (&vptr, x);

		    val->f = (x + r->postgres_epoch * 24 * 3600 );
		  }
	      }
	      break;
	    case TEXTOID:
	    case VARCHAROID:
	    case BPCHAROID:
	    case BYTEAOID:
	      memcpy (val->s, (char *) vptr, MIN (length, var_width));
	      break;

	    case NUMERICOID:
	      {
		double f = 0.0;
		int i;
		int16_t n_digits, weight, dscale;
		uint16_t sign;

		GET_VALUE (&vptr, n_digits);
		GET_VALUE (&vptr, weight);
		GET_VALUE (&vptr, sign);
		GET_VALUE (&vptr, dscale);

		{
		  struct fmt_spec fmt;
		  fmt.d = dscale;
		  fmt.type = FMT_E;
		  fmt.w = fmt_max_output_width (fmt.type) ;
		  fmt.d =  MIN (dscale, fmt_max_output_decimals (fmt.type, fmt.w));
		  var_set_both_formats (v, &fmt);
		}

		for (i = 0 ; i < n_digits;  ++i)
		  {
		    uint16_t x;
		    GET_VALUE (&vptr, x);
		    f += x * pow (10000, weight--);
		  }

		if ( sign == 0x4000)
		  f *= -1.0;

		if ( sign == 0xC000)
		  val->f = SYSMIS;
		else
		  val->f = f;
	      }
	      break;

	    default:
	      val->f = SYSMIS;
	      break;
	    }
	}
    }
}

#endif
