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

#include <glib.h>
#include <glib/gstdio.h> /* g_unlink */
#include <gtk/gtk.h>
#include <config.h>
#include "gettext.h"
#include "application.h"
#include "rs-actions.h"
#include "conf_interface.h"
#include "rs-store.h"
#include "rs-photo.h"
#include "filename.h"
#include "gtk-interface.h"
#include "gtk-save-dialog.h"
#include "gtk-progress.h"
#include "gtk-helper.h"
#include "rs-external-editor.h"
#include "rs-cache.h"
#include "rs-preview-widget.h"
#include "rs-batch.h"

static GtkActionGroup *core_action_group = NULL;
GStaticMutex rs_actions_spinlock = G_STATIC_MUTEX_INIT;

#define ACTION(Action) void rs_action_##Action(GtkAction *action, RS_BLOB *rs); \
	void rs_action_##Action(GtkAction *action, RS_BLOB *rs)
#define ACTION_CB(Action) G_CALLBACK(rs_action_##Action)

#define TOGGLEACTION(Action) void rs_action_##Action(GtkToggleAction *toggleaction, RS_BLOB *rs); \
	void rs_action_##Action(GtkToggleAction *toggleaction, RS_BLOB *rs)

#define RADIOACTION(Action) void rs_action_##Action(GtkRadioAction *radioaction, GtkRadioAction *current, RS_BLOB *rs); \
	void rs_action_##Action(GtkRadioAction *radioaction, GtkRadioAction *current, RS_BLOB *rs)

ACTION(todo)
{
	GString *gs = g_string_new("Action not implemented: ");
	g_string_append(gs, gtk_action_get_name(action));
	g_warning("%s", gs->str);
	gui_status_notify(gs->str);
	g_string_free(gs, TRUE);
}

