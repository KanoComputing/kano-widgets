/*
 * kano_notifications.c
 *
 * Copyright (C) 2014 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
 *
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

#include <lxpanel/plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FIFO_PATH "/tmp/kano-notifications.fifo"
#define DEFAULT_ICON_FILE "/usr/share/kano-updater/images/panel-default.png"

Panel *panel;

typedef struct {
	int fifo_fd;
	GIOChannel *fifo_channel;
	guint watch_id;

	GtkWidget *icon;

	GMutex lock;
	GList *queue;

	GtkWidget *notification;
	guint window_timeout;
} kano_notifications_t;

static gboolean show_menu(GtkWidget *, GdkEventButton *,
			  kano_notifications_t *);
static void selection_done(GtkWidget *);
static void popup_set_position(GtkWidget *, gint *, gint *, gboolean *,
			       GtkWidget *);
static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data);


static gboolean close_notification(kano_notifications_t *plugin_data);

static int plugin_constructor(Plugin *plugin, char **fp)
{
	(void)fp;

	panel = plugin->panel;

	/* allocate our private structure instance */
	kano_notifications_t *plugin_data = g_new0(kano_notifications_t, 1);

	plugin_data->notification = NULL;
	plugin_data->queue = NULL;

	g_mutex_init(&(plugin_data->lock));

	unlink(FIFO_PATH);
	if (mkfifo(FIFO_PATH, 0600) < 0) {
		perror("mkfifo");
		return 0;
	}

	plugin_data->fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK, 0);
	if (plugin_data->fifo_fd < 0) {
		perror("open");
		return 0;
	}

	plugin_data->fifo_channel = g_io_channel_unix_new(plugin_data->fifo_fd);
	plugin_data->watch_id = g_io_add_watch(plugin_data->fifo_channel,
		G_IO_IN, (GIOFunc)io_watch_cb, (gpointer)plugin_data);


	/* put it where it belongs */
	plugin->priv = plugin_data;

	GtkWidget *icon = gtk_image_new_from_file(DEFAULT_ICON_FILE);
	plugin_data->icon = icon;

	/* need to create a widget to show */
	plugin->pwid = gtk_event_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(plugin->pwid), 0);
	gtk_container_add(GTK_CONTAINER(plugin->pwid), GTK_WIDGET(icon));

	/* our widget doesn't have a window... */
	gtk_widget_set_has_window(plugin->pwid, FALSE);


	gtk_signal_connect(GTK_OBJECT(plugin->pwid), "button-press-event",
			   GTK_SIGNAL_FUNC(show_menu), plugin);


	/* Set a tooltip to the icon to show when the mouse sits over it */
	GtkTooltips *tooltips;
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, GTK_WIDGET(icon),
			     "Notification Centre", NULL);

	gtk_widget_set_sensitive(icon, TRUE);

	/*TODO plugin->timer = g_timeout_add(60000, (GSourceFunc) update_status,
				      (gpointer) plugin); */


	/* show our widget */
	gtk_widget_show_all(plugin->pwid);

	return 1;
}

static void plugin_destructor(Plugin *p)
{
	kano_notifications_t *plugin_data = (kano_notifications_t *)p->priv;

	g_source_remove(plugin_data->watch_id);
	g_io_channel_shutdown(plugin_data->fifo_channel, FALSE, NULL);
	g_io_channel_unref(plugin_data->fifo_channel);
	close(plugin_data->fifo_fd);
	unlink(FIFO_PATH);

	g_mutex_clear(&(plugin_data->lock));

	/* Disconnect the timer. */
	// TODO g_source_remove(plugin->timer);

	g_free(plugin_data);
}

static gboolean hide_notification(GtkWidget *w, GdkEventButton *event,
				  kano_notifications_t *plugin_data)
{
	if (g_mutex_trylock(&(plugin_data->lock)) == TRUE) {
		g_source_remove(plugin_data->window_timeout);
		g_mutex_unlock(&(plugin_data->lock));
		close_notification(plugin_data);
	}

	return TRUE;
}

