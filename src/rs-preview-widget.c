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
#include "rs-preview-widget.h"
#include "application.h"
#include "gtk-interface.h"
#include "gtk-helper.h"
#include "config.h"
#include "conf_interface.h"
#include "rs-photo.h"
#include "rs-actions.h"
#include "rs-toolbox.h"
#include <gettext.h>

enum {
	ROI_GRID_NONE = 0,
	ROI_GRID_GOLDEN,
	ROI_GRID_THIRDS,
	ROI_GRID_GOLDEN_TRIANGLES1,
	ROI_GRID_GOLDEN_TRIANGLES2,
	ROI_GRID_HARMONIOUS_TRIANGLES1,
	ROI_GRID_HARMONIOUS_TRIANGLES2,
};

typedef enum {
	NORMAL           = 0x000F, /* 0000 0000 0000 1111 */
	WB_PICKER        = 0x0001, /* 0000 0000 0000 0001 */

	STRAIGHTEN       = 0x0030, /* 0000 0000 0011 0000 */
	STRAIGHTEN_START = 0x0020, /* 0000 0000 0010 0000 */
	STRAIGHTEN_MOVE  = 0x0010, /* 0000 0000 0001 0000 */

	CROP             = 0x3FC0, /* 0011 1111 1100 0000 */
	CROP_START       = 0x2000, /* 0010 0000 0000 0000 */
	CROP_IDLE        = 0x1000, /* 0001 0000 0000 0000 */
	CROP_MOVE_ALL    = 0x0080, /* 0000 0000 1000 0000 */
	CROP_MOVE_CORNER = 0x0040, /* 0000 0000 0100 0000 */
	DRAW_ROI         = 0x10C0, /* 0001 0000 1100 0000 */

	MOVE             = 0x4000, /* 0100 0000 0000 0000 */
} STATE;

typedef enum {
	CROP_NEAR_INSIDE  = 0x10, /* 0001 0000 */ 
	CROP_NEAR_OUTSIDE = 0x20, /* 0010 0000 */
	CROP_NEAR_N       = 0x8,  /* 0000 1000 */
	CROP_NEAR_S       = 0x4,  /* 0000 0100 */
	CROP_NEAR_W       = 0x2,  /* 0000 0010 */
	CROP_NEAR_E       = 0x1,  /* 0000 0001 */
	CROP_NEAR_NW      = CROP_NEAR_N | CROP_NEAR_W,
	CROP_NEAR_NE      = CROP_NEAR_N | CROP_NEAR_E,
	CROP_NEAR_SE      = CROP_NEAR_S | CROP_NEAR_E,
	CROP_NEAR_SW      = CROP_NEAR_S | CROP_NEAR_W,
	CROP_NEAR_CORNER  = CROP_NEAR_N | CROP_NEAR_S | CROP_NEAR_W | CROP_NEAR_E,
	CROP_NEAR_NOTHING = CROP_NEAR_INSIDE | CROP_NEAR_OUTSIDE,
} CROP_NEAR;

typedef struct {
	gint x;
	gint y;
} COORD;

typedef enum {
	SPLIT_NONE,
	SPLIT_HORIZONTAL,
	SPLIT_VERTICAL,
} VIEW_SPLIT;

const static gint PADDING = 3;
const static gint SPLITTER_WIDTH = 4;
#define MAX_VIEWS 2 /* maximum 32! */
#define VIEW_IS_VALID(view) (((view)>=0) && ((view)<MAX_VIEWS))

static GdkCursor *cur_fleur = NULL;
static GdkCursor *cur_watch = NULL;
static GdkCursor *cur_normal = NULL;
static GdkCursor *cur_nw = NULL;
static GdkCursor *cur_ne = NULL;
static GdkCursor *cur_se = NULL;
static GdkCursor *cur_sw = NULL;
static GdkCursor *cur_busy = NULL;
static GdkCursor *cur_crop = NULL;
static GdkCursor *cur_rotate = NULL;
static GdkCursor *cur_color_picker = NULL;

struct _RSPreviewWidget
{
	GtkTable parent;
	STATE state;
    GtkAdjustment *vadjustment;
    GtkAdjustment *hadjustment;
    GtkWidget *vscrollbar;
    GtkWidget *hscrollbar;
	GtkDrawingArea *canvas;

	RSToolbox *toolbox;

	gboolean zoom_to_fit;
	gboolean exposure_mask;

	GdkColor bgcolor; /* Background color of widget */
	VIEW_SPLIT split;
	gint views;

	GtkWidget *tool;

	gfloat scale;

	/* Crop */
	RS_RECT roi;
	guint roi_grid;
	CROP_NEAR crop_near;
	gfloat crop_aspect;
	GString *crop_text;
	GtkWidget *crop_size_label;
	RS_RECT crop_move;
	COORD crop_start;

	COORD straighten_start;
	COORD straighten_end;
	gfloat straighten_angle;
	RSFilter *filter_input;

	RSFilter *filter_resample[MAX_VIEWS];
	RSFilter *filter_denoise[MAX_VIEWS];
	RSFilter *filter_cache[MAX_VIEWS];
	RSFilter *filter_render[MAX_VIEWS];
	RSFilter *filter_end[MAX_VIEWS]; /* For convenience */

	RS_PHOTO *photo;
	void *transform;
	gint snapshot[MAX_VIEWS];
	gint dirty[MAX_VIEWS]; /* Dirty flag, used for multiple things */

#if GTK_CHECK_VERSION(2,12,0)
	GtkWidget *lightsout_window;
#endif
	gboolean prev_inside_image; /* For motion and leave function*/
};

/* Define the boiler plate stuff using the predefined macro */
G_DEFINE_TYPE (RSPreviewWidget, rs_preview_widget, GTK_TYPE_TABLE);

#define SCREEN	(1<<0)
#define ALL		(0xffff)
#define DIRTY(a, b) do { (a) |= (b); } while (0)
#define UNDIRTY(a, b) do { (a) &= ~(b); } while (0)
#define ISDIRTY(a, b) (!!((a)&(b)))

enum {
	WB_PICKED,
	MOTION_SIGNAL,
	LEAVE_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static void get_max_size(RSPreviewWidget *preview, gint *width, gint *height);
static gboolean get_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement);
static void rescale(RSPreviewWidget *preview, const gint view);
static void redraw(RSPreviewWidget *preview, GdkRectangle *dirty_area);
static void realize(GtkWidget *widget, gpointer data);
static gboolean scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user_data);
static gboolean expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data);
static void size_allocate (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static void adjustment_changed(GtkAdjustment *adjustment, gpointer user_data);
static gboolean button(GtkWidget *widget, GdkEventButton *event, RSPreviewWidget *preview);
static gboolean motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data);
static gboolean leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data);
static void settings_changed(RS_PHOTO *photo, RSSettingsMask mask, RSPreviewWidget *preview);
static void filter_changed(RSFilter *filter, RSPreviewWidget *preview);
static void crop_aspect_changed(gpointer active, gpointer user_data);
static void crop_grid_changed(gpointer active, gpointer user_data);
static void crop_apply_clicked(GtkButton *button, gpointer user_data);
static void crop_cancel_clicked(GtkButton *button, gpointer user_data);
static void crop_end(RSPreviewWidget *preview, gboolean accept);
static void crop_find_size_from_aspect(RS_RECT *roi, gdouble aspect, CROP_NEAR state);
static CROP_NEAR crop_near(RS_RECT *roi, gint x, gint y);
static gboolean make_cbdata(RSPreviewWidget *preview, const gint view, RS_PREVIEW_CALLBACK_DATA *cbdata, gint screen_x, gint screen_y, gint real_x, gint real_y);

/**
 * Class initializer
 */
