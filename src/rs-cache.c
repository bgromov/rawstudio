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
#include <glib.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include "application.h"
#include "rs-cache.h"
#include "rs-photo.h"

/* This will be written to XML files for making backward compatibility easier to implement */
#define CACHEVERSION 2

static guint rs_cache_load_setting(RSSettings *rss, xmlDocPtr doc, xmlNodePtr cur);

gchar *
rs_cache_get_name(const gchar *src)
{
	gchar *ret=NULL;
	gchar *dotdir, *filename;
	GString *out;
	dotdir = rs_dotdir_get(src);
	filename = g_path_get_basename(src);
	if (dotdir)
	{
		out = g_string_new(dotdir);
		out = g_string_append(out, G_DIR_SEPARATOR_S);
		out = g_string_append(out, filename);
		out = g_string_append(out, ".cache.xml");
		ret = out->str;
		g_string_free(out, FALSE);
		g_free(dotdir);
	}
	g_free(filename);
	return(ret);
}

void
rs_cache_save(RS_PHOTO *photo, const RSSettingsMask mask)
{
	gint id, i;
	xmlTextWriterPtr writer;
	gchar *cachename;

	if (!photo->filename) return;

	cachename = rs_cache_get_name(photo->filename);
	if (!cachename) return;
	writer = xmlNewTextWriterFilename(cachename, 0); /* fixme, check for errors */
	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
	xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-cache");
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "version", "%d", CACHEVERSION);
	xmlTextWriterWriteFormatElement(writer, BAD_CAST "priority", "%d",
		photo->priority);
	if (photo->exported)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "exported", "yes");
	xmlTextWriterWriteFormatElement(writer, BAD_CAST "orientation", "%d",
		photo->orientation);
	xmlTextWriterWriteFormatElement(writer, BAD_CAST "angle", "%f",
		photo->angle);
	if (photo->crop)
	{
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "crop", "%d %d %d %d",
			photo->crop->x1, photo->crop->y1,
			photo->crop->x2, photo->crop->y2);
	}
	for(id=0;id<3&&mask>0;id++)
	{
		xmlTextWriterStartElement(writer, BAD_CAST "settings");
		xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "id", "%d", id);
		if (mask & MASK_EXPOSURE)
			xmlTextWriterWriteFormatElement(writer, BAD_CAST "exposure", "%f",
				photo->settings[id]->exposure);
		if (mask & MASK_SATURATION)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "saturation", "%f",
			photo->settings[id]->saturation);
		if (mask & MASK_HUE)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "hue", "%f",
			photo->settings[id]->hue);
		if (mask & MASK_CONTRAST)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "contrast", "%f",
			photo->settings[id]->contrast);
		if (mask & MASK_WARMTH)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "warmth", "%f",
			photo->settings[id]->warmth);
		if (mask & MASK_TINT)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "tint", "%f",
			photo->settings[id]->tint);
		if (mask & MASK_SHARPEN)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "sharpen", "%f",
			photo->settings[id]->sharpen);
		if (mask & MASK_CURVE && photo->settings[id]->curve_nknots > 0)
		{
			xmlTextWriterStartElement(writer, BAD_CAST "curve");
			xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "num", "%d", photo->settings[id]->curve_nknots);
			for(i=0;i<photo->settings[id]->curve_nknots;i++)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "knot", "%f %f",
					photo->settings[id]->curve_knots[i*2+0],
					photo->settings[id]->curve_knots[i*2+1]);
			xmlTextWriterEndElement(writer);
		}
		xmlTextWriterEndElement(writer);
	}
	xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);
	g_free(cachename);
	return;
}

static guint
rs_cache_load_setting(RSSettings *rss, xmlDocPtr doc, xmlNodePtr cur)
{
	RSSettingsMask mask = 0;
	xmlChar *val;
	gfloat *target=NULL;
	xmlNodePtr curve = NULL;
	while(cur)
	{
		target = NULL;
		if ((!xmlStrcmp(cur->name, BAD_CAST "exposure")))
		{
			mask |= MASK_EXPOSURE;
			target = &rss->exposure;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "saturation")))
		{
			mask |= MASK_SATURATION;
			target = &rss->saturation;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hue")))
		{
			mask |= MASK_HUE;
			target = &rss->hue;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "contrast")))
		{
			mask |= MASK_CONTRAST;
			target = &rss->contrast;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "warmth")))
		{
			mask |= MASK_WARMTH;
			target = &rss->warmth;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "tint")))
		{
			mask |= MASK_TINT;
			target = &rss->tint;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "sharpen")))
		{
			mask |= MASK_SHARPEN;
			target = &rss->sharpen;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "curve")))
		{
			gchar **vals;
			gint num;
			gfloat x,y;

			val = xmlGetProp(cur, BAD_CAST "num");
			if (val)
				num = atoi((gchar *) val);
			else
				num = 0;

			rss->curve_knots = g_new(gfloat, 2*num);
			rss->curve_nknots = 0;
			curve = cur->xmlChildrenNode;
			while (curve && num)
			{
				if ((!xmlStrcmp(curve->name, BAD_CAST "knot")))
				{
					mask |= MASK_CURVE;
					val = xmlNodeListGetString(doc, curve->xmlChildrenNode, 1);
					vals = g_strsplit((gchar *)val, " ", 4);
					if (vals[0] && vals[1])
					{
						x = rs_atof(vals[0]);
						y = rs_atof(vals[1]);
						rss->curve_knots[rss->curve_nknots*2+0] = x;
						rss->curve_knots[rss->curve_nknots*2+1] = y;
						rss->curve_nknots++;
						num--;
					}
					g_strfreev(vals);
					xmlFree(val);
				}
				curve = curve->next;
			}
		}

		if (target)
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			*target =  rs_atof((gchar *) val);
			xmlFree(val);
		}
		cur = cur->next;
	}

	return mask;
}

