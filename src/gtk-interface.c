#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "color.h"
#include "matrix.h"
#include "rawstudio.h"
#include "gtk-interface.h"
#include "drawingarea.h"
#include "toolbox.h"
#include "conf_interface.h"
#include "rs-cache.h"
#include "rs-image.h"
#include "gettext.h"
#include <string.h>
#include <unistd.h>

struct nextprev_helper {
	const gchar *filename;
	GtkTreePath *previous;
	GtkTreePath *next;
};

static gchar *option_dir = NULL;
static GOptionEntry entries[] = 
  {
    { "dir", 'd', 0, G_OPTION_ARG_STRING, &option_dir, "Open this directory as cwd instead of current or the last used.", NULL },
    { NULL }
  };

GtkStatusbar *statusbar;
static gboolean fullscreen = FALSE;
static GtkWidget *iconview[6];
static GtkWidget *current_iconview = NULL;
static guint priorities[6];
static guint current_priority = PRIO_ALL;
static GtkTreeIter current_iter;

void gui_status_push(const char *text);
gint fill_model_compare_func (GtkTreeModel *model, GtkTreeIter *tia,
	GtkTreeIter *tib, gpointer userdata);
void fill_model(GtkListStore *store, const char *path);
void icon_activated_helper(GtkIconView *iconview, GtkTreePath *path, gpointer user_data);
void icon_activated(GtkIconView *iconview, RS_BLOB *rs);
GtkWidget *make_iconbox(RS_BLOB *rs, GtkListStore *store);
void gui_menu_open_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_menu_reload_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_preview_bg_color_changed(GtkColorButton *widget, RS_BLOB *rs);
gboolean gui_fullscreen_callback(GtkWidget *widget, GdkEventWindowState *event, GtkWidget *iconbox);
void gui_menu_setprio_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_menu_widget_visible_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_menu_fullscreen_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
gboolean gui_menu_prevnext_helper(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data);
void gui_menu_prevnext_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_menu_preference_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_about();
void gui_menu_auto_wb_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_save_file_callback(gpointer callback_data, guint callback_action, GtkWidget *widget);
void gui_reset_current_settings_callback(RS_BLOB *rs);
void gui_menu_quit(gpointer callback_data, guint callback_action, GtkWidget *widget);
GtkWidget *gui_make_menubar(RS_BLOB *rs, GtkWidget *window, GtkListStore *store, GtkWidget *iconbox, GtkWidget *toolbox);
GtkWidget *gui_window_make(RS_BLOB *rs);

void
gui_status_push(const char *text)
{
	gtk_statusbar_pop(statusbar, gtk_statusbar_get_context_id(statusbar, "generic"));
	gtk_statusbar_push(statusbar, gtk_statusbar_get_context_id(statusbar, "generic"), text);
	return;
}

gboolean
update_preview_callback(GtkAdjustment *caller, RS_BLOB *rs)
{
	update_preview(rs);
	return(FALSE);
}