static void
rs_preview_widget_class_init(RSPreviewWidgetClass *klass)
{
	GtkWidgetClass *widget_class;
	GtkObjectClass *object_class;
	widget_class = GTK_WIDGET_CLASS(klass);
	object_class = GTK_OBJECT_CLASS(klass);

	signals[WB_PICKED] = g_signal_new ("wb-picked",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[MOTION_SIGNAL] = g_signal_new ("motion",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[LEAVE_SIGNAL] = g_signal_new ("leave",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
}

/**
 * Instance initialization
 */
static void
rs_preview_widget_init(RSPreviewWidget *preview)
{
	gint i;
	GtkTable *table = GTK_TABLE(preview);
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(preview));

	/* Initialize cursors */
	if (!cur_fleur) cur_fleur = gdk_cursor_new(GDK_FLEUR);
	if (!cur_watch) cur_watch = gdk_cursor_new(GDK_WATCH);
	if (!cur_nw) cur_nw = gdk_cursor_new(GDK_TOP_LEFT_CORNER);
	if (!cur_ne) cur_ne = gdk_cursor_new(GDK_TOP_RIGHT_CORNER);
	if (!cur_se) cur_se = gdk_cursor_new(GDK_BOTTOM_RIGHT_CORNER);
	if (!cur_sw) cur_sw = gdk_cursor_new(GDK_BOTTOM_LEFT_CORNER);
	if (!cur_busy) cur_busy = gdk_cursor_new(GDK_WATCH);
	if (!cur_crop) cur_crop = rs_cursor_new (display, RS_CURSOR_CROP);
	if (!cur_rotate) cur_rotate = rs_cursor_new (display, RS_CURSOR_ROTATE);
	if (!cur_color_picker) cur_color_picker = rs_cursor_new (display, RS_CURSOR_COLOR_PICKER);

	gtk_table_set_homogeneous(table, FALSE);
	gtk_table_resize (table, 2, 2);

	/* Initialize */
	preview->canvas = GTK_DRAWING_AREA(gtk_drawing_area_new());
	g_signal_connect_after (G_OBJECT (preview->canvas), "button_press_event",
		G_CALLBACK (button), preview);
	g_signal_connect_after (G_OBJECT (preview->canvas), "button_release_event",
		G_CALLBACK (button), preview);
	g_signal_connect (G_OBJECT (preview->canvas), "motion_notify_event",
		G_CALLBACK (motion), preview);
	g_signal_connect (G_OBJECT (preview->canvas), "leave_notify_event",
		G_CALLBACK (leave), preview);

	/* Let us know about pointer movements */
	gtk_widget_set_events(GTK_WIDGET(preview->canvas), 0
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_LEAVE_NOTIFY_MASK);

	preview->state = WB_PICKER;
	preview->split = SPLIT_VERTICAL;
	preview->views = 1;
	preview->zoom_to_fit = TRUE;
	preview->exposure_mask = FALSE;
	preview->crop_near = CROP_NEAR_NOTHING;

	preview->vadjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0));
	preview->hadjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0));
	g_signal_connect(G_OBJECT(preview->vadjustment), "value-changed", G_CALLBACK(adjustment_changed), preview);
	g_signal_connect(G_OBJECT(preview->hadjustment), "value-changed", G_CALLBACK(adjustment_changed), preview);
	preview->vscrollbar = gtk_vscrollbar_new(preview->vadjustment);
	preview->hscrollbar = gtk_hscrollbar_new(preview->hadjustment);

	gtk_table_attach(table, GTK_WIDGET(preview->canvas), 0, 1, 0, 1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(table, preview->vscrollbar, 1, 2, 0, 1, GTK_SHRINK, GTK_EXPAND|GTK_FILL, 0, 0);
    gtk_table_attach(table, preview->hscrollbar, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);

	preview->filter_input = NULL;
	for(i=0;i<MAX_VIEWS;i++)
	{
		preview->filter_resample[i] = rs_filter_new("RSResample", NULL);
		preview->filter_cache[i] = rs_filter_new("RSCache", preview->filter_resample[i]);
		preview->filter_denoise[i] = rs_filter_new("RSDenoise", preview->filter_cache[i]);
		preview->filter_cache[i] = rs_filter_new("RSCache", preview->filter_denoise[i]);
		preview->filter_render[i] = rs_filter_new("RSBasicRender", preview->filter_cache[i]);
		preview->filter_cache[i] = rs_filter_new("RSCache", preview->filter_render[i]);
		preview->filter_end[i] = preview->filter_cache[i];
		g_signal_connect(preview->filter_end[i], "changed", G_CALLBACK(filter_changed), preview);

#if MAX_VIEWS > 3
#error Fix line below
#endif
		preview->snapshot[i] = i;
		DIRTY(preview->dirty[i], ALL);
	}
	preview->photo = NULL;
	preview->scale = 1.0;

	/* We'll take care of double buffering ourself */
	gtk_widget_set_double_buffered(GTK_WIDGET(preview), FALSE);

	g_signal_connect(G_OBJECT(preview->canvas), "expose-event", G_CALLBACK(expose), preview);
	g_signal_connect(G_OBJECT(preview->canvas), "size-allocate", G_CALLBACK(size_allocate), preview);
	g_signal_connect(G_OBJECT(preview), "realize", G_CALLBACK(realize), NULL);
	g_signal_connect(G_OBJECT(preview->canvas), "scroll_event", G_CALLBACK (scroll), preview);

#if GTK_CHECK_VERSION(2,12,0)
	preview->lightsout_window = NULL;
#endif
	preview->prev_inside_image = FALSE;
}

/**
 * Creates a new RSPreviewWidget
 * @return A new RSPreviewWidget
 */
GtkWidget *
rs_preview_widget_new(GtkWidget *toolbox)
{
	GtkWidget *widget;
	RSPreviewWidget *preview;
	widget = g_object_new (RS_PREVIEW_TYPE_WIDGET, NULL);

	preview = RS_PREVIEW_WIDGET(widget);
	preview->toolbox = RS_TOOLBOX(toolbox);

	return widget;
}

/**
 * Sets the zoom level of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param zoom New zoom level (0.0 - 2.0)
 */
void
rs_preview_widget_set_zoom(RSPreviewWidget *preview, gdouble zoom)
{
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (preview->zoom_to_fit == FALSE)
		return;

	preview->zoom_to_fit = FALSE;

	/* Unsplit if needed */
	if (preview->views > 1)
		rs_core_action_group_activate("Split");
	for(view=0;view<preview->views;view++)
		rescale(preview, view);

	gtk_widget_show(preview->vscrollbar);
	gtk_widget_show(preview->hscrollbar);
}

/**
 * Select zoom-to-fit of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_set_zoom_to_fit(RSPreviewWidget *preview)
{
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	preview->zoom_to_fit = TRUE;
	for(view=0;view<preview->views;view++)
		rescale(preview, view);

	gtk_widget_hide(preview->vscrollbar);
	gtk_widget_hide(preview->hscrollbar);
}
/**
 * Sets active photo of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param photo A RS_PHOTO
 */
void
rs_preview_widget_set_photo(RSPreviewWidget *preview, RS_PHOTO *photo)
{
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	preview->photo = photo;

	/* Mark everything as dirty */
	for(view=0;view<preview->views;view++)
		DIRTY(preview->dirty[view], ALL);

	if (preview->state & CROP)
		crop_end(preview, FALSE);
	if (preview->state & STRAIGHTEN)
		preview->state = WB_PICKER;

	if (preview->photo)
	{
		g_signal_connect(G_OBJECT(preview->photo), "settings-changed", G_CALLBACK(settings_changed), preview);

		for(view=0;view<MAX_VIEWS;view++)
			g_object_set(preview->filter_render[view], "settings", preview->photo->settings[preview->snapshot[view]], NULL);

		for(view=0;view<preview->views;view++)
		{
			g_object_set(preview->filter_denoise[view], "sharpen", (gint) (preview->scale * preview->photo->settings[preview->snapshot[view]]->sharpen), NULL);
			rescale(preview, view);
		}
	}
}

/**
 * Set input filter for a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param filter A filter to listen for
 */
void
rs_preview_widget_set_filter(RSPreviewWidget *preview, RSFilter *filter)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(RS_IS_FILTER(filter));

	preview->filter_input = filter;
	rs_filter_set_previous(preview->filter_resample[0], preview->filter_input);
	rs_filter_set_previous(preview->filter_resample[1], preview->filter_input);
	rescale(preview, 0);
}

/**
 * Sets the CMS profile used in preview
 * @param preview A RSPreviewWidget
 * @param profile The profile to use
 */
void
rs_preview_widget_set_profile(RSPreviewWidget *preview, RSIccProfile *profile)
{
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(RS_IS_ICC_PROFILE(profile));

	for(view=0;view<MAX_VIEWS;view++)
		g_object_set(preview->filter_render[view], "icc-profile", profile, NULL);
}

/**
 * Sets the background color of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param color The new background color
 */
void
rs_preview_widget_set_bgcolor(RSPreviewWidget *preview, GdkColor *color)
{
	GdkRectangle rect;
	g_return_if_fail (RS_IS_PREVIEW_WIDGET(preview));
	g_return_if_fail (color != NULL);

	preview->bgcolor = *color;
	gtk_widget_modify_bg(GTK_WIDGET(preview->canvas), GTK_STATE_NORMAL, &preview->bgcolor);

	if (GTK_WIDGET_REALIZED(GTK_WIDGET(preview->canvas)))
	{
		rect.x = 0;
		rect.y = 0;
		rect.width = GTK_WIDGET(preview->canvas)->allocation.width;
		rect.height = GTK_WIDGET(preview->canvas)->allocation.height;
		redraw(preview, &rect);
	}
}

/**
 * Enables or disables split-view
 * @param preview A RSPreviewWidget
 * @param split_screen Enables split-view if TRUE, disables if FALSE
 */
void
rs_preview_widget_set_split(RSPreviewWidget *preview, gboolean split_screen)
{
	gint view;
	GdkRectangle rect;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (split_screen)
	{
		preview->split = SPLIT_VERTICAL;
		preview->views = 2;
		rs_preview_widget_set_zoom_to_fit(preview);
	}
	else
	{
		preview->split = SPLIT_NONE;
		preview->views = 1;
	}

	for(view=0;view<preview->views;view++)
		rescale(preview, view);

	rect.x = 0;
	rect.y = 0;
	rect.width = GTK_WIDGET(preview)->allocation.width;
	rect.height = GTK_WIDGET(preview)->allocation.height;
	redraw(preview, &rect);
}

