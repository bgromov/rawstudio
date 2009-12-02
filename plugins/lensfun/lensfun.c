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

/* Plugin tmpl version 4 */

#include <rawstudio.h>
#include <lensfun.h>

#define RS_TYPE_LENSFUN (rs_lensfun_type)
#define RS_LENSFUN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_LENSFUN, RSLensfun))
#define RS_LENSFUN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_LENSFUN, RSLensfunClass))
#define RS_IS_LENSFUN(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_LENSFUN))

typedef struct _RSLensfun RSLensfun;
typedef struct _RSLensfunClass RSLensfunClass;

struct _RSLensfun {
	RSFilter parent;

	lfDatabase *ldb;

	gchar *make;
	gchar *model;
	RSLens *lens;
	gchar *lens_make;
	gchar *lens_model;
	gfloat focal;
	gfloat aperture;
	gfloat tca_kr;
	gfloat tca_kb;
	gfloat vignetting_k1;
	gfloat vignetting_k2;
	gfloat vignetting_k3;

	const lfLens *selected_lens;
	const lfCamera *selected_camera;

	gboolean DIRTY;
};

struct _RSLensfunClass {
	RSFilterClass parent_class;
};

RS_DEFINE_FILTER(rs_lensfun, RSLensfun)

enum {
	PROP_0,
	PROP_MAKE,
	PROP_MODEL,
	PROP_LENS,
	PROP_LENS_MAKE,
	PROP_LENS_MODEL,
	PROP_FOCAL,
	PROP_APERTURE,
	PROP_TCA_KR,
	PROP_TCA_KB,
	PROP_VIGNETTING_K1,
	PROP_VIGNETTING_K2,
	PROP_VIGNETTING_K3,
};

static void get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static RSFilterResponse *get_image(RSFilter *filter, const RSFilterRequest *request);
static void inline rs_image16_nearest_full(RS_IMAGE16 *in, gushort *out, gfloat *pos);
static void inline rs_image16_bilinear_full(RS_IMAGE16 *in, gushort *out, gfloat *pos);

static RSFilterClass *rs_lensfun_parent_class = NULL;

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	rs_lensfun_get_type(G_TYPE_MODULE(plugin));
}

