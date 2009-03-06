#include <config.h>
#include "var-display.h"

#include <data/variable.h>
#include <data/format.h>
#include <stdlib.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "helper.h"

static const gchar none[] = N_("None");

gchar *
name_to_string (const struct variable *var, GError **err)
{
  const char *name = var_get_name (var);
  g_assert (name);

  return pspp_locale_to_utf8 (name, -1, err);
}


gchar *
label_to_string (const struct variable *var, GError **err)
{
  const char *label = var_get_label (var);

  if ( ! label ) return g_strdup (none);

  return pspp_locale_to_utf8 (label, -1, err);
}

gchar *
measure_to_string (const struct variable *var, GError **err)
{
  const gint measure = var_get_measure (var);

  g_assert (measure < n_MEASURES);
  return g_locale_to_utf8 (gettext (measures[measure]),
			   -1, 0, 0, err);
}


gchar *
missing_values_to_string (const struct variable *pv, GError **err)
{
  const struct fmt_spec *fmt =  var_get_print_format (pv);
  gchar *s;
  const struct missing_values *miss = var_get_missing_values (pv);
  if ( mv_is_empty (miss))
    return g_locale_to_utf8 (gettext (none), -1, 0, 0, err);
  else
    {
      if ( ! mv_has_range (miss))
	{
	  GString *gstr = g_string_sized_new (10);
	  const int n = mv_n_values (miss);
	  gchar *mv[4] = {0,0,0,0};
	  gint i;
	  for (i = 0 ; i < n; ++i )
	    {
	      union value v;
	      mv_get_value (miss, &v, i);
	      mv[i] = value_to_text (v, *fmt);
	      if ( i > 0 )
		g_string_append (gstr, ", ");
	      g_string_append (gstr, mv[i]);
	      g_free (mv[i]);
	    }
	  s = pspp_locale_to_utf8 (gstr->str, gstr->len, err);
	  g_string_free (gstr, TRUE);
	}
      else
	{
	  GString *gstr = g_string_sized_new (10);
	  gchar *l, *h;
	  union value low, high;
	  mv_get_range (miss, &low.f, &high.f);

	  l = value_to_text (low, *fmt);
	  h = value_to_text (high, *fmt);

	  g_string_printf (gstr, "%s - %s", l, h);
	  g_free (l);
	  g_free (h);

	  if ( mv_has_value (miss))
	    {
	      gchar *ss = 0;
	      union value v;
	      mv_get_value (miss, &v, 0);

	      ss = value_to_text (v, *fmt);

	      g_string_append (gstr, ", ");
	      g_string_append (gstr, ss);
	      free (ss);
	    }
	  s = pspp_locale_to_utf8 (gstr->str, gstr->len, err);
	  g_string_free (gstr, TRUE);
	}

      return s;
    }
}
