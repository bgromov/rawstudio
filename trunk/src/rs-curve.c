/*****************************************************************************
 * Curve widget
 * 
 * Copyright (C) 2007 Edouard Gomez <ed.gomez@free.fr>
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
 ****************************************************************************/

#include <math.h>

#include "rs-curve.h"

static void rs_curve_widget_class_init(RSCurveWidgetClass *klass);
static void rs_curve_widget_init(RSCurveWidget *curve);
static void rs_curve_widget_destroy(GtkObject *object);
static void rs_curve_changed(RSCurveWidget *curve);
static void rs_curve_draw(RSCurveWidget *curve);
static gboolean rs_curve_widget_expose(GtkWidget *widget, GdkEventExpose *event);
static gboolean rs_curve_widget_button_press(GtkWidget *widget, GdkEventButton *event);
static gboolean rs_curve_widget_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean rs_curve_widget_motion_notify(GtkWidget *widget, GdkEventMotion *event);

enum {
  CHANGED_SIGNAL,
  RIGHTCLICK_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Define the boiler plate stuff using the predefined macro */
G_DEFINE_TYPE (RSCurveWidget, rs_curve_widget, GTK_TYPE_DRAWING_AREA);

/**
 * Class initializer
 */
static void
rs_curve_widget_class_init(RSCurveWidgetClass *klass)
{
	GtkWidgetClass *widget_class;
	GtkObjectClass *object_class;
	widget_class = GTK_WIDGET_CLASS(klass);
	object_class = GTK_OBJECT_CLASS(klass);

	signals[CHANGED_SIGNAL] = g_signal_new ("changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[RIGHTCLICK_SIGNAL] = g_signal_new ("right-click",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	object_class->destroy = rs_curve_widget_destroy;
	widget_class->expose_event = rs_curve_widget_expose;
	widget_class->button_press_event = rs_curve_widget_button_press;
	widget_class->button_release_event = rs_curve_widget_button_release;
	widget_class->motion_notify_event = rs_curve_widget_motion_notify;
}

/**
 * Instance initialization
 */
static void
rs_curve_widget_init(RSCurveWidget *curve)
{
	curve->array = NULL;
	curve->array_length = 0;
	curve->spline = rs_spline_new(NULL, 0, NATURAL);

	/* Let us know about pointer movements */
	gtk_widget_set_events(GTK_WIDGET(curve), 0
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK);
}

/**
 * Instance Constructor
 */
GtkWidget *
rs_curve_widget_new(void)
{
	return g_object_new (RS_CURVE_TYPE_WIDGET, NULL);
}

/**
 * Sets sample array for a RSCurveWidget, this array will be updates whenever the curve changes
 * @param curve A RSCurveWidget
 * @param array An array of gfloats to be updated or NULL to unset
 * @params array_length: Length of array or 0 to unset
 */
void
rs_curve_widget_set_array(RSCurveWidget *curve, gfloat *array, guint array_length)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	if (array && array_length)
	{
		curve->array = array;
		curve->array_length = array_length;
	}
	else
	{
		curve->array = NULL;
		curve->array_length = 0;
	}
}

/**
 * Instance destruction
 */
static void
rs_curve_widget_destroy(GtkObject *object)
{
	RSCurveWidget *curve = NULL;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(object));

	curve = RS_CURVE_WIDGET(object);

	if (curve->spline != NULL) {
		rs_spline_destroy(curve->spline);
	}
}

/**
 * Add a knot to a curve widget
 * @param widget A RSCurveWidget
 * @param x X coordinate
 * @param y Y coordinate
 */
void
rs_curve_widget_add_knot(RSCurveWidget *curve, gfloat x, gfloat y)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	/* Reset active knot */
	curve->active_knot = -1;

	/* Add the knot */
	rs_spline_add(curve->spline, x, y);

	/* Redraw the widget */
	rs_curve_draw(curve);

	/* Propagate the change */
	rs_curve_changed(curve);
}

/**
 * Get samples from curve
 * @param widget A RSCurveWidget
 * @param nbsamples number of samples
 * @return An array of floats, should be freed
 */
gfloat *
rs_curve_widget_sample(RSCurveWidget *curve, guint nbsamples)
{
	g_return_val_if_fail (curve != NULL, NULL);
	g_return_val_if_fail (RS_IS_CURVE_WIDGET(curve), NULL);

	return(rs_spline_sample(curve->spline, NULL, nbsamples));
}

/**
 * Get knots from a RSCurveWidget
 * @param curve A RSCurveWidget
 * @param knots An array of knots (two values/knot) (out)
 * @param nknots Number of knots written (out)
 */
extern void
rs_curve_widget_get_knots(RSCurveWidget *curve, gfloat **knots, guint *nknots)
{
	rs_spline_get_knots(curve->spline, knots, nknots);
}

/**
 * Resets a RSCurveWidget
 * @param curve A RSCurveWidget
 */
