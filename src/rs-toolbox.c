/*
 * Copyright (C) 2006-2009 Anders Brander <anders@brander.dk> and 
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

#include <rawstudio.h>
#include <gtk/gtk.h>
#include <config.h>
#include <gconf/gconf-client.h>
#include "gettext.h"
#include "rs-toolbox.h"
#include "gtk-helper.h"
#include "rs-settings.h"
#include "rs-curve.h"
#include "rs-image.h"
#include "rs-histogram.h"
#include "rs-utils.h"
#include "rs-photo.h"
#include "conf_interface.h"
#include "rs-actions.h"

/* Some helpers for creating the basic sliders */
typedef struct {
	const gchar *property_name;
	gfloat step;
} BasicSettings;

const static BasicSettings basic[] = {
	{ "exposure",       0.05 },
	{ "saturation",     0.05 },
	{ "hue",            1.5 },
	{ "contrast",       0.05 },
	{ "warmth",         0.01 },
	{ "tint",           0.01 },
	{ "sharpen",        0.1 },
	{ "denoise_luma",   0.1 },
	{ "denoise_chroma", 0.1 },
};
#define NBASICS (9)

const static BasicSettings channelmixer[] = {
	{ "channelmixer_red",   1.0 },
	{ "channelmixer_green", 1.0 },
	{ "channelmixer_blue",  1.0 },
};
#define NCHANNELMIXER (3)

struct _RSToolbox {
	GtkScrolledWindow parent;
	GtkWidget *notebook;
	GtkBox *toolbox;
	GtkRange *ranges[3][NBASICS];
	GtkRange *channelmixer[3][NCHANNELMIXER];
	RSSettings *settings[3];
	GtkWidget *curve[3];

	gint selected_snapshot;
	RS_PHOTO *photo;
	GtkWidget *histogram;
	RS_IMAGE16 *histogram_dataset;
	guint histogram_connection; /* Got GConf notification */

	GConfClient *gconf;

	gboolean mute_from_sliders;
	gboolean mute_from_photo;
};

G_DEFINE_TYPE (RSToolbox, rs_toolbox, GTK_TYPE_SCROLLED_WINDOW)

enum {
	SNAPSHOT_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void conf_histogram_height_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data);
static void notebook_switch_page(GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, RSToolbox *toolbox);
static void basic_range_value_changed(GtkRange *range, gpointer user_data);
static gboolean basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkRange *basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic);
static void curve_changed(GtkWidget *widget, gpointer user_data);
static GtkWidget *gui_box(const gchar *title, GtkWidget *in, gchar *key, gboolean default_expanded);
static GtkWidget *new_snapshot_page();
static GtkWidget *new_transform(RSToolbox *toolbox, gboolean show);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);
static void photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data);
static void photo_spatial_changed(RS_PHOTO *photo, gpointer user_data);
static void photo_finalized(gpointer data, GObject *where_the_object_was);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);

static void
rs_toolbox_finalize (GObject *object)
{
	RSToolbox *toolbox = RS_TOOLBOX(object);

	gconf_client_notify_remove(toolbox->gconf, toolbox->histogram_connection);

	g_object_unref(toolbox->gconf);

	if (G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize)
		G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize (object);
}