#if GTK_CHECK_VERSION(2,12,0)
static gboolean
lightsout_window_on_expose(GtkWidget *widget, GdkEventExpose *do_not_use_this, RSPreviewWidget *preview)
{
	gint view;
	gint x, y;
	gint width, height;
	cairo_t* cairo_context = NULL;

	cairo_context = gdk_cairo_create (widget->window);
	if (!cairo_context)
		return FALSE;

	gtk_window_get_size(GTK_WINDOW(widget), &width, &height);

	cairo_set_source_rgba (cairo_context, 0.0f, 0.0f, 0.0f, 0.8f);
	cairo_set_operator (cairo_context, CAIRO_OPERATOR_SOURCE);
	cairo_paint (cairo_context);

	/* Paint the images with alpha=0 */
	for(view=0;view<preview->views;view++)
	{
		GdkRectangle rect;
		get_placement(preview, view, &rect);

		gdk_window_get_origin(GTK_WIDGET(preview->canvas)->window, &x, &y);
		cairo_set_source_rgba(cairo_context, 0.0, 0.0, 0.0, 0.0);
		cairo_rectangle (cairo_context, x+rect.x, y+rect.y, rect.width, rect.height);
		cairo_fill (cairo_context);
	}

	cairo_destroy (cairo_context);

	/* Set opacity to 100% when we're done drawing */
	gtk_window_set_opacity(GTK_WINDOW(widget), 1.0);

	return FALSE;
}
#endif

#if GTK_CHECK_VERSION(2,12,0)
/**
 * Enables or disables lights out mode
 * @param preview A RSPreviewWidget
 * @param lightsout Enables lights out mode if TRUE, disables if FALSE
 */
void
rs_preview_widget_set_lightsout(RSPreviewWidget *preview, gboolean lightsout)
{
	/* FIXME: Make this follow the loaded image(s) somehow */
	if (lightsout && !preview->lightsout_window)
	{
		GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(preview->canvas));
		gint width = gdk_screen_get_width(screen);
		gint height = gdk_screen_get_height(screen);
		GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		GdkColormap* colormap = gdk_screen_get_rgba_colormap(screen);

		/* Check if the system even supports composite - and bail out if needed */
		if (!colormap || !gdk_display_supports_composite(gdk_display_get_default()))
		{
			GtkWidget *dialog = gui_dialog_make_from_text(
				GTK_STOCK_DIALOG_ERROR,
				_("Light out mode not available"),
				_("Your setup doesn't seem to support RGBA visuals and/or compositing. Consult your operating system manual for enabling RGBA visuals and compositing.")
			);
			
			GtkWidget *button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
			gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_ACCEPT);

            gtk_widget_show_all(dialog);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
			return;
		}

		/* Set our colormap to the RGBA colormap */
		gtk_widget_set_colormap(window, colormap);

		/* Cover whole screen */
		gtk_window_resize(GTK_WINDOW(window), width, height);
		gtk_window_move(GTK_WINDOW(window), 0, 0);

		/* Set the input shape to a rectangle covering everything with alpha=0,
		 * to let everything pass through */
		GdkPixmap *bitmap = (GdkBitmap*) gdk_pixmap_new (NULL, width, height, 1);
		cairo_t *cairo_context = gdk_cairo_create (bitmap);

		cairo_scale (cairo_context, (double) width, (double) height);
		cairo_set_source_rgba (cairo_context, 1.0f, 1.0f, 1.0f, 0.0f);
		cairo_set_operator (cairo_context, CAIRO_OPERATOR_SOURCE);
		cairo_paint (cairo_context);
		cairo_destroy (cairo_context);

		gtk_widget_input_shape_combine_mask (GTK_WIDGET(window), NULL, 0, 0);
		gtk_widget_input_shape_combine_mask (GTK_WIDGET(window), bitmap, 0, 0);
		g_object_unref(bitmap);

		g_signal_connect (G_OBJECT (window), "expose-event", G_CALLBACK(lightsout_window_on_expose), preview);

		gtk_widget_set_app_paintable (window, TRUE);
		gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
		gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
		gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
#if GTK_CHECK_VERSION(2,10,0)
		gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
#endif
		gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
		gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
		gtk_window_set_title(GTK_WINDOW(window), "Rawstudio lights out helper");

		/* Let the window be completely transparent for now to avoid initial flicker */
		gtk_window_set_opacity(GTK_WINDOW(window), 0.0);
		gtk_widget_show_all(window);
		preview->lightsout_window = window;
	}
	else if (!lightsout && preview->lightsout_window)
	{
		gtk_widget_destroy(preview->lightsout_window);
		preview->lightsout_window = NULL;
	}
}
#endif

/**
 * Sets the active snapshot of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param view Which view to set (0..1)
 * @param snapshot Which snapshot to view (0..2)
 */
void
rs_preview_widget_set_snapshot(RSPreviewWidget *preview, const guint view, const gint snapshot)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(VIEW_IS_VALID(view));

	if (preview->snapshot[view] == snapshot)
		return;

	preview->snapshot[view] = snapshot;

	if (!preview->photo)
		return;

	g_object_set(preview->filter_denoise[view], "sharpen", (gint) preview->photo->settings[preview->snapshot[view]]->sharpen, NULL);

	DIRTY(preview->dirty[view], SCREEN);
	rs_preview_widget_update(preview, TRUE);
}

/**
 * Enables or disables the exposure mask
 * @param preview A RSPreviewWidget
 * @param show_exposure_mask Set to TRUE to enabled
 */
void
rs_preview_widget_set_show_exposure_mask(RSPreviewWidget *preview, gboolean show_exposure_mask)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (preview->exposure_mask != show_exposure_mask)
	{
		gint view;
		preview->exposure_mask = show_exposure_mask;
		for(view=0;view<preview->views;view++)
			/* FIXME: Port to RSFilter */
//			g_object_set(preview->filter_mask, "exposure", preview->exposure_mask, NULL);
			DIRTY(preview->dirty[view], SCREEN);
		rs_preview_widget_update(preview, FALSE);
	}
}


/**
 * Gets the status of whether the exposure mask is displayed
 * @param preview A RSPreviewWidget
 * @return TRUE is exposure mask is displayed, FALSE otherwise
 */
gboolean
rs_preview_widget_get_show_exposure_mask(RSPreviewWidget *preview, gboolean show_exposure_mask)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	return preview->exposure_mask;
}

/**
 * Tells the preview widget to update itself
 * @param preview A RSPreviewWidget
 * @param full_redraw Set to TRUE to redraw everything, FALSE to only redraw the image.
 */
void
rs_preview_widget_update(RSPreviewWidget *preview, gboolean full_redraw)
{
	GdkRectangle rect;
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (rs_filter_get_width(preview->filter_input) < 0)
		return;

	if (!GTK_WIDGET_DRAWABLE(GTK_WIDGET(preview)))
		return;

	if (full_redraw)
	{
		rect.x = 0;
		rect.y = 0;
		rect.width = GTK_WIDGET(preview->canvas)->allocation.width;
		rect.height = GTK_WIDGET(preview->canvas)->allocation.height;
		if (preview->zoom_to_fit)
		{
			redraw(preview, &rect);
			for(view=0;view<preview->views;view++)
				UNDIRTY(preview->dirty[view], SCREEN);
		}
		else
		{
			/* Construct full rectangle */
			rect.x = 0;
			rect.y = 0;
			rect.width = GTK_WIDGET(preview->canvas)->allocation.width;
			rect.height = GTK_WIDGET(preview->canvas)->allocation.height;
			redraw(preview, &rect);
			for(view=0;view<preview->views;view++)
				UNDIRTY(preview->dirty[view], SCREEN);
		}
	}
	else
	{
		for(view=0;view<preview->views;view++)
		{
			if (ISDIRTY(preview->dirty[view], SCREEN))
			{
				get_placement(preview, view, &rect);
				if (preview->zoom_to_fit)
				{
					redraw(preview, &rect);
					UNDIRTY(preview->dirty[view], SCREEN);
				}
				else
				{
					/* Construct full rectangle */
					rect.x = 0;
					rect.y = 0;
					rect.width = GTK_WIDGET(preview->canvas)->allocation.width;
					rect.height = GTK_WIDGET(preview->canvas)->allocation.height;
					redraw(preview, &rect);
					UNDIRTY(preview->dirty[view], SCREEN);
				}
			}
		}
	}
}