static void
rs_lensfun_class_init(RSLensfunClass *klass)
{
	RSFilterClass *filter_class = RS_FILTER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	rs_lensfun_parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = get_property;
	object_class->set_property = set_property;

	g_object_class_install_property(object_class,
		PROP_MAKE, g_param_spec_string(
			"make", "make", "The make of the camera (ie. \"Canon\")",
			NULL, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_MODEL, g_param_spec_string(
			"model", "model", "The model of the camera (ie. \"Canon EOS 20D\")",
			NULL, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_LENS, g_param_spec_object(
			"lens", "lens", "A RSLens object describing the lens",
			RS_TYPE_LENS, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_LENS_MAKE, g_param_spec_string(
			"lens_make", "lens_make", "The make of the lens (ie. \"Canon\")",
			NULL, G_PARAM_READABLE)
	);
	g_object_class_install_property(object_class,
		PROP_LENS_MODEL, g_param_spec_string(
			"lens_model", "lens_model", "The model of the lens (ie. \"Canon\")",
			NULL, G_PARAM_READABLE)
	);
	g_object_class_install_property(object_class,
		PROP_FOCAL, g_param_spec_float(
			"focal", "focal", "focal",
			0.0, G_MAXFLOAT, 50.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_APERTURE, g_param_spec_float(
			"aperture", "aperture", "aperture",
			1.0, G_MAXFLOAT, 5.6, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TCA_KR, g_param_spec_float(
			"tca_kr", "tca_kr", "tca_kr",
			-1, 1, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TCA_KB, g_param_spec_float(
			"tca_kb", "tca_kb", "tca_kb",
			-1, 1, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_VIGNETTING_K1, g_param_spec_float(
			"vignetting_k1", "vignetting_k1", "vignetting_k1",
			-1, 2, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_VIGNETTING_K2, g_param_spec_float(
			"vignetting_k2", "vignetting_k2", "vignetting_k2",
			-1, 2, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_VIGNETTING_K3, g_param_spec_float(
			"vignetting_k3", "vignetting_k3", "vignetting_k3",
			-1, 2, 0.0, G_PARAM_READWRITE)
	);

	filter_class->name = "Lensfun filter";
	filter_class->get_image = get_image;
}

static void
rs_lensfun_init(RSLensfun *lensfun)
{
	lensfun->make = NULL;
	lensfun->model = NULL;
	lensfun->lens = NULL;
	lensfun->lens_make = NULL;
	lensfun->lens_model = NULL;
	lensfun->focal = 50.0; /* Well... */
	lensfun->aperture = 5.6;
	lensfun->tca_kr = 0.0;
	lensfun->tca_kb = 0.0;
	lensfun->vignetting_k1 = 0.0;
	lensfun->vignetting_k2 = 0.0;
	lensfun->vignetting_k3 = 0.0;

	/* Initialize Lensfun database */
	lensfun->ldb = lf_db_new ();
	lf_db_load (lensfun->ldb);
}

static void
get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	RSLensfun *lensfun = RS_LENSFUN(object);

	switch (property_id)
	{
		case PROP_MAKE:
			g_value_set_string(value, lensfun->make);
			break;
		case PROP_MODEL:
			g_value_set_string(value, lensfun->model);
			break;
		case PROP_LENS:
			g_value_set_object(value, lensfun->lens);
			break;
		case PROP_LENS_MAKE:
			g_value_set_string(value, lensfun->lens_make);
			break;
		case PROP_LENS_MODEL:
			g_value_set_string(value, lensfun->lens_model);
			break;
		case PROP_FOCAL:
			g_value_set_float(value, lensfun->focal);
			break;
		case PROP_APERTURE:
			g_value_set_float(value, lensfun->aperture);
			break;
		case PROP_TCA_KR:
			g_value_set_float(value, lensfun->tca_kr);
			break;
		case PROP_TCA_KB:
			g_value_set_float(value, lensfun->tca_kb);
			break;
		case PROP_VIGNETTING_K1:
			g_value_set_float(value, lensfun->vignetting_k1);
			break;
		case PROP_VIGNETTING_K2:
			g_value_set_float(value, lensfun->vignetting_k2);
			break;
		case PROP_VIGNETTING_K3:
			g_value_set_float(value, lensfun->vignetting_k3);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	RSLensfun *lensfun = RS_LENSFUN(object);

	switch (property_id)
	{
		case PROP_MAKE:
			g_free(lensfun->make);
			lensfun->make = g_value_dup_string(value);
			lensfun->DIRTY = TRUE;
			break;
		case PROP_MODEL:
			g_free(lensfun->model);
			lensfun->model = g_value_dup_string(value);
			lensfun->DIRTY = TRUE;
			break;
		case PROP_LENS:
			if (lensfun->lens)
				g_object_unref(lensfun->lens);
			lensfun->lens = g_value_dup_object(value);
			lensfun->DIRTY = TRUE;
			break;
		case PROP_FOCAL:
			lensfun->focal = g_value_get_float(value);
			break;
		case PROP_APERTURE:
			lensfun->aperture = g_value_get_float(value);
			break;
		case PROP_TCA_KR:
			lensfun->tca_kr = g_value_get_float(value);
			rs_filter_changed(RS_FILTER(lensfun), RS_FILTER_CHANGED_PIXELDATA);
			break;
		case PROP_TCA_KB:
			lensfun->tca_kb = g_value_get_float(value);
			rs_filter_changed(RS_FILTER(lensfun), RS_FILTER_CHANGED_PIXELDATA);
			break;
		case PROP_VIGNETTING_K1:
			lensfun->vignetting_k1 = g_value_get_float(value);
			rs_filter_changed(RS_FILTER(lensfun), RS_FILTER_CHANGED_PIXELDATA);
			break;
		case PROP_VIGNETTING_K2:
			lensfun->vignetting_k2 = g_value_get_float(value);
			rs_filter_changed(RS_FILTER(lensfun), RS_FILTER_CHANGED_PIXELDATA);
			break;
		case PROP_VIGNETTING_K3:
			lensfun->vignetting_k3 = g_value_get_float(value);
			rs_filter_changed(RS_FILTER(lensfun), RS_FILTER_CHANGED_PIXELDATA);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

typedef struct {
	gint start_y;
	gint end_y;
	lfModifier *mod;
	RS_IMAGE16 *input;
	RS_IMAGE16 *output;
	GThread *threadid;
	gint effective_flags;
	GdkRectangle *roi;
	gint stage;	
} ThreadInfo;

static gpointer
thread_func(gpointer _thread_info)
{
	gint x, y;
	ThreadInfo* t = _thread_info;

	if (t->stage == 2) 
	{
		/* Do lensfun vignetting */
		if (t->effective_flags & LF_MODIFY_VIGNETTING)
		{
			lf_modifier_apply_color_modification (t->mod, GET_PIXEL(t->input, t->roi->x, t->start_y), 
				t->roi->x, t->start_y, t->roi->width, t->end_y - t->start_y,
				LF_CR_4 (RED, GREEN, BLUE, UNKNOWN),
				t->input->rowstride*2);
		}
	}

	if (t->stage == 3) 
	{
		/* Do TCA and distortion */
		gfloat *pos = g_new0(gfloat, t->input->w*6);
		const gint pixelsize = t->output->pixelsize;
		
		for(y = t->start_y; y < t->end_y; y++)
		{
			gushort *target;
			lf_modifier_apply_subpixel_geometry_distortion(t->mod, t->roi->x, (gfloat) y, t->roi->width, 1, pos);
			target = GET_PIXEL(t->output, t->roi->x, y);
			gfloat* l_pos = pos;

			for(x = 0; x < t->roi->width ; x++)
			{
				rs_image16_bilinear_full(t->input, target, l_pos);
				target += pixelsize;
				l_pos += 6;
			}
		}
		g_free(pos);
	}
	return NULL;
}


static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSLensfun *lensfun = RS_LENSFUN(filter);
	RSFilterResponse *previous_response;
	RSFilterResponse *response;
	RS_IMAGE16 *input;
	RS_IMAGE16 *output = NULL;
	const gchar *make = NULL;
	const gchar *model = NULL;
	GdkRectangle *roi;

	previous_response = rs_filter_get_image(filter->previous, request);
	input = rs_filter_response_get_image(previous_response);
	response = rs_filter_response_clone(previous_response);
	g_object_unref(previous_response);

	if (rs_filter_request_get_quick(request))
	{
		rs_filter_response_set_quick(response);
		if (input)
		{
			rs_filter_response_set_image(response, input);
			g_object_unref(input);
		}
		return response;
	}

	if (!RS_IS_IMAGE16(input))
		return response;

	gint i;

	if (!lensfun->ldb)
	{
		g_warning ("Failed to create database");
		rs_filter_response_set_image(response, input);
		g_object_unref(input);
		return response;
	}

	if(lensfun->DIRTY)
	{

		const lfCamera **cameras = NULL;
		if (lensfun->make && lensfun->model)
			cameras = lf_db_find_cameras(lensfun->ldb, lensfun->make, lensfun->model);

		if (!cameras)
		{
			g_warning("camera not found (make: \"%s\" model: \"%s\")", lensfun->make, lensfun->model);
			rs_filter_response_set_image(response, input);
			g_object_unref(input);
			return response;
		}

		/* FIXME: selecting first camera */
		lensfun->selected_camera = cameras [0];
		lf_free (cameras);

		const lfLens **lenses = NULL;

		if (rs_lens_get_lensfun_model(lensfun->lens))
		{
			model = rs_lens_get_lensfun_model(lensfun->lens);
			make = rs_lens_get_lensfun_make(lensfun->lens);
			lenses = lf_db_find_lenses_hd(lensfun->ldb, lensfun->selected_camera, make, model, 0);
		}

		if (!lenses)
		{
			g_warning("lens not found");
			rs_filter_response_set_image(response, input);
			g_object_unref(input);
			return response;
		}

		/* FIXME: selecting first lens */
		lensfun->selected_lens = lenses [0];
		lf_free (lenses);

		lensfun->DIRTY = FALSE;
	}
	
	roi = rs_filter_request_get_roi(request);
	gboolean destroy_roi = FALSE;
	printf("ROI: ");
	if (!roi) 
	{
		roi = g_new(GdkRectangle, 1);
		roi->x = 0;
		roi->y = 0;
		roi->width = input->w;
		roi->height = input->h;
		destroy_roi = TRUE;
		printf("(new) ");
	}
	printf("x:%d, y:%d; w:%d, h:%d\n",roi->x,roi->y,roi->width, roi->height);
	/* Procedd if we got everything */
	if (lf_lens_check((lfLens *) lensfun->selected_lens))
	{
		gint effective_flags;

		/*FIXME: TCA and Vignetting should default to the values in lensfun db */
		/* Set TCA */
		lfLensCalibTCA tca;
		tca.Model = LF_TCA_MODEL_LINEAR;
		const char *details = NULL;
		const lfParameter **params = NULL;
		lf_get_tca_model_desc (tca.Model, &details, &params);
		tca.Terms[0] = (lensfun->tca_kr/100)+1;
		tca.Terms[1] = (lensfun->tca_kb/100)+1;
		lf_lens_add_calib_tca((lfLens *) lensfun->selected_lens, (lfLensCalibTCA *) &tca);

		/* Set vignetting */
		lfLensCalibVignetting vignetting;
		vignetting.Model = LF_VIGNETTING_MODEL_PA;
//			const char *details;
//			const lfParameter **params;
//			lf_get_vignetting_model_desc(vignetting.Model, &details, &params);
		vignetting.Distance = 1.0;
		vignetting.Focal = lensfun->focal;
		vignetting.Aperture = lensfun->aperture;
		vignetting.Terms[0] = lensfun->vignetting_k1;
		vignetting.Terms[1] = lensfun->vignetting_k2;
		vignetting.Terms[2] = lensfun->vignetting_k3;
		lf_lens_add_calib_vignetting((lfLens *) lensfun->selected_lens, &vignetting);

		lfModifier *mod = lf_modifier_new (lensfun->selected_lens, lensfun->selected_camera->CropFactor, input->w, input->h);
		effective_flags = lf_modifier_initialize (mod, lensfun->selected_lens,
			LF_PF_U16, /* lfPixelFormat */
			lensfun->focal, /* focal */
			lensfun->aperture, /* aperture */
			1.0, /* distance */
			1.0, /* scale */
			LF_UNKNOWN, /* lfLensType targeom, */ /* FIXME: ? */
			LF_MODIFY_ALL, /* flags */ /* FIXME: ? */
			FALSE); /* reverse */

		/* Print flags used */
		GString *flags = g_string_new("");
		if (effective_flags & LF_MODIFY_TCA)
			g_string_append(flags, " LF_MODIFY_TCA");
		if (effective_flags & LF_MODIFY_VIGNETTING)
			g_string_append(flags, " LF_MODIFY_VIGNETTING");
		if (effective_flags & LF_MODIFY_CCI)
			g_string_append(flags, " LF_MODIFY_CCI");
		if (effective_flags & LF_MODIFY_DISTORTION)
			g_string_append(flags, " LF_MODIFY_DISTORTION");
		if (effective_flags & LF_MODIFY_GEOMETRY)
			g_string_append(flags, " LF_MODIFY_GEOMETRY");
		if (effective_flags & LF_MODIFY_SCALE)
			g_string_append(flags, " LF_MODIFY_SCALE");
		g_debug("Effective flags:%s", flags->str);
		g_string_free(flags, TRUE);

			
		if (effective_flags > 0)
		{
			guint y_offset, y_per_thread, threaded_h;
			const guint threads = rs_get_number_of_processor_cores();
			ThreadInfo *t = g_new(ThreadInfo, threads);
			threaded_h = roi->height;
			y_per_thread = (threaded_h + threads-1)/threads;
			y_offset = roi->y;

			/* Set up job description for individual threads */
			for (i = 0; i < threads; i++)
			{
				t[i].mod = mod;
				t[i].start_y = y_offset;
				y_offset += y_per_thread;
				y_offset = MIN(roi->y + roi->height, y_offset);
				t[i].end_y = y_offset;
				t[i].effective_flags = effective_flags;
				t[i].roi = roi;
			}

			/* Start threads to apply phase 2, Vignetting and CA Correction */
			if (effective_flags & (LF_MODIFY_VIGNETTING | LF_MODIFY_CCI)) 
			{
				/* Phase 2 is corrected inplace, so copy input first */
				output = rs_image16_copy(input, TRUE);
				g_object_unref(input);
				for (i = 0; i < threads; i++)
				{
					t[i].input = t[i].output = output;
					t[i].stage = 2;
					t[i].threadid = g_thread_create(thread_func, &t[i], TRUE, NULL);
				}
				
				/* Wait for threads to finish */
				for(i = 0; i < threads; i++)
					g_thread_join(t[i].threadid);

				input = output;
			}
			
			/* Start threads to apply phase 1+3, Chromatic abberation and distortion Correction */
			if (effective_flags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY)) 
			{
				output = rs_image16_copy(input, FALSE);
				for (i = 0; i < threads; i++)
				{
					t[i].input = input;
					t[i].output = output;
					t[i].stage = 3;
					t[i].threadid = g_thread_create(thread_func, &t[i], TRUE, NULL);
				}
				
				/* Wait for threads to finish */
				for(i = 0; i < threads; i++)
					g_thread_join(t[i].threadid);
			}
			else
			{
				output = rs_image16_copy(input, TRUE);
			}			
			g_free(t);
			rs_filter_response_set_image(response, output);
			g_object_unref(output);
		}
		else
			rs_filter_response_set_image(response, input);

		lf_modifier_destroy(mod);
	}
	else
		g_debug("lf_lens_check() failed");
//	lfModifier *mod = lfModifier::Create (lens, opts.Crop, img->width, img->height);
	if (destroy_roi)
		g_free(roi);

	g_object_unref(input);
	return response;
}

static void inline
rs_image16_nearest_full(RS_IMAGE16 *in, gushort *out, gfloat *pos)
{
	gint ipos[6];
	gint i;
	for (i = 0; i < 6; i+=2)
	{
		ipos[i] = CLAMP((gint)pos[i], 0, in->w-1);
		ipos[i+1] = CLAMP((gint)pos[i+1], 0, in->h-1);
	}
	out[R] = GET_PIXEL(in, ipos[0], ipos[1])[R];
	out[G] = GET_PIXEL(in, ipos[2], ipos[3])[G];
	out[B] = GET_PIXEL(in, ipos[4], ipos[5])[B];
}

static void inline
rs_image16_bilinear_full(RS_IMAGE16 *in, gushort *out, gfloat *pos)
{
	gint ipos_x, ipos_y ;
	gint i;
	gint m_w = (in->w-1);
	gint m_h = (in->h-1);
	for (i = 0; i < 3; i++)
	{
		ipos_x = CLAMP((gint)(pos[i*2]*256.0f), 0, m_w << 8);
		ipos_y = CLAMP((gint)(pos[i*2+1]*256.0f), 0, m_h << 8);

		/* Calculate next pixel offset */
		const gint nx = MIN((ipos_x>>8) + 1, m_w);
		const gint ny = MIN((ipos_y>>8) + 1, m_h);

		gushort* a = GET_PIXEL(in, ipos_x>>8, ipos_y>>8);
		gushort* b = GET_PIXEL(in, nx , ipos_y>>8);
		gushort* c = GET_PIXEL(in, ipos_x>>8, ny);
		gushort* d = GET_PIXEL(in, nx, ny);

		/* Calculate distances */
		const gint diffx = ipos_x & 0xff; /* x distance from a */
		const gint diffy = ipos_y & 0xff; /* y distance fromy a */
		const gint inv_diffx = 256 - diffx; /* inverse x distance from a */
		const gint inv_diffy = 256 - diffy; /* inverse y distance from a */

		/* Calculate weightings */
		const gint aw = (inv_diffx * inv_diffy) >> 1;  /* Weight is now 0.15 fp */
		const gint bw = (diffx * inv_diffy) >> 1;
		const gint cw = (inv_diffx * diffy) >> 1;
		const gint dw = (diffx * diffy) >> 1;

		out[i]  = (gushort) ((a[i]*aw  + b[i]*bw  + c[i]*cw  + d[i]*dw + 16384) >> 15 );
	}
}