void update_histogram(RS_BLOB *rs)
{
	guint c,i,x,y,rowstride;
	guint max = 0;
	guint factor = 0;
	guint hist[3][256];
	gint height;
	GdkPixbuf *pixbuf;
	guchar *pixels, *p;

	pixbuf = gtk_image_get_pixbuf(rs->histogram_image);
	rowstride = gdk_pixbuf_get_rowstride (pixbuf);
	pixels = gdk_pixbuf_get_pixels (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	/* sets all the pixels black */
	memset(pixels, 0x00, rowstride*height);

	/* draw a grid with 7 bars with 32 pixels space */
	p = pixels;
	for(y = 0; y < height; y++)
	{
		for(x = 0; x < 256 * 3; x +=93)
		{
			p[x++] = 100;
			p[x++] = 100;
			p[x++] = 100;
		}
		p+=rowstride;
	}

	/* find the max value */
	for (c = 0; c < 3; c++)
	{
		for (i = 0; i < 256; i++)
		{
			_MAX(rs->histogram_table[c][i], max);
		}
	}

	/* find the factor to scale the histogram */
	factor = (max+height)/height;

	/* calculate the histogram values */
	for (c = 0; c < 3; c++)
	{
		for (i = 0; i < 256; i++)
		{
			hist[c][i] = rs->histogram_table[c][i]/factor;
		}
	}

	/* draw the histogram */
	for (x = 0; x < 256; x++)
	{
		for (c = 0; c < 3; c++)
		{
			for (y = 0; y < hist[c][x]; y++)
			{				
				/* address the pixel - the (rs->hist_h-1)-y is to draw it from the bottom */
				p = pixels + ((height-1)-y) * rowstride + x * 3;
				p[c] = 0xFF;
			}
		}
	}
	gtk_image_set_from_pixbuf((GtkImage *) rs->histogram_image, pixbuf);

}

gint
fill_model_compare_func (GtkTreeModel *model, GtkTreeIter *tia,
	GtkTreeIter *tib, gpointer userdata)
{
	gint ret;
	gchar *a, *b;

	gtk_tree_model_get(model, tia, TEXT_COLUMN, &a, -1);
	gtk_tree_model_get(model, tib, TEXT_COLUMN, &b, -1);
	ret = g_utf8_collate(a,b);
	g_free(a);
	g_free(b);
	return(ret);
}

void
fill_model(GtkListStore *store, const gchar *inpath)
{
	static gchar *path=NULL;
	gchar *name;
	GtkTreeIter iter;
	GdkPixbuf *pixbuf;
	GError *error;
	GDir *dir;
	GtkTreeSortable *sortable;
	gint priority;
	RS_FILETYPE *filetype;

	if (inpath)
	{
		if (path)
			g_free(path);
		path = g_strdup(inpath);
	}
	dir = g_dir_open(path, 0, &error);
	if (dir == NULL) return;

	rs_conf_set_string(CONF_LWD, path);

	gui_status_push(_("Opening directory ..."));
	GUI_CATCHUP();

	g_dir_rewind(dir);

	gtk_list_store_clear(store);
	while((name = (gchar *) g_dir_read_name(dir)))
	{
		filetype = rs_filetype_get(name, TRUE);
		if (filetype)
			if (filetype->load)
			{
				GString *fullname;
				fullname = g_string_new(path);
				fullname = g_string_append(fullname, "/");
				fullname = g_string_append(fullname, name);
				priority = PRIO_U;
				rs_cache_load_quick(fullname->str, &priority);
				pixbuf = NULL;
				if (filetype->thumb)
					pixbuf = filetype->thumb(fullname->str);
				gtk_list_store_prepend (store, &iter);
				if (pixbuf==NULL)
				{
					pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 64);
					gdk_pixbuf_fill(pixbuf, 0x00000000);
				}
				gtk_list_store_set (store, &iter,
					PIXBUF_COLUMN, pixbuf,
					TEXT_COLUMN, name,
					FULLNAME_COLUMN, fullname->str,
					PRIORITY_COLUMN, priority,
					-1);
				g_object_unref (pixbuf);
				g_string_free(fullname, FALSE);
			}
	}
	sortable = GTK_TREE_SORTABLE(store);
	gtk_tree_sortable_set_sort_func(sortable,
		TEXT_COLUMN,
		fill_model_compare_func,
		NULL,
		NULL);
	gtk_tree_sortable_set_sort_column_id(sortable, TEXT_COLUMN, GTK_SORT_ASCENDING);
	gui_status_push(_("Directory opened"));
}

void
icon_activated_helper(GtkIconView *iconview, GtkTreePath *path, gpointer user_data)
{
	gchar *name;
	gchar **out = user_data;
	GtkTreeModel *model = gtk_icon_view_get_model (iconview);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter(model, &iter, path))
	{
		gtk_tree_model_get (model, &iter, FULLNAME_COLUMN, &name, -1);
		gtk_tree_model_filter_convert_iter_to_child_iter((GtkTreeModelFilter *)model, &current_iter, &iter);
		*out = name;
	}
}