static void
rs_toolbox_class_init (RSToolboxClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals[SNAPSHOT_CHANGED] = g_signal_new ("snapshot-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	object_class->finalize = rs_toolbox_finalize;
}

static void
rs_toolbox_init (RSToolbox *self)
{
	GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW(self);
	gint page;
	GtkWidget *label[3];
	GtkWidget *viewport;
	gint height;

	for(page=0;page<3;page++)
		self->settings[page] = NULL;

	/* Set up our scrolled window */
	gtk_scrolled_window_set_policy(scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment(scrolled_window, NULL);
	gtk_scrolled_window_set_vadjustment(scrolled_window, NULL);

	/* Get a GConfClient */
	self->gconf = gconf_client_get_default();

	/* Snapshot labels */
	label[0] = gtk_label_new(_(" A "));
	label[1] = gtk_label_new(_(" B "));
	label[2] = gtk_label_new(_(" C "));

	/* A notebook for the snapshots */
	self->notebook = gtk_notebook_new();
	g_signal_connect(self->notebook, "switch-page", G_CALLBACK(notebook_switch_page), self);

	/* A box to hold everything */
	self->toolbox = GTK_BOX(gtk_vbox_new (FALSE, 1));

	/* Iterate over 3 snapshots */
	for(page=0;page<3;page++)
		gtk_notebook_append_page(GTK_NOTEBOOK(self->notebook), new_snapshot_page(self, page), label[page]);

	gtk_box_pack_start(self->toolbox, self->notebook, FALSE, FALSE, 0);

	gtk_box_pack_start(self->toolbox, new_transform(self, TRUE), FALSE, FALSE, 0);

	/* Initialize this to some dummy image to keep it simple */
	self->histogram_dataset = rs_image16_new(1,1,4,4);

	/* Histogram */
	self->histogram = rs_histogram_new();
	if (!rs_conf_get_integer(CONF_HISTHEIGHT, &height))
		height = 70;
	gtk_widget_set_size_request(self->histogram, 64, height); /* FIXME: Get height from gconf */
	gtk_box_pack_start(self->toolbox, gui_box(_("Histogram"), self->histogram, "show_histogram", TRUE), FALSE, FALSE, 0);

	/* Watch out for gconf-changes */
	self->histogram_connection = gconf_client_notify_add(self->gconf, "/apps/" PACKAGE "/histogram_height", conf_histogram_height_changed, self, NULL, NULL);

	/* Pack everything nice with scrollers */
	viewport = gtk_viewport_new (gtk_scrolled_window_get_hadjustment (scrolled_window),
		gtk_scrolled_window_get_vadjustment (scrolled_window));
	gtk_container_add (GTK_CONTAINER (viewport), GTK_WIDGET(self->toolbox));
	gtk_container_add (GTK_CONTAINER (scrolled_window), viewport);
		
	rs_toolbox_set_selected_snapshot(self, 0);
	rs_toolbox_set_photo(self, NULL);

	self->mute_from_sliders = FALSE;
	self->mute_from_photo = FALSE;
}

static void
conf_histogram_height_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (entry->value)
	{
		gint height = gconf_value_get_int(entry->value);
		height = CLAMP(height, 30, 500);
		gtk_widget_set_size_request(toolbox->histogram, 64, height);
	}
}

static void
notebook_switch_page(GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, RSToolbox *toolbox)
{
	toolbox->selected_snapshot = page_num;

	/* Propagate event */
	g_signal_emit(toolbox, signals[SNAPSHOT_CHANGED], 0, toolbox->selected_snapshot);

	if (toolbox->photo)
		photo_settings_changed(toolbox->photo, page_num<<24|MASK_ALL, toolbox);
}

static void
basic_range_value_changed(GtkRange *range, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_sliders && toolbox->photo)
	{
		/* Remember which snapshot we belong to */
		gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-snapshot"));
		gfloat value = gtk_range_get_value(range);
		BasicSettings *basic = g_object_get_data(G_OBJECT(range), "rs-basic");
		g_object_set(toolbox->photo->settings[snapshot], basic->property_name, value, NULL);
	}

	if (toolbox->photo)
	{
		/* Always label ... What?! */
		GtkLabel *label = g_object_get_data(G_OBJECT(range), "rs-value-label");
		gui_label_set_text_printf(label, "%.2f", gtk_range_get_value(range));
	}
}

static gboolean
basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	BasicSettings *basic = g_object_get_data(G_OBJECT(user_data), "rs-basic");
	RSToolbox *toolbox = g_object_get_data(G_OBJECT(user_data), "rs-toolbox");
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(user_data), "rs-snapshot"));

	g_assert(basic != NULL);
	g_assert(RS_IS_TOOLBOX(toolbox));

	if (toolbox->photo)
		rs_object_class_property_reset(G_OBJECT(toolbox->photo->settings[snapshot]), basic->property_name);

	return TRUE;
}

static gboolean
value_label_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	GtkRange *range = GTK_RANGE(user_data);
	gdouble value = gtk_range_get_value(range);

	if (event->direction == GDK_SCROLL_UP)
		gtk_range_set_value(range, value+0.01);
	else
		gtk_range_set_value(range, value-0.01);

	return TRUE;
}