/**
 * Puts a RSPreviewWidget in crop-mode
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_crop_start(RSPreviewWidget *preview)
{
	GtkWidget *vbox;
	GtkWidget *roi_size_hbox;
	GtkWidget *label;
	GtkWidget *roi_grid_hbox;
	GtkWidget *roi_grid_label;
	GtkWidget *roi_grid_combobox;
	GtkWidget *aspect_hbox;
	GtkWidget *aspect_label;
	GtkWidget *button_box;
	GtkWidget *apply_button;
	GtkWidget *cancel_button;
	RS_CONFBOX *grid_confbox;
	RS_CONFBOX *aspect_confbox;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!(preview->state & NORMAL))
		return;

	/* predefined aspects */
	/* aspect MUST be => 1.0 */
	const static gdouble aspect_freeform = 0.0f;
	const static gdouble aspect_32 = 3.0f/2.0f;
	const static gdouble aspect_43 = 4.0f/3.0f;
	const static gdouble aspect_1008 = 10.0f/8.0f;
	const static gdouble aspect_1610 = 16.0f/10.0f;
	const static gdouble aspect_83 = 8.0f/3.0f;
	const static gdouble aspect_11 = 1.0f;
	static gdouble aspect_iso216;
	static gdouble aspect_golden;
	aspect_iso216 = sqrt(2.0f);
	aspect_golden = (1.0f+sqrt(5.0f))/2.0f;

	vbox = gtk_vbox_new(FALSE, 4);

	label = gtk_label_new(_("Size"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

	roi_size_hbox = gtk_hbox_new(FALSE, 0);

	/* Default aspect (freeform) */
	preview->crop_aspect = 0.0f;

	preview->crop_size_label = gtk_label_new(_("-"));
	gtk_box_pack_start (GTK_BOX (roi_size_hbox), label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (roi_size_hbox), preview->crop_size_label, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (vbox), roi_size_hbox, FALSE, TRUE, 0);

	roi_grid_hbox = gtk_hbox_new(FALSE, 0);
	roi_grid_label = gtk_label_new(_("Grid"));
	gtk_misc_set_alignment(GTK_MISC(roi_grid_label), 0.0, 0.5);

	grid_confbox = gui_confbox_new(CONF_ROI_GRID);
	gui_confbox_set_callback(grid_confbox, preview, crop_grid_changed);
	gui_confbox_add_entry(grid_confbox, "none", _("None"), (gpointer) ROI_GRID_NONE);
	gui_confbox_add_entry(grid_confbox, "goldensections", _("Golden sections"), (gpointer) ROI_GRID_GOLDEN);
	gui_confbox_add_entry(grid_confbox, "ruleofthirds", _("Rule of thirds"), (gpointer) ROI_GRID_THIRDS);
	gui_confbox_add_entry(grid_confbox, "goldentriangles1", _("Golden triangles #1"), (gpointer) ROI_GRID_GOLDEN_TRIANGLES1);
	gui_confbox_add_entry(grid_confbox, "goldentriangles2", _("Golden triangles #2"), (gpointer) ROI_GRID_GOLDEN_TRIANGLES2);
	gui_confbox_add_entry(grid_confbox, "harmonioustriangles1", _("Harmonious triangles #1"), (gpointer) ROI_GRID_HARMONIOUS_TRIANGLES1);
	gui_confbox_add_entry(grid_confbox, "harmonioustriangles2", _("Harmonious triangles #2"), (gpointer) ROI_GRID_HARMONIOUS_TRIANGLES2);
	gui_confbox_load_conf(grid_confbox, "none");

	roi_grid_combobox = gui_confbox_get_widget(grid_confbox);

	gtk_box_pack_start (GTK_BOX (roi_grid_hbox), roi_grid_label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (roi_grid_hbox), roi_grid_combobox, FALSE, TRUE, 4);

	aspect_hbox = gtk_hbox_new(FALSE, 0);
	aspect_label = gtk_label_new(_("Aspect"));
	gtk_misc_set_alignment(GTK_MISC(aspect_label), 0.0, 0.5);

	aspect_confbox = gui_confbox_new(CONF_CROP_ASPECT);
	gui_confbox_set_callback(aspect_confbox, preview, crop_aspect_changed);
	gui_confbox_add_entry(aspect_confbox, "freeform", _("Freeform"), (gpointer) &aspect_freeform);
	gui_confbox_add_entry(aspect_confbox, "iso216", _("ISO paper (A4)"), (gpointer) &aspect_iso216);
	gui_confbox_add_entry(aspect_confbox, "3:2", _("3:2 (35mm)"), (gpointer) &aspect_32);
	gui_confbox_add_entry(aspect_confbox, "4:3", _("4:3"), (gpointer) &aspect_43);
	gui_confbox_add_entry(aspect_confbox, "10:8", _("10:8 (SXGA)"), (gpointer) &aspect_1008);
	gui_confbox_add_entry(aspect_confbox, "16:10", _("16:10 (Wide XGA)"), (gpointer) &aspect_1610);
	gui_confbox_add_entry(aspect_confbox, "8:3", _("8:3 (Dualhead XGA)"), (gpointer) &aspect_83);
	gui_confbox_add_entry(aspect_confbox, "1:1", _("1:1"), (gpointer) &aspect_11);
	gui_confbox_add_entry(aspect_confbox, "goldenrectangle", _("Golden rectangle"), (gpointer) &aspect_golden);
	gui_confbox_load_conf(aspect_confbox, "freeform");

	gtk_box_pack_start (GTK_BOX (aspect_hbox), aspect_label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (aspect_hbox),
		gui_confbox_get_widget(aspect_confbox), FALSE, TRUE, 4);

	button_box = gtk_hbox_new(FALSE, 0);
	apply_button = gtk_button_new_with_label(_("Crop"));
	g_signal_connect (G_OBJECT(apply_button), "clicked", G_CALLBACK (crop_apply_clicked), preview);
	cancel_button = gtk_button_new_with_label(_("Don't crop"));
	g_signal_connect (G_OBJECT(cancel_button), "clicked", G_CALLBACK (crop_cancel_clicked), preview);
	gtk_box_pack_start (GTK_BOX (button_box), apply_button, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (button_box), cancel_button, TRUE, TRUE, 4);

	gtk_box_pack_start (GTK_BOX (vbox), roi_grid_hbox, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), aspect_hbox, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, TRUE, 0);

	preview->tool = rs_toolbox_add_widget(preview->toolbox, vbox, _("Crop"));
	gtk_widget_show_all(preview->tool);

	if (preview->photo->crop)
	{
		preview->roi = *preview->photo->crop;
		rs_photo_set_crop(preview->photo, NULL);
		preview->state = CROP_IDLE;
		rs_preview_widget_update(preview, TRUE);
	}
	else
		preview->state = CROP_START;
}

/**
 * Removes crop from the loaded photo
 * @param preview A RSpreviewWidget
 */
void
rs_preview_widget_uncrop(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!preview->photo) return;

	rs_photo_set_crop(preview->photo, NULL);
}

/**
 * Puts a RSPreviewWidget in straighten-mode
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_straighten(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!(preview->state & NORMAL))
		return;

	preview->state = STRAIGHTEN_START;
}

/**
 * Removes straighten from the loaded photo
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_unstraighten(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	rs_photo_set_angle(preview->photo, 0.0, FALSE);
}

static void
get_max_size(RSPreviewWidget *preview, gint *width, gint *height)
{
	gint splitters = preview->views - 1; /* Splitters between the views */

	*width = GTK_WIDGET(preview)->allocation.width - PADDING*2;
	*height = GTK_WIDGET(preview)->allocation.height - PADDING*2;

	if (preview->split == SPLIT_VERTICAL)
		*width = (GTK_WIDGET(preview)->allocation.width - splitters*SPLITTER_WIDTH)/preview->views - PADDING*2;

	if (preview->split == SPLIT_HORIZONTAL)
		*height = (GTK_WIDGET(preview)->allocation.height - splitters*SPLITTER_WIDTH)/preview->views - PADDING*2;
}

static gint
get_view_from_coord(RSPreviewWidget *preview, const gint x, const gint y)
{
	gint view;

	if (preview->split == SPLIT_VERTICAL)
		view = preview->views*x/GTK_WIDGET(preview)->allocation.width;
	else
		view = preview->views*y/GTK_WIDGET(preview)->allocation.height;

	if (view>MAX_VIEWS)
		view=MAX_VIEWS;

	/* Clamp */
	view = MAX(MIN(view, MAX_VIEWS), 0);

	return view;
}

