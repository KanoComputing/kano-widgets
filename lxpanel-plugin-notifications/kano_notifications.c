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
#include <glib/gprintf.h>
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
#include <pwd.h>

#include "parson.h"

#define FIFO_FILENAME ".kano-notifications.fifo"
#define ON_ICON_FILE "/usr/share/kano-widgets/icons/notifications-on.png"
#define OFF_ICON_FILE "/usr/share/kano-widgets/icons/notifications-off.png"

#define NOTIFICATION_IMAGE_WIDTH 280 // TODO fix image sizes
#define NOTIFICATION_IMAGE_HEIGHT 170

#define WINDOW_WIDTH NOTIFICATION_IMAGE_WIDTH
#define WINDOW_HEIGHT (NOTIFICATION_IMAGE_HEIGHT + 90)
#define WINDOW_MARGIN_RIGHT 20
#define WINDOW_MARGIN_BOTTOM 20

#define __STR_HELPER(x) #x
#define STR(x) __STR_HELPER(x)

#define LEVEL_IMG_BASE_PATH ("/usr/share/kano-profile/media/images/%s/" \
	STR(NOTIFICATION_IMAGE_WIDTH)  "x" STR(NOTIFICATION_IMAGE_HEIGHT) \
		"/Level-%s.png")
#define AWARD_IMG_BASE_PATH ("/usr/share/kano-profile/media/images/%s/" \
	STR(NOTIFICATION_IMAGE_WIDTH) "x" STR(NOTIFICATION_IMAGE_HEIGHT) \
	"/%s/%s_levelup.png")
#define WORLD_IMG_BASE_PATH ("/usr/share/kano-profile/media/images/notification/" \
    STR(NOTIFICATION_IMAGE_WIDTH)  "x" STR(NOTIFICATION_IMAGE_HEIGHT) \
        "/notification.png")

#define LEVEL_TITLE "New level!"
#define LEVEL_BYLINE "You're now Level %s"

#define BADGE_TITLE "New badge!"
#define ENV_TITLE "New environment!"
#define AVATAR_TITLE "New avatar!"

#define WORLD_NOTIFICATION_TITLE "Kano World"
#define WORLD_NOTIFICATION_BYLINE "%s unread notifications"

#define RULES_BASE_PATH "/usr/share/kano-profile/rules/%s/%s.json"


Panel *panel;

typedef struct {
	gboolean enabled;

        // paused means notifications are still accepted and queued
        // but they are not displayed to the end user until it is resumed.
        // see pipe verbs "pause" and "resume".
	gboolean paused;

        // keeps the time the last alert was displayed to the user
        time_t time_last_alert;

	int fifo_fd;
	GIOChannel *fifo_channel;
	guint watch_id;

	GtkWidget *icon;

	GMutex lock;
	GList *queue;

	GtkWidget *window;
	guint window_timeout;
} kano_notifications_t;

typedef struct {
	gchar *image_path;
	gchar *title;
	gchar *byline;
        gboolean displayed;
        gboolean play_sound;
} notification_info_t;

static gboolean plugin_clicked(GtkWidget *, GdkEventButton *,
			       kano_notifications_t *);
static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data);
static gboolean close_notification(kano_notifications_t *plugin_data);

static int plugin_constructor(Plugin *plugin, char **fp);
static void plugin_destructor(Plugin *p);

gchar *get_fifo_filename(void);

static void launch_cmd(const char *cmd);


gchar *get_fifo_filename(void)
{
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;

    // You are responsible for freeing the returned char buffer
    int buff_len=strlen(homedir) + strlen(FIFO_FILENAME) + sizeof(char) * 2;
    gchar *fifo_filename = g_new0(gchar, buff_len);
    if (!fifo_filename) {
        return NULL;
    }
    else {
        g_strlcpy(fifo_filename, homedir, buff_len);
        g_strlcat(fifo_filename, "/", buff_len);
        g_strlcat(fifo_filename, FIFO_FILENAME, buff_len);
        return (fifo_filename);
    }
}

