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

#ifndef RS_PLUGIN_MANAGER_H
#define RS_PLUGIN_MANAGER_H

G_BEGIN_DECLS

typedef struct _RSPluginManager RSPluginManager;
typedef struct _RSPluginManagerClass RSPluginManagerClass;

struct _RSPluginmanager {
	GObject  parent;

	gchar *plugin_path;
	GList *plugins;
};

struct _RSPluginManagerClass {
	GObjectClass  parent;
};

GType rs_plugin_manager_get_type(void) G_GNUC_CONST;

typedef enum {
	RS_MODULE_INVALID = 0,
	RS_MODULE_LOADER,
	RS_MODULE_FILTER,
} RSModuleType;

/**
 * Load all installed Rawstudio plugins
 */
extern gint
rs_plugin_manager_load_all_plugins();

G_END_DECLS

#endif /* RS_PLUGIN_MANAGER_H */
