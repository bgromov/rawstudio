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

#ifndef RS_LENS_H
#define RS_LENS_H

#include <glib-object.h>
#include <rawstudio.h>

G_BEGIN_DECLS

#define RS_TYPE_LENS rs_lens_get_type()
#define RS_LENS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_LENS, RSLens))
#define RS_LENS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_LENS, RSLensClass))
#define RS_IS_LENS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_LENS))
#define RS_IS_LENS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_LENS))
#define RS_LENS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_LENS, RSLensClass))
GType rs_lens_get_type(void);

typedef struct _RSLens RSLens;

typedef struct {
	GObjectClass parent_class;
} RSLensClass;

/**
 * Instantiate a new RSLens
 * @return A new RSLens with a refcount of 1
 */
RSLens *rs_lens_new(void);

/**
 * Instantiate a new RSLens from a RSMetadata
 * @param metadata A RSMetadata type with lens information embedded
 * @return A new RSLens with a refcount of 1
 */
RSLens *rs_lens_new_from_medadata(RSMetadata *metadata);

/**
 * Get the Lensfun idenfier from a RSLens
 * @param lens A RSLens
 * @return The identifier as used by Lensfun or NULL if unknown
 */
gchar *rs_lens_get_lensfun_identifier(RSLens *lens);

G_END_DECLS

#endif /* RS_LENS_H */