static int plugin_constructor(Plugin *plugin, char **fp)
{
	(void)fp;

	panel = plugin->panel;

	/* allocate our private structure instance */
	kano_notifications_t *plugin_data = g_new0(kano_notifications_t, 1);

	plugin_data->window = NULL;
	plugin_data->queue = NULL;

	plugin_data->enabled = TRUE; // TODO load from the configuration
	plugin_data->paused = FALSE; // TODO load from the configuration
        plugin_data->time_last_alert = 0L;

	g_mutex_init(&(plugin_data->lock));

        // Create the pipe file
        gchar *pipe_filename=get_fifo_filename();
        if (pipe_filename) {

            // remove previous instance of the pipe
            unlink(pipe_filename);

            // Set access mode as wide as possible (this depends on current umask)
            if (mkfifo(pipe_filename, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP | S_IROTH) < 0) {
		perror("mkfifo");
		return 0;
            }
            else {
                // Enforce write mode to group and others
                chmod (pipe_filename, 0666);
            }

            plugin_data->fifo_fd = open(pipe_filename, O_RDWR | O_NONBLOCK, 0);
            if (plugin_data->fifo_fd < 0) {
		perror("open");
		return 0;
            }

            plugin_data->fifo_channel = g_io_channel_unix_new(plugin_data->fifo_fd);
            plugin_data->watch_id = g_io_add_watch(plugin_data->fifo_channel,
                                                   G_IO_IN, (GIOFunc)io_watch_cb, (gpointer)plugin_data);
            g_free(pipe_filename);
        }

	/* put it where it belongs */
	plugin->priv = plugin_data;

	GtkWidget *icon = gtk_image_new_from_file(plugin_data->enabled ?
					ON_ICON_FILE : OFF_ICON_FILE);
	plugin_data->icon = icon;

	gtk_widget_set_sensitive(icon, TRUE);

	/* Set a tooltip to the icon to show when the mouse sits over it */
	GtkTooltips *tooltips;
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, GTK_WIDGET(icon),
			     "Notification Centre", NULL);

	plugin->pwid = gtk_event_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(plugin->pwid), 0);
	gtk_container_add(GTK_CONTAINER(plugin->pwid), GTK_WIDGET(icon));

	gtk_signal_connect(GTK_OBJECT(plugin->pwid), "button-press-event",
			   GTK_SIGNAL_FUNC(plugin_clicked), plugin_data);

	/* our widget doesn't have a window... */
	gtk_widget_set_has_window(plugin->pwid, FALSE);

	/* show our widget */
	gtk_widget_show_all(plugin->pwid);

	return 1;
}

static void plugin_destructor(Plugin *p)
{
        // FIXME: We are not being called during destructor. lxpanel is agressively killed?

	kano_notifications_t *plugin_data = (kano_notifications_t *)p->priv;

	g_source_remove(plugin_data->watch_id);
	g_io_channel_shutdown(plugin_data->fifo_channel, FALSE, NULL);
	g_io_channel_unref(plugin_data->fifo_channel);
	close(plugin_data->fifo_fd);

        gchar *pipe_filename=get_fifo_filename();
        if (pipe_filename) {
            unlink(pipe_filename);
            g_free(pipe_filename);
        }

	close_notification(plugin_data);

	g_mutex_clear(&(plugin_data->lock));

	g_free(plugin_data);
}

static void free_notification(notification_info_t *data)
{
	g_free(data->image_path);
	g_free(data->title);
	g_free(data->byline);
	g_free(data);
}

