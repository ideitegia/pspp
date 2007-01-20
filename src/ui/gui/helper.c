/* This file is a rubbish bin where stuff gets put when it doesn't seem to
   belong anywhere else.
*/
#include <config.h>

#include <glib.h>
#include "helper.h"
#include "message-dialog.h"
#include <data/data-in.h>
#include <data/data-out.h>
#include <data/dictionary.h>
#include <libpspp/message.h>

#include <libpspp/i18n.h>

#include <ctype.h>
#include <string.h>
#include <data/settings.h>

#include <language/command.h>
#include <data/procedure.h>
#include <language/lexer/lexer.h>


#include <gettext.h>

/* Formats a value according to FORMAT
   The returned string must be freed when no longer required */
gchar *
value_to_text (union value v, struct fmt_spec format)
{
  gchar *s = 0;

  s = g_new (gchar, format.w + 1);
  data_out (&v, &format, s);
  s[format.w]='\0';
  g_strchug (s);

  return s;
}



gboolean
text_to_value (const gchar *text, union value *v,
	      struct fmt_spec format)
{
  bool ok;

  if ( format.type != FMT_A)
    {
      if ( ! text ) return FALSE;

      {
	const gchar *s = text;
	while (*s)
	  {
	    if ( !isspace (*s))
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
get_widget_assert (GladeXML *xml, const gchar *name)
{
  GtkWidget *w;
  g_assert (xml);
  g_assert (name);

  w = glade_xml_get_widget (xml, name);

  if ( !w )
    g_warning ("Widget \"%s\" could not be found\n", name);

  return w;
}

/* Converts a string in the pspp locale to utf-8 */
char *
pspp_locale_to_utf8 (const gchar *text, gssize len, GError **err)
{
  return recode_string (CONV_PSPP_TO_UTF8, text, len);
}

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
give_help (void)
{
  static struct msg m = {
    MSG_GENERAL,
    MSG_NOTE,
    {0, -1},
    0,
  };

  if (! m.text)
    m.text=g_strdup (_("Sorry. The help system hasn't yet been implemented."));

  popup_message (&m);
}

void
connect_help (GladeXML *xml)
{
  GList *helps = glade_xml_get_widget_prefix (xml, "help_button_");

  GList *i;
  for ( i = g_list_first (helps); i ; i = g_list_next (i))
    g_signal_connect (GTK_WIDGET (i->data), "clicked", give_help, 0);
}



void
reference_manual (GtkMenuItem *menu, gpointer data)
{
  GError *err = NULL;
  if ( ! g_spawn_command_line_async ("yelp info:pspp", &err) )
    {
      msg (ME, _("Cannot open reference manual: %s"), err->message);
    }
  g_clear_error (&err);
}


extern struct dataset *the_dataset;
extern struct source_stream *the_source_stream;

void
execute_syntax (struct getl_interface *sss)
{
  struct lexer *lexer;
  g_return_if_fail (proc_has_source (the_dataset));

  lexer = lex_create (the_source_stream);

  getl_append_source (the_source_stream, sss);

  for (;;)
    {
      const struct dictionary *dict = dataset_dict (the_dataset);

      int result = cmd_parse (lexer, the_dataset,
			      dict_get_var_cnt (dict) > 0 ?
			      CMD_STATE_DATA : CMD_STATE_INITIAL);

      if (result == CMD_EOF || result == CMD_FINISH)
	break;
    }

  getl_abort_noninteractive (the_source_stream);

  lex_destroy (lexer);
}