static GtkRange *
basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic)
{
	static RSSettings *settings;
	static GStaticMutex lock = G_STATIC_MUTEX_INIT;
	g_static_mutex_lock(&lock);
	if (!settings)
		settings = rs_settings_new();
	g_static_mutex_unlock(&lock);

	GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(settings), basic->property_name);
	GParamSpecFloat *fspec = G_PARAM_SPEC_FLOAT(spec);
	
	GtkWidget *label = gui_label_new_with_mouseover(g_param_spec_get_blurb(spec), _("Reset"));
	GtkWidget *seperator1 = gtk_vseparator_new();
	GtkWidget *seperator2 = gtk_vseparator_new();
	GtkWidget *scale = gtk_hscale_new_with_range(fspec->minimum, fspec->maximum, basic->step);
	GtkWidget *event = gtk_event_box_new();
	GtkWidget *value_label = gtk_label_new(NULL);

	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

	/* Set default value */
	gtk_range_set_value(GTK_RANGE(scale), fspec->default_value);
	gtk_widget_set_sensitive(scale, FALSE);

	/* Remember which snapshot we belong to */
	g_object_set_data(G_OBJECT(scale), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_object_set_data(G_OBJECT(scale), "rs-basic", (gpointer) basic);
	g_object_set_data(G_OBJECT(scale), "rs-value-label", value_label);
	g_object_set_data(G_OBJECT(scale), "rs-toolbox", toolbox);

	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
	g_signal_connect(scale, "value-changed", G_CALLBACK(basic_range_value_changed), toolbox);

	gtk_widget_set_events(label, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(label, "button_press_event", G_CALLBACK (basic_range_reset), GTK_RANGE(scale));

	gui_label_set_text_printf(GTK_LABEL(value_label), "%.2f", fspec->default_value);
	gtk_label_set_width_chars(GTK_LABEL(value_label), 5);
	gtk_widget_set_events(event, GDK_SCROLL_MASK);
	gtk_container_add(GTK_CONTAINER(event), value_label);
	g_signal_connect(event, "scroll-event", G_CALLBACK (value_label_scroll), GTK_RANGE(scale));

	gtk_table_attach(table, label,      0, 1, row, row+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator1, 1, 2, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, scale,      2, 3, row, row+1, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator2, 3, 4, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, event,      4, 5, row, row+1, GTK_SHRINK,          GTK_SHRINK, 0, 0);

	return GTK_RANGE(scale);
}

static void
curve_changed(GtkWidget *widget, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	/* Remember which snapshot we belong to */
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rs-snapshot"));

	if (toolbox->mute_from_sliders)
		return;

	/* Copy curve to photo if any */
	if (toolbox->photo)
	{
		gfloat *knots;
		guint nknots;
		toolbox->mute_from_photo = TRUE;
		rs_curve_widget_get_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), &knots, &nknots);
		rs_settings_set_curve_knots(toolbox->photo->settings[snapshot], knots, nknots);
		g_free(knots);
		toolbox->mute_from_photo = FALSE;
	}
}

/* FIXME: Move gui_box_* somewhere sane! */

static void
gui_box_toggle_callback(GtkExpander *expander, gchar *key)
{
	GConfClient *client = gconf_client_get_default();
	gboolean expanded = gtk_expander_get_expanded(expander);

	/* Save state to gconf */
	gconf_client_set_bool(client, key, expanded, NULL);
}

static void
gui_box_notify(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	GtkExpander *expander = GTK_EXPANDER(user_data);

	if (entry->value)
	{
		gboolean expanded = gconf_value_get_bool(entry->value);
		gtk_expander_set_expanded(expander, expanded);
	}
}

static GtkWidget *
gui_box(const gchar *title, GtkWidget *in, gchar *key, gboolean default_expanded)
{
	GtkWidget *expander, *label;
	gboolean expanded;

	rs_conf_get_boolean_with_default(key, &expanded, default_expanded);

	expander = gtk_expander_new(NULL);

	if (key)
	{
		GConfClient *client = gconf_client_get_default();
		gchar *name = g_build_filename("/apps", PACKAGE, key, NULL);
		g_signal_connect_after(expander, "activate", G_CALLBACK(gui_box_toggle_callback), name);
		gconf_client_notify_add(client, name, gui_box_notify, expander, NULL, NULL);
	}
	gtk_expander_set_expanded(GTK_EXPANDER(expander), expanded);

	label = gtk_label_new(title);
	gtk_expander_set_label_widget (GTK_EXPANDER (expander), label);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_container_add (GTK_CONTAINER (expander), in);
	return expander;
}