static void get_award_byline(gchar *json_file, gchar *key,
			     notification_info_t *notification)
{
	JSON_Value *root_value = NULL;
	JSON_Object *root = NULL;
	JSON_Object *award = NULL;
	const char *byline = NULL;

	notification->byline = NULL;

	root_value = json_parse_file(json_file);
	if (json_value_get_type(root_value) != JSONObject) {
		json_value_free(root_value);
		return;
	}

	root = json_value_get_object(root_value);
	award = json_object_get_object(root, key);
	if (!award) {
		json_value_free(root_value);
		return;
	}

	byline = json_object_get_string(award, "title");
	if (!byline) {
		json_value_free(root_value);
		return;
	}

	notification->byline = g_new0(gchar, strlen(byline) + 1);
	g_strlcpy(notification->byline, byline, strlen(byline) + 1);
	json_value_free(root_value);
}

static notification_info_t *get_notification_by_id(gchar *id, kano_notifications_t *plugin_data, gboolean *b_resumed)
{
	gchar **tokens = g_strsplit(id, ":", 0);
	gchar **iter;
	size_t bufsize = 0;

	size_t length = 0;
	for (iter = tokens; *iter; iter++)
		length++;

	if (length < 1) {
		g_strfreev(tokens);
		return NULL;
	}


	if (g_strcmp0(tokens[0], "pause") == 0) {
            g_mutex_lock(&(plugin_data->lock));
            plugin_data->paused = TRUE;
            g_mutex_unlock(&(plugin_data->lock));
            return NULL;
        }

	if (g_strcmp0(tokens[0], "resume") == 0) {
            g_mutex_lock(&(plugin_data->lock));
            plugin_data->paused = FALSE;
            g_mutex_unlock(&(plugin_data->lock));

            // Return the last notification from the queue, if any,
            // so that notification UI alerts retake their jazz.
            if (b_resumed) {
                *b_resumed = TRUE;
            }
            
            notification_info_t *last_notification = g_list_nth_data(plugin_data->queue, 0);
            return last_notification;
        }


	if (g_strcmp0(tokens[0], "level") == 0) {
		if (length < 2) {
			g_strfreev(tokens);
			return NULL;
		}

		notification_info_t *data = g_new0(notification_info_t, 1);

		/* Allocate and set the title */
		bufsize = strlen(LEVEL_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, LEVEL_TITLE, bufsize+1);

		/* Allocate and set the byline */
		bufsize = strlen(LEVEL_BYLINE) + strlen(tokens[1]);
		data->byline = g_new0(gchar, bufsize+1);
		g_sprintf(data->byline, LEVEL_BYLINE, tokens[1]);

		/* Allocate and set image_path */
		bufsize += strlen(LEVEL_IMG_BASE_PATH);
		bufsize += strlen(tokens[0]) + strlen(tokens[1]);
		data->image_path = g_new0(gchar, bufsize+1);
		g_sprintf(data->image_path, LEVEL_IMG_BASE_PATH, tokens[0], tokens[1]);

                data->displayed=FALSE;
                data->play_sound=FALSE;

		g_strfreev(tokens);
		return data;
	}

    if (g_strcmp0(tokens[0], "world_notification") == 0) {
        if (length < 2) {
            g_strfreev(tokens);
            return NULL;
        }

        notification_info_t *data = g_new0(notification_info_t, 1);

        /* Allocate and set the title */
        bufsize = strlen(WORLD_NOTIFICATION_TITLE);
        data->title = g_new0(gchar, bufsize+1);
        g_strlcpy(data->title, WORLD_NOTIFICATION_TITLE, bufsize+1);

        /* Allocate and set the byline */
        bufsize = strlen(WORLD_NOTIFICATION_BYLINE) + strlen(tokens[1]);
        data->byline = g_new0(gchar, bufsize+1);
        g_sprintf(data->byline, WORLD_NOTIFICATION_BYLINE, tokens[1]);

        /* Allocate and set image_path */
        bufsize += strlen(WORLD_IMG_BASE_PATH);
        data->image_path = g_new0(gchar, bufsize+1);
        g_sprintf(data->image_path, WORLD_IMG_BASE_PATH);

        data->displayed=FALSE;
        data->play_sound=FALSE;

        g_strfreev(tokens);
        return data;
    }

	/* badge, environment, or avatar */
	if (length < 3) {
		g_strfreev(tokens);
		return NULL;
	}

	notification_info_t *data = g_new0(notification_info_t, 1);

	if (g_strcmp0(tokens[0], "badges") == 0) {
		/* Allocate and set the title */
		bufsize = strlen(BADGE_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, BADGE_TITLE, bufsize+1);
                data->play_sound=TRUE;
	} else if (g_strcmp0(tokens[0], "environments") == 0) {
		/* Allocate and set the title */
		bufsize = strlen(ENV_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, ENV_TITLE, bufsize+1);
                data->play_sound=TRUE;
	} else if (g_strcmp0(tokens[0], "avatars") == 0) {
		/* Allocate and set the title */
		bufsize = strlen(AVATAR_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, AVATAR_TITLE, bufsize+1);
                data->play_sound=TRUE;
	} else {
		g_strfreev(tokens);
		free_notification(data);
		return NULL;
	}

	/* Allocate and set json rules path */
	bufsize = strlen(RULES_BASE_PATH);
	bufsize += strlen(tokens[0]) + strlen(tokens[1]);
	gchar *json_path = g_new0(gchar, bufsize+1);
	g_sprintf(json_path, RULES_BASE_PATH, tokens[0], tokens[1]);


	/* Load award title */
	get_award_byline(json_path, tokens[2], data);
	g_free(json_path);

	if (!data->byline) {
		free_notification(data);
		g_strfreev(tokens);
		return NULL;
	}

	/* Allocate and set image_path */
	if (g_strcmp0(tokens[0], "avatars") == 0) {
		/* There's a path exception for avatars, handle it here. */
		bufsize = strlen(AWARD_IMG_BASE_PATH);
		bufsize += strlen(tokens[0]) + 2*strlen(tokens[1]);
		data->image_path = g_new0(gchar, bufsize+1);
		g_sprintf(data->image_path, AWARD_IMG_BASE_PATH, tokens[0], tokens[1], tokens[1]);
	} else {
		bufsize = strlen(AWARD_IMG_BASE_PATH);
		bufsize += strlen(tokens[0]) + strlen(tokens[1]) + strlen(tokens[2]);
		data->image_path = g_new0(gchar, bufsize+1);
		g_sprintf(data->image_path, AWARD_IMG_BASE_PATH, tokens[0], tokens[1], tokens[2]);
	}

        data->displayed=FALSE;

	g_strfreev(tokens);
	return data;
}

