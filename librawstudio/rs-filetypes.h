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
#ifndef RS_FILETYPES_H
#define RS_FILETYPES_H

#include "rs-types.h"

typedef RS_IMAGE16 *(*RSFileLoaderFunc)(const gchar *filename, const gboolean half_size);
typedef void (*RSFileMetaLoaderFunc)(const gchar *service, RAWFILE *rawfile, guint offset, RSMetadata *meta);

/**
 * Initialize the RSFiletype subsystem, this MUST be called before any other
 * rs_filetype_*-functions
 */
extern void rs_filetype_init();

/**
 * Register a new image loader
 * @param extension The filename extension including the dot, ie: ".cr2"
 * @param description A human readable description of the file-format/loader
 * @param loader The loader function
 * @param priority A loader priority, lowest is served first.
 */
extern void rs_filetype_register_loader(const gchar *extension, const gchar *description, const RSFileLoaderFunc loader, const gint priority);

/**
 * Register a new metadata loader
 * @param extension The filename extension including the dot, ie: ".cr2"
 * @param description A human readable description of the file-format/loader
 * @param meta_loader The loader function
 * @param priority A loader priority, lowest is served first.
 */
extern void rs_filetype_register_meta_loader(const gchar *service, const gchar *description, const RSFileMetaLoaderFunc meta_loader, const gint priority);

/**
 * Check if we support loading a given extension
 * @param filename A filename or extension to look-up
 */
extern gboolean rs_filetype_can_load(const gchar *filename);

/**
 * Load an image according to registered loaders
 * @param filename The file to load
 * @param half_size Set to TRUE to avoid preparing image for debayer
 * @return A new RS_IMAGE16 or NULL if the loading failed
 */
extern RS_IMAGE16 *rs_filetype_load(const gchar *filename, const gboolean half_size);

/**
 * Load metadata from a specified file
 * @param service The file to load metadata from OR a servicename (".exif" for example)
 * @param meta A RSMetadata structure to load everything into
 * @param rawfile An open RAWFILE
 * @param offset An offset in the open RAWFILE
 */
extern void rs_filetype_meta_load(const gchar *service, RSMetadata *meta, RAWFILE *rawfile, guint offset);

#endif /* RS_FILETYPES_H */
