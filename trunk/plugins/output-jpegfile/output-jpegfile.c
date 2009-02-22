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
#include <jpeglib.h>
#include <gettext.h>
#include "config.h"

/* stat() */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* open() */
#include <fcntl.h>

#define RS_TYPE_JPEGFILE (rs_jpegfile_type)
#define RS_JPEGFILE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_JPEGFILE, RSJpegfile))
#define RS_JPEGFILE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_JPEGFILE, RSJpegfileClass))
#define RS_IS_JPEGFILE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_JPEGFILE))

typedef struct _RSJpegfile RSJpegfile;
typedef struct _RSJpegfileClass RSJpegfileClass;

struct _RSJpegfile {
	RSOutput parent;

	gchar *filename;
	gint quality;
};

struct _RSJpegfileClass {
	RSOutputClass parent_class;
};

RS_DEFINE_OUTPUT(rs_jpegfile, RSJpegfile)

enum {
	PROP_0,
	PROP_FILENAME,
	PROP_QUALITY
};

static void get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static gboolean execute8(RSOutput *output, GdkPixbuf *pixbuf);

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	rs_jpegfile_get_type(G_TYPE_MODULE(plugin));
}

static void
rs_jpegfile_class_init(RSJpegfileClass *klass)
{
	RSOutputClass *output_class = RS_OUTPUT_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = get_property;
	object_class->set_property = set_property;

	g_object_class_install_property(object_class,
		PROP_FILENAME, g_param_spec_string(
			"filename", "filename", "Full export path",
			NULL, G_PARAM_READWRITE)
	);

	g_object_class_install_property(object_class,
		PROP_QUALITY, g_param_spec_int(
			"quality", "JPEG Quality", _("JPEG Quality"),
			10, 100, 90, G_PARAM_READWRITE)
	);

	output_class->execute8 = execute8;
	output_class->extension = "jpg";
	output_class->display_name = _("JPEG (Joint Photographic Experts Group)");
}

static void
rs_jpegfile_init(RSJpegfile *jpegfile)
{
	jpegfile->filename = NULL;
	jpegfile->quality = 90;
}

static void
get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	RSJpegfile *jpegfile = RS_JPEGFILE(object);

	switch (property_id)
	{
		case PROP_FILENAME:
			g_value_set_string(value, jpegfile->filename);
			break;
		case PROP_QUALITY:
			g_value_set_int(value, jpegfile->quality);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	RSJpegfile *jpegfile = RS_JPEGFILE(object);

	switch (property_id)
	{
		case PROP_FILENAME:
			jpegfile->filename = g_value_dup_string(value);
			break;
		case PROP_QUALITY:
			jpegfile->quality = g_value_get_int(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

/* This following function is an almost verbatim copy from little cms. Thanks Marti, you rock! */

#define ICC_MARKER  (JPEG_APP0 + 2) /* JPEG marker code for ICC */
#define ICC_OVERHEAD_LEN  14        /* size of non-profile data in APP2 */
#define MAX_BYTES_IN_MARKER  65533  /* maximum data len of a JPEG marker */
#define MAX_DATA_BYTES_IN_MARKER  (MAX_BYTES_IN_MARKER - ICC_OVERHEAD_LEN)
#define ICC_MARKER_IDENT "ICC_PROFILE"

static void rs_jpeg_write_icc_profile(j_compress_ptr cinfo, const JOCTET *icc_data_ptr, guint icc_data_len);

static void
rs_jpeg_write_icc_profile(j_compress_ptr cinfo, const JOCTET *icc_data_ptr, guint icc_data_len)
{
	gchar *ident = ICC_MARKER_IDENT;
	guint num_markers; /* total number of markers we'll write */
	gint cur_marker = 1;       /* per spec, counting starts at 1 */
	guint length;      /* number of bytes to write in this marker */

	num_markers = icc_data_len / MAX_DATA_BYTES_IN_MARKER;
	if (num_markers * MAX_DATA_BYTES_IN_MARKER != icc_data_len)
		num_markers++;
	while (icc_data_len > 0)
	{
		length = icc_data_len;
		if (length > MAX_DATA_BYTES_IN_MARKER)
			length = MAX_DATA_BYTES_IN_MARKER;
		icc_data_len -= length;
		jpeg_write_m_header(cinfo, ICC_MARKER, (guint) (length + ICC_OVERHEAD_LEN));

		do {
			jpeg_write_m_byte(cinfo, *ident);
		} while(*ident++);
		jpeg_write_m_byte(cinfo, cur_marker);
		jpeg_write_m_byte(cinfo, (gint) num_markers);

		while (length--)
		{
			jpeg_write_m_byte(cinfo, *icc_data_ptr);
			icc_data_ptr++;
		}
		cur_marker++;
	}
	return;
}

static gboolean
execute8(RSOutput *output, GdkPixbuf *pixbuf)
{
	RSJpegfile *jpegfile = RS_JPEGFILE(output);
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE * outfile;
	JSAMPROW row_pointer[1];
	gchar *profile_filename = NULL; /* FIXME: Fix this somehow */

	guchar *buffer;
	guint len;
	gint fd;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	if ((outfile = fopen(jpegfile->filename, "wb")) == NULL)
		return(FALSE);
	jpeg_stdio_dest(&cinfo, outfile);
	cinfo.image_width = gdk_pixbuf_get_width(pixbuf);
	cinfo.image_height = gdk_pixbuf_get_height(pixbuf);
	cinfo.input_components = gdk_pixbuf_get_n_channels(pixbuf);
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, jpegfile->quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);
	if (profile_filename)
	{
		struct stat st;
		stat(profile_filename, &st);
		if (st.st_size>0)
			if ((fd = open(profile_filename, O_RDONLY)) != -1)
			{
				gint bytes_read = 0;
				len = st.st_size;
				buffer = g_malloc(len);
				while(bytes_read < len)
					bytes_read += read(fd, buffer+bytes_read, len-bytes_read);
				close(fd);
				rs_jpeg_write_icc_profile(&cinfo, buffer, len);
				g_free(buffer);
			}
	}
	while (cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer[0] = GET_PIXBUF_PIXEL(pixbuf, 0, cinfo.next_scanline);
		if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1)
			break;
	}
	jpeg_finish_compress(&cinfo);
	fclose(outfile);
	jpeg_destroy_compress(&cinfo);
	return(TRUE);
}