static gboolean hide_notification_window(GtkWidget *w, GdkEventButton *event,
				  kano_notifications_t *plugin_data)
{
	if (g_mutex_trylock(&(plugin_data->lock)) == TRUE) {
		g_source_remove(plugin_data->window_timeout);
		g_mutex_unlock(&(plugin_data->lock));
		close_notification(plugin_data);
	}

	return TRUE;
}

static void show_notification_window(kano_notifications_t *plugin_data,
			      notification_info_t *notification)
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
	plugin_data->window = win;
	gtk_window_set_default_size(GTK_WINDOW(win), WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

	/* TODO Positioning doesn't take into account the position of the
	   panel itself. */
	gtk_window_set_gravity(GTK_WINDOW(win), GDK_GRAVITY_SOUTH_EAST);
	gtk_window_move(GTK_WINDOW(win),
		gdk_screen_width() - WINDOW_WIDTH - WINDOW_MARGIN_RIGHT,
		gdk_screen_height() - WINDOW_HEIGHT - panel->height
		- WINDOW_MARGIN_BOTTOM);

	GtkStyle *style;
	GdkColor white;
	gdk_color_parse("white", &white);
	gtk_widget_modify_bg(win, GTK_STATE_NORMAL, &white);

	GtkWidget *image = gtk_image_new_from_file(notification->image_path);
	gtk_widget_add_events(image, GDK_BUTTON_RELEASE_MASK);

	GtkWidget *eventbox = gtk_event_box_new();
	gtk_signal_connect(GTK_OBJECT(eventbox), "button-release-event",
                     GTK_SIGNAL_FUNC(hide_notification_window), plugin_data);

	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(eventbox), GTK_WIDGET(box));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(image),
			   FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(eventbox));

	GtkWidget *labels = gtk_vbox_new(FALSE, 0);

	GtkWidget *title = gtk_label_new(notification->title);
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

	GtkWidget *byline = gtk_label_new(notification->byline);
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

	plugin_data->window_timeout = g_timeout_add(6000,
				(GSourceFunc) close_notification,
				(gpointer) plugin_data);
}

