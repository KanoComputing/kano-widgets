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
	gchar *title; /* mandatory field */
	gchar *byline; /* mandatory field */

	gchar *type; /* types: normal, small. If omitted normal is assumed. */

	/* The following fields are optional. Some are only supported by
	   certain types of notifications. */
	gchar *image_path; /* an absolute path to an image (png,jpg,gif,bmp) */
	gchar *command; /* a command to be executed in a shell */
	gchar *sound; /* an absolute path to a wav file */

	/* Optional button config for notification type=small */
	gchar *button1_label;
	gchar *button1_command;
	gchar *button1_colour;
	gchar *button1_hover;

	gchar *button2_label;
	gchar *button2_command;
	gchar *button2_colour;
	gchar *button2_hover;
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

	g_free(data->button1_label);
	g_free(data->button1_command);
	g_free(data->button1_colour);
	g_free(data->button1_hover);

	g_free(data->button2_label);
	g_free(data->button2_command);
	g_free(data->button2_colour);
	g_free(data->button2_hover);

	g_free(data);
}

notification_info_t *get_json_notification(gchar *json_data);

#endif