ACTION(file_menu)
{
	rs_core_action_group_set_sensivity("QuickExport", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("ExportAs", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("ExportToGimp", RS_IS_PHOTO(rs->photo));
}

ACTION(edit_menu)
{
	rs_core_action_group_set_sensivity("RevertSettings", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("CopySettings", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("PasteSettings", !!(rs->settings_buffer));
}

ACTION(photo_menu)
{
	GList *selected = NULL, *selected_iters = NULL;
	gint num_selected, selected_groups;
	gboolean photos_selected;

	selected = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected);

	photos_selected = (RS_IS_PHOTO(rs->photo) || (num_selected > 0));

	selected_iters = rs_store_get_selected_iters(rs->store);
	selected_groups = rs_store_selection_n_groups(rs->store, selected_iters);

	rs_core_action_group_set_sensivity("FlagPhoto", photos_selected);
	rs_core_action_group_set_sensivity("PriorityMenu", photos_selected);
	rs_core_action_group_set_sensivity("WhiteBalanceMenu", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Crop", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Uncrop", (RS_IS_PHOTO(rs->photo) && rs->photo->crop));
	rs_core_action_group_set_sensivity("Straighten", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Unstraighten", (RS_IS_PHOTO(rs->photo) && (rs->photo->angle != 0.0)));
	rs_core_action_group_set_sensivity("Group", (num_selected > 1));
	rs_core_action_group_set_sensivity("Ungroup", (selected_groups > 0));
#ifndef EXPERIMENTAL
	rs_core_action_group_set_visibility("Group", FALSE);
	rs_core_action_group_set_visibility("Ungroup", FALSE);
	rs_core_action_group_set_visibility("AutoGroup", FALSE);
#endif
	g_list_free(selected);
}

ACTION(batch_menu)
{
	GList *selected = NULL;
	gint num_selected;
	gboolean photos_selected;

	selected = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected);

	photos_selected = (RS_IS_PHOTO(rs->photo) || (num_selected > 0));

	rs_core_action_group_set_sensivity("AddToBatch", photos_selected && !rs_batch_exists_in_queue(rs->queue, rs->photo->filename, rs->current_setting));
	rs_core_action_group_set_sensivity("RemoveFromBatch", photos_selected && rs->photo && rs_batch_exists_in_queue(rs->queue, rs->photo->filename, rs->current_setting));
	rs_core_action_group_set_sensivity("ProcessBatch", (rs_batch_num_entries(rs->queue)>0));
	g_list_free(selected);
}

ACTION(preview_popup)
{
	rs_core_action_group_set_sensivity("Crop", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Uncrop", (RS_IS_PHOTO(rs->photo) && rs->photo->crop));
	rs_core_action_group_set_sensivity("Straighten", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Unstraighten", (RS_IS_PHOTO(rs->photo) && (rs->photo->angle != 0.0)));
}
ACTION(open)
{
	GtkWidget *fc;
	gchar *lwd = rs_conf_get_string(CONF_LWD);

	fc = gtk_file_chooser_dialog_new (_("Open directory"), NULL,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);
	
	if (g_file_test(lwd, G_FILE_TEST_IS_DIR))
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), lwd);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		gtk_widget_destroy (fc);
		rs_store_remove(rs->store, NULL, NULL);
		if (rs_store_load_directory(rs->store, filename) >= 0)
			rs_conf_set_string(CONF_LWD, filename);
		g_free (filename);
	} else
		gtk_widget_destroy (fc);

	g_free(lwd);
}

ACTION(quick_export)
{
	gchar *dirname;
	gchar *conf_export_directory;
	gchar *conf_export_filename;
	GString *export_path;
	GString *save;
	gchar *parsed_filename;
	RS_FILETYPE *filetype;
	GString *status;

	if (!rs->photo) return;

	dirname = g_path_get_dirname(rs->photo->filename);
	
	conf_export_directory = rs_conf_get_string(CONF_EXPORT_DIRECTORY);
	if (!conf_export_directory)
		conf_export_directory = g_strdup(DEFAULT_CONF_EXPORT_DIRECTORY);

	conf_export_filename = rs_conf_get_string(CONF_EXPORT_FILENAME);
	if (!conf_export_filename)
		conf_export_filename = DEFAULT_CONF_EXPORT_FILENAME;

	rs_conf_get_filetype(CONF_EXPORT_FILETYPE, &filetype);

	if (conf_export_directory)
	{
		if (conf_export_directory[0]==G_DIR_SEPARATOR)
		{
			g_free(dirname);
			dirname = conf_export_directory;
		}
		else
		{
			export_path = g_string_new(dirname);
			g_string_append(export_path, G_DIR_SEPARATOR_S);
			g_string_append(export_path, conf_export_directory);
			g_free(dirname);
			dirname = export_path->str;
			g_string_free(export_path, FALSE);
			g_free(conf_export_directory);
		}
		g_mkdir_with_parents(dirname, 00755);
	}
	
	save = g_string_new(dirname);
	if (dirname[strlen(dirname)-1] != G_DIR_SEPARATOR)
		g_string_append(save, G_DIR_SEPARATOR_S);
	g_string_append(save, conf_export_filename);

	g_string_append(save, filetype->ext);

	parsed_filename = filename_parse(save->str, rs->photo->filename, rs->current_setting);
	g_string_free(save, TRUE);

	rs_photo_save(rs->photo, parsed_filename, filetype->filetype, -1, -1, FALSE, 1.0, rs->current_setting, rs->cms);
	status = g_string_new("");
	g_string_printf(status, "%s (%s)", _("File exported"), parsed_filename);
	gui_status_notify(status->str);
	g_string_free(status, TRUE);
	g_free(parsed_filename);
}

ACTION(export_as)
{
	gui_save_file_dialog(rs);
}

ACTION(export_to_gimp)
{
	if (!RS_IS_PHOTO(rs->photo)) return;

	if (!rs_external_editor_gimp(rs->photo, rs->current_setting, rs->cms))
	{
		GtkWidget *dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_WARNING, 
			_("Error exporting"),
			_("Error exporting photo to gimp."));
		gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT);
		gtk_widget_show_all(dialog);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
}

ACTION(reload)
{
	rs_store_remove(rs->store, NULL, NULL);
	rs_store_load_directory(rs->store, NULL);
}

ACTION(delete_flagged)
{
	gchar *cache;
	GtkWidget *dialog;
	GList *photos_d = NULL;
	gint items = 0, i;
	RS_PROGRESS *progress;

	dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_WARNING,
		_("Deleting photos"),
		_("Your files will be <b>permanently</b> deleted!"));
	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Delete photos"), GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	gtk_widget_show_all(dialog);

	if((gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT))
	{
		gtk_widget_destroy(dialog);
		return;
	}
	else
		gtk_widget_destroy(dialog);

	photos_d = rs_store_get_iters_with_priority(rs->store, PRIO_D);
	items = g_list_length(photos_d);

	progress = gui_progress_new(_("Deleting photos"), items);

	for (i=0;i<items;i++)
	{
		gchar *fullname = rs_store_get_name(rs->store, g_list_nth_data(photos_d, i));

		if(0 == g_unlink(fullname))
		{
			rs_metadata_delete_cache(fullname);
			if ((cache = rs_cache_get_name(fullname)))
			{
				g_unlink(cache);
				g_free(cache);
			}
			/* Try to delete thm-files */
			{
				gchar *thm;
				gchar *ext;

				thm = g_strdup(fullname);
				ext = g_strrstr(thm, ".");
				ext++;
				g_strlcpy(ext, "thm", 4);
				if(g_unlink(thm))
				{
					g_strlcpy(ext, "THM", 4);
					g_unlink(thm);
				}
				g_free(thm);
			}
			rs_store_remove(rs->store, NULL, g_list_nth_data(photos_d, i));
			gui_progress_advance_one(progress);
			GUI_CATCHUP();
		}
	}
	g_list_free(photos_d);
	gui_progress_free(progress);
}

