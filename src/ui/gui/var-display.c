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
missing_values_to_string (const struct variable *pv, GError **err)
{
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
	      mv[i] = value_to_text (*mv_get_value (miss, i), pv);
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

	  l = value_to_text (low, pv);
	  h = value_to_text (high, pv);

	  g_string_printf (gstr, "%s - %s", l, h);
	  g_free (l);
	  g_free (h);

	  if ( mv_has_value (miss))
	    {
	      gchar *ss = NULL;

	      ss = value_to_text (*mv_get_value (miss, 0), pv);

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
