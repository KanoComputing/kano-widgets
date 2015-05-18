/*
 * notificatoins.h
 *
 * Copyright (C) 2015 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
 *
 * The common types, definitions and helper functions.
 *
 */


#include <gtk/gtk.h>

#ifndef notif_notifications_h
#define notif_notifications_h


#define IS_TYPE(notification, notif_type) \
	(notification->type && g_strcmp0(notification->type, notif_type) == 0)

/*
 * The structure used by the load_conf() and save_conf() functions to
 * hold the configuration of the widget.
 */
struct notification_conf {
	gboolean enabled;
	gboolean allow_world_notifications;
};

/*
 * The main data structure of the plugin. Kept as plugin_data in
 * the lxpanel's Plugin object.
 */
typedef struct {
	int fifo_fd;
	GIOChannel *fifo_channel;
	guint watch_id;

	gboolean paused;

	GtkWidget *icon;

	GMutex lock;
	GList *queue;
	gboolean queue_has_reminders;

	GtkWidget *window;
	guint window_timeout;

	int panel_height;

	struct notification_conf conf;
} kano_notifications_t;


/*
 * Represents a single notification to be displayed.
 */
typedef struct {
	gchar *image_path;
	gchar *title;
	gchar *byline;
	gchar *command;
	gchar *sound;
	gchar *type;
} notification_info_t;

/*
 * A shortcut to freeing the whole notification_info_t function.
 */
static inline void free_notification(notification_info_t *data)
{
	g_free(data->image_path);
	g_free(data->title);
	g_free(data->byline);
	g_free(data->command);
	g_free(data->sound);
	g_free(data->type);
	g_free(data);
}

notification_info_t *get_json_notification(gchar *json_data);

#endif