ACTION(quit)
{
	if (rs->photo)
		rs_photo_close(rs->photo);
	rs_conf_set_integer(CONF_LAST_PRIORITY_PAGE, rs_store_get_current_page(rs->store));
	gtk_main_quit();
}

ACTION(revert_settings)
{
	if (RS_IS_PHOTO(rs->photo))
		rs_cache_load(rs->photo);
}

ACTION(copy_settings)
{
	if (!rs->settings_buffer)
		rs->settings_buffer = rs_settings_new();
	rs_settings_copy(rs->settings[rs->current_setting], MASK_ALL, rs->settings_buffer);
	gui_status_notify(_("Copied settings"));
}

ACTION(paste_settings)
{
	gint mask = 0xffffff; /* Should be RSSettingsMask, is gint to satisfy rs_conf_get_integer() */

	GtkWidget *dialog, *cb_box;
	GtkWidget *cb_exposure, *cb_saturation, *cb_hue, *cb_contrast, *cb_whitebalance, *cb_curve, *cb_sharpen;

	if (rs->settings_buffer)
	{
		/* Build GUI */
		cb_exposure = gtk_check_button_new_with_label (_("Exposure"));
		cb_saturation = gtk_check_button_new_with_label (_("Saturation"));
		cb_hue = gtk_check_button_new_with_label (_("Hue"));
		cb_contrast = gtk_check_button_new_with_label (_("Contrast"));
		cb_whitebalance = gtk_check_button_new_with_label (_("White balance"));
		cb_sharpen = gtk_check_button_new_with_label (_("Sharpen"));
		cb_curve = gtk_check_button_new_with_label (_("Curve"));

		rs_conf_get_integer(CONF_PASTE_MASK, &mask);

		if (mask & MASK_EXPOSURE)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_exposure), TRUE);
		if (mask & MASK_SATURATION)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_saturation), TRUE);
		if (mask & MASK_HUE)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_hue), TRUE);
		if (mask & MASK_CONTRAST)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_contrast), TRUE);
		if (mask & MASK_WARMTH && mask & MASK_TINT)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_whitebalance), TRUE);
		if (mask & MASK_SHARPEN)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_sharpen), TRUE);
		if (mask & MASK_CURVE)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_curve), TRUE);

		cb_box = gtk_vbox_new(FALSE, 0);

		gtk_box_pack_start (GTK_BOX (cb_box), cb_exposure, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_saturation, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_hue, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_contrast, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_whitebalance, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_sharpen, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (cb_box), cb_curve, FALSE, TRUE, 0);

		dialog = gui_dialog_make_from_widget(GTK_STOCK_DIALOG_QUESTION, _("Select settings to paste"), cb_box);

		gtk_dialog_add_buttons(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, NULL);
		gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);

		gtk_widget_show_all(dialog);

		mask=0;

		if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY)
		{
			if (GTK_TOGGLE_BUTTON(cb_exposure)->active)
				mask |= MASK_EXPOSURE;
			if (GTK_TOGGLE_BUTTON(cb_saturation)->active)
				mask |= MASK_SATURATION;
			if (GTK_TOGGLE_BUTTON(cb_hue)->active)
				mask |= MASK_HUE;
			if (GTK_TOGGLE_BUTTON(cb_contrast)->active)
				mask |= MASK_CONTRAST;
			if (GTK_TOGGLE_BUTTON(cb_whitebalance)->active)
				mask |= MASK_WB;
			if (GTK_TOGGLE_BUTTON(cb_sharpen)->active)
				mask |= MASK_SHARPEN;
			if (GTK_TOGGLE_BUTTON(cb_curve)->active)
				mask |= MASK_CURVE;
			rs_conf_set_integer(CONF_PASTE_MASK, mask);
   		}
		gtk_widget_destroy (dialog);

		if(mask > 0)
		{
			RS_PHOTO *photo;
			gint cur;
			GList *selected = NULL;
			gint num_selected;
			guint new_mask;

			/* Apply to all selected photos */
			selected = rs_store_get_selected_names(rs->store);
			num_selected = g_list_length(selected);
			for(cur=0;cur<num_selected;cur++)
			{
				/* This is nothing but a hack around rs_cache_*() */
				photo = rs_photo_new();
				photo->filename = g_strdup(g_list_nth_data(selected, cur));
				new_mask = rs_cache_load(photo);
				rs_settings_copy(rs->settings_buffer, mask, photo->settings[rs->current_setting]);
				rs_cache_save(photo, new_mask | mask);
				g_object_unref(photo);
			}
			g_list_free(selected);

			/* Apply to current photo */
			rs_settings_copy(rs->settings_buffer, mask, rs->settings[rs->current_setting]); 

			gui_status_notify(_("Pasted settings"));
		}
		else
			gui_status_notify(_("Nothing to paste"));
	}
	else 
		gui_status_notify(_("Buffer empty"));
}

