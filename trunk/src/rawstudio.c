#include <gtk/gtk.h>
#include <math.h> /* pow() */
#include <string.h> /* memset() */
#include "dcraw_api.h"
#include "rawstudio.h"
#include "gtk-interface.h"
#include "color.h"
#include "matrix.h"

#define GETVAL(adjustment) \
	gtk_adjustment_get_value((GtkAdjustment *) adjustment)
#define SETVAL(adjustment, value) \
	gtk_adjustment_set_value((GtkAdjustment *) adjustment, value)

guchar previewtable[65536];

void
update_previewtable(RS_BLOB *rs, const gdouble gamma, const gdouble contrast)
{
	gint n;
	gdouble nd;
	gint res;
	static double gammavalue;
	if (gammavalue == (contrast/gamma)) return;
	gammavalue = contrast/gamma;

	for(n=0;n<65536;n++)
	{
		nd = ((gdouble) n) / 65535.0;
		res = (gint) (pow(nd, gammavalue) * 255.0);
		_CLAMP255(res);
		previewtable[n] = res;
	}
}

void
rs_debug(RS_BLOB *rs)
{
	printf("rs: %d\n", (guint) rs);
	printf("rs->input: %d\n", (guint) rs->input);
	printf("rs->preview: %d\n", (guint) rs->preview);
	if(rs->input!=NULL)
	{
		printf("rs->input->w: %d\n", rs->input->w);
		printf("rs->input->h: %d\n", rs->input->h);
		printf("rs->input->pitch: %d\n", rs->input->pitch);
		printf("rs->input->channels: %d\n", rs->input->channels);
		printf("rs->input->pixels: %d\n", (guint) rs->input->pixels);
	}
	if(rs->preview!=NULL)
	{
		printf("rs->preview->w: %d\n", rs->preview->w);
		printf("rs->preview->h: %d\n", rs->preview->h);
		printf("rs->preview->pitch: %d\n", rs->preview->pitch);
		printf("rs->preview_scale: %d\n", rs->preview_scale);
		printf("rs->preview->pixels: %d\n", (guint) rs->preview->pixels);
	}
	printf("\n");
	return;
}

void
update_scaled(RS_BLOB *rs)
{
	guint y,x;
	guint srcoffset, destoffset;

	guint width, height;
	const guint scale = GETVAL(rs->scale);

	width=rs->input->w/scale;
	height=rs->input->h/scale;
	
	if (!rs->in_use) return;

	if (rs->preview==NULL)
	{
		rs->preview = rs_image_new(width, height, rs->input->channels);
		rs->preview_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	}

	/* 16 bit downscaled */
	if (rs->preview_scale != GETVAL(rs->scale)) /* do we need to? */
	{
		rs->preview_scale = GETVAL(rs->scale);
		rs_image_free(rs->preview);
		rs->preview = rs_image_new(width, height, rs->input->channels);
		for(y=0; y<rs->preview->h; y++)
		{
			destoffset = y*rs->preview->pitch*rs->preview->channels;
			srcoffset = y*rs->preview_scale*rs->input->pitch*rs->preview->channels;
			for(x=0; x<rs->preview->w; x++)
			{
				rs->preview->pixels[destoffset+R] = rs->input->pixels[srcoffset+R];
				rs->preview->pixels[destoffset+G] = rs->input->pixels[srcoffset+G];
				rs->preview->pixels[destoffset+B] = rs->input->pixels[srcoffset+B];
				if (rs->input->channels==4) rs->preview->pixels[destoffset+G2] = rs->input->pixels[srcoffset+G2];
				destoffset += rs->preview->channels;
				srcoffset += rs->preview_scale*rs->preview->channels;
			}
		}
		rs->preview_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, rs->preview->w, rs->preview->h);
		gtk_image_set_from_pixbuf(rs->preview_image, rs->preview_pixbuf);
		g_object_unref(rs->preview_pixbuf);
	}
	return;
}