extern void
rs_curve_widget_reset(RSCurveWidget *curve)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	/* Free thew current spline */
	rs_spline_destroy(curve->spline);

	/* Allocate new spline */
	curve->spline = rs_spline_new(NULL, 0, NATURAL);

	/* Redraw changes */
	rs_curve_draw(curve);

	/* Propagate changes */
	rs_curve_changed(curve);
}

/* Background color */
static const GdkColor darkgrey = {0, 0xaaaa, 0xaaaa, 0xaaaa};

/* Foreground color */
static const GdkColor lightgrey = {0, 0xcccc, 0xcccc, 0xcccc};

/* White */
static const GdkColor white = {0, 0xffff, 0xffff, 0xffff};

static void
rs_curve_draw_background(GdkDrawable *window)
{
	/* Iterator */
	gint i;

	/* Width */
	gint width;

	/* Height */
	gint height;

	if (!window) return;

	/* Graphics context */
	GdkGC *gc = gdk_gc_new(window);

	/* Width and height */
	gdk_drawable_get_size(window, &width, &height);

	/* Prepare the graphics context */
	gdk_gc_set_rgb_fg_color(gc, &lightgrey);

	/* Clear the window */
	gdk_draw_rectangle(window, gc, TRUE,
			   0, 0,
			   width, height);

	/* Draw all lines */
	gdk_gc_set_rgb_fg_color(gc, &darkgrey);
	for (i=0; i<=4; i++) {
		gint x = i*width/4;
		gint y = i*height/4;
		gdk_draw_line(window, gc, x, 0, x, height);
		gdk_draw_line(window, gc, 0, y, width, y);
	}

	g_object_unref(gc);
}

static void
rs_curve_draw_knots(GtkWidget *widget)
{
	gfloat *knots = NULL;
	guint n = 0;
	gint width;
	gint height;
	guint i;
	RSCurveWidget *curve;
	GdkDrawable *window;
	GdkGC *gc;

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Get the drawing window */
	window = GDK_DRAWABLE(widget->window);

	if (!window) return;

	/* Graphics context */
	gc = gdk_gc_new(window);

	/* Get the knots from the spline */
	rs_spline_get_knots(curve->spline, &knots, &n);

	/* Get the width and height */
	gdk_drawable_get_size(window, &width, &height);

	/* Put the right bg color */
	gdk_gc_set_rgb_fg_color(gc, &white);

	/* Draw the stuff */
	for (i=0; i<n; i++) {
		gint x = (gint)(knots[2*i + 0]*width);
		gint y = (gint)(height*(1-knots[2*i + 1]));
		gdk_draw_rectangle(window, gc, TRUE, x-2, y-2, 4, 4);
	}

	g_free(knots);
}

static void
rs_curve_draw_spline(GtkWidget *widget)
{
	RSCurveWidget *curve;

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Get the drawing window */
	GdkDrawable *window = GDK_DRAWABLE(widget->window);

	if (!window) return;

	/* Graphics context */
	GdkGC *gc = gdk_gc_new(window);

	/* Curve samples */
	gfloat *samples = NULL;

	/* Width and height */
	gint width;
	gint height;
	gint i;

	/* Width and height */
	gdk_drawable_get_size(window, &width, &height);

	/* Put the right bg color */
	gdk_gc_set_rgb_fg_color(gc, &white);

	samples = rs_spline_sample(curve->spline, NULL, width);

	if (!samples) return;

	for (i=0; i<width; i++)
	{
		gint y = (gint)(height*(1-samples[i]));
		gdk_draw_point(window, gc, i, y);
	}

	g_free(samples);
}
/**
 * Draw everything
 */
static void
rs_curve_draw(RSCurveWidget *curve)
{
	GtkWidget *widget;
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	widget = GTK_WIDGET(curve);

	if (GTK_WIDGET_VISIBLE(widget))
	{
		/* Draw the background */
		rs_curve_draw_background(widget->window);

		/* Draw the control points */
		rs_curve_draw_knots(widget);

		/* Draw the curve */
		rs_curve_draw_spline(widget);
	}
}

/**
 * Propagate changes
 */
static void
rs_curve_changed(RSCurveWidget *curve)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	if (curve->array_length>0)
		rs_spline_sample(curve->spline, curve->array, curve->array_length);

	g_signal_emit (G_OBJECT (curve), 
		signals[CHANGED_SIGNAL], 0);
}

/**
 * Expose event handler
 */