static void show_notification(kano_notifications_t *plugin_data,
			      gchar *id)
{
#define WIDTH 280
#define HEIGHT (170 + 90)

	GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
	plugin_data->notification = win;
	//gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
	//gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(win), WIDTH, HEIGHT);
	gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

	gtk_window_set_gravity(GTK_WINDOW(win), GDK_GRAVITY_SOUTH_EAST);
	gtk_window_move(GTK_WINDOW(win), gdk_screen_width() - WIDTH - 20,
			gdk_screen_height() - HEIGHT - 50);

	GtkStyle *style;
	GdkColor white;
	gdk_color_parse("#fff", &white);
	gtk_widget_modify_bg(win, GTK_STATE_NORMAL, &white);

	GtkWidget *image = gtk_image_new_from_file(
		"/home/radek/kano-widgets/lxpanel-plugin-notifications/badge.png");
	gtk_widget_add_events(image, GDK_BUTTON_RELEASE_MASK);

	GtkWidget *eventbox = gtk_event_box_new();
	gtk_signal_connect(GTK_OBJECT(eventbox), "button-release-event",
                     GTK_SIGNAL_FUNC(hide_notification), plugin_data);

	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(eventbox), GTK_WIDGET(box));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(image),
			   FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(eventbox));

	GtkWidget *labels = gtk_vbox_new(FALSE, 0);

	GtkWidget *title = gtk_label_new(id);
	gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_LEFT);
	style = gtk_widget_get_style(title);
	pango_font_description_set_size(style->font_desc, 18*PANGO_SCALE);
	pango_font_description_set_weight(style->font_desc, PANGO_WEIGHT_BOLD);
	gtk_widget_modify_font(title, style->font_desc);

	GtkWidget *title_align = gtk_alignment_new(0,0,0,0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(title_align), 20, 0, 20, 0);
	gtk_container_add(GTK_CONTAINER(title_align), title);
	gtk_box_pack_start(GTK_BOX(labels), GTK_WIDGET(title_align),
			   FALSE, FALSE, 0);

	GtkWidget *byline = gtk_label_new("Computer Commander");
	gtk_label_set_justify(GTK_LABEL(byline), GTK_JUSTIFY_LEFT);
	style = gtk_widget_get_style(byline);
	pango_font_description_set_size(style->font_desc, 12*PANGO_SCALE);
	pango_font_description_set_weight(style->font_desc, PANGO_WEIGHT_NORMAL);
	gtk_widget_modify_font(byline, style->font_desc);

	GtkWidget *byline_align = gtk_alignment_new(0,0,0,0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(byline_align), 0, 0, 20, 0);
	gtk_container_add(GTK_CONTAINER(byline_align), byline);
	gtk_box_pack_start(GTK_BOX(labels), GTK_WIDGET(byline_align),
			   FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(labels),
			   TRUE, TRUE, 0);


	gtk_widget_show_all(win);

	plugin_data->window_timeout = g_timeout_add(3000,
				(GSourceFunc) close_notification,
				(gpointer) plugin_data);
}

static gboolean close_notification(kano_notifications_t *plugin_data)
{
	if (plugin_data->notification != NULL) {
		g_mutex_lock(&(plugin_data->lock));

		gtk_widget_destroy(plugin_data->notification);
		plugin_data->notification = NULL;

		gchar *id = g_list_nth_data(plugin_data->queue, 0);
		g_free(id);
		plugin_data->queue = g_list_remove(plugin_data->queue, id);


		if (g_list_length(plugin_data->queue) >= 1) {
			gchar *id = g_list_nth_data(plugin_data->queue, 0);
			show_notification(plugin_data, id);
		}

		g_mutex_unlock(&(plugin_data->lock));
	}

	return FALSE;
}

static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data)
{
	kano_notifications_t *plugin_data = (kano_notifications_t *)data;

	gchar *line = NULL;
	gsize len, tpos;
	GIOStatus status;

	status = g_io_channel_read_line(source, &line, &len, &tpos, NULL);
	if (status == G_IO_STATUS_NORMAL) {
		line[tpos] = '\0';

		g_mutex_lock(&(plugin_data->lock));

		plugin_data->queue = g_list_append(plugin_data->queue, line);

		if (g_list_length(plugin_data->queue) <= 1) {
			show_notification(plugin_data, line);
		}

		g_mutex_unlock(&(plugin_data->lock));
		//g_free(line);
	}

	return TRUE;
}