void
icon_activated(GtkIconView *iconview, RS_BLOB *rs)
{
	GtkTreeModel *model;
	gchar *name = NULL;
	RS_FILETYPE *filetype;
	extern GtkLabel *infolabel;
	GString *label;

	model = gtk_icon_view_get_model(iconview);
	gtk_icon_view_selected_foreach(iconview, icon_activated_helper, &name);
	if (name!=NULL)
	{
		gui_status_push(_("Opening image ..."));
		GUI_CATCHUP();
		if ((filetype = rs_filetype_get(name, TRUE)))
		{
			rs_cache_save(rs);
			rs->in_use = FALSE;
			rs_reset(rs);
			filetype->load(rs, name);
			if (filetype->load_meta)
			{
				filetype->load_meta(name, rs->metadata);
				switch (rs->metadata->orientation)
				{
					case 6: ORIENTATION_90(rs->orientation);
						break;
					case 8: ORIENTATION_270(rs->orientation);
						break;
				}
				label = g_string_new("");
				if (rs->metadata->shutterspeed!=0.0)
					g_string_append_printf(label, _("1/%.0f "), rs->metadata->shutterspeed);
				if (rs->metadata->iso!=0)
					g_string_append_printf(label, _("ISO%d "), rs->metadata->iso);
				if (rs->metadata->aperture!=0.0)
					g_string_append_printf(label, _("F/%.1f"), rs->metadata->aperture);
				gtk_label_set_text(infolabel, label->str);
				g_string_free(label, TRUE);
			} else
				gtk_label_set_text(infolabel, _("No metadata"));
			rs_cache_load(rs);
		}
		rs->in_use = TRUE;
		update_preview(rs);
		gui_status_push(_("Image opened"));
	}
}

gboolean
gui_tree_filter_helper(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gint p;
	gint prio = (gint) data;
	gtk_tree_model_get (model, iter, PRIORITY_COLUMN, &p, -1);
	switch(prio)
	{
		case PRIO_ALL:
			return(TRUE);
			break;
		case PRIO_U:
			switch (p)
			{
				case PRIO_1:
				case PRIO_2:
				case PRIO_3:
				case PRIO_D:
					return(FALSE);
					break;
				default:
					return(TRUE);
					break;
			}
		default:
			if (prio==p) return(TRUE);
			break;
	}
	return(FALSE);
}

GtkWidget *
make_iconview(RS_BLOB *rs, GtkWidget *iconview, GtkListStore *store, gint prio)
{
	GtkWidget *scroller;
	GtkTreeModel *tree;

	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (iconview), PIXBUF_COLUMN);
	gtk_icon_view_set_text_column (GTK_ICON_VIEW (iconview), TEXT_COLUMN);

	tree = gtk_tree_model_filter_new(GTK_TREE_MODEL (store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER (tree),
		gui_tree_filter_helper, (gpointer) prio, NULL);
	gtk_icon_view_set_model (GTK_ICON_VIEW (iconview), tree);
	gtk_icon_view_set_columns(GTK_ICON_VIEW (iconview), 1000);
	gtk_icon_view_set_selection_mode(GTK_ICON_VIEW (iconview), GTK_SELECTION_BROWSE);
	gtk_widget_set_size_request (iconview, -1, 160);
	g_signal_connect((gpointer) iconview, "selection_changed",
		G_CALLBACK (icon_activated), rs);
	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_container_add (GTK_CONTAINER (scroller), iconview);
	return(scroller);
}

void
gui_icon_notebook_callback(GtkNotebook *notebook, GtkNotebookPage *page,
	guint page_num, gpointer date)
{
	current_iconview = iconview[page_num];
	current_priority = priorities[page_num];
	return;
}