static GtkWidget *
new_snapshot_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkTable *table, *channelmixertable;
	gint row;

	table = GTK_TABLE(gtk_table_new(NBASICS, 5, FALSE));
	channelmixertable = GTK_TABLE(gtk_table_new(NCHANNELMIXER, 5, FALSE));

	/* Add basic sliders */
	for(row=0;row<NBASICS;row++)
		toolbox->ranges[snapshot][row] = basic_slider(toolbox, snapshot, table, row, &basic[row]);
	for(row=0;row<NCHANNELMIXER;row++)
		toolbox->channelmixer[snapshot][row] = basic_slider(toolbox, snapshot, channelmixertable, row, &channelmixer[row]);

	/* Add curve editor */
	toolbox->curve[snapshot] = rs_curve_widget_new();
	g_object_set_data(G_OBJECT(toolbox->curve[snapshot]), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_signal_connect(toolbox->curve[snapshot], "changed", G_CALLBACK(curve_changed), toolbox);

	/* Pack everything nice */
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Basic"), GTK_WIDGET(table), "show_basic", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Channel Mixer"), GTK_WIDGET(channelmixertable), "show_channelmixer", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Curve"), toolbox->curve[snapshot], "show_curve", TRUE), FALSE, FALSE, 0);

	return vbox;
}

static void
gui_transform_rot90_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateClockwise");
}

static void
gui_transform_rot270_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateCounterClockwise");
}

static void
gui_transform_mirror_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Mirror");
}

static void
gui_transform_flip_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Flip");
}

static GtkWidget *
new_transform(RSToolbox *toolbox, gboolean show)
{
	GtkWidget *hbox;
	GtkWidget *flip;
	GtkWidget *mirror;
	GtkWidget *rot90;
	GtkWidget *rot270;

	hbox = gtk_hbox_new(FALSE, 0);
	flip = gtk_button_new_from_stock(RS_STOCK_FLIP);
	mirror = gtk_button_new_from_stock(RS_STOCK_MIRROR);
	rot90 = gtk_button_new_from_stock(RS_STOCK_ROTATE_CLOCKWISE);
	rot270 = gtk_button_new_from_stock(RS_STOCK_ROTATE_COUNTER_CLOCKWISE);

	gui_tooltip_window(flip, _("Flip the photo over the x-axis"), NULL);
	gui_tooltip_window(mirror, _("Mirror the photo over the y-axis"), NULL);
	gui_tooltip_window(rot90, _("Rotate the photo 90 degrees clockwise"), NULL);
	gui_tooltip_window(rot270, _("Rotate the photo 90 degrees counter clockwise"), NULL);

	g_signal_connect(flip, "clicked", G_CALLBACK (gui_transform_flip_clicked), NULL);
	g_signal_connect(mirror, "clicked", G_CALLBACK (gui_transform_mirror_clicked), NULL);
	g_signal_connect(rot90, "clicked", G_CALLBACK (gui_transform_rot90_clicked), NULL);
	g_signal_connect(rot270, "clicked", G_CALLBACK (gui_transform_rot270_clicked), NULL);

	gtk_box_pack_start(GTK_BOX (hbox), flip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), mirror, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot270, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot90, FALSE, FALSE, 0);

	return gui_box(_("Transforms"), hbox, "show_transforms", show);
}

GtkWidget *
rs_toolbox_new(void)
{
	return g_object_new (RS_TYPE_TOOLBOX, NULL);
}

static void
photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data)
{
	const gint snapshot = mask>>24;
	mask &= 0x00ffffff;
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_photo)
		toolbox_copy_from_photo(toolbox, snapshot, mask, photo);

	if (mask)
		/* Update histogram */
		rs_histogram_set_settings(RS_HISTOGRAM_WIDGET(toolbox->histogram), photo->settings[toolbox->selected_snapshot]);

	if (mask ^ MASK_CURVE)
		/* Update histogram in curve editor */
		rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[snapshot]), toolbox->histogram_dataset, photo->settings[snapshot]);
}

