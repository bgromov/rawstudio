/*
 * Copyright (C) 2006 Anders Brander <anders@brander.dk> and 
 * Anders Kvist <akv@lnxbx.dk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <glib.h>
#include <gtk/gtk.h>

GtkWidget *gui_tooltip_no_window(GtkWidget *widget, gchar *tip_tip, gchar *tip_private)
{
	GtkWidget *e;
	GtkTooltips *tip;

	tip = gtk_tooltips_new();
	e = gtk_event_box_new();
	gtk_tooltips_set_tip(tip, e, tip_tip, tip_private);
	gtk_widget_show(widget);
	gtk_container_add(GTK_CONTAINER(e), widget);

	return e;
}

void gui_tooltip_window(GtkWidget *widget, gchar *tip_tip, gchar *tip_private)
{
	GtkTooltips *tip;

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, widget, tip_tip, tip_private);
	gtk_widget_show(widget);

	return;
}