GtkWidget *
make_iconbox(RS_BLOB *rs, GtkListStore *store)
{
	GtkWidget *notebook;
	GtkWidget *label1;
	GtkWidget *label2;
	GtkWidget *label3;
	GtkWidget *label4;
	GtkWidget *label5;
	GtkWidget *label6;
	gint n;

	for(n=0;n<6;n++)
		iconview[n] = gtk_icon_view_new();

	label1 = gtk_label_new(_("*"));
	label2 = gtk_label_new(_("1"));
	label3 = gtk_label_new(_("2"));
	label4 = gtk_label_new(_("3"));
	label5 = gtk_label_new(_("U"));
	label6 = gtk_label_new(_("D"));

	priorities[0] = PRIO_ALL;
	priorities[1] = PRIO_1;
	priorities[2] = PRIO_2;
	priorities[3] = PRIO_3;
	priorities[4] = PRIO_U;
	priorities[5] = PRIO_D;

	notebook = gtk_notebook_new();

	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[0], store, priorities[0]), label1);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[1], store, priorities[1]), label2);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[2], store, priorities[2]), label3);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[3], store, priorities[3]), label4);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[4], store, priorities[4]), label5);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_iconview(rs, iconview[5], store, priorities[5]), label6);

	g_signal_connect(notebook, "switch-page", G_CALLBACK(gui_icon_notebook_callback), NULL);

	return(notebook);
}

void
gui_menu_open_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkWidget *fc;
	GtkListStore *store = (GtkListStore *) callback_action;
	gchar *lwd = rs_conf_get_string(CONF_LWD);

	fc = gtk_file_chooser_dialog_new (_("Open File"), NULL,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), lwd);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		gtk_widget_destroy (fc);
		fill_model(store, filename);
		g_free (filename);
	} else
		gtk_widget_destroy (fc);

	g_free(lwd);
	return;
}

void
gui_menu_reload_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkListStore *store = (GtkListStore *) callback_action;
	fill_model(store, NULL);
	return;
}
void
gui_preview_bg_color_changed(GtkColorButton *widget, RS_BLOB *rs)
{
	GdkColor color;
	gtk_color_button_get_color(GTK_COLOR_BUTTON(widget), &color);
	gtk_widget_modify_bg(rs->preview_drawingarea->parent->parent,
		GTK_STATE_NORMAL, &color);
	rs_conf_set_color(CONF_PREBGCOLOR, &color);
	return;
}

gboolean
gui_fullscreen_callback(GtkWidget *widget, GdkEventWindowState *event, GtkWidget *iconbox)
{
	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
	{
		gtk_widget_hide(iconbox);
		fullscreen = TRUE;
	}
	if (!(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN))
	{
		gtk_widget_show(iconbox);
		fullscreen = FALSE;
	}
	return(FALSE);
}

gboolean
gui_menu_prevnext_helper(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
	struct nextprev_helper *helper = user_data;
    gchar *name;
	guint priority;
	gchar *needle;

	gtk_tree_model_get(model, iter, PRIORITY_COLUMN, &priority, -1);

	if ((priority == current_priority) || (current_priority==PRIO_ALL))
	{
		needle = g_path_get_basename(helper->filename);
		gtk_tree_model_get(model, iter, TEXT_COLUMN, &name, -1);
		if(g_utf8_collate(needle, name) < 0) /* after */
		{
			helper->next = gtk_tree_path_copy(path);
			g_free(needle);
			g_free(name);
			return(TRUE);
		}
		else if (g_utf8_collate(needle, name) > 0) /* before */
		{
			if (helper->previous)
				gtk_tree_path_free(helper->previous);
			helper->previous = gtk_tree_path_copy(path);
			g_free(needle);
			g_free(name);
		}
		else
		{
			g_free(needle);
	    	g_free(name);
		}
	}
    return FALSE;
}

