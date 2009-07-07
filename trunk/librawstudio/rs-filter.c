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
#include "rs-filter.h"

#if 0 /* Change to 1 to enable debugging info */
#define filter_debug g_debug
#else
#define filter_debug(...)
#endif

G_DEFINE_TYPE (RSFilter, rs_filter, G_TYPE_OBJECT)

enum {
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rs_filter_class_init(RSFilterClass *klass)
{
	filter_debug("rs_filter_class_init(%p)", klass);

	signals[CHANGED_SIGNAL] = g_signal_new ("changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	klass->get_image = NULL;
	klass->get_image8 = NULL;
	klass->get_width = NULL;
	klass->get_height = NULL;
	klass->previous_changed = NULL;
}

static void
rs_filter_init(RSFilter *self)
{
	filter_debug("rs_filter_init(%p)", self);
	self->previous = NULL;
	self->next_filters = NULL;
	self->enabled = TRUE;
}

/**
 * Return a new instance of a RSFilter
 * @param name The name of the filter
 * @param previous The previous filter or NULL
 * @return The newly instantiated RSFilter or NULL
 */
RSFilter *
rs_filter_new(const gchar *name, RSFilter *previous)
{
	filter_debug("rs_filter_new(%s, %s [%p])", name, RS_FILTER_NAME(previous), previous);
	g_assert(name != NULL);
	g_assert((previous == NULL) || RS_IS_FILTER(previous));

	GType type = g_type_from_name(name);
	RSFilter *filter = NULL;

	if (g_type_is_a (type, RS_TYPE_FILTER))
		filter = g_object_new(type, NULL);

	if (!RS_IS_FILTER(filter))
		g_warning("Could not instantiate filter of type \"%s\"", name);

	if (previous)
		rs_filter_set_previous(filter, previous);

	return filter;
}

static void
rs_filter_weak_unlink(gpointer data, GObject *where_the_object_was)
{
	RSFilter *filter = RS_FILTER(data);
	RSFilter *next = RS_FILTER(where_the_object_was);

	filter->next_filters = g_slist_remove(filter->next_filters, next);
}

/**
 * Set the previous RSFilter in a RSFilter-chain
 * @param filter A RSFilter
 * @param previous A previous RSFilter or NULL
 */
void
rs_filter_set_previous(RSFilter *filter, RSFilter *previous)
{
	filter_debug("rs_filter_set_previous(%p, %p)", filter, previous);
	g_assert(RS_IS_FILTER(filter));
	g_assert(RS_IS_FILTER(previous));

	if (filter->previous && (filter->previous != previous))
	{
		g_object_weak_unref(G_OBJECT(filter), rs_filter_weak_unlink, previous);
		filter->previous->next_filters = g_slist_remove(filter->previous->next_filters, filter);
	}

	filter->previous = previous;
	previous->next_filters = g_slist_append(previous->next_filters, filter);

	g_object_weak_ref(G_OBJECT(filter), rs_filter_weak_unlink, previous);
}

/**
 * Signal that a filter has changed, filters depending on this will be invoked
 * This should only be called from filter code
 * @param filter The changed filter
 * @param mask A mask indicating what changed
 */
void
rs_filter_changed(RSFilter *filter, RSFilterChangedMask mask)
{
	filter_debug("rs_filter_changed(%s [%p], %04x)", RS_FILTER_NAME(filter), filter, mask);
	g_assert(RS_IS_FILTER(filter));

	gint i, n_next = g_slist_length(filter->next_filters);

	for(i=0; i<n_next; i++)
	{
		RSFilter *next = RS_FILTER(g_slist_nth_data(filter->next_filters, i));

		g_assert(RS_IS_FILTER(next));

		/* Notify "next" filter or try "next next" filter */
		if (RS_FILTER_GET_CLASS(next)->previous_changed)
			RS_FILTER_GET_CLASS(next)->previous_changed(next, filter, mask);
		else
			rs_filter_changed(next, mask);
	}

	g_signal_emit(G_OBJECT(filter), signals[CHANGED_SIGNAL], 0, mask);
}

/**
 * Get the output image from a RSFilter
 * @param filter A RSFilter
 * @param param A RSFilterParam defining parameters for a image request
 * @return A RS_IMAGE16, this must be unref'ed
 */
RSFilterResponse *
rs_filter_get_image(RSFilter *filter, const RSFilterParam *param)
{
	filter_debug("rs_filter_get_image(%s [%p])", RS_FILTER_NAME(filter), filter);

	/* This timer-hack will break badly when multithreaded! */
	static gfloat last_elapsed = 0.0;
	static count = -1;
	gfloat elapsed;
	static GTimer *gt = NULL;

	RSFilterResponse *response;
	RS_IMAGE16 *image;
	g_assert(RS_IS_FILTER(filter));

	if (count == -1)
		gt = g_timer_new();
	count++;

	if (RS_FILTER_GET_CLASS(filter)->get_image && filter->enabled)
		response = RS_FILTER_GET_CLASS(filter)->get_image(filter, param);
	else
		response = rs_filter_get_image(filter->previous, param);

	g_assert(RS_IS_FILTER_RESPONSE(response));

	image = rs_filter_response_get_image(response);

	elapsed = g_timer_elapsed(gt, NULL) - last_elapsed;

	printf("%s took: \033[32m%.0f\033[0mms", RS_FILTER_NAME(filter), elapsed*1000);
	if ((elapsed > 0.001) && (image != NULL))
		printf(" [\033[33m%.01f\033[0mMpix/s]", ((gfloat)(image->w*image->h))/elapsed/1000000.0);
	if (image)
		printf(" [w: %d, h: %d, channels: %d, pixelsize: %d, rowstride: %d]",
			image->w, image->h, image->channels, image->pixelsize, image->rowstride);
	printf("\n");
	last_elapsed += elapsed;

	g_assert(RS_IS_IMAGE16(image) || (image == NULL));

	count--;
	if (count == -1)
	{
		last_elapsed = 0.0;
		printf("Complete chain took: \033[32m%.0f\033[0mms\n\n", g_timer_elapsed(gt, NULL)*1000.0);
		g_timer_destroy(gt);
	}

	if (image)
		g_object_unref(image);

	return response;
}

/**
 * Get 8 bit output image from a RSFilter
 * @param filter A RSFilter
 * @param param A RSFilterParam defining parameters for a image request
 * @return A RS_IMAGE16, this must be unref'ed
 */
RSFilterResponse *
rs_filter_get_image8(RSFilter *filter, const RSFilterParam *param)
{
	filter_debug("rs_filter_get_image8(%s [%p])", RS_FILTER_NAME(filter), filter);

	/* This timer-hack will break badly when multithreaded! */
	static gfloat last_elapsed = 0.0;
	static count = -1;
	gfloat elapsed;
	static GTimer *gt = NULL;

	RSFilterResponse *response;
	GdkPixbuf *image = NULL;
	g_assert(RS_IS_FILTER(filter));

	if (count == -1)
		gt = g_timer_new();
	count++;

	if (RS_FILTER_GET_CLASS(filter)->get_image8 && filter->enabled)
		response = RS_FILTER_GET_CLASS(filter)->get_image8(filter, param);
	else if (filter->previous)
		response = rs_filter_get_image8(filter->previous, param);

	g_assert(RS_IS_FILTER_RESPONSE(response));

	image = rs_filter_response_get_image8(response);
	elapsed = g_timer_elapsed(gt, NULL) - last_elapsed;

	printf("%s took: \033[32m%.0f\033[0mms", RS_FILTER_NAME(filter), elapsed*1000);
	if ((elapsed > 0.001) && (image != NULL))
		printf(" [\033[33m%.01f\033[0mMpix/s]", ((gfloat)(gdk_pixbuf_get_width(image)*gdk_pixbuf_get_height(image)))/elapsed/1000000.0);
	printf("\n");
	last_elapsed += elapsed;

	g_assert(GDK_IS_PIXBUF(image) || (image == NULL));

	count--;
	if (count == -1)
	{
		last_elapsed = 0.0;
		printf("Complete chain took: \033[32m%.0f\033[0mms\n\n", g_timer_elapsed(gt, NULL)*1000.0);
		g_timer_destroy(gt);
	}

	if (image)
		g_object_unref(image);

	return response;
}

/**
 * Get the ICC profile from a filter
 * @param filter A RSFilter
 * @return A RSIccProfile, must be unref'ed
 */
extern RSIccProfile *rs_filter_get_icc_profile(RSFilter *filter)
{
	RSIccProfile *profile;
	g_assert(RS_IS_FILTER(filter));

	if (RS_FILTER_GET_CLASS(filter)->get_icc_profile)
		profile = RS_FILTER_GET_CLASS(filter)->get_icc_profile(filter);
	else
		profile = rs_filter_get_icc_profile(filter->previous);

	g_assert(RS_IS_ICC_PROFILE(profile));

	return profile;
}

/**
 * Get the returned width of a RSFilter
 * @param filter A RSFilter
 * @return Width in pixels
 */
gint
rs_filter_get_width(RSFilter *filter)
{
	gint width;
	g_assert(RS_IS_FILTER(filter));

	if (RS_FILTER_GET_CLASS(filter)->get_width && filter->enabled)
		width = RS_FILTER_GET_CLASS(filter)->get_width(filter);
	else
		width = rs_filter_get_width(filter->previous);

	return width;
}

/**
 * Get the returned height of a RSFilter
 * @param filter A RSFilter
 * @return Height in pixels
 */
gint
rs_filter_get_height(RSFilter *filter)
{
	gint height;
	g_assert(RS_IS_FILTER(filter));

	if (RS_FILTER_GET_CLASS(filter)->get_height && filter->enabled)
		height = RS_FILTER_GET_CLASS(filter)->get_height(filter);
	else
		height = rs_filter_get_height(filter->previous);

	return height;
}

/**
 * Set enabled state of a RSFilter
 * @param filter A RSFilter
 * @param enabled TRUE to enable filter, FALSE to disable
 * @return Previous state
 */
gboolean
rs_filter_set_enabled(RSFilter *filter, gboolean enabled)
{
	gboolean previous_state;

	g_assert(RS_IS_FILTER(filter));

	previous_state = filter->enabled;

	if (filter->enabled != enabled)
	{
		filter->enabled = enabled;
		rs_filter_changed(filter, RS_FILTER_CHANGED_PIXELDATA);
	}

	return previous_state;
}

/**
 * Get enabled state of a RSFilter
 * @param filter A RSFilter
 * @return TRUE if filter is enabled, FALSE if disabled
 */
gboolean
rs_filter_get_enabled(RSFilter *filter)
{
	g_assert(RS_IS_FILTER(filter));

	return filter->enabled;
}
