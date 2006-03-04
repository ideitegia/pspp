
#include "helper.h"
#include "data-in.h"
#include "message.h"

#include <ctype.h>
#include <string.h>

/* Formats a value according to FORMAT 
   The returned string must be freed when no longer required */
gchar *
value_to_text(union value v, struct fmt_spec format)
{
  gchar *s = 0;

  s = g_new(gchar, format.w + 1);
  if ( ! data_out(s, &format, &v) ) 
    {
      g_warning("Can't format missing discrete value \n");
    }
  s[format.w]='\0';
  g_strchug(s);

  return s;
}



gboolean 
text_to_value(const gchar *text, union value *v, 
	      struct fmt_spec format)
{
  struct data_in di;

  if ( format.type != FMT_A) 
    {
      if ( ! text ) return FALSE;
  
      const gchar *s = text;
      while(*s) 
	{
	  if ( !isspace(*s))
	    break;
	  s++;
	}
 
      if ( !*s) return FALSE;
    }

  di.s = text;
  di.e = text + strlen(text);
  di.v = v;
  di.flags = DI_IGNORE_ERROR;
  di.f1 = di.f2 = 0;
  di.format = format;
  
  return data_in(&di);

}


GtkWidget *
get_widget_assert(GladeXML *xml, const gchar *name)
{
  g_assert(xml);
  g_assert(name);
  GtkWidget * w = glade_xml_get_widget(xml, name);

  if ( !w ) 
    g_warning("Widget \"%s\" could not be found\n",name);

  return w;
}

