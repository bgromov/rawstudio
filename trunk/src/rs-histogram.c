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
#include "rs-histogram.h"

/* FIXME: Do some cleanup in finalize! */

struct _RSHistogramWidget
{
	GtkDrawingArea parent;
	gint width;
	gint height;
	GdkPixmap *blitter;
	RS_IMAGE16 *image;
	RSSettings *settings;
	RSColorTransform *rct;
	guint input_samples[4][256];
	guint *output_samples[4];
};

struct _RSHistogramWidgetClass
{
	GtkDrawingAreaClass parent_class;
};

/* Define the boiler plate stuff using the predefined macro */
G_DEFINE_TYPE (RSHistogramWidget, rs_histogram_widget, GTK_TYPE_DRAWING_AREA);

static void size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static gboolean expose(GtkWidget *widget, GdkEventExpose *event);

/**
 * Class initializer
 */
static void
rs_histogram_widget_class_init(RSHistogramWidgetClass *klass)
{
	GtkWidgetClass *widget_class;
	GtkObjectClass *object_class;
	widget_class = GTK_WIDGET_CLASS(klass);
	object_class = GTK_OBJECT_CLASS(klass);
	widget_class->expose_event = expose;
}

/**
 * Instance initialization
 */
static void
rs_histogram_widget_init(RSHistogramWidget *hist)
{
	hist->output_samples[0] = NULL;
	hist->output_samples[1] = NULL;
	hist->output_samples[2] = NULL;
	hist->output_samples[3] = NULL;
	hist->image = NULL;
	hist->settings = NULL;
	hist->rct = rs_color_transform_new();
	hist->blitter = NULL;

	g_signal_connect(G_OBJECT(hist), "size-allocate", G_CALLBACK(size_allocate), NULL);
}

static void
size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	gint c;

	RSHistogramWidget *histogram = RS_HISTOGRAM_WIDGET(widget);

	histogram->width = allocation->width;
	histogram->height = allocation->height;

	/* Free the samples array if needed */
	for (c=0;c<4;c++)
	{
		if (histogram->output_samples[c])
			g_free(histogram->output_samples[c]);
		histogram->output_samples[c] = NULL;
	}

	/* Free blitter if needed */
	if (histogram->blitter)
	{
		g_object_unref(histogram->blitter);
		histogram->blitter = NULL;
	}
}

static gboolean
expose(GtkWidget *widget, GdkEventExpose *event)
{
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(widget));

	return FALSE;
}

/**
 * Creates a new RSHistogramWidget
 */
GtkWidget *
rs_histogram_new(void)
{
	return g_object_new (RS_HISTOGRAM_TYPE_WIDGET, NULL);
}

/**
 * Set an image to base the histogram from
 * @param histogram A RSHistogramWidget
 * @param image An image
 */
void
rs_histogram_set_image(RSHistogramWidget *histogram, RS_IMAGE16 *image)
{
	g_return_if_fail (RS_IS_HISTOGRAM_WIDGET(histogram));
	g_return_if_fail (image);

	histogram->image = image;

	rs_histogram_redraw(histogram);
}

/**
 * Set a RSSettings to use
 * @param histogram A RSHistogramWidget
 * @param settings A RSSettings object to use
 */
void
rs_histogram_set_settings(RSHistogramWidget *histogram, RSSettings *settings)
{
	g_return_if_fail (RS_IS_HISTOGRAM_WIDGET(histogram));
	g_return_if_fail (RS_IS_SETTINGS(settings));

	if (histogram->settings)
		g_object_unref(histogram->settings);

	histogram->settings = g_object_ref(settings);

	rs_color_transform_set_from_settings(histogram->rct, histogram->settings, MASK_ALL);

	rs_histogram_redraw(histogram);
}

/**
 * Redraw a RSHistogramWidget
 * @param histogram A RSHistogramWidget
 */
