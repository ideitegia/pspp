/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "data/psql-reader.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "data/calendar.h"
#include "data/casereader-provider.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)


#if !PSQL_SUPPORT
struct casereader *
psql_open_reader (struct psql_read_info *info UNUSED, struct dictionary **dict UNUSED)
{
  msg (ME, _("Support for reading postgres databases was not compiled into this installation of PSPP"));

  return NULL;
}

#else

#include <stdint.h>
#include <libpq-fe.h>


/* Default width of string variables. */
#define PSQL_DEFAULT_WIDTH 8

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

static struct ccase *psql_casereader_read (struct casereader *, void *);

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
  PGresult *res;
  int tuple;

  bool integer_datetimes;

  double postgres_epoch;

  struct caseproto *proto;
  struct dictionary *dict;

  /* An array of ints, which maps psql column numbers into
     pspp variables */
  struct variable **vmap;
  size_t vmapsize;

  struct string fetch_cmd;
  int cache_size;
};


static struct ccase *set_value (struct psql_reader *r);



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
  struct variable *var;
  char *name;

  name = dict_make_unique_var_name (r->dict, suggested_name, &vx);
  var = dict_create_var (r->dict, name, width);
  free (name);

  var_set_both_formats (var, fmt);

  if ( col != -1)
    {
      r->vmap = xrealloc (r->vmap, (col + 1) * sizeof (*r->vmap));

      r->vmap[col] = var;
      r->vmapsize = col + 1;
    }

  return var;
}




/* Fill the cache */
static bool
reload_cache (struct psql_reader *r)
{
  PQclear (r->res);
  r->tuple = 0;

  r->res = PQexec (r->conn, ds_cstr (&r->fetch_cmd));

  if (PQresultStatus (r->res) != PGRES_TUPLES_OK || PQntuples (r->res) < 1)
    {
      PQclear (r->res);
      r->res = NULL;
      return false;
    }

  return true;
}


struct casereader *
psql_open_reader (struct psql_read_info *info, struct dictionary **dict)
{
  int i;
  int n_fields, n_tuples;
  PGresult *qres = NULL;
  casenumber n_cases = CASENUMBER_MAX;
  const char *encoding;

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
    int ver_num;
    const char *vers = PQparameterStatus (r->conn, "server_version");

    sscanf (vers, "%d", &ver_num);

    if ( ver_num < 8)
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

    r->integer_datetimes = ( 0 == c_strcasecmp (dt, "on"));
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

  r->postgres_epoch = calendar_gregorian_to_offset (2000, 1, 1, NULL);

  {
    const int enc = PQclientEncoding (r->conn);

    /* According to section 22.2 of the Postgresql manual
       a value of zero (SQL_ASCII) indicates
       "a declaration of ignorance about the encoding".
       Accordingly, we use the default encoding
       if we find this value.
    */
    encoding = enc ? pg_encoding_to_char (enc) : get_default_encoding ();

    /* Create the dictionary and populate it */
    *dict = r->dict = dict_create (encoding);
  }

  /*
    select count (*) from (select * from medium) stupid_sql_standard;
  */
  ds_init_cstr (&query,
		"BEGIN READ ONLY ISOLATION LEVEL SERIALIZABLE; "
		"DECLARE  pspp BINARY CURSOR FOR ");

  ds_put_substring (&query, info->sql.ss);

  qres = PQexec (r->conn, ds_cstr (&query));
  ds_destroy (&query);
  if ( PQresultStatus (qres) != PGRES_COMMAND_OK )
    {
      msg (ME, _("Error from psql source: %s."),
	   PQresultErrorMessage (qres));
      goto error;
    }

  PQclear (qres);


  /* Now use the count() function to find the total number of cases
     that this query returns.
     Doing this incurs some overhead.  The server has to iterate every
     case in order to find this number.  However, it's performed on the
     server side, and in all except the most huge databases the extra
     overhead will be worth the effort.
     On the other hand, most PSPP functions don't need to know this.
     The GUI is the notable exception.
  */
  ds_init_cstr (&query, "SELECT count (*) FROM (");
  ds_put_substring (&query, info->sql.ss);
  ds_put_cstr (&query, ") stupid_sql_standard");

  qres = PQexec (r->conn, ds_cstr (&query));
  ds_destroy (&query);
  if ( PQresultStatus (qres) != PGRES_TUPLES_OK )
    {
      msg (ME, _("Error from psql source: %s."),
	   PQresultErrorMessage (qres));
      goto error;
    }
  n_cases = atol (PQgetvalue (qres, 0, 0));
  PQclear (qres);

  qres = PQexec (r->conn, "FETCH FIRST FROM pspp");
  if ( PQresultStatus (qres) != PGRES_TUPLES_OK )
    {
      msg (ME, _("Error from psql source: %s."),
	   PQresultErrorMessage (qres));
      goto error;
    }

  n_tuples = PQntuples (qres);
  n_fields = PQnfields (qres);

  r->proto = NULL;
  r->vmap = NULL;
  r->vmapsize = 0;

  for (i = 0 ; i < n_fields ; ++i )
    {
      struct variable *var;
      struct fmt_spec fmt = {FMT_F, 8, 2};
      Oid type = PQftype (qres, i);
      int width = 0;
      int length ;

      /* If there are no data then make a finger in the air 
	 guess at the contents */
      if ( n_tuples > 0 )
	length = PQgetlength (qres, 0, i);
      else 
	length = PSQL_DEFAULT_WIDTH;

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
	    ROUND_UP (length, PSQL_DEFAULT_WIDTH) : info->str_width;
	  fmt.w = width;
	  fmt.d = 0;
          break;
	case BYTEAOID:
	  fmt.type = FMT_AHEX;
	  width = length > 0 ? length : PSQL_DEFAULT_WIDTH;
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
	  width = length > 0 ? length : PSQL_DEFAULT_WIDTH;
	  fmt.w = width ;
	  fmt.d = 0;
	  break;
	}

      if ( width == 0 && fmt_is_string (fmt.type))
	fmt.w = width = PSQL_DEFAULT_WIDTH;


      var = create_var (r, &fmt, width, PQfname (qres, i), i);
      if ( type == NUMERICOID && n_tuples > 0)
	{
	  const uint8_t *vptr = (const uint8_t *) PQgetvalue (qres, 0, i);
	  struct fmt_spec fmt;
	  int16_t n_digits, weight, dscale;
	  uint16_t sign;

	  GET_VALUE (&vptr, n_digits);
	  GET_VALUE (&vptr, weight);
	  GET_VALUE (&vptr, sign);
	  GET_VALUE (&vptr, dscale);

	  fmt.d = dscale;
	  fmt.type = FMT_E;
	  fmt.w = fmt_max_output_width (fmt.type) ;
	  fmt.d =  MIN (dscale, fmt_max_output_decimals (fmt.type, fmt.w));
	  var_set_both_formats (var, &fmt);
	}

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

  PQclear (qres);

  qres = PQexec (r->conn, "MOVE BACKWARD 1 FROM pspp");
  if ( PQresultStatus (qres) != PGRES_COMMAND_OK)
    {
      PQclear (qres);
      goto error;
    }
  PQclear (qres);

  r->cache_size = info->bsize != -1 ? info->bsize: 4096;

  ds_init_empty (&r->fetch_cmd);
  ds_put_format (&r->fetch_cmd,  "FETCH FORWARD %d FROM pspp", r->cache_size);

  reload_cache (r);
  r->proto = caseproto_ref (dict_get_proto (*dict));

  return casereader_create_sequential
    (NULL,
     r->proto,
     n_cases,
     &psql_casereader_class, r);

 error:
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

  ds_destroy (&r->fetch_cmd);
  free (r->vmap);
  if (r->res) PQclear (r->res);
  PQfinish (r->conn);
  caseproto_unref (r->proto);

  free (r);
}



