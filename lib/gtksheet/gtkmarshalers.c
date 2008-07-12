/*******************************************************************************
**3456789 123456789 123456789 123456789 123456789 123456789 123456789 123456789 
**      10        20        30        40        50        60        70        80
**
**  library for GtkXPaned-widget, a 2x2 grid-like variation of GtkPaned of gtk+
**  Copyright (C) 2005-2006 Mirco "MacSlow" Müller <macslow@bangang.de>
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
**
**  GtkXPaned is based on GtkPaned which was done by...
**
**  "Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald"
**
**  and later modified by...
**
**  "the GTK+ Team and others 1997-2000"
**
*******************************************************************************/

#include "gtkmarshalers.h"

/* lazy copied some marshalers copied from gtk+-2.6.10/gtk/gtkmarshalers.c */
void gtk_marshal_BOOLEAN__BOOLEAN (GClosure* closure,
								   GValue* return_value,
								   guint n_param_values,
								   const GValue* param_values,
								   gpointer invocation_hint,
								   gpointer marshal_data)
{
	typedef gboolean (*GMarshalFunc_BOOLEAN__BOOLEAN) (gpointer data1,
													   gboolean arg_1,
													   gpointer data2);
	register GMarshalFunc_BOOLEAN__BOOLEAN callback;
	register GCClosure* cc = (GCClosure*) closure;
	register gpointer data1;
	register gpointer data2;
	gboolean v_return;

	g_return_if_fail (return_value != NULL);
	g_return_if_fail (n_param_values == 2);

	if (G_CCLOSURE_SWAP_DATA (closure))
	{
		data1 = closure->data;
		data2 = g_value_peek_pointer (param_values + 0);
	}
	else
	{
		data1 = g_value_peek_pointer (param_values + 0);
		data2 = closure->data;
	}

	callback = (GMarshalFunc_BOOLEAN__BOOLEAN) (marshal_data ? marshal_data : cc->callback);
	v_return = callback (data1, g_marshal_value_peek_boolean (param_values + 1), data2);
	g_value_set_boolean (return_value, v_return);
}

void gtk_marshal_BOOLEAN__ENUM (GClosure* closure,
								GValue* return_value,
								guint n_param_values,
								const GValue* param_values,
								gpointer invocation_hint,
								gpointer marshal_data)
{
	typedef gboolean (*GMarshalFunc_BOOLEAN__ENUM) (gpointer data1,
													gint arg_1,
													gpointer data2);
	register GMarshalFunc_BOOLEAN__ENUM callback;
	register GCClosure* cc = (GCClosure*) closure;
	register gpointer data1;
	register gpointer data2;
	gboolean v_return;

	g_return_if_fail (return_value != NULL);
	g_return_if_fail (n_param_values == 2);

	if (G_CCLOSURE_SWAP_DATA (closure))
	{
		data1 = closure->data;
		data2 = g_value_peek_pointer (param_values + 0);
	}
	else
	{
		data1 = g_value_peek_pointer (param_values + 0);
		data2 = closure->data;
	}

	callback = (GMarshalFunc_BOOLEAN__ENUM) (marshal_data ? marshal_data : cc->callback);
	v_return = callback (data1, g_marshal_value_peek_enum (param_values + 1), data2);
	g_value_set_boolean (return_value, v_return);
}

void gtk_marshal_BOOLEAN__VOID (GClosure* closure,
								GValue* return_value,
								guint n_param_values,
								const GValue* param_values,
								gpointer invocation_hint,
								gpointer marshal_data)
{
	typedef gboolean (*GMarshalFunc_BOOLEAN__VOID) (gpointer data1,
													gpointer data2);
	register GMarshalFunc_BOOLEAN__VOID callback;
	register GCClosure* cc = (GCClosure*) closure;
	register gpointer data1;
	register gpointer data2;
	gboolean v_return;

	g_return_if_fail (return_value != NULL);
	g_return_if_fail (n_param_values == 1);

	if (G_CCLOSURE_SWAP_DATA (closure))
	{
		data1 = closure->data;
		data2 = g_value_peek_pointer (param_values + 0);
	}
	else
	{
		data1 = g_value_peek_pointer (param_values + 0);
		data2 = closure->data;
	}

	callback = (GMarshalFunc_BOOLEAN__VOID) (marshal_data ? marshal_data : cc->callback);
	v_return = callback (data1, data2);
	g_value_set_boolean (return_value, v_return);
}