ACTION(reset_settings)
{
	rs_settings_reset(rs->settings[rs->current_setting], MASK_ALL);

	if (RS_IS_PHOTO(rs->photo))
		rs_photo_apply_settings(rs->photo, rs->current_setting, rs->settings[rs->current_setting], MASK_ALL);
}

ACTION(preferences)
{
	gui_make_preference_window(rs);
}

ACTION(flag_for_deletion)
{
	gui_setprio(rs, PRIO_D);
}

ACTION(priority_1)
{
	gui_setprio(rs, PRIO_1);
}

ACTION(priority_2)
{
	gui_setprio(rs, PRIO_2);
}

ACTION(priority_3)
{
	gui_setprio(rs, PRIO_3);
}

ACTION(priority_0)
{
	gui_setprio(rs, PRIO_U);
}

ACTION(auto_wb)
{
	if (RS_IS_PHOTO(rs->photo))
	{
		gui_status_notify(_("Adjusting to auto white balance"));
		rs_photo_set_wb_auto(rs->photo, rs->current_setting);
	}
}

ACTION(camera_wb)
{
	if (RS_IS_PHOTO(rs->photo))
	{
		if (rs->photo->metadata->cam_mul[R] == -1.0)
			gui_status_notify(_("No white balance to set from"));
		else
		{
			gui_status_notify(_("Adjusting to camera white balance"));
			rs_photo_set_wb_from_mul(rs->photo, rs->current_setting, rs->photo->metadata->cam_mul);
		}
	}
}