static struct ccase *
psql_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct psql_reader *r = r_;

  if ( NULL == r->res || r->tuple >= r->cache_size)
    {
      if ( ! reload_cache (r) )
	return false;
    }

  return set_value (r);
}

static struct ccase *
set_value (struct psql_reader *r)
{
  struct ccase *c;
  int n_vars;
  int i;

  assert (r->res);

  n_vars = PQnfields (r->res);

  if ( r->tuple >= PQntuples (r->res))
    return NULL;

  c = case_create (r->proto);
  case_set_missing (c);


  for (i = 0 ; i < n_vars ; ++i )
    {
      Oid type = PQftype (r->res, i);
      const struct variable *v = r->vmap[i];
      union value *val = case_data_rw (c, v);

      union value *val1 = NULL;

      switch (type)
	{
	case INTERVALOID:
	case TIMESTAMPTZOID:
	case TIMETZOID:
	  if (i < r->vmapsize && var_get_dict_index(v) + 1 < dict_get_var_cnt (r->dict))
	    {
	      const struct variable *v1 = NULL;
	      v1 = dict_get_var (r->dict, var_get_dict_index (v) + 1);

	      val1 = case_data_rw (c, v1);
	    }
	  break;
	default:
	  break;
	}


      if (PQgetisnull (r->res, r->tuple, i))
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
	  const uint8_t *vptr = (const uint8_t *) PQgetvalue (r->res, r->tuple, i);
	  int length = PQgetlength (r->res, r->tuple, i);

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
		/* Postgres 8.3 uses 64 bits.
		   Earlier versions use 32 */
		switch (length)
		  {
		  case 8:
		    {
		      int64_t x;
		      GET_VALUE (&vptr, x);
		      val->f = x / 100.0;
		    }
		    break;
		  case 4:
		    {
		      int32_t x;
		      GET_VALUE (&vptr, x);
		      val->f = x / 100.0;
		    }
		    break;
		  default:
		    val->f = SYSMIS;
		    break;
		  }
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
	      memcpy (value_str_rw (val, var_width), vptr,
                      MIN (length, var_width));
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

#if 0
		{
		  struct fmt_spec fmt;
		  fmt.d = dscale;
		  fmt.type = FMT_E;
		  fmt.w = fmt_max_output_width (fmt.type) ;
		  fmt.d =  MIN (dscale, fmt_max_output_decimals (fmt.type, fmt.w));
		  var_set_both_formats (v, &fmt);
		}
#endif

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

  r->tuple++;

  return c;
}

#endif