static void
get_canvas_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement)
{
	gint xoffset = 0, yoffset = 0;
	gint width, height;

	g_assert(VIEW_IS_VALID(view));
	g_assert(placement);

	if (preview->split == SPLIT_VERTICAL)
	{
		xoffset = view * (GTK_WIDGET(preview)->allocation.width/preview->views + SPLITTER_WIDTH/2);
		width = (GTK_WIDGET(preview)->allocation.width - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	if (preview->split == SPLIT_HORIZONTAL)
	{
		yoffset = view * (GTK_WIDGET(preview)->allocation.height/preview->views + SPLITTER_WIDTH/2);
		height = (GTK_WIDGET(preview)->allocation.height - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	placement->x = xoffset;
	placement->y = yoffset;
	placement->width = width;
	placement->height = height;
}

static gboolean
get_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement)
{
	gint xoffset = 0, yoffset = 0;
	gint width, height;

	if (rs_filter_get_width(preview->filter_end[view])<1)
		return FALSE;
	if (!VIEW_IS_VALID(view))
		return FALSE;

	width = GTK_WIDGET(preview)->allocation.width;
	height = GTK_WIDGET(preview)->allocation.height;

	if (preview->split == SPLIT_VERTICAL)
	{
		xoffset = view * (GTK_WIDGET(preview)->allocation.width/preview->views + SPLITTER_WIDTH/2);
		width = (width - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	if (preview->split == SPLIT_HORIZONTAL)
	{
		yoffset = view * (GTK_WIDGET(preview)->allocation.height/preview->views + SPLITTER_WIDTH/2);
		height = (height - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	placement->x = xoffset + (width - rs_filter_get_width(preview->filter_end[view]))/2;
	placement->y = yoffset + (height - rs_filter_get_height(preview->filter_end[view]))/2;
	placement->width = rs_filter_get_width(preview->filter_end[view]);
	placement->height = rs_filter_get_height(preview->filter_end[view]);
	return TRUE;
}

/**
 * Get the image coordinates from canvas-coordinates
 * @note Output will be clamped to image-space - ie all values are valid
 * @param preview A RSPreviewWidget
 * @param view The current view
 * @param x X coordinate as returned by GDK
 * @param y Y coordinate as returned by GDK
 * @param scaled_x A pointer to the scaled x or NULL
 * @param scaled_y A pointer to the scaled x or NULL
 * @param real_x A pointer to the "real" x (scale at 100%) or NULL
 * @param real_y A pointer to the "real" y (scale at 100%) or NULL
 * @param max_w A pointer to the width of the image at 100% scale or NULL
 * @param max_h A pointer to the height of the image at 100% scale or NULL
 * @return TRUE if coordinate is inside image, FALSE otherwise
 */
static gboolean
get_image_coord(RSPreviewWidget *preview, gint view, const gint x, const gint y, gint *scaled_x, gint *scaled_y, gint *real_x, gint *real_y, gint *max_w, gint *max_h)
{
	gboolean ret = FALSE;
	GdkRectangle placement;
	gint _scaled_x, _scaled_y;
	gint _real_x, _real_y;
	gint _max_w, _max_h;

	/* FIXME: This is so outdated */
	if (!preview->photo)
		return ret;

	if (!rs_filter_get_width(preview->filter_end[view])<0)
		return ret;

	rs_image16_transform_getwh(preview->photo->input, preview->photo->crop, preview->photo->angle, preview->photo->orientation, &_max_w, &_max_h);

	get_placement(preview, view, &placement);

	if (preview->zoom_to_fit)
	{
		_scaled_x = x - placement.x;
		_scaled_y = y - placement.y;
		_real_x = _scaled_x / preview->scale;
		_real_y = _scaled_y / preview->scale;
	}
	else
	{
		_scaled_x = x + gtk_adjustment_get_value(preview->hadjustment);
		_scaled_y = y + gtk_adjustment_get_value(preview->vadjustment);
		_real_x = _scaled_x;
		_real_y = _scaled_y;
	}

	if ((_scaled_x < rs_filter_get_width(preview->filter_end[view])) && (_scaled_y < rs_filter_get_height(preview->filter_end[view])) && (_scaled_x >= 0) && (_scaled_y >= 0))
		ret = TRUE;

	if (scaled_x)
		*scaled_x = MIN(MAX(0, _scaled_x), rs_filter_get_width(preview->filter_end[view]));
	if (scaled_y)
		*scaled_y = MIN(MAX(0, _scaled_y), rs_filter_get_height(preview->filter_end[view]));
	if (real_x)
		*real_x = MIN(MAX(0, _real_x), _max_w);
	if (real_y)
		*real_y = MIN(MAX(0, _real_y), _max_h);
	if (max_w)
		*max_w = _max_w;
	if (max_h)
		*max_h = _max_h;

	return ret;
}

static void
rescale(RSPreviewWidget *preview, const gint view)
{
	gint max_width, max_height;
	gint width, height;

	/* FIXME: This is outdated */

	g_return_if_fail(VIEW_IS_VALID(view));
	if (!GTK_WIDGET_REALIZED(GTK_WIDGET(preview)))
		return;

	get_max_size(preview, &max_width, &max_height);
	width = rs_filter_get_width(preview->filter_input);
	height = rs_filter_get_height(preview->filter_input);

	if (width < 0)
		return;

	width = MIN(width, max_width);
	height = MIN(height, max_height);

	preview->scale = ((gfloat) width) / ((gfloat) rs_filter_get_width(preview->filter_input));
	preview->scale = MIN(preview->scale, ((gfloat) height) / ((gfloat) rs_filter_get_height(preview->filter_input)));
	width = preview->scale * rs_filter_get_width(preview->filter_input);
	height = preview->scale * rs_filter_get_height(preview->filter_input);
	if (preview->zoom_to_fit)
		g_object_set(preview->filter_resample[view],
			"width", width,
			"height", height,
			NULL);
	else
	{
		gdk_window_set_cursor(GTK_WIDGET(rawstudio_window)->window, cur_busy);
		GUI_CATCHUP();
		gdouble upper;
		g_object_set(preview->filter_resample[view],
			"width", rs_filter_get_width(preview->filter_input),
			"height", rs_filter_get_height(preview->filter_input),
			NULL);
		preview->scale = 1.0;
		gdk_window_set_cursor(GTK_WIDGET(rawstudio_window)->window, NULL);

		/* Update scrollbars to reflect the change */
		upper = (gdouble) rs_filter_get_width(preview->filter_end[view]);
		g_object_set(G_OBJECT(preview->hadjustment), "upper", upper, NULL);
		upper = (gdouble) rs_filter_get_height(preview->filter_end[view]);
		g_object_set(G_OBJECT(preview->vadjustment), "upper", upper, NULL);
	}
}

static void
redraw(RSPreviewWidget *preview, GdkRectangle *dirty_area)
{
	GdkRectangle area;
	GdkRectangle placement;
	GtkWidget *widget = GTK_WIDGET(preview->canvas);
	GdkWindow *window = widget->window;
	GdkDrawable *drawable = GDK_DRAWABLE(window);
	GdkGC *gc = gdk_gc_new(drawable);
	gint i;
	cairo_t *cr;
	const static gdouble dashes[] = { 4.0, 4.0, };

#define CAIRO_LINE(cr, x1, y1, x2, y2) do { \
	cairo_move_to((cr), (x1), (y1)); \
	cairo_line_to((cr), (x2), (y2)); } while (0);

	gdk_window_begin_paint_rect(window, dirty_area);

	/* Prepare for drawing snapshot-identifier */
	cr = gdk_cairo_create(drawable);

	/* Clip Cairo to dirty area */
    cairo_new_path(cr);
    cairo_rectangle(cr, dirty_area->x, dirty_area->y, dirty_area->width, dirty_area->height);
	cairo_clip(cr);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);

	for(i=0;i<preview->views;i++)
	{
		GdkPixbuf *buffer = rs_filter_get_image8(preview->filter_end[i]);
		if (!buffer)
			break;

		if (preview->zoom_to_fit)
			get_placement(preview, i, &placement);
		else
		{
			if (gdk_pixbuf_get_width(buffer) > GTK_WIDGET(preview->canvas)->allocation.width)
				placement.x = -gtk_adjustment_get_value(preview->hadjustment);
			else
				placement.x = ((GTK_WIDGET(preview->canvas)->allocation.width)-gdk_pixbuf_get_width(buffer))/2;

			if (gdk_pixbuf_get_height(buffer) > GTK_WIDGET(preview->canvas)->allocation.height)
				placement.y = -gtk_adjustment_get_value(preview->vadjustment);
			else
				placement.y = ((GTK_WIDGET(preview->canvas)->allocation.height)-gdk_pixbuf_get_height(buffer))/2;

			placement.width = gdk_pixbuf_get_width(buffer);
			placement.height = gdk_pixbuf_get_height(buffer);
		}

		/* Render the photo itself */
		if (gdk_rectangle_intersect(dirty_area, &placement, &area))
		{
			if (area.x-placement.x >= 0 && area.x-placement.x + area.width <= gdk_pixbuf_get_width(buffer)
				&& area.y-placement.y >= 0 && area.y-placement.y + area.height <= gdk_pixbuf_get_height(buffer))
				gdk_draw_pixbuf(drawable, gc,
					buffer,
					area.x-placement.x,
					area.y-placement.y,
					area.x, area.y,
					area.width, area.height,
					GDK_RGB_DITHER_NONE, 0, 0);
		}

		if (preview->state & DRAW_ROI)
		{
			gchar *text;
			cairo_text_extents_t te;

			gint x1,y1,x2,y2;
			/* Translate to screen coordinates */
			x1 = preview->roi.x1*preview->scale;
			y1 = preview->roi.y1*preview->scale;
			x2 = preview->roi.x2*preview->scale;
			y2 = preview->roi.y2*preview->scale;

			text = g_strdup_printf("%d x %d", preview->roi.x2-preview->roi.x1, preview->roi.y2-preview->roi.y1);

			/* creates a rectangle that matches the photo */
			gdk_cairo_rectangle(cr, &placement);

			/* Translate to photo coordinates */
			cairo_translate(cr, placement.x, placement.y);

			/* creates a rectangle that matches ROI */
			cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
			/* create fill rule that only fills between the two rectangles */
			cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
			/* fill acording to rule */
			cairo_fill_preserve (cr);
			/* center rectangle */
			cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
			cairo_stroke (cr);

			cairo_set_line_width(cr, 2.0);

			cairo_set_dash(cr, dashes, 0, 0.0);
			cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
			cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
			cairo_stroke(cr);

			cairo_set_line_width(cr, 1.0);
			cairo_set_dash(cr, dashes, 2, 0.0);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);

			/* Print size below rectangle */
			cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cr, 12.0);
    		cairo_text_extents (cr, text, &te);
			if (y2 > (placement.height-18))
				cairo_move_to(cr, (x2+x1)/2.0 - te.width/2.0, y2-5.0);
			else
				cairo_move_to(cr, (x2+x1)/2.0 - te.width/2.0, y2+14.0);
			cairo_show_text (cr, text);

			switch(preview->roi_grid)
			{
				case ROI_GRID_NONE:
					break;
				case ROI_GRID_GOLDEN:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					/* vertical */
					golden = ((x2-x1)/goldenratio);

					t = (x1+golden);
					CAIRO_LINE(cr, t, y1, t, y2);
					t = (x2-golden);
					CAIRO_LINE(cr, t, y1, t, y2);

					/* horizontal */
					golden = ((y2-y1)/goldenratio);

					t = (y1+golden);
					CAIRO_LINE(cr, x1, t, x2, t);
					t = (y2-golden);
					CAIRO_LINE(cr, x1, t, x2, t);
					break;
				}
				case ROI_GRID_THIRDS:
				{
					gint t;

					/* vertical */
					t = ((x2-x1+1)/3*1+x1);
					CAIRO_LINE(cr, t, y1, t, y2);
					t = ((x2-x1+1)/3*2+x1);
					CAIRO_LINE(cr, t, y1, t, y2);

					/* horizontal */
					t = ((y2-y1+1)/3*1+y1);
					CAIRO_LINE(cr, x1, t, x2, t);
					t = ((y2-y1+1)/3*2+y1);
					CAIRO_LINE(cr, x1, t, x2, t);
					break;
				}

				case ROI_GRID_GOLDEN_TRIANGLES1:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y1, x2, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x1, y2, t, y1);

					t = (x1+golden);
					CAIRO_LINE(cr, x2, y1, t, y2);
					break;
				}
				case ROI_GRID_GOLDEN_TRIANGLES2:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x2, y1, x1, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x1, y1, t, y2);

					t = (x1+golden);
					CAIRO_LINE(cr, x2, y2, t, y1);
					break;
				}

				case ROI_GRID_HARMONIOUS_TRIANGLES1:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y1, x2, y2);

					t = (x1+golden);
					CAIRO_LINE(cr, x1, y2, t, y1);

					t = (x2-golden);
					CAIRO_LINE(cr, x2, y1, t, y2);
					break;
				}
				case ROI_GRID_HARMONIOUS_TRIANGLES2:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y2, x2, y1);

					t = (x1+golden);
					CAIRO_LINE(cr, x1, y1, t, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x2, y2, t, y1);
					break;
				}
			}
			cairo_stroke(cr);

			/* Translate "back" */
			cairo_translate(cr, -placement.x, -placement.y);
			gtk_label_set_text(GTK_LABEL(preview->crop_size_label), text);
			g_free(text);
		}

		/* Draw snapshot-identifier */
		if (preview->views > 1)
		{
			GdkRectangle canvas;
			const gchar *txt;
			switch (preview->snapshot[i])
			{
				case 0:
					txt = "A";
					break;
				case 1:
					txt = "B";
					break;
				case 2:
					txt = "C";
					break;
				default:
					txt = "-";
					break;
			}

			get_canvas_placement(preview, i, &canvas);

			cairo_set_dash(cr, dashes, 0, 0.0);
			cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 20.0);

			cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.7);
			cairo_move_to(cr, canvas.x+3.0, canvas.y+21.0);
			cairo_text_path(cr, txt);
			cairo_fill(cr);

			cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);
			cairo_move_to(cr, canvas.x+3.0, canvas.y+21.0);
			cairo_text_path(cr, txt);
			cairo_stroke(cr);
		}
		g_object_unref(buffer);
	}

	/* Draw straighten-line */
	if (preview->state & STRAIGHTEN_MOVE)
	{
		cairo_set_line_width(cr, 1.0);

		cairo_set_dash(cr, dashes, 2, 0.0);
		cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1.0);
		cairo_move_to(cr, preview->straighten_start.x, preview->straighten_start.y);
		cairo_line_to(cr, preview->straighten_end.x, preview->straighten_end.y);
		cairo_stroke(cr);

		cairo_set_dash(cr, dashes, 2, 10.0);
		cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
		cairo_move_to(cr, preview->straighten_start.x, preview->straighten_start.y);
		cairo_line_to(cr, preview->straighten_end.x, preview->straighten_end.y);
		cairo_stroke(cr);
	}

	/* Draw splitters */
	if (preview->views>0)
	{
		for(i=1;i<preview->views;i++)
		{
			if (preview->split == SPLIT_VERTICAL)
				gtk_paint_vline(GTK_WIDGET(preview)->style, window, GTK_STATE_NORMAL, NULL, widget, NULL,
					0,
					GTK_WIDGET(preview->canvas)->allocation.height,
					i * GTK_WIDGET(preview)->allocation.width/preview->views - SPLITTER_WIDTH/2);
			else if (preview->split == SPLIT_HORIZONTAL)
				gtk_paint_hline(GTK_WIDGET(preview)->style, window, GTK_STATE_NORMAL, NULL, widget, NULL,
					0,
					GTK_WIDGET(preview->canvas)->allocation.width,
					i * GTK_WIDGET(preview)->allocation.height/preview->views - SPLITTER_WIDTH/2);
		}
	}

	g_object_unref(gc);
	cairo_destroy(cr);

	gdk_window_end_paint(window);
