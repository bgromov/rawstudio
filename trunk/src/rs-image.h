/*
 * Copyright (C) 2006-2008 Anders Brander <anders@brander.dk> and 
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

#ifndef RS_IMAGE_H
#define RS_IMAGE_H

#include <glib-object.h>

#define RS_TYPE_IMAGE16        (rs_image16_get_type ())
#define RS_IMAGE16(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_IMAGE16, RS_IMAGE16))
#define RS_IMAGE16_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_IMAGE16, RS_IMAGE16Class))
#define RS_IS_IMAGE16(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_IMAGE16))
#define RS_IS_IMAGE16_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_IMAGE16))
#define RS_IMAGE16_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_IMAGE16, RS_IMAGE16Class))

typedef struct _RS_IMAGE16Class RS_IMAGE16Class;

struct _RS_IMAGE16Class {
	GObjectClass parent;
};

GType rs_image16_get_type (void);

typedef enum {
	RS_DEMOSAIC_BILINEAR,
	RS_DEMOSAIC_PPG,
} RS_DEMOSAIC;

#define rs_image16_scale(in, out, scale) rs_image16_scale_double(in, out, scale)
#define rs_image16_free(image) g_object_unref(image)
#define rs_image8_free(image) rs_image8_unref(image)

/**
 * Convenience macro to get a pixel at specific position
 * @param image RS_IMAGE8 or RS_IMAGE16
 * @param x X coordinate (column)
 * @param y Y coordinate (row)
 */
#define GET_PIXEL(image, x, y) ((image)->pixels + (y)*(image)->rowstride + (x)*(image)->pixelsize)

#define GET_PIXBUF_PIXEL(pixbuf, x, y) (gdk_pixbuf_get_pixels((pixbuf)) + (y)*gdk_pixbuf_get_rowstride((pixbuf)) + (x)*gdk_pixbuf_get_n_channels((pixbuf)))

#define rs_image16_ref(image) g_object_ref(image)
#define rs_image16_unref(image) g_object_unref(image)

extern RS_IMAGE16 *rs_image16_new(const guint width, const guint height, const guint channels, const guint pixelsize);

/**
 * Renders an exposure map on top of an GdkPixbuf with 3 channels
 * @param pixbuf A GdkPixbuf
 * @param only_row A single row to render or -1 to render all
 */
extern void gdk_pixbuf_render_exposure_mask(GdkPixbuf *pixbuf, gint only_row);

extern void rs_image16_orientation(RS_IMAGE16 *rsi, gint orientation);
extern void rs_image16_transform_getwh(RS_IMAGE16 *in, RS_RECT *crop, gdouble angle, gint orientation, gint *w, gint *h);

/**
 * Transforms an RS_IMAGE16
 * @param in An input image
 * @param out An output image or NULL
 * @param affine Will be set to forward affine matrix if not NULL.
 * @param inverse_affine Will be set to inverse affine matrix if not NULL.
 * @param crop Crop to apply or NULL
 * @param width Output width or -1
 * @param height Output height or -1
 * @param keep_aspect if set to TRUE aspect will be locked
 * @param scale How much to scale the image (0.01 - 2.0)
 * @param angle Rotation angle in degrees
 * @param orientation The orientation
 * @param actual_scale The resulting scale or NULL
 * @return A new RS_IMAGE16 or out
 */
extern RS_IMAGE16 *rs_image16_transform(RS_IMAGE16 *in, RS_IMAGE16 *out, RS_MATRIX3 *affine, RS_MATRIX3 *inverse_affine,
	RS_RECT *crop, gint width, gint height, gboolean keep_aspect, gdouble scale, gdouble angle, gint orientation, gdouble *actual_scale);
extern RS_IMAGE16 *rs_image16_scale_double(RS_IMAGE16 *in, RS_IMAGE16 *out, gdouble scale);
extern RS_IMAGE16 *rs_image16_copy(RS_IMAGE16 *rsi, gboolean copy_pixels);
extern RS_IMAGE16 *rs_image16_convolve(RS_IMAGE16 *input, RS_IMAGE16 *output, RS_MATRIX3 *matrix, gfloat scaler, gboolean *abort);

/**
 * Returns a single pixel from a RS_IMAGE16
 * @param image A RS_IMAGE16
 * @param x X coordinate (column)
 * @param y Y coordinate (row)
 * @param extend_edges Tries to extend edges beyond image borders if TRUE
 */
extern inline gushort *rs_image16_get_pixel(RS_IMAGE16 *image, gint x, gint y, gboolean extend_edges);

extern size_t rs_image16_get_footprint(RS_IMAGE16 *image);
extern RS_IMAGE16 *rs_image16_sharpen(RS_IMAGE16 *in, RS_IMAGE16 *out, gdouble amount, gboolean *abort);

/**
 * Copies an RS_IMAGE16, making it double size in the process
 * @param in The input image
 * @param out The output image or NULL
 */
extern RS_IMAGE16 *(*rs_image16_copy_double)(RS_IMAGE16 *in, RS_IMAGE16 *out);
extern RS_IMAGE16 *rs_image16_copy_double_c(RS_IMAGE16 *in, RS_IMAGE16 *out);
#if defined (__i386__) || defined (__x86_64__)
extern RS_IMAGE16 *rs_image16_copy_double_mmx(RS_IMAGE16 *in, RS_IMAGE16 *out);
#endif /* defined (__i386__) || defined (__x86_64__) */

/**
 * Demosaics a RS_IMAGE16
 * @param image The image to demosaic, this MUST be preprocessed, ie. doubled in size
 * @param demosaic The demosaic algorithm to use
 * @return FALSE if the image was not suited for demosaic, TRUE if we succeed
 */
gboolean rs_image16_demosaic(RS_IMAGE16 *image, RS_DEMOSAIC demosaic);

#endif
