#ifndef __PSPPIRE_BUTTONBOX_H__
#define __PSPPIRE_BUTTONBOX_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkvbbox.h>


G_BEGIN_DECLS

#define PSPPIRE_BUTTONBOX_TYPE            (psppire_button_box_get_type ())
#define PSPPIRE_BUTTONBOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_BUTTONBOX_TYPE, PsppireButtonBox))
#define PSPPIRE_BUTTONBOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_BUTTONBOX_TYPE, PsppireButtonBoxClass))
#define PSPPIRE_IS_BUTTONBOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_BUTTONBOX_TYPE))
#define PSPPIRE_IS_BUTTONBOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_BUTTONBOX_TYPE))


typedef struct _PsppireButtonBox       PsppireButtonBox;
typedef struct _PsppireButtonBoxClass  PsppireButtonBoxClass;

struct _PsppireButtonBox
{
  GtkVButtonBox parent;
};

struct _PsppireButtonBoxClass
{
  GtkVButtonBoxClass parent_class;
};

GType          psppire_button_box_get_type        (void);
GtkWidget*     psppire_buttonbox_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_BUTTONBOX_H__ */