void
gui_menu_prevnext_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkTreeModel *model;
	GtkTreeModel *child;
	GtkTreePath *path = NULL;
	struct nextprev_helper helper;
	RS_BLOB *rs = (RS_BLOB *) callback_data;

	if (!rs->in_use) return;

	helper.filename = rs->filename;
	helper.previous = NULL;
	helper.next = NULL;

	model = gtk_icon_view_get_model((GtkIconView *) current_iconview);
	child = gtk_tree_model_filter_get_model((GtkTreeModelFilter *) model);
	gtk_tree_model_foreach(child, gui_menu_prevnext_helper, &helper);

	switch (callback_action)
	{
		case 1: /* previous */
			if (helper.previous)
				path = gtk_tree_model_filter_convert_child_path_to_path(
					(GtkTreeModelFilter *) model, helper.previous);
			break;
		case 2: /* next */
			if (helper.next)
				path = gtk_tree_model_filter_convert_child_path_to_path(
					(GtkTreeModelFilter *) model, helper.next);
			break;
	}

	if (path)
		gtk_icon_view_select_path((GtkIconView *) current_iconview, path);

	if (helper.next)
		gtk_tree_path_free(helper.next);
	if (helper.previous)
		gtk_tree_path_free(helper.previous);
	return;
}

void
gui_menu_setprio_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkTreeModel *model;
	RS_BLOB *rs = (RS_BLOB *) callback_data;

	model = gtk_icon_view_get_model((GtkIconView *) current_iconview);
	model = gtk_tree_model_filter_get_model ((GtkTreeModelFilter *) model);
	if (gtk_list_store_iter_is_valid((GtkListStore *)model, &current_iter))
	{
		gtk_list_store_set ((GtkListStore *)model, &current_iter,
			PRIORITY_COLUMN, callback_action,
			-1);
		rs->priority = callback_action;
		gui_status_push(_("Changed image priority"));
	}
	return;
}

void
gui_menu_widget_visible_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkWidget *target = (GtkWidget *) callback_action;
	if (GTK_WIDGET_VISIBLE(target))
		gtk_widget_hide(target);
	else
		gtk_widget_show(target);
	return;
}

void
gui_menu_fullscreen_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkWindow *window = (GtkWindow *) callback_action;
	if (fullscreen)
		gtk_window_unfullscreen(window);
	else
		gtk_window_fullscreen(window);
	return;
}

gboolean
gui_histogram_height_changed(GtkAdjustment *caller, RS_BLOB *rs)
{
	GdkPixbuf *pixbuf;
	const gint newheight = (gint) caller->value;
	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 256, newheight);
	gtk_image_set_from_pixbuf((GtkImage *) rs->histogram_image, pixbuf);
	update_histogram(rs);
	rs_conf_set_integer(CONF_HISTHEIGHT, newheight);
	return(FALSE);
}