void
rs_histogram_redraw(RSHistogramWidget *histogram)
{
	gint c, x;
	guint max;
	GdkDrawable *window;
	GtkWidget *widget;
	GdkGC *gc;

	g_return_if_fail (RS_IS_HISTOGRAM_WIDGET(histogram));

	widget = GTK_WIDGET(histogram);
	/* Draw histogram if we got everything needed */
	if (histogram->rct && histogram->image && GTK_WIDGET_VISIBLE(widget) && GTK_WIDGET_REALIZED(widget))
	{
		const static GdkColor bg = {0, 0x9900, 0x9900, 0x9900};
		const static GdkColor lines = {0, 0x7700, 0x7700, 0x7700};

		window = GDK_DRAWABLE(widget->window);
		gc = gdk_gc_new(window);

		/* Allocate new buffer if needed */
		if (histogram->blitter == NULL)
			histogram->blitter = gdk_pixmap_new(window, histogram->width, histogram->height, -1);

		/* Reset background to a nice grey */
		gdk_gc_set_rgb_fg_color(gc, &bg);
		gdk_draw_rectangle(histogram->blitter, gc, TRUE, 0, 0, histogram->width, histogram->height);

		/* Draw vertical lines */
		gdk_gc_set_rgb_fg_color(gc, &lines);
		gdk_draw_line(histogram->blitter, gc, histogram->width*0.25, 0, histogram->width*0.25, histogram->height-1);
		gdk_draw_line(histogram->blitter, gc, histogram->width*0.5, 0, histogram->width*0.5, histogram->height-1);
		gdk_draw_line(histogram->blitter, gc, histogram->width*0.75, 0, histogram->width*0.75, histogram->height-1);

		/* Sample some data */
		rs_color_transform_make_histogram(histogram->rct, histogram->image, histogram->input_samples);

		/* Interpolate data for correct width and find maximum value */
		max = 0;
		for (c=0;c<4;c++)
			histogram->output_samples[c] = interpolate_dataset_int(
				&histogram->input_samples[c][1], 253,
				histogram->output_samples[c], histogram->width,
				&max);

		/* Find the scaling factor */
		gfloat factor = (gfloat)(max+histogram->height)/(gfloat)histogram->height;

#if GTK_CHECK_VERSION(2,8,0)
		cairo_t *cr;

		/* We will use Cairo for this if possible */
		cr = gdk_cairo_create (histogram->blitter);

		/* Line width */
		cairo_set_line_width (cr, 2.0);

		/* Red */
		cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 1.0);
		/* Start at first column */
		cairo_move_to (cr, 0, (histogram->height-1)-histogram->output_samples[0][0]/factor);
		/* Walk through columns */
		for (x = 1; x < histogram->width; x++)
			cairo_line_to(cr, x, (histogram->height-1)-histogram->output_samples[0][x]/factor);
		/* Draw the line */
		cairo_stroke (cr);

		/* Underexposed */
		cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, histogram->input_samples[0][0]/100.0);
		cairo_arc(cr, 8.0, 8.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);

		/* Overexposed */
		cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, histogram->input_samples[0][255]/100.0);
		cairo_arc(cr, histogram->width-8.0, 8.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);

		/* Green */
		cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.5);
		cairo_move_to (cr, 0, (histogram->height-1)-histogram->output_samples[1][0]/factor);
		for (x = 1; x < histogram->width; x++)
			cairo_line_to(cr, x, (histogram->height-1)-histogram->output_samples[1][x]/factor);
		cairo_stroke (cr);
		cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, histogram->input_samples[1][0]/100.0);
		cairo_arc(cr, 8.0, 16.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, histogram->input_samples[1][255]/100.0);
		cairo_arc(cr, histogram->width-8.0, 16.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);

		/* Blue */
		cairo_set_source_rgba(cr, 0.2, 0.2, 1.0, 0.5);
		cairo_move_to (cr, 0, (histogram->height-1)-histogram->output_samples[2][0]/factor);
		for (x = 1; x < histogram->width; x++)
			cairo_line_to(cr, x, (histogram->height-1)-histogram->output_samples[2][x]/factor);
		cairo_stroke (cr);
		cairo_set_source_rgba(cr, 0.2, 0.2, 1.0, histogram->input_samples[2][0]/100.0);
		cairo_arc(cr, 8.0, 24.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.2, 0.2, 1.0, histogram->input_samples[2][255]/100.0);
		cairo_arc(cr, histogram->width-8.0, 24.0, 3.0, 0.0, 2*M_PI);
		cairo_fill(cr);

		/* Luma */
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
		cairo_move_to (cr, 0, histogram->height);
		for (x = 0; x < histogram->width; x++)
			cairo_line_to(cr, x, (histogram->height)-histogram->output_samples[3][x]/factor);
		cairo_line_to(cr, x, histogram->height);
		cairo_fill (cr);

		/* We're done */
		cairo_destroy (cr);
#else /* GTK_CHECK_VERSION(2,8,0) */
		GdkPoint points[histogram->width];
		const static GdkColor red = {0, 0xffff, 0x0000, 0x0000 };
		const static GdkColor green = {0, 0x0000, 0xffff, 0x0000 };
		const static GdkColor blue = {0, 0x0000, 0x0000, 0xffff };
		const static GdkColor luma = {0, 0xeeee, 0xeeee, 0xeeee };

		/* Red */
		gdk_gc_set_rgb_fg_color(gc, &red);
		for (x = 0; x < histogram->width; x++)
		{
			points[x].x = x; /* Only update x the first time! */
			points[x].y = (histogram->height-1)-histogram->output_samples[0][x]/factor;
		}
		gdk_draw_lines(histogram->blitter, gc, points, histogram->width);
		/* Underexposed */
		if (histogram->input_samples[0][0]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, 1, 0, 8, 8, 0, 360*64);
		/* Overexposed */
		if (histogram->input_samples[0][255]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, histogram->width-10, 0, 8, 8, 0, 360*64);

		/* Green */
		gdk_gc_set_rgb_fg_color(gc, &green);
		for (x = 0; x < histogram->width; x++)
			points[x].y = (histogram->height-1)-histogram->output_samples[1][x]/factor;
		gdk_draw_lines(histogram->blitter, gc, points, histogram->width);
		if (histogram->input_samples[1][0]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, 1, 10, 8, 8, 0, 360*64);
		if (histogram->input_samples[1][255]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, histogram->width-10, 10, 8, 8, 0, 360*64);

		/* Blue */
		gdk_gc_set_rgb_fg_color(gc, &blue);
		for (x = 0; x < histogram->width; x++)
			points[x].y = (histogram->height-1)-histogram->output_samples[2][x]/factor;
		gdk_draw_lines(histogram->blitter, gc, points, histogram->width);
		if (histogram->input_samples[2][0]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, 1, 20, 8, 8, 0, 360*64);
		if (histogram->input_samples[2][255]>99)
			gdk_draw_arc(histogram->blitter, gc, TRUE, histogram->width-10, 20, 8, 8, 0, 360*64);

		/* Luma */
		gdk_gc_set_rgb_fg_color(gc, &luma);
		for (x = 0; x < histogram->width; x++)
			points[x].y = (histogram->height-1)-histogram->output_samples[3][x]/factor;
		gdk_draw_lines(histogram->blitter, gc, points, histogram->width);
#endif /* GTK_CHECK_VERSION(2,8,0) */

		/* Blit to screen */
		gdk_draw_drawable(window, gc, histogram->blitter, 0, 0, 0, 0, histogram->width, histogram->height);

		g_object_unref(gc);
	}

}