guint
rs_cache_load(RS_PHOTO *photo)
{
	RSSettingsMask mask = 0;
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlChar *val;
	gchar *cachename;
	gint id;
	gint version = 0;
	RSSettings *settings;

	cachename = rs_cache_get_name(photo->filename);
	if (!cachename) return mask;
	if (!g_file_test(cachename, G_FILE_TEST_IS_REGULAR)) return FALSE;
	photo->exported = FALSE;
	doc = xmlParseFile(cachename);
	if(doc==NULL) return mask;

	/* Return something if the file exists */
	mask = 0x80000000;

	cur = xmlDocGetRootElement(doc);

	if ((!xmlStrcmp(cur->name, BAD_CAST "rawstudio-cache")))
	{
		val = xmlGetProp(cur, BAD_CAST "version");
		if (val)
			version = atoi((gchar *) val);
	}

	cur = cur->xmlChildrenNode;
	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "settings")))
		{
			val = xmlGetProp(cur, BAD_CAST "id");
			id = atoi((gchar *) val);
			xmlFree(val);
			if (id>2) id=0;
			if (id<0) id=0;
			settings = rs_settings_new();
			mask |= rs_cache_load_setting(settings, doc, cur->xmlChildrenNode);
			rs_photo_apply_settings(photo, id, settings, MASK_ALL);
			g_object_unref(settings);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "priority")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			photo->priority = atoi((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "orientation")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			photo->orientation = atoi((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "angle")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			photo->angle = rs_atof((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "exported")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
				photo->exported = TRUE;
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "crop")))
		{
			RS_RECT *crop = g_new0(RS_RECT, 1);
			gchar **vals;

			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			vals = g_strsplit((gchar *)val, " ", 4);
			if (vals[0])
			{
				crop->x1 = atoi((gchar *) vals[0]);
				if (vals[1])
				{
					crop->y1 = atoi((gchar *) vals[1]);
					if (vals[2])
					{
						crop->x2 = atoi((gchar *) vals[2]);
						if (vals[3])
							crop->y2 = atoi((gchar *) vals[3]);
					}
				}
			}

			/* If crop was done before demosaic was implemented, we should
			   double the dimensions */
			if (version < 2)
			{
				crop->x1 *= 2;
				crop->y1 *= 2;
				crop->x2 *= 2;
				crop->y2 *= 2;
			}

			rs_photo_set_crop(photo, crop);
			g_free(crop);
			g_strfreev(vals);
			xmlFree(val);
		}
		cur = cur->next;
	}

	xmlFreeDoc(doc);
	g_free(cachename);
	return mask;
}

void
rs_cache_load_quick(const gchar *filename, gint *priority, gboolean *exported)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlChar *val;
	gchar *cachename;

	if (!filename)
		return;

	cachename = rs_cache_get_name(filename);

	if (!cachename)
		return;

	if (!g_file_test(cachename, G_FILE_TEST_IS_REGULAR))
	{
		g_free(cachename);
		return;
	}

	doc = xmlParseFile(cachename);
	g_free(cachename);

	if(doc==NULL)
		return;

	if (exported) *exported = FALSE;

	cur = xmlDocGetRootElement(doc);

	cur = cur->xmlChildrenNode;
	while(cur)
	{
		if (priority && (!xmlStrcmp(cur->name, BAD_CAST "priority")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			*priority = atoi((gchar *) val);
			xmlFree(val);
		}
		if (exported && (!xmlStrcmp(cur->name, BAD_CAST "exported")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
				*exported = TRUE;
			xmlFree(val);
		}
		cur = cur->next;
	}
	
	xmlFreeDoc(doc);
	return;
}

void
rs_cache_save_flags(const gchar *filename, const guint *priority, const gboolean *exported)
{
	RS_PHOTO *photo;
	RSSettingsMask mask;

	g_assert(filename != NULL);

	if (!(priority || exported)) return;

	/* Aquire a "fake" RS_PHOTO */
	photo = rs_photo_new();
	photo->filename = (gchar *) filename;

	if ((mask = rs_cache_load(photo)))
	{
		/* If we got a cache file, save as normal */
		if (priority)
			photo->priority = *priority;
		if (exported)
			photo->exported = *exported;
		rs_cache_save(photo, mask);
	}
	else
	{
		/* If we're creating a new file, only save what we know */
		xmlTextWriterPtr writer;
		gchar *cachename = rs_cache_get_name(photo->filename);

		if (cachename)
		{
			writer = xmlNewTextWriterFilename(cachename, 0); /* fixme, check for errors */
			g_free(cachename);

			xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
			xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-cache");

			if (priority)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "priority", "%d",
					*priority);

			if (exported && *exported)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "exported", "yes");

			xmlTextWriterEndDocument(writer);
			xmlFreeTextWriter(writer);
		}
	}

	/* Free the photo */
	photo->filename = NULL;
	g_object_unref(photo);

	return;
}