static gboolean
rs_curve_widget_expose(GtkWidget *widget, GdkEventExpose *event)
{
	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(RS_IS_CURVE_WIDGET (widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	/* Do nothing if there's more expose events */
	if (event->count > 0)
		return FALSE;

	rs_curve_draw(RS_CURVE_WIDGET(widget));

	return FALSE;
}

/**
 * Handle button press
 */
static gboolean
rs_curve_widget_button_press(GtkWidget *widget, GdkEventButton *event)
{
	gint w, h;
	gfloat x,y;
	RSCurveWidget *curve;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(RS_IS_CURVE_WIDGET (widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	gdk_drawable_get_size(GDK_DRAWABLE(widget->window), &w, &h);
	x = event->x/w;
	y = 1.0 - event->y/h;

	/* Add a point */
	if ((event->button==1) && (curve->active_knot==-1))
		rs_curve_widget_add_knot(curve, x, y);
	else if ((event->button==1) && (curve->active_knot>=0))
		rs_spline_move(curve->spline, curve->active_knot, x, y);

	/* Delete a point if not first or last */
	else if ((event->button==2)
		&& (curve->active_knot>0)
		&& (curve->active_knot<(rs_spline_length(curve->spline)-1)))
	{
		rs_spline_delete(curve->spline, curve->active_knot);
		curve->active_knot = -1;
	}
	else if (event->button==3)
		g_signal_emit (G_OBJECT (curve), 
			signals[RIGHTCLICK_SIGNAL], 0);

	rs_curve_draw(curve);

	return(TRUE);
}
/*
 * Update when button is released
 */
static gboolean
rs_curve_widget_button_release(GtkWidget *widget, GdkEventButton *event)
{
	rs_curve_changed(RS_CURVE_WIDGET(widget));
	return(TRUE);
}

/*
 * Handle motion
 */
static gboolean
rs_curve_widget_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
	gint w, h;
	gfloat x,y;
	guint i, n = 0;
	gfloat *knots;
	RSCurveWidget *curve;
	static GdkCursor *cur_fleur = NULL;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(RS_IS_CURVE_WIDGET (widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	/* Initialize fleur cursor */
	if (!cur_fleur)
		cur_fleur = gdk_cursor_new(GDK_FLEUR);
	
	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	gdk_drawable_get_size(GDK_DRAWABLE(widget->window), &w, &h);

	/* Get a working copy of current knots */
	rs_spline_get_knots(curve->spline, &knots, &n);

	/* Calculate pixel coordinates for X-axis */
	for(i=0;i<n;i++)
		knots[i*2+0] = (float) w * knots[i*2+0];

	/* Moving a knot? */
	if ((event->state&GDK_BUTTON1_MASK) && (curve->active_knot>=0))
	{
		x = event->x/w;
		y = 1.0f - event->y/h;

		/* Clamp Y value */
		if (y<0.0f) y = 0.0f;
		if (y>1.0f) y = 1.0f;

		/* Restrict X-axis for first and last knot */
		if (curve->active_knot == 0) /* first */
			rs_spline_move(curve->spline, curve->active_knot, 0.0f, y);
		else if (curve->active_knot == rs_spline_length(curve->spline)-1) /* last */
			rs_spline_move(curve->spline, curve->active_knot, 1.0f, y);
		else
		{
			/* Delete knot if we collide with neighbour */
			if (event->x <= knots[(curve->active_knot-1)*2+0])
			{
				rs_spline_delete(curve->spline, curve->active_knot);
				curve->active_knot--;
			}
			else if (event->x >= knots[(curve->active_knot+1)*2+0])
				rs_spline_delete(curve->spline, curve->active_knot);

			/* Move the knot */
			rs_spline_move(curve->spline, curve->active_knot, x, y);
		}

		rs_curve_draw(curve);
	}
	else /* Only reset active_knot if we're not moving anything */
	{

		/* Find knot below cursor if any  */
		curve->active_knot = -1;
		for(i=0;i<n;i++)
		{
			if (fabsf(event->x-knots[i*2+0]) < 4.0)
			{
				curve->active_knot = i;
				break;
			}
		}
	}

	/* Update cursor */
	if (curve->active_knot>=0)
		gdk_window_set_cursor(widget->window, cur_fleur);
	else
		gdk_window_set_cursor(widget->window, NULL);

	g_free(knots);

	return(TRUE);
}

#ifdef RS_CURVE_TEST

void
changed(GtkWidget *widget, gpointer user_data)
{
	gfloat *s;
	gint i;
	s = rs_curve_widget_sample(RS_CURVE_WIDGET(widget), 20);
	for(i=0;i<20;i++)
	{
		printf("%.05f\n", s[i]);
	}
	g_free(s);
}

int
main(int argc, char **argv)
{
	/* Iterator */
	gint i;

        /* A window */
	GtkWidget *window;

	/* The curve */
        GtkWidget *curve;

	/* A simple S-curve */
	const gfloat scurve_knots[] = {
		0.625f, 0.75f,
		0.125f, 0.25f,
		0.5f, 0.5f,
		0.0f, 0.0f,
		1.0f, 1.0f
	};

	gtk_init (&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* Build a nice curve */
	curve = rs_curve_widget_new();
	for (i=0; i<sizeof(scurve_knots)/(2*sizeof(scurve_knots[0])); i++)
	{
		/* Add knots to the curve */
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(curve), scurve_knots[2*i], scurve_knots[2*i+1]);
	}

	gtk_container_add(GTK_CONTAINER(window), curve);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(curve, "changed", G_CALLBACK(changed), NULL);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
#endif