void
gui_menu_preference_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	GtkWidget *dialog;
	GtkWidget *notebook;
	GtkWidget *vbox;
	GtkWidget *colorsel;
	GtkWidget *colorsel_label;
	GtkWidget *colorsel_hbox;
	GtkWidget *preview_page;
	GtkWidget *button_close;
	GdkColor color;
	GtkWidget *histsize;
	GtkWidget *histsize_label;
	GtkWidget *histsize_hbox;
	GtkObject *histsize_adj;
	gint histogram_height;
	RS_BLOB *rs = (RS_BLOB *) callback_data;

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), _("Preferences"));
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_dialog_set_has_separator (GTK_DIALOG(dialog), FALSE);
	g_signal_connect_swapped(dialog, "delete_event",
		G_CALLBACK (gtk_widget_destroy), dialog);
	g_signal_connect_swapped(dialog, "response",
		G_CALLBACK (gtk_widget_destroy), dialog);

	vbox = GTK_DIALOG (dialog)->vbox;

	preview_page = gtk_vbox_new(FALSE, 0);
	colorsel_hbox = gtk_hbox_new(FALSE, 0);
	colorsel_label = gtk_label_new(_("Preview background color:"));
	colorsel = gtk_color_button_new();
	COLOR_BLACK(color);
	if (rs_conf_get_color(CONF_PREBGCOLOR, &color))
		gtk_color_button_set_color(GTK_COLOR_BUTTON(colorsel), &color);
	g_signal_connect(colorsel, "color-set", G_CALLBACK (gui_preview_bg_color_changed), rs);
	gtk_box_pack_start (GTK_BOX (colorsel_hbox), colorsel_label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (colorsel_hbox), colorsel, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (preview_page), colorsel_hbox, FALSE, TRUE, 0);

	if (!rs_conf_get_integer(CONF_HISTHEIGHT, &histogram_height))
		histogram_height = 128;
	histsize_hbox = gtk_hbox_new(FALSE, 0);
	histsize_label = gtk_label_new(_("Histogram height:"));
	histsize_adj = gtk_adjustment_new(histogram_height, 15.0, 500.0, 1.0, 10.0, 10.0);
	g_signal_connect(histsize_adj, "value_changed",
		G_CALLBACK(gui_histogram_height_changed), rs);
	histsize = gtk_spin_button_new(GTK_ADJUSTMENT(histsize_adj), 1, 0);
	gtk_box_pack_start (GTK_BOX (histsize_hbox), histsize_label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (histsize_hbox), histsize, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (preview_page), histsize_hbox, FALSE, TRUE, 0);

	notebook = gtk_notebook_new();
	gtk_container_set_border_width (GTK_CONTAINER (notebook), 6);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), preview_page, gtk_label_new(_("Preview")));
	gtk_box_pack_start (GTK_BOX (vbox), notebook, FALSE, FALSE, 0);

	button_close = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button_close, GTK_RESPONSE_CLOSE);

	gtk_widget_show_all(dialog);
	return;
}

void
gui_about()
{
	static GtkWidget *aboutdialog = NULL;
	const gchar *authors[] = {
		"Anders Brander <anders@brander.dk>",
		"Anders Kvist <anders@kvistmail.dk>",
	};
	if (!aboutdialog)
	{
		aboutdialog = gtk_about_dialog_new ();
		gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (aboutdialog), "0.1rc");
		gtk_about_dialog_set_name (GTK_ABOUT_DIALOG (aboutdialog), "Rawstudio");
		gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG (aboutdialog), _("A raw image converter for GTK+/GNOME"));
		gtk_about_dialog_set_website(GTK_ABOUT_DIALOG (aboutdialog), "http://rawstudio.org/");
		gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG (aboutdialog), authors);
	}
	gtk_widget_show(aboutdialog);
	return;
}

void
gui_dialog_simple(gchar *title, gchar *message)
{
	GtkWidget *dialog, *label;

	dialog = gtk_dialog_new_with_buttons(title, NULL, GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_OK, GTK_RESPONSE_NONE, NULL);
	label = gtk_label_new(message);
	g_signal_connect_swapped(dialog, "response",
		G_CALLBACK (gtk_widget_destroy), dialog);
	gtk_container_add(GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), label);
	gtk_widget_show_all(dialog);
	return;
}

void
gui_menu_auto_wb_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	RS_BLOB *rs = (RS_BLOB *) callback_data;
	rs_set_wb_auto(rs);
}

