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

#include <gtk/gtk.h>
#include <math.h>
#include "matrix.h"
#include "rs-batch.h"
#include "rawstudio.h"
#include "rawfile.h"
#include "ciff-meta.h"

gboolean raw_crw_walker(RAWFILE *rawfile, guint offset, guint length, RS_METADATA *meta);

gboolean
raw_crw_walker(RAWFILE *rawfile, guint offset, guint length, RS_METADATA *meta)
{
	guint valuedata=0;
	gushort records=0;
	guint blockstart = offset;
	raw_get_uint(rawfile, offset+length-4, &valuedata);
	valuedata += offset;
	raw_get_ushort(rawfile, valuedata, &records);
	offset = valuedata+2;

	while (records--)
	{
		gushort type=0;
		guint size=0;
		guint absoffset=0;
		guint reloffset=0;
		guint uint_temp1=0;
		gushort ushort_temp1=0;
		gushort wbi=0;
		raw_get_ushort(rawfile, offset, &type);
		raw_get_uint(rawfile, offset+2, &size);
		raw_get_uint(rawfile, offset+6, &reloffset);
		absoffset = blockstart + reloffset;

		switch (type)
		{
			case 0x2007: /* Preview image */
				meta->preview_start = absoffset;
				meta->preview_length = size;
				break;
			case 0x2008: /* Thumbnail image */
				meta->thumbnail_start = absoffset;
				meta->thumbnail_length = size;
				break;
			case 0x1810: /* ImageInfo */
				raw_get_uint(rawfile, absoffset+12, &uint_temp1); /* Orientation */
				meta->orientation = uint_temp1;
				break;
			case 0x10a9: /* white balance for D60, 10D, 300D */
				if (size > 66)
					wbi = "0134567028"[wbi]-'0';
				absoffset += 2+wbi*8;
				raw_get_ushort(rawfile, absoffset, &ushort_temp1); /* R */
				meta->cam_mul[0] = ushort_temp1;
				raw_get_ushort(rawfile, absoffset+2, &ushort_temp1); /* G */
				meta->cam_mul[1] = ushort_temp1;
				raw_get_ushort(rawfile, absoffset+4, &ushort_temp1); /* G */
				meta->cam_mul[3] = ushort_temp1;
				raw_get_ushort(rawfile, absoffset+6, &ushort_temp1); /* B */
				meta->cam_mul[2] = ushort_temp1;
				break;
			case 0x102a: /* CanonShotInfo */
				raw_get_ushort(rawfile, absoffset+4, &ushort_temp1); /* iso */
				meta->iso = pow(2, ushort_temp1/32.0 - 4) * 50;

				raw_get_ushort(rawfile, absoffset+8, &ushort_temp1); /* aperture */
				meta->aperture = pow(2, ushort_temp1/64.0);

				raw_get_ushort(rawfile, absoffset+10, &ushort_temp1); /* shutter */
				meta->shutterspeed = 1.0/pow(2,-((short)ushort_temp1)/32.0);

				raw_get_ushort(rawfile, absoffset+14, &wbi);
				if (wbi > 17)
					wbi = 0;
				break;
			default:
				if (type >> 8 == 0x28 || type >> 8 == 0x30)
					raw_crw_walker(rawfile, absoffset, size, meta);
				break;
		}
		offset+=10;
	}
	return(TRUE);
}

void
rs_ciff_load_meta(const gchar *filename, RS_METADATA *meta)
{
	guint root=0;
	RAWFILE *rawfile;
	rawfile = raw_open_file(filename);
	if (0 != raw_strcmp(rawfile, 6, "HEAPCCDR", 8))
		return;
	raw_get_uint(rawfile, 2, &root);
	raw_crw_walker(rawfile, root, rawfile->size-root, meta);
	raw_close_file(rawfile);
	return;
}

GdkPixbuf *
rs_ciff_load_thumb(const gchar *src)
{
	GdkPixbuf *pixbuf = NULL, *pixbuf2 = NULL;
	gdouble ratio;
	guint start=0, length=0, root=0;
	RS_METADATA *m;
	RAWFILE *rawfile;
	gchar *thumbname;
	m = rs_metadata_new();

	raw_init();
	
	thumbname = rs_thumb_get_name(src);
	if (thumbname)
	{
		if (g_file_test(thumbname, G_FILE_TEST_EXISTS))
		{
			pixbuf = gdk_pixbuf_new_from_file(thumbname, NULL);
			g_free(thumbname);
			if (pixbuf) return(pixbuf);
		}
	}

	rawfile = raw_open_file(src);
	if (0 != raw_strcmp(rawfile, 6, "HEAPCCDR", 8))
		return(NULL);
	raw_get_uint(rawfile, 2, &root);
	raw_crw_walker(rawfile, root, rawfile->size-root, m);

	if ((m->thumbnail_start>0) && (m->thumbnail_length>0))
	{
		start = m->thumbnail_start;
		length = m->thumbnail_length;
	}

	else if ((m->preview_start>0) && (m->preview_length>0))
	{
		start = m->preview_start;
		length = m->preview_length;
	}

	if ((start>0) && (length>0))
	{
		pixbuf = raw_get_pixbuf(rawfile, start, length);

		ratio = ((gdouble) gdk_pixbuf_get_width(pixbuf))/((gdouble) gdk_pixbuf_get_height(pixbuf));
		if (ratio>1.0)
			pixbuf2 = gdk_pixbuf_scale_simple(pixbuf, 128, (gint) (128.0/ratio), GDK_INTERP_BILINEAR);
		else
			pixbuf2 = gdk_pixbuf_scale_simple(pixbuf, (gint) (128.0*ratio), 128, GDK_INTERP_BILINEAR);
		g_object_unref(pixbuf);
		pixbuf = pixbuf2;
		switch (m->orientation)
		{
			/* this is very COUNTER-intuitive - gdk_pixbuf_rotate_simple() is wierd */
			case 90:
				pixbuf2 = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
				g_object_unref(pixbuf);
				pixbuf = pixbuf2;
				break;
			case 270:
				pixbuf2 = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
				g_object_unref(pixbuf);
				pixbuf = pixbuf2;
				break;
		}
		if (thumbname)
			gdk_pixbuf_save(pixbuf, thumbname, "png", NULL, NULL);
	}
	raw_close_file(rawfile);
	rs_metadata_free(m);
	return(pixbuf);
}