#undef CAIRO_LINE
}

static void
realize(GtkWidget *widget, gpointer data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(widget);

	if (preview->zoom_to_fit)
	{
		gtk_widget_hide(preview->vscrollbar);
		gtk_widget_hide(preview->hscrollbar);
	}
	else
	{
		gtk_widget_show(preview->vscrollbar);
		gtk_widget_show(preview->hscrollbar);
	}
}

static gboolean
scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	if (!preview->zoom_to_fit)
	{
		GtkAdjustment *adj;
		gdouble value;
		gdouble page_size;
		gdouble upper;

		if (event->state & GDK_CONTROL_MASK)
			adj = preview->hadjustment;
		else
			adj = preview->vadjustment;
		g_object_get(G_OBJECT(adj), "page-size", &page_size, "upper", &upper, NULL);
		
		if (event->direction == GDK_SCROLL_UP)
			value = MIN(gtk_adjustment_get_value(adj)-page_size/5.0, upper-page_size);
		else
			value = MIN(gtk_adjustment_get_value(adj)+page_size/5.0, upper-page_size);
		gtk_adjustment_set_value(adj, value);
	}
	return TRUE;
}

static gboolean
expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	redraw(preview, &event->area);

	return TRUE;
}

static void
size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	gint view;
	const gdouble width = (gdouble) allocation->width;
	const gdouble height = (gdouble) allocation->height;

	g_object_set(G_OBJECT(preview->hadjustment), "page_size", width, "page-increment", width/1.2, NULL);
	g_object_set(G_OBJECT(preview->vadjustment), "page_size", height, "page-increment", height/1.2, NULL);

	if (preview->zoom_to_fit)
		for(view=0;view<preview->views;view++)
			rescale(preview, view);
}

static void
adjustment_changed(GtkAdjustment *adjustment, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	if (!preview->zoom_to_fit)
	{
		DIRTY(preview->dirty[0], SCREEN);
		rs_preview_widget_update(preview, FALSE);
	}
}