/*static void launch_cmd(const char *cmd)
{
	GAppInfo *appinfo = NULL;
	gboolean ret = FALSE;

	appinfo = g_app_info_create_from_commandline(cmd, NULL,
				G_APP_INFO_CREATE_NONE, NULL);

	if (appinfo == NULL) {
		perror("Command lanuch failed.");
		return;
	}

	ret = g_app_info_launch(appinfo, NULL, NULL, NULL);
	if (!ret)
		perror("Command lanuch failed.");
}*/

static gboolean show_menu(GtkWidget *widget, GdkEventButton *event,
		      kano_notifications_t *plugin)
{
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *header_item, *check_item,
		  *no_updates_item;

	if (event->button != 1)
		return FALSE;

	/* Create the menu items */
	header_item = gtk_menu_item_new_with_label("Kano Updater");
	gtk_widget_set_sensitive(header_item, FALSE);
	gtk_menu_append(GTK_MENU(menu), header_item);
	gtk_widget_show(header_item);

	no_updates_item = gtk_menu_item_new_with_label("No updates found");
	gtk_widget_set_sensitive(no_updates_item, FALSE);
	gtk_menu_append(GTK_MENU(menu), no_updates_item);
	gtk_widget_show(no_updates_item);

	check_item = gtk_menu_item_new_with_label("Check again");
	/*g_signal_connect(check_item, "activate",
			 G_CALLBACK(check_for_update_clicked),
			 plugin);*/
	gtk_menu_append(GTK_MENU(menu), check_item);
	gtk_widget_show(check_item);

	g_signal_connect(menu, "selection-done",
			 G_CALLBACK(selection_done), NULL);

	/* Show the menu. */
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
		       (GtkMenuPositionFunc) popup_set_position, widget,
		       event->button, event->time);


		GtkWidget *dialog = gtk_message_dialog_new(NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			"weeee");

		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

	return TRUE;
}

static void selection_done(GtkWidget *menu)
{
    gtk_widget_destroy(menu);
}

/* Helper for position-calculation callback for popup menus. */
void lxpanel_plugin_popup_set_position_helper(Panel * p, GtkWidget * near,
	GtkWidget * popup, GtkRequisition * popup_req, gint * px, gint * py)
{
	/* Get the origin of the requested-near widget in
	   screen coordinates. */
	gint x, y;
	gdk_window_get_origin(GDK_WINDOW(near->window), &x, &y);

	/* Doesn't seem to be working according to spec; the allocation.x
	   sometimes has the window origin in it */
	if (x != near->allocation.x) x += near->allocation.x;
	if (y != near->allocation.y) y += near->allocation.y;

	/* Dispatch on edge to lay out the popup menu with respect to
	   the button. Also set "push-in" to avoid any case where it
	   might flow off screen. */
	switch (p->edge)
	{
		case EDGE_TOP:    y += near->allocation.height; break;
		case EDGE_BOTTOM: y -= popup_req->height;       break;
		case EDGE_LEFT:   x += near->allocation.width;  break;
		case EDGE_RIGHT:  x -= popup_req->width;        break;
	}
	*px = x;
	*py = y;
}

/* Position-calculation callback for popup menu. */
static void popup_set_position(GtkWidget *menu, gint *px, gint *py,
				gboolean *push_in, GtkWidget *p)
{
    /* Get the allocation of the popup menu. */
    GtkRequisition popup_req;
    gtk_widget_size_request(menu, &popup_req);

    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper(panel, p, menu, &popup_req, px, py);
    *push_in = TRUE;
}

static void plugin_configure(Plugin *p, GtkWindow *parent)
{
  // doing nothing here, so make sure neither of the parameters
  // emits a warning at compilation
  (void)p;
  (void)parent;
}

static void plugin_save_configuration(Plugin *p, FILE *fp)
{
  // doing nothing here, so make sure neither of the parameters
  // emits a warning at compilation
  (void)p;
  (void)fp;
}

/* Plugin descriptor. */
PluginClass kano_notifications_plugin_class = {
	// this is a #define taking care of the size/version variables
	PLUGINCLASS_VERSIONING,

	// type of this plugin
	type : "kano_notifications",
	name : N_("Kano Notifications"),
	version: "1.0",
	description : N_("Displays various types of alerts and notifications."),

	// we can have many running at the same time
	one_per_system : TRUE,

	// can't expand this plugin
	expand_available : FALSE,

	// assigning our functions to provided pointers.
	constructor : plugin_constructor,
	destructor  : plugin_destructor,
	config : plugin_configure,
	save : plugin_save_configuration
};