void
gui_save_file_callback(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	RS_BLOB *rs = (RS_BLOB *) callback_data;
	GtkWidget *fc;
	GString *name;
	gchar *dirname;
	gchar *basename;
	GString *export_path;
	gchar *conf_export;
	if (!rs->in_use) return;
	dirname = g_path_get_dirname(rs->filename);
	basename = g_path_get_basename(rs->filename);

	conf_export = rs_conf_get_string("default_export_template");

	if (conf_export)
	{
		if (conf_export[0]=='/')
		{
			g_free(dirname);
			dirname = conf_export;
		}
		else
		{
			export_path = g_string_new(dirname);
			g_string_append(export_path, "/");
			g_string_append(export_path, conf_export);
			g_free(dirname);
			dirname = export_path->str;
			g_string_free(export_path, FALSE);
			g_free(conf_export);
		}
		g_mkdir_with_parents(dirname, 00755);
	}

	gui_status_push(_("Saving file ..."));
	name = g_string_new(basename);
	g_string_append(name, "_output.png");

	fc = gtk_file_chooser_dialog_new (_("Save File"), NULL,
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
#if GTK_CHECK_VERSION(2,8,0)
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (fc), TRUE);
#endif
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (fc), dirname);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (fc), name->str);
	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		GdkPixbuf *pixbuf;
		RS_IMAGE16 *rsi;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		gtk_widget_destroy(fc);
		if (rs->orientation)
		{
			rsi = rs_image16_copy(rs->input);
			rs_image16_orientation(rsi, rs->orientation);
		}
		else
			rsi = rs->input;
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, rsi->w, rsi->h);
		rs_render(rs, rsi->w, rsi->h, rsi->pixels,
			rsi->rowstride, rsi->channels,
			gdk_pixbuf_get_pixels(pixbuf), gdk_pixbuf_get_rowstride(pixbuf));
		gdk_pixbuf_save(pixbuf, filename, "png", NULL, NULL);
		if (rs->orientation)
			rs_image16_free(rsi);
		g_object_unref(pixbuf);
		g_free (filename);
	} else
		gtk_widget_destroy(fc);
	g_free(dirname);
	g_free(basename);
	g_string_free(name, TRUE);
	gui_status_push(_("File saved"));
	return;
}

void
gui_reset_current_settings_callback(RS_BLOB *rs)
{
	gboolean in_use = rs->in_use;
	rs->in_use = FALSE;
	rs_settings_reset(rs->settings[rs->current_setting], MASK_ALL);
	rs->in_use = in_use;
	update_preview(rs);
	return;
}

void
gui_menu_quit(gpointer callback_data, guint callback_action, GtkWidget *widget)
{
	RS_BLOB *rs = (RS_BLOB *) callback_data;
	rs_shutdown(NULL, NULL, rs);
	return;
}