static void
photo_spatial_changed(RS_PHOTO *photo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	g_object_unref(toolbox->histogram_dataset);

	toolbox->histogram_dataset = rs_image16_transform(photo->input, NULL,
		NULL, NULL, photo->crop, 250, 250,
		TRUE, -1.0f, photo->angle, photo->orientation, NULL);

	rs_histogram_set_image(RS_HISTOGRAM_WIDGET(toolbox->histogram), toolbox->histogram_dataset);

	/* Force update of histograms */
	photo_settings_changed(photo, MASK_ALL, toolbox);

	/* FIXME: Deal with curve */
}

static void
photo_finalized(gpointer data, GObject *where_the_object_was)
{
	gint snapshot,i;
	RSToolbox *toolbox = RS_TOOLBOX(data);

	toolbox->photo = NULL;

	/* Reset all sliders and make them insensitive */
	for(snapshot=0;snapshot<3;snapshot++)
	{
		for(i=0;i<NBASICS;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), FALSE);
		}
		for(i=0;i<NCHANNELMIXER;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), FALSE);
		}
		rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 0.0,0.0);
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 1.0,1.0);
	}
}

static void
toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo)
{
	gint i;

	if (mask)
	{
		toolbox->mute_from_sliders = TRUE;

		/* Update basic sliders */
		for(i=0;i<NBASICS;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], basic[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->ranges[snapshot][i], value);
			}

		/* Update channel mixer */
		for(i=0;i<NCHANNELMIXER;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], channelmixer[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->channelmixer[snapshot][i], value);
			}

		/* Update curve */
		if(mask & MASK_CURVE)
		{
			gfloat *knots = rs_settings_get_curve_knots(toolbox->photo->settings[snapshot]);
			gint nknots = rs_settings_get_curve_nknots(toolbox->photo->settings[snapshot]);
			rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
			rs_curve_widget_set_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), knots, nknots);
			g_free(knots);
		}
		toolbox->mute_from_sliders = FALSE;
	}
}

void
rs_toolbox_set_photo(RSToolbox *toolbox, RS_PHOTO *photo)
{
	gint snapshot;
	gint i;

	g_assert (RS_IS_TOOLBOX(toolbox));
	g_assert (RS_IS_PHOTO(photo) || (photo == NULL));

	if (toolbox->photo)
		g_object_weak_unref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);

	toolbox->photo = photo;

	toolbox->mute_from_sliders = TRUE;
	if (toolbox->photo)
	{
		g_object_weak_ref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "settings-changed", G_CALLBACK(photo_settings_changed), toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "spatial-changed", G_CALLBACK(photo_spatial_changed), toolbox);

		for(snapshot=0;snapshot<3;snapshot++)
		{
			/* Copy all settings */
			toolbox_copy_from_photo(toolbox, snapshot, MASK_ALL, toolbox->photo);

			/* Set the basic types sensitive */
			for(i=0;i<NBASICS;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), TRUE);
			for(i=0;i<NCHANNELMIXER;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), TRUE);
		}
		photo_spatial_changed(toolbox->photo, toolbox);
	}
	else
		/* This will reset everything */
		photo_finalized(toolbox, NULL);

	toolbox->mute_from_sliders = FALSE;
}

GtkWidget *
rs_toolbox_add_widget(RSToolbox *toolbox, GtkWidget *widget, const gchar *title)
{
	GtkWidget *ret = widget;

	g_assert(RS_IS_TOOLBOX(toolbox));
	g_assert(GTK_IS_WIDGET(widget));

	if (title)
	{
		ret = gtk_frame_new(title);
		gtk_container_set_border_width(GTK_CONTAINER(ret), 3);
		gtk_container_add(GTK_CONTAINER(ret), widget);
	}

	gtk_box_pack_start(toolbox->toolbox, ret, FALSE, FALSE, 1);

	return ret;
}

gint
rs_toolbox_get_selected_snapshot(RSToolbox *toolbox)
{
	g_assert(RS_IS_TOOLBOX(toolbox));

	return toolbox->selected_snapshot;
}

void
rs_toolbox_set_selected_snapshot(RSToolbox *toolbox, const gint snapshot)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(toolbox->notebook), snapshot);
}