ACTION(crop)
{
	rs_preview_widget_crop_start(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(uncrop)
{
	rs_preview_widget_uncrop(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(straighten)
{
	rs_preview_widget_straighten(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(unstraighten)
{
	rs_preview_widget_unstraighten(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(group_photos)
{
	rs_store_group_photos(rs->store);
}

ACTION(ungroup_photos)
{
	rs_store_ungroup_photos(rs->store);
}

ACTION(auto_group_photos)
{
	rs_store_auto_group(rs->store);
}

ACTION(previous_photo)
{
	gchar *current_filename = NULL;

	/* Get current filename if a photo is loaded */
	if (RS_IS_PHOTO(rs->photo))
		current_filename = rs->photo->filename;

	rs_store_select_prevnext(rs->store, current_filename, 1);
}

ACTION(next_photo)
{
	gchar *current_filename = NULL;

	/* Get current filename if a photo is loaded */
	if (RS_IS_PHOTO(rs->photo))
		current_filename = rs->photo->filename;

	rs_store_select_prevnext(rs->store, current_filename, 2);
}

ACTION(zoom_to_fit)
{
	rs_preview_widget_set_zoom_to_fit(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(zoom_to_100)
{
	rs_preview_widget_set_zoom(RS_PREVIEW_WIDGET(rs->preview), 1.0);
}

TOGGLEACTION(iconbox)
{
	gui_widget_show(rs->iconbox, gtk_toggle_action_get_active(toggleaction), CONF_SHOW_ICONBOX_FULLSCREEN, CONF_SHOW_ICONBOX);
}

TOGGLEACTION(toolbox)
{
	gui_widget_show(rs->toolbox, gtk_toggle_action_get_active(toggleaction), CONF_SHOW_TOOLBOX_FULLSCREEN, CONF_SHOW_TOOLBOX);
}

TOGGLEACTION(fullscreen)
{
	if (gtk_toggle_action_get_active(toggleaction))
	{
		gtk_window_fullscreen(GTK_WINDOW(rs->window));
		rs_conf_set_boolean(CONF_FULLSCREEN, TRUE);
	}
	else
	{
		gtk_window_unfullscreen(GTK_WINDOW(rs->window));
		rs_conf_set_boolean(CONF_FULLSCREEN, FALSE);
	}
}

TOGGLEACTION(exposure_mask)
{
	if (gtk_toggle_action_get_active(toggleaction))
		gui_status_notify(_("Showing exposure mask"));
	else
		gui_status_notify(_("Hiding exposure mask"));
	rs_preview_widget_set_show_exposure_mask(RS_PREVIEW_WIDGET(rs->preview), gtk_toggle_action_get_active(toggleaction));
}

TOGGLEACTION(split)
{
	rs_preview_widget_set_split(RS_PREVIEW_WIDGET(rs->preview), gtk_toggle_action_get_active(toggleaction));
}

ACTION(add_to_batch)
{
	GString *gs = g_string_new("");
	GList *selected = NULL;
	gint num_selected, cur;

	selected = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected);

	if (RS_IS_PHOTO(rs->photo) && num_selected == 1)
	{
		rs_cache_save(rs->photo, MASK_ALL);

		if (rs_batch_add_to_queue(rs->queue, rs->photo->filename, rs->current_setting))
			g_string_printf(gs, _(" %s added to batch queue"), rs->photo->filename);
		else
			g_string_printf(gs, _("%s already added to batch queue"), rs->photo->filename);
	}
	else
	{
		/* Deal with selected icons */
		for(cur=0;cur<num_selected;cur++)
			rs_batch_add_to_queue(rs->queue, g_list_nth_data(selected, cur), rs->current_setting);
		g_string_printf(gs, _("%d photos added to batch queue"), num_selected);
	}
	g_list_free(selected);
	gui_status_notify(gs->str);
	g_string_free(gs, TRUE);
}

ACTION(add_view_to_batch)
{
	GString *gs = g_string_new("");
	GtkWidget *dialog, *cb_box;
	GtkWidget *cb_a, *cb_b, *cb_c;

	cb_a = gtk_check_button_new_with_label (_("A"));
	cb_b = gtk_check_button_new_with_label (_("B"));
	cb_c = gtk_check_button_new_with_label (_("C"));

	switch (rs->current_setting)
	{
		case 0:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_a), TRUE);
			break;
		case 1:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_b), TRUE);
			break;
		case 2:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_c), TRUE);
			break;
	}

	cb_box = gtk_vbox_new(FALSE, 4);

	gtk_box_pack_start (GTK_BOX (cb_box), cb_a, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_b, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_c, FALSE, TRUE, 0);

	dialog = gui_dialog_make_from_widget(GTK_STOCK_DIALOG_QUESTION, 
				_("Select which settings to\nadd to batch queue"), cb_box);

	gtk_dialog_add_buttons(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY)
	{
		GList *selected = NULL;
		gint num_selected, i;

		rs_store_get_names(rs->store, NULL, &selected, NULL);
		selected = rs_store_sort_selected(selected);
		num_selected = g_list_length(selected);

		for (i=0;i<num_selected;i++)
		{
			gchar *fullname = g_list_nth_data(selected, i);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_a)))
				rs_batch_add_to_queue(rs->queue, fullname, 0);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_b)))
				rs_batch_add_to_queue(rs->queue, fullname, 1);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_c)))
				rs_batch_add_to_queue(rs->queue, fullname, 2);
		}
		g_list_foreach(selected, (GFunc) g_free, NULL);
		g_list_free(selected);

		/* Save settings of current photo just to be sure */
		if (rs->photo)
			rs_cache_save(rs->photo, MASK_ALL);

		g_string_printf(gs, _("%d photos added to batch queue"),
			((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_a))) ? num_selected : 0)
			+ ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_b))) ? num_selected : 0)
			+ ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_c))) ? num_selected : 0));

		gui_status_notify(gs->str);
	}

	gtk_widget_destroy (dialog);
	g_string_free(gs, TRUE);
}

