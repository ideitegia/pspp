#include "helper.h"
#include <data/data-in.h>
#include <data/data-out.h>
#include <libpspp/message.h>

#include <libpspp/i18n.h>

#include <ctype.h>
#include <string.h>
#include <data/settings.h>

/* Formats a value according to FORMAT 
   The returned string must be freed when no longer required */
gchar *
value_to_text(union value v, struct fmt_spec format)
{
  gchar *s = 0;

  s = g_new(gchar, format.w + 1);
  data_out(&v, &format, s);
  s[format.w]='\0';
  g_strchug(s);

  return s;
}



gboolean 
text_to_value(const gchar *text, union value *v, 
	      struct fmt_spec format)
{
  bool ok;

  if ( format.type != FMT_A) 
    {
      if ( ! text ) return FALSE;

      {
	const gchar *s = text;
	while(*s) 
	  {
	    if ( !isspace(*s))
	      break;
	    s++;
	  }
 
	if ( !*s) return FALSE;
      }
    }

  msg_disable ();
  ok = data_in (ss_cstr (text), format.type, 0, 0,
                v, fmt_var_width (&format));
  msg_enable ();

  return ok;
}


GtkWidget *
get_widget_assert(GladeXML *xml, const gchar *name)
{
  GtkWidget *w;
  g_assert(xml);
  g_assert(name);
  
  w = glade_xml_get_widget(xml, name);

  if ( !w ) 
    g_warning("Widget \"%s\" could not be found\n", name);

  return w;
}

/* Converts a string in the pspp locale to utf-8 */
char *
pspp_locale_to_utf8(const gchar *text, gssize len, GError **err)
{
  return recode_string(CONV_PSPP_TO_UTF8, text, len);
}