GtkWidget *
gui_make_menubar(RS_BLOB *rs, GtkWidget *window, GtkListStore *store, GtkWidget *iconbox, GtkWidget *toolbox)
{
	GtkItemFactoryEntry menu_items[] = {
		{ _("/_File"), NULL, NULL, 0, "<Branch>"},
		{ _("/File/_Open..."), "<CTRL>O", gui_menu_open_callback, (gint) store, "<StockItem>", GTK_STOCK_OPEN},
		{ _("/File/_Save as..."), "<CTRL>S", gui_save_file_callback, (gint) store, "<StockItem>", GTK_STOCK_SAVE_AS},
		{ _("/File/_Reload"), "<CTRL>R", gui_menu_reload_callback, (gint) store, "<StockItem>", GTK_STOCK_REFRESH},
		{ _("/File/_Quit"), "<CTRL>Q", gui_menu_quit, 0, "<StockItem>", GTK_STOCK_QUIT},
		{ _("/_Edit"), NULL, NULL, 0, "<Branch>"},
		{ _("/_Edit/_Reset current settings"), NULL , gui_reset_current_settings_callback, (gint) store},
		{ _("/_Edit/_Set priority/_1"),  "1", gui_menu_setprio_callback, PRIO_1},
		{ _("/_Edit/_Set priority/_2"),  "2", gui_menu_setprio_callback, PRIO_2},
		{ _("/_Edit/_Set priority/_3"),  "3", gui_menu_setprio_callback, PRIO_3},
		{ _("/_Edit/_Set priority/_Delete"),  "Delete", gui_menu_setprio_callback, PRIO_D, "<StockItem>", GTK_STOCK_DELETE},
		{ _("/_Edit/_Set priority/_Remove"),  "0", gui_menu_setprio_callback, PRIO_U, "<StockItem>", GTK_STOCK_DELETE},
		{ _("/_Edit/_White balance/_Auto"), "A", gui_menu_auto_wb_callback, 0 },
		{ _("/_Edit/_Preferences"), NULL, gui_menu_preference_callback, 0, "<StockItem>", GTK_STOCK_PREFERENCES},
		{ _("/_View"), NULL, NULL, 0, "<Branch>"},
		{ _("/_View/_Previous image"), "<CTRL>Left", gui_menu_prevnext_callback, 1, "<StockItem>", GTK_STOCK_GO_BACK},
		{ _("/_View/_Next image"), "<CTRL>Right", gui_menu_prevnext_callback, 2, "<StockItem>", GTK_STOCK_GO_FORWARD},
		{ _("/_View/_Icon Box"), "<CTRL>I", gui_menu_widget_visible_callback, (gint) iconbox},
		{ _("/_View/_Tool Box"), "<CTRL>T", gui_menu_widget_visible_callback, (gint) toolbox},
		{ _("/_View/sep1"), NULL, NULL, 0, "<Separator>"},
#if GTK_CHECK_VERSION(2,8,0)
		{ _("/_View/_Fullscreen"), "F11", gui_menu_fullscreen_callback, (gint) window, "<StockItem>", GTK_STOCK_FULLSCREEN},
#else
		{ _("/_View/_Fullscreen"), "F11", gui_menu_fullscreen_callback, (gint) window},
#endif
		{ _("/_Help"), NULL, NULL, 0, "<LastBranch>"},
		{ _("/_Help/About"), NULL, gui_about, 0, "<StockItem>", GTK_STOCK_ABOUT},
	};
	static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);
	GtkItemFactory *item_factory;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new ();
	item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
	gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, (gpointer) rs);
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
	return(gtk_item_factory_get_widget (item_factory, "<main>"));
}

GtkWidget *
gui_window_make(RS_BLOB *rs)
{
	GtkWidget *window;
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_resize((GtkWindow *) window, 800, 600);
	gtk_window_set_title (GTK_WINDOW (window), _("Rawstudio"));
	g_signal_connect((gpointer) window, "delete_event", G_CALLBACK(rs_shutdown), rs);
	return(window);
}

int
gui_init(int argc, char **argv)
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *pane;
	GtkWidget *toolbox;
	GtkWidget *iconbox;
	GtkWidget *preview;
	GtkListStore *store;
	GtkWidget *menubar;
	RS_BLOB *rs;
	gchar *lwd;

	GError *error = NULL;
	GOptionContext* context;

	gtk_init(&argc, &argv);
	
	context = g_option_context_new ("");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free(context);

	rs = rs_new();
	window = gui_window_make(rs);
	statusbar = (GtkStatusbar *) gtk_statusbar_new();
	toolbox = make_toolbox(rs);

	store = gtk_list_store_new (NUM_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
	iconbox = make_iconbox(rs, store);
	g_signal_connect((gpointer) window, "window-state-event", G_CALLBACK(gui_fullscreen_callback), iconbox);

	/* if -d og --dir is given, use that as path */
	if (option_dir)
		lwd = option_dir;
	else
	{
		lwd = rs_conf_get_string(CONF_LWD);
		if (!lwd)
			lwd = g_get_current_dir();
	}
	fill_model(store, lwd);
	g_free(lwd);

	menubar = gui_make_menubar(rs, window, store, iconbox, toolbox);
	preview = gui_drawingarea_make(rs);

	pane = gtk_hpaned_new ();

	gtk_paned_pack1 (GTK_PANED (pane), preview, TRUE, TRUE);
	gtk_paned_pack2 (GTK_PANED (pane), toolbox, FALSE, TRUE);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (window), vbox);

	gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), iconbox, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), pane, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (statusbar), FALSE, TRUE, 0);

	gui_status_push(_("Ready"));

	gtk_widget_show_all (window);
	gtk_main();
	return(0);
}