ACTION(remove_from_batch)
{
	/* FIXME: Deal with mutiple selected photos! */
	if (RS_IS_PHOTO(rs->photo))
	{
		if (rs_batch_remove_from_queue(rs->queue, rs->photo->filename, rs->current_setting))
			gui_status_notify(_("Removed from batch queue"));
		else
			gui_status_notify(_("Not in batch queue"));
	}
}

ACTION(ProcessBatch)
{
	rs_batch_process(rs->queue);
}

ACTION(about)
{
	const static gchar *authors[] = {
		"Anders Brander <anders@brander.dk>",
		"Anders Kvist <anders@kvistmail.dk>",
		NULL
	};
	const static gchar *artists[] = {
		"Kristoffer Jørgensen <kristoffer@vektormusik.dk>",
		"Rune Stowasser <rune.stowasser@gmail.com>",
		NULL
	};
	gtk_show_about_dialog(GTK_WINDOW(rawstudio_window),
		"authors", authors,
		"artists", artists,
		"translator-credits", "Simone Contini\nPaweł Gołaszewski\nAlexandre Prokoudine\nJakub Friedl\nCarsten Mathaes\nEdouard Gomez\nMartin Egger\nKrzysztof Kościuszkiewicz\nEinar Ryeng\nOlli Hänninen\nCarlos Dávila\nPatrik Jarl\nOlav Lavell\nRafael Sachetto Oliveira\nPaco Rivière",
		"comments", _("A raw image converter for GTK+/GNOME"),
		"version", VERSION,
		"website", "http://rawstudio.org/",
		"name", "Rawstudio",
		NULL
	);
}

RADIOACTION(right_popup)
{
	rs_preview_widget_set_snapshot(RS_PREVIEW_WIDGET(rs->preview), 1, gtk_radio_action_get_current_value(radioaction));
}

RADIOACTION(sort_by_popup)
{
	rs_store_set_sort_method(rs->store, gtk_radio_action_get_current_value(radioaction));
}

#ifndef GTK_STOCK_FULLSCREEN
 #define GTK_STOCK_FULLSCREEN NULL
#endif

/**
 * Get the core action group
 * @return A pointer to the core action group
 */