static gboolean
button(GtkWidget *widget, GdkEventButton *event, RSPreviewWidget *preview)
{
	const gint x = (gint) (event->x+0.5f);
	const gint y = (gint) (event->y+0.5f);
	GdkWindow *window = GTK_WIDGET(preview->canvas)->window;
	const gint view = get_view_from_coord(preview, x, y);
	GtkUIManager *ui_manager = gui_get_uimanager();
	gint real_x, real_y;
	gint scaled_x, scaled_y;
	gboolean inside_image = get_image_coord(preview, view, x, y, &scaled_x, &scaled_y, &real_x, &real_y, NULL, NULL);

	g_return_val_if_fail(VIEW_IS_VALID(view), FALSE);

	/* White balance picker */
	if (inside_image
		&& (event->type == GDK_BUTTON_PRESS)
		&& (event->button == 1)
		&& (preview->state & WB_PICKER)
		&& g_signal_has_handler_pending(preview, signals[WB_PICKED], 0, FALSE))
	{
		RS_PREVIEW_CALLBACK_DATA cbdata;
		make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y);
		g_signal_emit (G_OBJECT (preview), signals[WB_PICKED], 0, &cbdata);
	}
	/* Pop-up-menu */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==3)
		&& (preview->state & NORMAL))
	{
		/* Hack to mark uncrop and unstraighten as in/sensitive */
		rs_core_action_group_activate("PhotoMenu");
		if (view==0)
		{
			GtkWidget *menu = gtk_ui_manager_get_widget (ui_manager, "/PreviewPopup");
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
		}
		else
		{
			GtkWidget *menu = gtk_ui_manager_get_widget (ui_manager, "/PreviewPopupRight");
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
		}
	}
	/* Crop begin */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & CROP_START))
	{
		preview->crop_start.x = real_x;
		preview->crop_start.y = real_y;
		preview->crop_near = CROP_NEAR_SE;
		preview->state = CROP_MOVE_CORNER;
	}
	/* Crop release */
	else if ((event->type == GDK_BUTTON_RELEASE)
		&& (event->button==1)
		&& (preview->state & CROP))
	{
		preview->state = CROP_IDLE;
	}
	/* Crop move corner */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & CROP_IDLE))
	{
		preview->crop_start.x = real_x;
		preview->crop_start.y = real_y;
		switch(preview->crop_near)
		{
			case CROP_NEAR_N:
			case CROP_NEAR_S:
			case CROP_NEAR_W:
			case CROP_NEAR_E:
			case CROP_NEAR_INSIDE:
				preview->state = CROP_MOVE_ALL;
				break;
			case CROP_NEAR_NW:
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_NE:
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_SE:
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_SW:
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_CORNER;
				break;
			default:
				preview->crop_start.x = real_x;
				preview->crop_start.y = real_y;
				preview->state = CROP_MOVE_CORNER;
				break;
		}
	}
	/* Cancel */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==3)
		&& (!(preview->state & NORMAL)))
	{
		if (preview->state & CROP)
		{
			if (preview->crop_near == CROP_NEAR_INSIDE)
				crop_end(preview, TRUE);
			else
				crop_end(preview, FALSE);
		}
		
		preview->state = WB_PICKER;
		gdk_window_set_cursor(window, cur_normal);
	}
	/* Begin straighten */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & STRAIGHTEN_START))
	{
		preview->straighten_start.x = (gint) (event->x+0.5f);
		preview->straighten_start.y = (gint) (event->y+0.5f);
		preview->state = STRAIGHTEN_MOVE;
	}
	/* Move straighten */
	else if ((event->type == GDK_BUTTON_RELEASE)
		&& (event->button==1)
		&& (preview->state & STRAIGHTEN_MOVE))
	{
		preview->straighten_end.x = (gint) (event->x+0.5f);
		preview->straighten_end.y = (gint) (event->y+0.5f);
		preview->state = WB_PICKER;
		rs_photo_set_angle(preview->photo, preview->straighten_angle, TRUE);
	}
	return FALSE;
}
static gboolean
motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);
	GdkWindow *window = GTK_WIDGET(preview->canvas)->window;
	gint x, y;
	gint real_x, real_y;
	gint scaled_x, scaled_y;
	gint max_w, max_h;
	gint view;
	gint i;
	GdkModifierType mask;
	RS_RECT scaled;
	gboolean inside_image = FALSE;

	gdk_window_get_pointer(window, &x, &y, &mask);
	view = get_view_from_coord(preview, x, y);

	/* Bail in silence */
	if (!VIEW_IS_VALID(view))
		return TRUE;

	if (preview->photo)
		inside_image = get_image_coord(preview, view, x, y, &scaled_x, &scaled_y, &real_x, &real_y, &max_w, &max_h);

/*	if (preview->state & MOVE)
	{
		GtkAdjustment *adj;
		gdouble val;

		if (coord_diff.x != 0)
		{
			adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(preview->viewport[0]));
			val = gtk_adjustment_get_value(adj) + coord_diff.x;
			if (val > (preview->scaled->w - adj->page_size))
				val = preview->scaled->w - adj->page_size;
			gtk_adjustment_set_value(adj, val);
		}
		if (coord_diff.y != 0)
		{
			adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(preview->viewport[0]));
			val = gtk_adjustment_get_value(adj) + coord_diff.y;
			if (val > (preview->scaled->h - adj->page_size))
				val = preview->scaled->h - adj->page_size;
			gtk_adjustment_set_value(adj, val);
		}
	}
*/

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & CROP_MOVE_CORNER))
	{
		preview->roi.x1 = preview->crop_start.x;
		preview->roi.y1 = preview->crop_start.y;
		preview->roi.x2 = real_x;
		preview->roi.y2 = real_y;

		rs_rect_normalize(&preview->roi, &preview->roi);

		/* Update near */
		if (real_x > preview->crop_start.x)
			preview->crop_near = CROP_NEAR_E;
		else
			preview->crop_near = CROP_NEAR_W;

		if (real_y > preview->crop_start.y)
			preview->crop_near |= CROP_NEAR_S;
		else
			preview->crop_near |= CROP_NEAR_N;

		/* Do aspect restriction */
		crop_find_size_from_aspect(&preview->roi, preview->crop_aspect, preview->crop_near);

		for(i=0;i<preview->views;i++)
			DIRTY(preview->dirty[i], SCREEN);
		rs_preview_widget_update(preview, TRUE);
	}

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & CROP_MOVE_ALL))
	{
		gint dist_x, dist_y;
		dist_x = real_x - preview->crop_start.x;
		dist_y = real_y - preview->crop_start.y;

		/* check borders */
		if ((preview->crop_move.x1 + dist_x) < 0)
			dist_x = 0 - preview->crop_move.x1;
		if ((preview->crop_move.y1 + dist_y) < 0)
			dist_y = 0 - preview->crop_move.y1;
		if (((preview->crop_move.x2 + dist_x) > max_w))
			dist_x = max_w - preview->crop_move.x2;
		if (((preview->crop_move.y2 + dist_y) > max_h))
			dist_y = max_h - preview->crop_move.y2;

		preview->roi.x1 = preview->crop_move.x1+dist_x;
		preview->roi.y1 = preview->crop_move.y1+dist_y;
		preview->roi.x2 = preview->crop_move.x2+dist_x;
		preview->roi.y2 = preview->crop_move.y2+dist_y;

		for(i=0;i<preview->views;i++)
			DIRTY(preview->dirty[i], SCREEN);
		rs_preview_widget_update(preview, TRUE);
	}

	/* Update crop_near if mouse button 1 is NOT pressed */
	if ((preview->state & CROP) && !(mask & GDK_BUTTON1_MASK) && (preview->state != CROP_START))
	{
		scaled.x1 = preview->roi.x1*preview->scale;
		scaled.y1 = preview->roi.y1*preview->scale;
		scaled.x2 = preview->roi.x2*preview->scale;
		scaled.y2 = preview->roi.y2*preview->scale;
		preview->crop_near = crop_near(&scaled, scaled_x, scaled_y);
		/* Set cursor accordingly */
		switch(preview->crop_near)
		{
			case CROP_NEAR_NW:
				gdk_window_set_cursor(window, cur_nw);
				break;
			case CROP_NEAR_NE:
				gdk_window_set_cursor(window, cur_ne);
				break;
			case CROP_NEAR_SE:
				gdk_window_set_cursor(window, cur_se);
				break;
			case CROP_NEAR_SW:
				gdk_window_set_cursor(window, cur_sw);
				break;
			case CROP_NEAR_N:
			case CROP_NEAR_S:
			case CROP_NEAR_W:
			case CROP_NEAR_E:
			case CROP_NEAR_INSIDE:
				preview->crop_move = preview->roi;
				gdk_window_set_cursor(window, cur_fleur);
				break;
			default:
				if (inside_image)
					gdk_window_set_cursor(window, cur_crop);
				else
					gdk_window_set_cursor(window, cur_normal);
				break;
		}
	}

	if (preview->state == CROP_START)
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_crop);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if (preview->state & STRAIGHTEN)
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_rotate);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if ((preview->state & WB_PICKER))
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_color_picker);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & STRAIGHTEN_MOVE))
	{
		gint i;
		gdouble degrees;
		gint vx, vy;

		preview->straighten_end.x = x;
		preview->straighten_end.y = y;
		vx = preview->straighten_start.x - preview->straighten_end.x;
		vy = preview->straighten_start.y - preview->straighten_end.y;
		for(i=0;i<preview->views;i++)
			DIRTY(preview->dirty[i], SCREEN);
		rs_preview_widget_update(preview, TRUE);
		degrees = -atan2(vy,vx)*180/M_PI;
		if (degrees>=0.0)
		{
			if ((degrees>45.0) && (degrees<=135.0))
				degrees -= 90.0;
			else if (degrees>135.0)
				degrees -= 180.0;
		}
		else /* <0.0 */
		{
			if ((degrees < -45.0) && (degrees >= -135.0))
				degrees += 90.0;
			else if (degrees<-135.0)
				degrees += 180.0;
		}
		preview->straighten_angle = degrees;
	}

	/* If anyone is listening, go ahead and emit signal */
	if (inside_image && g_signal_has_handler_pending(preview, signals[MOTION_SIGNAL], 0, FALSE))
	{
		RS_PREVIEW_CALLBACK_DATA cbdata;
		if (make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y))
			g_signal_emit (G_OBJECT (preview), signals[MOTION_SIGNAL], 0, &cbdata);
	}

	/* Check not to generate superfluous signals "leave"*/
	if (inside_image != preview->prev_inside_image)
	{
		preview->prev_inside_image = inside_image;
		if (!inside_image && g_signal_has_handler_pending(preview, signals[LEAVE_SIGNAL], 0, FALSE))	
		{
			RS_PREVIEW_CALLBACK_DATA cbdata;
			if (make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y))
				g_signal_emit (G_OBJECT (preview), signals[LEAVE_SIGNAL], 0, &cbdata);
		}
	}

	return TRUE;
}