static gboolean close_notification(kano_notifications_t *plugin_data)
{
	if (plugin_data->window != NULL) {
		g_mutex_lock(&(plugin_data->lock));

		gtk_widget_destroy(plugin_data->window);
		plugin_data->window = NULL;

		notification_info_t *notification = g_list_nth_data(plugin_data->queue, 0);
		free_notification(notification);
		plugin_data->queue = g_list_remove(plugin_data->queue, notification);


		if (g_list_length(plugin_data->queue) >= 1) {
                        // process the next notification in the queue if there is any
			notification_info_t *notification = g_list_nth_data(plugin_data->queue, 0);
                        show_notification_window(plugin_data, notification);
		}

		g_mutex_unlock(&(plugin_data->lock));
	}

	return FALSE;
}

static void launch_cmd(const char *cmd)
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
}

static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data)
{
        // read from the pipe and process the notification message
	kano_notifications_t *plugin_data = (kano_notifications_t *)data;

	gchar *line = NULL;
	gsize len, tpos;
	GIOStatus status;

	status = g_io_channel_read_line(source, &line, &len, &tpos, NULL);
	if (status == G_IO_STATUS_NORMAL) {
		line[tpos] = '\0';

		if (!plugin_data->enabled) {
			g_free(line);
			return TRUE;
		}

                gboolean b_resumed=FALSE;
		notification_info_t *data = get_notification_by_id(line, plugin_data, &b_resumed);
		if (data) {
			g_mutex_lock(&(plugin_data->lock));

                        // this is a new notification message, add it to the queure
                        // otherwise it is a previously queued notification, this covers those queued via a "pause" verb
                        if (b_resumed==FALSE) {
                            plugin_data->queue = g_list_append(plugin_data->queue, data);
                        }

                        // Display the notification window if necessary
                        // FIXME: Without the below if statement, popups get blocked and never disappear
                        // Are we holding many queues each with 1 notification event data?
                        if (g_list_length(plugin_data->queue) <= 1) {

                            // Play sound if needed by the notification and last time is not too recent
                            // TODO: Extract the 5 seconds into a configuration file
                            time_t time_now;
                            time(&time_now);

                            if (data->play_sound==TRUE &&
                                (!plugin_data->time_last_alert || 
                                 (plugin_data->time_last_alert && ((time_now - plugin_data->time_last_alert) > 5)))) {

                                time(&plugin_data->time_last_alert);
                                launch_cmd("aplay /usr/share/kano-media/sounds/kano_level_up.wav");
                            }

                            show_notification_window(plugin_data, data);
                            data->displayed = TRUE;
                        }

			g_mutex_unlock(&(plugin_data->lock));

		}
		g_free(line);
	}

	return TRUE;
}

static gboolean plugin_clicked(GtkWidget *widget, GdkEventButton *event,
		      kano_notifications_t *plugin_data)
{
	// TODO TOGGLE NOTIFICATIONS!
	if (event->button != 1)
		return FALSE;

	plugin_data->enabled = !(plugin_data->enabled);

	gtk_image_set_from_file(GTK_IMAGE(plugin_data->icon),
		plugin_data->enabled ? ON_ICON_FILE : OFF_ICON_FILE);

	return TRUE;
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