void
update_preview(RS_BLOB *rs)
{
	RS_MATRIX4 mat;
	RS_MATRIX4Int mati;
	gint rowstride, x, y, srcoffset, destoffset;
	register gint r,g,b;
	guchar *pixels;

	if(!rs->in_use) return;

	SETVAL(rs->scale, floor(GETVAL(rs->scale))); // we only do integer scaling
	update_scaled(rs);
	update_previewtable(rs, GETVAL(rs->gamma), GETVAL(rs->contrast));
	matrix4_identity(&mat);
	matrix4_color_exposure(&mat, GETVAL(rs->exposure));
	matrix4_color_mixer(&mat, GETVAL(rs->rgb_mixer[R]), GETVAL(rs->rgb_mixer[G]), GETVAL(rs->rgb_mixer[B]));
	matrix4_color_saturate(&mat, GETVAL(rs->saturation));
	matrix4_color_hue(&mat, GETVAL(rs->hue));
	matrix4_to_matrix4int(&mat, &mati);

	pixels = gdk_pixbuf_get_pixels(rs->preview_pixbuf);
	rowstride = gdk_pixbuf_get_rowstride(rs->preview_pixbuf);
	memset(rs->histogram_table, 0x00, sizeof(guint)*3*256); // reset histogram
	for(y=0 ; y<rs->preview->h ; y++)
	{
		srcoffset = y * rs->preview->pitch * rs->preview->channels;
		destoffset = y * rowstride;
		for(x=0 ; x<rs->preview->w ; x++)
		{
			r = (rs->preview->pixels[srcoffset+R]*mati.coeff[0][0]
				+ rs->preview->pixels[srcoffset+G]*mati.coeff[0][1]
				+ rs->preview->pixels[srcoffset+B]*mati.coeff[0][2])>>MATRIX_RESOLUTION;
			g = (rs->preview->pixels[srcoffset+R]*mati.coeff[1][0]
				+ rs->preview->pixels[srcoffset+G]*mati.coeff[1][1]
				+ rs->preview->pixels[srcoffset+B]*mati.coeff[1][2])>>MATRIX_RESOLUTION;
			b = (rs->preview->pixels[srcoffset+R]*mati.coeff[2][0]
				+ rs->preview->pixels[srcoffset+G]*mati.coeff[2][1]
				+ rs->preview->pixels[srcoffset+B]*mati.coeff[2][2])>>MATRIX_RESOLUTION;
			_CLAMP65535_TRIPLET(r,g,b);
			pixels[destoffset] = previewtable[r];
			rs->histogram_table[R][pixels[destoffset++]]++;
			pixels[destoffset] = previewtable[g];
			rs->histogram_table[G][pixels[destoffset++]]++;
			pixels[destoffset] = previewtable[b];
			rs->histogram_table[B][pixels[destoffset++]]++;
			srcoffset+=rs->preview->channels; /* increment srcoffset by rs->preview->pixels */
		}
	}
	update_histogram(rs);
	gtk_image_set_from_pixbuf(rs->preview_image, rs->preview_pixbuf);
	return;
}	

void
rs_reset(RS_BLOB *rs)
{
	guint c;
	gtk_adjustment_set_value((GtkAdjustment *) rs->exposure, 0.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->gamma, 2.2);
	gtk_adjustment_set_value((GtkAdjustment *) rs->saturation, 1.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->hue, 0.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->contrast, 1.0);
	for(c=0;c<3;c++)
		gtk_adjustment_set_value((GtkAdjustment *) rs->rgb_mixer[c], rs->raw->pre_mul[c]);
	rs->preview_scale = 0;
	return;
}

void
rs_free_raw(RS_BLOB *rs)
{
	dcraw_close(rs->raw);
	g_free(rs->raw);
	rs->raw = NULL;
}

void
rs_free(RS_BLOB *rs)
{
	if (rs->in_use)
	{
		g_free(rs->input->pixels);
		rs->input->pixels=0;
		rs->input->w=0;
		rs->input->h=0;
		if (rs->raw!=NULL)
			rs_free_raw(rs);
		if (rs->input!=NULL)
			rs_image_free(rs->input);
		if (rs->preview!=NULL)
			rs_image_free(rs->preview);
		rs->input=NULL;
		rs->preview=NULL;
		rs->in_use=FALSE;
	}
}

RS_IMAGE *
rs_image_new(const guint width, const guint height, const guint channels)
{
	RS_IMAGE *rsi;
	rsi = (RS_IMAGE *) g_malloc(sizeof(RS_IMAGE));
	rsi->w = width;
	rsi->h = height;
	rsi->pitch = PITCH(width);
	rsi->channels = channels;
	rsi->pixels = (gushort *) g_malloc(sizeof(gushort)*rsi->h*rsi->pitch*rsi->channels);
	return(rsi);
}

void
rs_image_free(RS_IMAGE *rsi)
{
	if (rsi!=NULL)
	{
		g_assert(rsi->pixels!=NULL);
		g_free(rsi->pixels);
		g_assert(rsi!=NULL);
		g_free(rsi);
	}
	return;
}