static gboolean 
leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	/* Check not to generate superfluous signals "leave"*/
	if (preview->prev_inside_image)
	{
		preview->prev_inside_image = FALSE;
		if (g_signal_has_handler_pending(preview, signals[LEAVE_SIGNAL], 0, FALSE))
			g_signal_emit (G_OBJECT (preview), signals[LEAVE_SIGNAL], 0, NULL);
	}
	return TRUE;
}

static void
settings_changed(RS_PHOTO *photo, RSSettingsMask mask, RSPreviewWidget *preview)
{
	gint view;

	/* Seperate snapshot */
	const gint snapshot = mask>>24;
	mask &= 0x00ffffff;

	/* Return if no more relevant */
	if (photo != preview->photo)
		return;

	for(view=0;view<preview->views;view++)
	{
		if (preview->snapshot[view] == snapshot)
		{
			DIRTY(preview->dirty[view], SCREEN);
			if (mask & MASK_SHARPEN)
			{
				gfloat f = 0.0;
				g_object_get(preview->photo->settings[preview->snapshot[view]], "sharpen", &f, NULL);
				g_object_set(preview->filter_denoise[view], "sharpen", (gint) f, NULL);
			}
			if (mask & MASK_DENOISE_LUMA)
			{
				gfloat f = 0.0;
				g_object_get(preview->photo->settings[preview->snapshot[view]], "denoise_luma", &f, NULL);
				g_object_set(preview->filter_denoise[view], "denoise_luma", (gint) f, NULL);
			}
			if (mask & MASK_DENOISE_CHROMA)
			{
				gfloat f = 0.0;
				g_object_get(preview->photo->settings[preview->snapshot[view]], "denoise_chroma", &f, NULL);
				g_object_set(preview->filter_denoise[view], "denoise_chroma", (gint) f, NULL);
			}
		}
	}
}

static void
filter_changed(RSFilter *filter, RSPreviewWidget *preview)
{
	gint view;

	/* See if we can find a matching plugin */
	for(view=0;view<preview->views;view++)
	{
		if (filter == preview->filter_end[view])
		{
			DIRTY(preview->dirty[view], SCREEN);
			rescale(preview, 0);
			rs_preview_widget_update(preview, TRUE);
		}
	}
}

static void
crop_aspect_changed(gpointer active, gpointer user_data)
{
	gint view;
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	preview->crop_aspect = *((gdouble *)active);
	for(view=0;view<preview->views;view++)
		DIRTY(preview->dirty[view], SCREEN);
	rs_preview_widget_update(preview, FALSE);
}

static void
crop_grid_changed(gpointer active, gpointer user_data)
{
	gint view;
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	preview->roi_grid = GPOINTER_TO_INT(active);
	for(view=0;view<preview->views;view++)
		DIRTY(preview->dirty[view], SCREEN);
	rs_preview_widget_update(preview, FALSE);
}

static void
crop_apply_clicked(GtkButton *button, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	crop_end(preview, TRUE);
}

static void
crop_cancel_clicked(GtkButton *button, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	crop_end(preview, FALSE);
}

static void
crop_end(RSPreviewWidget *preview, gboolean accept)
{
	gint view;

	for(view=0;view<preview->views;view++)
		DIRTY(preview->dirty[view], SCREEN);

	if (accept)
		rs_photo_set_crop(preview->photo, &preview->roi);

	gtk_widget_destroy(preview->tool);
	preview->state = WB_PICKER;

	gdk_window_set_cursor(GTK_WIDGET(preview->canvas)->window, cur_normal);

	rs_preview_widget_update(preview, TRUE);
}

static void
crop_find_size_from_aspect(RS_RECT *roi, gdouble aspect, CROP_NEAR near)
{
	const gdouble original_w = (gdouble) abs(roi->x2 - roi->x1 + 1);
	const gdouble original_h = (gdouble) abs(roi->y2 - roi->y1 + 1);
	gdouble corrected_w, corrected_h;
	gdouble original_aspect = original_w/original_h;

	if (aspect == 0.0)
		return;

	if (original_aspect > 1.0)
	{ /* landscape */
		if (original_aspect > aspect)
		{
			corrected_h = original_h;
			corrected_w = original_h * aspect;
		}
		else
		{
			corrected_w = original_w;
			corrected_h = original_w / aspect;
		}
	}
	else
	{ /* portrait */
		if ((1.0/original_aspect) > aspect)
		{
			corrected_w = original_w;
			corrected_h = original_w * aspect;
		}
		else
		{
			corrected_h = original_h;
			corrected_w = original_h / aspect;
		}
	}

	switch(near)
	{
		case CROP_NEAR_NW: /* x1,y1 */
			roi->x1 = roi->x2 - ((gint)corrected_w) + 1;
			roi->y1 = roi->y2 - ((gint)corrected_h) + 1;
			break;
		case CROP_NEAR_NE: /* x2,y1 */
			roi->x2 = roi->x1 + ((gint)corrected_w) - 1;
			roi->y1 = roi->y2 - ((gint)corrected_h) + 1;
			break;
		case CROP_NEAR_SE: /* x2,y2 */
			roi->x2 = roi->x1 + ((gint)corrected_w) - 1;
			roi->y2 = roi->y1 + ((gint)corrected_h) - 1;
			break;
		case CROP_NEAR_SW: /* x1,y2 */
			roi->x1 = roi->x2 - ((gint)corrected_w) + 1;
			roi->y2 = roi->y1 + ((gint)corrected_h) - 1;
			break;
		default: /* Shut up GCC! */
			break;
	}
}

static CROP_NEAR
crop_near(RS_RECT *roi, gint x, gint y)
{
	CROP_NEAR near = CROP_NEAR_NOTHING;
#define NEAR(aim, target) (ABS((target)-(aim))<9)
	if (NEAR(y, roi->y1)) /* N */
	{
		if (NEAR(x,roi->x1)) /* NW */
			near = CROP_NEAR_NW;
		else if (NEAR(x,roi->x2)) /* NE */
			near = CROP_NEAR_NE;
		else if ((x > roi->x1) && (x < roi->x2)) /* N */
			near = CROP_NEAR_N;
	}
	else if (NEAR(y, roi->y2)) /* S */
	{
		if (NEAR(x,roi->x1)) /* SW */
			near = CROP_NEAR_SW;
		else if (NEAR(x,roi->x2)) /* SE */
			near = CROP_NEAR_SE;
		else if ((x > roi->x1) && (x < roi->x2))
			near = CROP_NEAR_S;
	}
	else if (NEAR(x, roi->x1)
		&& (y > roi->y1)
		&& (y < roi->y2)) /* West */
		near = CROP_NEAR_W;
	else if (NEAR(x, roi->x2)
		&& (y > roi->y1)
		&& (y < roi->y2)) /* East */
		near = CROP_NEAR_E;

	if (near == CROP_NEAR_NOTHING)
	{
		if (((x>roi->x1) && (x<roi->x2))
			&& ((y>roi->y1) && (y<roi->y2))
			&& (((roi->x2-roi->x1)>2) && ((roi->y2-roi->y1)>2)))
			near = CROP_NEAR_INSIDE;
		else
			near = CROP_NEAR_OUTSIDE;
	}
	return near;
#undef NEAR
}

static gboolean
make_cbdata(RSPreviewWidget *preview, const gint view, RS_PREVIEW_CALLBACK_DATA *cbdata, gint screen_x, gint screen_y, gint real_x, gint real_y)
{
	gint row, col;
	gushort *pixel;
	gdouble r=0.0f, g=0.0f, b=0.0f;

	if ((view<0) || (view>(preview->views-1)))
		return FALSE;

	RS_IMAGE16 *image = rs_filter_get_image(preview->filter_end[view]);
	/* Get the real coordinates */
	cbdata->pixel = rs_image16_get_pixel(image, screen_x, screen_y, TRUE);

	cbdata->x = real_x;
	cbdata->y = real_y;

	/* Render current pixel */
	/* FIXME: Change to something else... */
//	rs_color_transform_transform(preview->rct[view],
//		1, 1,
//		cbdata->pixel, image->rowstride,
//		&cbdata->pixel8, 1);

	/* Find average pixel values from 3x3 pixels */
	for(row=-1; row<2; row++)
	{
		for(col=-1; col<2; col++)
		{
			pixel = rs_image16_get_pixel(image, screen_x+col, screen_y+row, TRUE);
			r += pixel[R]/65535.0;
			g += pixel[G]/65535.0;
			b += pixel[B]/65535.0;
		}
	}
	cbdata->pixelfloat[R] = (gfloat) r/9.0f;
	cbdata->pixelfloat[G] = (gfloat) g/9.0f;
	cbdata->pixelfloat[B] = (gfloat) b/9.0f;

	g_object_unref(image);
	return TRUE;
}
