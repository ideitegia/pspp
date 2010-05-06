#include <config.h>
#include "var-display.h"

#include <data/variable.h>
#include <data/format.h>
#include <stdlib.h>
#include "psppire-dict.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "helper.h"
#include <libpspp/i18n.h>

static const gchar none[] = N_("None");


gchar *
measure_to_string (const struct variable *var, GError **err)
{
  const gint measure = var_get_measure (var);

  g_assert (measure < n_MEASURES);
  return gettext (measures[measure]);
}


gchar *
missing_values_to_string (const PsppireDict *dict, const struct variable *pv, GError **err)
{
  const struct fmt_spec *fmt =  var_get_print_format (pv);
  gchar *s;
  const struct missing_values *miss = var_get_missing_values (pv);
  if ( mv_is_empty (miss))
    return xstrdup (gettext (none));
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
	      mv[i] = value_to_text (*mv_get_value (miss, i), dict, *fmt);
	      if ( i > 0 )
		g_string_append (gstr, ", ");
	      g_string_append (gstr, mv[i]);
	      g_free (mv[i]);
	    }
	  s = gstr->str;
	  g_string_free (gstr, FALSE);
	}
      else
	{
	  GString *gstr = g_string_sized_new (10);
	  gchar *l, *h;
	  union value low, high;
	  mv_get_range (miss, &low.f, &high.f);

	  l = value_to_text (low, dict, *fmt);
	  h = value_to_text (high, dict,*fmt);

	  g_string_printf (gstr, "%s - %s", l, h);
	  g_free (l);
	  g_free (h);

	  if ( mv_has_value (miss))
	    {
	      gchar *ss = 0;

	      ss = value_to_text (*mv_get_value (miss, 0), dict, *fmt);

	      g_string_append (gstr, ", ");
	      g_string_append (gstr, ss);
	      free (ss);
	    }
	  s = gstr->str;
	  g_string_free (gstr, FALSE);
	}

      return s;
    }
}