RS_BLOB *
rs_new()
{
	RS_BLOB *rs;
	guint c;
	rs = g_malloc(sizeof(RS_BLOB));

	rs->exposure = make_adj(rs, 0.0, -2.0, 2.0, 0.1, 0.5);
	rs->gamma = make_adj(rs, 2.2, 0.0, 3.0, 0.1, 0.5);
	rs->saturation = make_adj(rs, 1.0, 0.0, 3.0, 0.1, 0.5);
	rs->hue = make_adj(rs, 0.0, 0.0, 360.0, 0.5, 30.0);
	rs->contrast = make_adj(rs, 1.0, 0.0, 3.0, 0.1, 0.1);
	rs->scale = make_adj(rs, 2.0, 1.0, 5.0, 1.0, 1.0);
	for(c=0;c<3;c++)
		rs->rgb_mixer[c] = make_adj(rs, 0.0, 0.0, 5.0, 0.1, 0.5);
	rs->raw = NULL;
	rs->input = NULL;
	rs->preview = NULL;
	rs->in_use = FALSE;
	return(rs);
}

void
rs_load_raw_from_memory(RS_BLOB *rs)
{
	gushort *src = (gushort *) rs->raw->raw.image;
	guint x,y;
	guint srcoffset, destoffset;

	for (y=0; y<rs->raw->raw.height; y++)
	{
#ifdef __i386__
		destoffset = (guint) (rs->input->pixels + y*rs->input->pitch * rs->input->channels);
		srcoffset = (guint) (src + y * rs->input->w * rs->input->channels);
		x = rs->raw->raw.width;
		while(x)
		{
			asm volatile (
				"xorl %%ecx, %%ecx\n\t" /* set %ecx to zero */
				"movw (%1), %%eax\n\t" /* copy source into register */
				"subl %2, %%eax\n\t" /* subtract black */
				"cmovs %%ecx, %%eax\n\t" /* if negative, set to %ecx */
				"sall $4, %%eax\n\t" /* bitshift (12 -> 16 bits) */
				"movl %%eax, (%0)\n\t" /* copy to dest */

				"add $2, %0\n\t" /* increment destination pointer */
				"add $2, %1\n\t" /* increment source pointer */
				"movw (%1), %%ebx\n\t"
				"subl %2, %%ebx\n\t"
				"cmovs %%ecx, %%ebx\n\t"
				"sall $4, %%ebx\n\t"
				"movl %%ebx, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"
				"movw (%1), %%eax\n\t"
				"subl %2, %%eax\n\t"
				"cmovs %%ecx, %%eax\n\t"
				"sall $4, %%eax\n\t"
				"movl %%eax, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"
				"movw (%1), %%ebx\n\t"
				"subl %2, %%ebx\n\t"
				"cmovs %%ecx, %%ebx\n\t"
				"sall $4, %%ebx\n\t"
				"movl %%ebx, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"

				: "+r" (destoffset), "+r" (srcoffset)
				: "r" (rs->raw->black)
				: "%eax", "%ebx", "%ecx"
			);
			x--;
		}
#else
		destoffset = y*rs->input->pitch*rs->input->channels;
		srcoffset = y*rs->input->w*rs->input->channels;
		for (x=0; x<rs->raw->raw.width; x++)
		{
			register gint r,g,b;
			r = (src[srcoffset++] - rs->raw->black)<<4;
			g = (src[srcoffset++] - rs->raw->black)<<4;
			b = (src[srcoffset++] - rs->raw->black)<<4;
			_CLAMP65535_TRIPLET(r, g, b);
			rs->input->pixels[destoffset++] = r;
			rs->input->pixels[destoffset++] = g;
			rs->input->pixels[destoffset++] = b;

			if (rs->input->channels==4)
			{
				g = (src[srcoffset++] - rs->raw->black)<<4;
				_CLAMP65535(g);
				rs->input->pixels[destoffset++] = g;
			}
		}
#endif
	}
	rs->in_use=TRUE;
	return;
}

void
rs_load_raw_from_file(RS_BLOB *rs, const gchar *filename)
{
	dcraw_data *raw;

	if (rs->raw!=NULL) rs_free_raw(rs);
	raw = (dcraw_data *) g_malloc(sizeof(dcraw_data));
	dcraw_open(raw, (char *) filename);
	dcraw_load_raw(raw);
	rs_image_free(rs->input); /*FIXME: free preview */
	rs->input = NULL;
	rs->input = rs_image_new(raw->raw.width, raw->raw.height, 4);
	rs->raw = raw;
	rs_load_raw_from_memory(rs);
	update_preview(rs);
	return;
}


int
main(int argc, char **argv)
{
	gui_init(argc, argv);
	return(0);
}