GtkActionGroup *
rs_get_core_action_group(RS_BLOB *rs)
{
	/* FIXME: This should be static */
	GtkActionEntry actionentries[] = {
	{ "FileMenu", NULL, _("_File"), NULL, NULL, ACTION_CB(file_menu) },
	{ "EditMenu", NULL, _("_Edit"), NULL, NULL, ACTION_CB(edit_menu) },
	{ "PhotoMenu", NULL, _("_Photo"), NULL, NULL, ACTION_CB(photo_menu) },
	{ "PriorityMenu", NULL, _("_Set Priority") },
	{ "WhiteBalanceMenu", "NULL", _("_White Balance") },
	{ "ViewMenu", NULL, _("_View") },
	{ "SortByMenu", NULL, _("_Sort by") },
	{ "BatchMenu", NULL, _("_Batch"), NULL, NULL, ACTION_CB(batch_menu) },
	{ "HelpMenu", NULL, _("_Help") },
	{ "PreviewPopup", NULL, NULL, NULL, NULL, ACTION_CB(preview_popup) },

	/* File menu */
	{ "Open", GTK_STOCK_OPEN, _("_Open Directory"), "<control>O", NULL, ACTION_CB(open) },
	{ "QuickExport", GTK_STOCK_SAVE, _("_Quick Export"), "<control>S", NULL, ACTION_CB(quick_export) },
	{ "ExportAs", GTK_STOCK_SAVE_AS, _("_Export As"), "<control><shift>S", NULL, ACTION_CB(export_as) },
	{ "ExportToGimp", GTK_STOCK_EXECUTE, _("_Export to Gimp"), "<control>G", NULL, ACTION_CB(export_to_gimp) },
	{ "Reload", GTK_STOCK_REFRESH, _("_Reload directory"), "<control>R", NULL, ACTION_CB(reload) },
	{ "DeleteFlagged", GTK_STOCK_DELETE, _("_Delete flagged photos"), "<control><shift>D", NULL, ACTION_CB(delete_flagged) },
	{ "Quit", GTK_STOCK_QUIT, _("_Quit"), "<control>Q", NULL, ACTION_CB(quit) },

	/* Edit menu */
	{ "RevertSettings", GTK_STOCK_UNDO, _("_Revert settings"), "<control>Z", NULL, ACTION_CB(revert_settings) },
	{ "CopySettings", GTK_STOCK_COPY, _("_Copy settings"), "<control>C", NULL, ACTION_CB(copy_settings) },
	{ "PasteSettings", GTK_STOCK_PASTE, _("_Paste settings"), "<control>V", NULL, ACTION_CB(paste_settings) },
	{ "ResetSettings", GTK_STOCK_REFRESH, _("_Reset settings"), NULL, NULL, ACTION_CB(reset_settings) },
	{ "Preferences", GTK_STOCK_PREFERENCES, _("_Preferences"), NULL, NULL, ACTION_CB(preferences) },

	/* Photo menu */
	{ "FlagPhoto", GTK_STOCK_DELETE, _("_Flag photo for deletion"), "Delete", NULL, ACTION_CB(flag_for_deletion) },
	{ "Priority1", NULL, _("_1"), "1", NULL, ACTION_CB(priority_1) },
	{ "Priority2", NULL, _("_2"), "2", NULL, ACTION_CB(priority_2) },
	{ "Priority3", NULL, _("_3"), "3", NULL, ACTION_CB(priority_3) },
	{ "RemovePriority", NULL, _("_Remove priority"), "0", NULL, ACTION_CB(priority_0) },
	{ "AutoWB", NULL, _("_Auto"), "A", NULL, ACTION_CB(auto_wb) },
	{ "CameraWB", NULL, _("_Camera"), "C", NULL, ACTION_CB(camera_wb) },
	{ "Crop", RS_STOCK_CROP, _("_Crop"), "<shift>C", NULL, ACTION_CB(crop) },
	{ "Uncrop", NULL, _("_Uncrop"), "<shift>V", NULL, ACTION_CB(uncrop) },
	{ "Straighten", RS_STOCK_ROTATE, _("_Straighten"), NULL, NULL, ACTION_CB(straighten) },
	{ "Unstraighten", NULL, _("_Unstraighten"), NULL, NULL, ACTION_CB(unstraighten) },
	{ "Group", NULL, _("_Group"), NULL, NULL, ACTION_CB(group_photos) },
	{ "Ungroup", NULL, _("_Ungroup"), NULL, NULL, ACTION_CB(ungroup_photos) },
	{ "AutoGroup", NULL, _("_Auto group"), NULL, NULL, ACTION_CB(auto_group_photos) },

	/* View menu */
	{ "PreviousPhoto", GTK_STOCK_GO_BACK, _("_Previous photo"), "<control>Left", NULL, ACTION_CB(previous_photo) },
	{ "NextPhoto", GTK_STOCK_GO_FORWARD, _("_Next Photo"), "<control>Right", NULL, ACTION_CB(next_photo) },
	{ "ZommToFit", GTK_STOCK_ZOOM_FIT, _("_Zoom to fit"), "slash", NULL, ACTION_CB(zoom_to_fit) },
	{ "ZoomTo100", GTK_STOCK_ZOOM_100, _("_Zoom to 100%"), "asterisk", NULL, ACTION_CB(zoom_to_100) },

	/* Batch menu */
	{ "AddToBatch", GTK_STOCK_ADD, _("_Add to batch queue"), "<control>B", NULL, ACTION_CB(add_to_batch) },
	{ "AddViewToBatch", NULL, _("_Add current view to queue"), NULL, NULL, ACTION_CB(add_view_to_batch) },
	{ "RemoveFromBatch", GTK_STOCK_REMOVE, _("_Remove from batch queue"), "<control><alt>B", NULL, ACTION_CB(remove_from_batch) },
	{ "ProcessBatch", GTK_STOCK_EXECUTE, _("_Start"), NULL, NULL, ACTION_CB(ProcessBatch) },

	/* help menu */
	{ "About", GTK_STOCK_ABOUT, _("_About"), NULL, NULL, ACTION_CB(about) },
	};
	static guint n_actionentries = G_N_ELEMENTS (actionentries);

	GtkToggleActionEntry toggleentries[] = {
	{ "Iconbox", NULL, _("_Iconbox"), "<control>I", NULL, ACTION_CB(iconbox), TRUE },
	{ "Toolbox", NULL, _("_Toolbox"), "<control>T", NULL, ACTION_CB(toolbox), TRUE },
	{ "Fullscreen", GTK_STOCK_FULLSCREEN, _("_Fullscreen"), "F11", NULL, ACTION_CB(fullscreen), FALSE },
	{ "ExposureMask", NULL, _("_Exposure mask"), "<control>E", NULL, ACTION_CB(exposure_mask), FALSE },
	{ "Split", NULL, _("_Split"), NULL, NULL, ACTION_CB(split), FALSE },
	};
	static guint n_toggleentries = G_N_ELEMENTS (toggleentries);

	GtkRadioActionEntry sort_by_popup[] = {
	{ "SortByName", NULL, _("Name"), NULL, NULL, RS_STORE_SORT_BY_NAME },
	{ "SortByTimestamp", NULL, _("Timestamp"), NULL, NULL, RS_STORE_SORT_BY_TIMESTAMP },
	{ "SortByISO", NULL, _("ISO"), NULL, NULL, RS_STORE_SORT_BY_ISO },
	{ "SortByAperture", NULL, _("Aperture"), NULL, NULL, RS_STORE_SORT_BY_APERTURE },
	{ "SortByFocallength", NULL, _("Focallength"), NULL, NULL, RS_STORE_SORT_BY_FOCALLENGTH },
	{ "SortByShutterspeed", NULL, _("Shutterspeed"), NULL, NULL, RS_STORE_SORT_BY_SHUTTERSPEED },
	};
	static guint n_sort_by_popup = G_N_ELEMENTS (sort_by_popup);

	GtkRadioActionEntry right_popup[] = {
	{ "RightA", NULL, _(" A "), NULL, NULL, 0 },
	{ "RightB", NULL, _(" B "), NULL, NULL, 1 },
	{ "RightC", NULL, _(" C "), NULL, NULL, 2 },
	};
	static guint n_right_popup = G_N_ELEMENTS (right_popup);

	g_static_mutex_lock(&rs_actions_spinlock);
	if (core_action_group == NULL)
	{
		core_action_group = gtk_action_group_new ("CoreActions");
		/* FIXME: gtk_action_group_set_translation_domain */
		gtk_action_group_add_actions (core_action_group, actionentries, n_actionentries, rs);
		gtk_action_group_add_toggle_actions(core_action_group, toggleentries, n_toggleentries, rs);
		gtk_action_group_add_radio_actions(core_action_group, sort_by_popup, n_sort_by_popup, rs_store_get_sort_method(rs->store), ACTION_CB(sort_by_popup), rs);
		gtk_action_group_add_radio_actions(core_action_group, right_popup, n_right_popup, 1, ACTION_CB(right_popup), rs);
	}
	g_static_mutex_unlock(&rs_actions_spinlock);

	return core_action_group;
}

/**
 * Set sensivity of an action
 * @param name The name of the action
 * @param sensitive The sensivity of the action
 */
void
rs_core_action_group_set_sensivity(const gchar *name, gboolean sensitive)
{
	if (core_action_group)
		gtk_action_set_sensitive(gtk_action_group_get_action(core_action_group, name), sensitive);
}

/**
 * Activate an action
 * @param name The action to activate
 */
void
rs_core_action_group_activate(const gchar *name)
{
	if (core_action_group)
		gtk_action_activate(gtk_action_group_get_action(core_action_group, name));
}

/**
 * Set visibility of an action
 * @param name The name of the action
 * @param visibility The visibility of the action
 */
void
rs_core_action_group_set_visibility(const gchar *name, gboolean visible)
{
	if (core_action_group)
		gtk_action_set_visible(gtk_action_group_get_action(core_action_group, name), visible);
}
