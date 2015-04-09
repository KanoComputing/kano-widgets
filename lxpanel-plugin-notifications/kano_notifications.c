/*
 * kano_notifications.c
 *
 * Copyright (C) 2014 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
 *
 */

/* TODO && FIXME: The next time we're adding things here it needs to be
 *                split into several modules.
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

#include <kdesk-hourglass.h>

#include "parson/parson.h"

#define UPDATE_STATUS_FILE "/var/cache/kano-updater/status"

#define FIFO_FILENAME ".kano-notifications.fifo"
#define CONF_FILENAME ".kano-notifications.conf"

#define ON_ICON_FILE "/usr/share/kano-widgets/icons/notifications-on.png"
#define OFF_ICON_FILE "/usr/share/kano-widgets/icons/notifications-off.png"
#define RIGHT_ARROW "/usr/share/kano-widgets/icons/arrow-right.png"

#define CHEER_SOUND "/usr/share/kano-media/sounds/kano_level_up.wav"

#define BUTTON_HIGHLIGHTED_COLOUR "#d4d4d4"
#define BUTTON_COLOUR "#e4e4e4"
#define BUTTON_WIDTH 44
#define BUTTON_HEIGHT 90

#define NOTIFICATION_IMAGE_WIDTH 280
#define NOTIFICATION_IMAGE_HEIGHT 170

#define PANEL_WIDTH NOTIFICATION_IMAGE_WIDTH
#define PANEL_HEIGHT 90

#define WINDOW_MARGIN_RIGHT 20
#define WINDOW_MARGIN_BOTTOM 20

#define LABELS_WIDTH (NOTIFICATION_IMAGE_WIDTH - WINDOW_MARGIN_RIGHT*2 - \
		      BUTTON_WIDTH)

#define __STR_HELPER(x) #x
#define STR(x) __STR_HELPER(x)

#define MAX_QUEUE_LEN 50

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

#define TITLE_COLOUR "#323232"
#define BYLINE_COLOUR "#6e6e6e"

#define ON_TIME 6000

#define REGISTER_REMINDER \
	"{" \
		"\"title\": \"Kano World\", " \
		"\"byline\": \"Remember to register\", " \
		"\"image\": \"/usr/share/kano-profile/media/images/notification/280x170/register.png\", " \
		"\"sound\": null, " \
		"\"command\": \"kano-login 3\" " \
	"}"

#define UPDATE_REMINDER \
	"{" \
		"\"title\": \"Updates Available\"," \
		"\"byline\": \"Click here to update your Kano\"," \
		"\"image\": \"/usr/share/kano-profile/media/images/notification/280x170/update.png\"," \
		"\"sound\": null," \
		"\"command\": \"sudo check-for-updates -d\"" \
	"}"

#define KANO_PROFILE_CMD "kano-profile"
#define KANO_LOGIN_CMD "kano-login 3"

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

/* This struct is used exclusively for passing user data to GTK signals. */
typedef struct {
	notification_info_t *notification;
	kano_notifications_t *plugin_data;
} gtk_user_data_t;

static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data);
static gboolean close_notification(kano_notifications_t *plugin_data);

static GtkWidget *plugin_constructor(LXPanel *panel, config_setting_t *settings);
static void plugin_destructor(gpointer data);
static void launch_cmd(const char *cmd, gboolean hourglass);

gchar *get_fifo_filename(void);
gchar *get_conf_filename(void);

/*
 * Resolve the path to the pipe file in the user's $HOME directory.
 *
 * WARNING: You're expected to g_free() the string returned.
 */
gchar *get_fifo_filename(void)
{
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;

	/* You are responsible for freeing the returned char buffer */
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


/*
 * Resolve the path to the config file in the user's $HOME directory.
 *
 * WARNING: You're expected to g_free() the string returned.
 */
gchar *get_conf_filename(void)
{
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;

	/* You are responsible for freeing the returned char buffer */
	int buff_len=strlen(homedir) + strlen(CONF_FILENAME) + sizeof(char) * 2;
	gchar *conf_filename = g_new0(gchar, buff_len);
	if (!conf_filename) {
		return NULL;
	}
	else {
		g_strlcpy(conf_filename, homedir, buff_len);
		g_strlcat(conf_filename, "/", buff_len);
		g_strlcat(conf_filename, CONF_FILENAME, buff_len);
		return (conf_filename);
	}
}


/*
 * Save the configuration into the current user's $HOME.
 */
int save_conf(struct notification_conf *conf)
{
	int status;

	gchar *conf_file = get_conf_filename();
	if (conf_file == NULL)
		return -1;

	JSON_Value *root_value = json_value_init_object();
	JSON_Object *root_object = json_value_get_object(root_value);

	json_object_set_boolean(root_object, "enabled", conf->enabled);
	json_object_set_boolean(root_object, "allow_world_notifications",
				conf->allow_world_notifications);

	status = json_serialize_to_file(root_value, conf_file);

	/* Free the conf file path before we return */
	g_free(conf_file);
	json_value_free(root_value);

	return status;
}


/*
 * Load the configuration from the current user's $HOME.
 */
void load_conf(struct notification_conf *conf)
{
	gchar *conf_file = get_conf_filename();

	if (conf_file != NULL && access(conf_file, F_OK) != -1) {
		JSON_Value *root_value = NULL;
		JSON_Object *root = NULL;

		root_value = json_parse_file(conf_file);

		/* Free the conf file path before we return */
		g_free(conf_file);

		if (json_value_get_type(root_value) == JSONObject) {
			root = json_value_get_object(root_value);

			conf->enabled = json_object_get_boolean(root, "enabled");
			conf->allow_world_notifications = json_object_get_boolean(root,
							"allow_world_notifications");

			json_value_free(root_value);
			return;
		}

		json_value_free(root_value);
	}

	/* There's no or broken configuration, so create a default one. */
	conf->enabled = TRUE;
	conf->allow_world_notifications = TRUE;
	save_conf(conf);

	return;
}


/*
 * This is the entry point of the plugin in LXPanel. It's meant to
 * initialise the plugin_data structure.
 */
static GtkWidget *plugin_constructor(LXPanel *panel, config_setting_t *settings)
{
	/* allocate our private structure instance */
	kano_notifications_t *plugin_data = g_new0(kano_notifications_t, 1);

	GtkAllocation allocation;
	gtk_widget_get_allocation(GTK_WIDGET(&(panel->window)), &allocation);
	plugin_data->panel_height = allocation.height;


	plugin_data->window = NULL;
	plugin_data->queue = NULL;
	plugin_data->queue_has_reminders = FALSE;

	plugin_data->paused = FALSE; /* TODO load from the configuration */

	g_mutex_init(&(plugin_data->lock));

	/* Create the pipe file */
	gchar *pipe_filename=get_fifo_filename();
	if (pipe_filename) {
		/* remove previous instance of the pipe */
		unlink(pipe_filename);

		/* Set access mode as wide as possible
		   (this depends on current umask) */
		if (mkfifo(pipe_filename, S_IWUSR | S_IRUSR | S_IRGRP |
					  S_IWGRP | S_IROTH) < 0) {
			perror("mkfifo");
			return 0;
		} else {
			/* Enforce write mode to group and others */
			chmod (pipe_filename, 0666);
		}

		plugin_data->fifo_fd = open(pipe_filename, O_RDWR | O_NONBLOCK,
					    0);
		if (plugin_data->fifo_fd < 0) {
			perror("open");
			return 0;
		}

		/* Start watching the pipe for input. */
		plugin_data->fifo_channel = g_io_channel_unix_new(plugin_data->fifo_fd);
		plugin_data->watch_id = g_io_add_watch(plugin_data->fifo_channel,
						       G_IO_IN, (GIOFunc)io_watch_cb,
						       (gpointer)plugin_data);
		g_free(pipe_filename);
	}

	load_conf(&(plugin_data->conf));

	GtkWidget *pwid = gtk_event_box_new();

	/* put it where it belongs */
	lxpanel_plugin_set_data(pwid, plugin_data, plugin_destructor);

	return pwid;
}

/*
 * The oposite of plugin_constructor, to free up resources.
 */
static void plugin_destructor(gpointer data)
{
	/* FIXME: We are not being called during destructor.
	   lxpanel is agressively killed? */

	kano_notifications_t *plugin_data = (kano_notifications_t *)data;

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

/*
 * A shortcut to freeing the whole notification_info_t function.
 */
static void free_notification(notification_info_t *data)
{
	g_free(data->image_path);
	g_free(data->title);
	g_free(data->byline);
	g_free(data->command);
	g_free(data->sound);
	g_free(data->type);
	g_free(data);
}


/*
 * Check whether the user is registered in kano world
 *
 * This will open the profile json file and see whether it has the
 * 'kanoworld_id' key.
 */
static gboolean is_user_registered()
{
	const gchar *pf_tmp = "/home/%s/.kanoprofile/profile/profile.json";
	size_t bufsize = strlen(pf_tmp);

	/* Get username for the homedir path */
	uid_t uid = geteuid();
	struct passwd *pw = getpwuid(uid);
	if (!pw)
		/* We assume the user is not logged in when we cannot
		   even get the username. */
		return FALSE;

	/* Put the path together */
	bufsize = strlen(pf_tmp) + strlen(pw->pw_name) + 2;
	gchar *profile = g_new0(gchar, bufsize);
	g_sprintf(profile, pf_tmp, pw->pw_name);

	/* Open the JSON */
	JSON_Value *root_value = NULL;
	JSON_Object *root = NULL;
	const gchar *id = NULL;

	root_value = json_parse_file(profile);
	g_free(profile);

	/* We expect dict as the root value of the JSON.
	   The assumption is that the user is not logged in if this
	   fails. */
	if (json_value_get_type(root_value) != JSONObject) {
		json_value_free(root_value);
		return FALSE;
	}

	root = json_value_get_object(root_value);
	id = json_object_get_string(root, "kanoworld_id");
	json_value_free(root_value);
	return id != NULL;
}

/*
 * Check whether there's internet available.
 */
static gboolean is_internet()
{
	return system("is_internet") == 0;
}

/*
 * Is update available?
 */
static gboolean is_update_available()
{
	FILE *fp;
	gchar *line = NULL;
	size_t len = 0;
	ssize_t read;
	int value = 0;

	fp = fopen(UPDATE_STATUS_FILE, "r");
	if (fp == NULL)
		/* In case the status file isn't there, we say there
		   are no updates available. */
		return FALSE;

	gchar *key = "update_available=";
	while ((read = getline(&line, &len, fp)) != -1) {
		if (strncmp(line, key, strlen(key)) == 0) {
			value = atoi(line + strlen(key));
			break;
		}
	}

	return value == 1;
}


/*
 * Parse the byline from the JSON file that represents the award
 * (badges, environments, avatars)
 */
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


/*
 * Determine the appropriate command for an award notification based
 * on whether the user is logged in to kano world or not.
 */
static void set_award_command(notification_info_t *notification)
{
	ssize_t len;
	if (is_user_registered()) {
		len = strlen(KANO_PROFILE_CMD);
		notification->command = g_new0(gchar, len + 2);
		g_strlcpy(notification->command, KANO_PROFILE_CMD, len);
	} else {
		len = strlen(KANO_LOGIN_CMD);
		notification->command = g_new0(gchar, len + 2);
		g_strlcpy(notification->command, KANO_LOGIN_CMD, len + 2);
	}
}


/*
 * Prepare a notification_t instance to be displayed based on an id
 * for it. The format of the id is the following:
 *
 *  - badges:application:feedbacker
 *  - avatars:conductor:conductor_1
 *
 * TODO: Now that the widget supports JSON notifications, this logic
 *       could be moved outside of the widget itself.
 */
static notification_info_t *get_notification_by_id(gchar *id)
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
		g_sprintf(data->image_path, LEVEL_IMG_BASE_PATH,
			  tokens[0], tokens[1]);

		/* Allocate and set the sound */
		bufsize = strlen(CHEER_SOUND);
		data->sound = g_new0(gchar, bufsize+1);
		g_strlcpy(data->sound, CHEER_SOUND, bufsize+1);

		g_strfreev(tokens);
		return data;
	}

	/* TODO This needs to be removed at some point */
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
	} else if (g_strcmp0(tokens[0], "environments") == 0) {
		/* Allocate and set the title */
		bufsize = strlen(ENV_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, ENV_TITLE, bufsize+1);
	} else if (g_strcmp0(tokens[0], "avatars") == 0) {
		/* Allocate and set the title */
		bufsize = strlen(AVATAR_TITLE);
		data->title = g_new0(gchar, bufsize+1);
		g_strlcpy(data->title, AVATAR_TITLE, bufsize+1);
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

	set_award_command(data);

	/* Allocate and set image_path */
	if (g_strcmp0(tokens[0], "avatars") == 0) {
		/* There's a path exception for avatars, handle it here. */
		bufsize = strlen(AWARD_IMG_BASE_PATH);
		bufsize += strlen(tokens[0]) + 2*strlen(tokens[1]);
		data->image_path = g_new0(gchar, bufsize+1);
		g_sprintf(data->image_path, AWARD_IMG_BASE_PATH,
			  tokens[0], tokens[1], tokens[1]);
	} else {
		bufsize = strlen(AWARD_IMG_BASE_PATH);
		bufsize += strlen(tokens[0]) + strlen(tokens[1]) +
				  strlen(tokens[2]);
		data->image_path = g_new0(gchar, bufsize+1);
		g_sprintf(data->image_path, AWARD_IMG_BASE_PATH,
			  tokens[0], tokens[1], tokens[2]);
	}

	/* Allocate and set the sound */
	bufsize = strlen(CHEER_SOUND);
	data->sound = g_new0(gchar, bufsize+1);
	g_strlcpy(data->sound, CHEER_SOUND, bufsize+1);

	g_strfreev(tokens);
	return data;
}

/*
 * Construct a notification from a JSON string.
 *
 * Example JSON data:
 * {
 *     "title": "Hello",
 *     "byline": "How are you today?",
 *     "imgae": "/path/to/a/picture.png",
 *     "sound": "/path/to/a/wav-file.wav",
 *     "command": "lxterminal",
 *     "type": "normal",
 * }
 *
 * All keys except the title and byline are optional.
 */
static notification_info_t *get_json_notification(gchar *json_data)
{
	JSON_Value *root_value = NULL;
	JSON_Object *root = NULL;
	const char *title = NULL;
	const char *byline = NULL;
	const char *image_path = NULL;
	const char *command = NULL;
	const char *sound = NULL;
	const char *type = NULL;

	root_value = json_parse_string(json_data);
	if (json_value_get_type(root_value) != JSONObject) {
		json_value_free(root_value);
		return NULL;
	}

	root = json_value_get_object(root_value);

	title = json_object_get_string(root, "title");
	if (!title) {
		json_value_free(root_value);
		return NULL;
	}

	byline = json_object_get_string(root, "byline");
	if (!byline) {
		json_value_free(root_value);
		return NULL;
	}

	image_path = json_object_get_string(root, "image");
	command = json_object_get_string(root, "command");
	sound = json_object_get_string(root, "sound");
	type = json_object_get_string(root, "type");

	notification_info_t *data = g_new0(notification_info_t, 1);

	data->title = g_new0(gchar, strlen(title) + 1);
	g_strlcpy(data->title, title, strlen(title) + 1);

	data->byline = g_new0(gchar, strlen(byline) + 1);
	g_strlcpy(data->byline, byline, strlen(byline) + 1);

	if (image_path) {
		data->image_path = g_new0(gchar, strlen(image_path) + 1);
		g_strlcpy(data->image_path, image_path, strlen(image_path) + 1);
	}

	if (command) {
		data->command = g_new0(gchar, strlen(command) + 1);
		g_strlcpy(data->command, command, strlen(command) + 1);
	}

	if (sound) {
		data->sound = g_new0(gchar, strlen(sound) + 1);
		g_strlcpy(data->sound, sound, strlen(sound) + 1);
	}

	if (type) {
		data->type = g_new0(gchar, strlen(type) + 1);
		g_strlcpy(data->type, type, strlen(type) + 1);
	}

	json_value_free(root_value);

	return data;
}


/*
 * Destroy the notification and show the next one in the queue.
 */
static void hide_notification_window(kano_notifications_t *plugin_data)
{
	if (g_mutex_trylock(&(plugin_data->lock)) == TRUE) {
		g_source_remove(plugin_data->window_timeout);
		g_mutex_unlock(&(plugin_data->lock));
		close_notification(plugin_data);
	}
}

/*
 * A callback for when the user clicks on the image.
 */
static gboolean eventbox_click_cb(GtkWidget *w, GdkEventButton *event,
				  kano_notifications_t *plugin_data)
{
	hide_notification_window(plugin_data);
	return TRUE;
}

/*
 * Set the hover cursor of the launch button to HAND1
 */
static gboolean button_realize_cb(GtkWidget *widget, void *data)
{
	GdkCursor *cursor;
	cursor = gdk_cursor_new(GDK_HAND1);
	gdk_window_set_cursor(widget->window, cursor);
	gdk_flush();
	gdk_cursor_destroy(cursor);

	return TRUE;
}


/*
 * The command button's hover in callback. Changes the colour of it.
 */
static gboolean button_enter_cb(GtkWidget *widget, GdkEvent *event, void *data)
{
	GdkColor button_bg;
	gdk_color_parse(BUTTON_HIGHLIGHTED_COLOUR, &button_bg);
	gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &button_bg);
	return TRUE;
}


/*
 * The command button's hover out callback. Changes the colour of it.
 */
static gboolean button_leave_cb(GtkWidget *widget, GdkEvent *event, void *data)
{
	GdkColor button_bg;
	gdk_color_parse(BUTTON_COLOUR, &button_bg);
	gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &button_bg);
	return TRUE;
}


/*
 * Launch the command that is associated with the notification.
 */
static gboolean button_click_cb(GtkWidget *w, GdkEventButton *event,
				gtk_user_data_t *user_data)
{
	/* User clicked on this world notification command.
	   Save this event in Kano Tracker. It will be audited
	   in the form: "world-notification <byline>" to keep a counter. */
	char *tracker_cmd_prolog = "kano-profile-cli increment_app_runtime "
				   "'world-notification ";
	int tracker_cmd_len = strlen(tracker_cmd_prolog) +
				     strlen(user_data->notification->byline) +
				     (sizeof(gchar) * 4);
	gchar *tracker_cmd=g_new0(gchar, tracker_cmd_len);

	if (tracker_cmd) {
		g_strlcpy(tracker_cmd, tracker_cmd_prolog, tracker_cmd_len);
		g_strlcat(tracker_cmd, user_data->notification->byline,
			  tracker_cmd_len);
		g_strlcat(tracker_cmd, "' 0", tracker_cmd_len);
	}

	/* Launch the application pointed to by the "command"
	   notification field */
	launch_cmd(user_data->notification->command, TRUE);
	hide_notification_window(user_data->plugin_data);
	g_free(user_data);

	/* Notification tracking is done after processing the visual work,
	   to avoid UIX delays */
	if (tracker_cmd) {
		launch_cmd(tracker_cmd, FALSE);
		g_free(tracker_cmd);
	}

	return TRUE;
}


/*
 * Constructs the notification window and display's it.
 *
 * This function also sets up a timer that will destroy the window after
 * a set period of time. It's expected that no notification is being
 * shown at the time of this function call.
 */
static void show_notification_window(kano_notifications_t *plugin_data,
				     notification_info_t *notification)
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
	plugin_data->window = win;

	GtkStyle *style;
	GtkWidget *eventbox = gtk_event_box_new();
	gtk_signal_connect(GTK_OBJECT(eventbox), "button-release-event",
			   GTK_SIGNAL_FUNC(eventbox_click_cb), plugin_data);

	GdkColor white;
	gdk_color_parse("white", &white);
	gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(eventbox), GTK_WIDGET(box));

	if (!IS_TYPE(notification, "small") && notification->image_path) {
		GtkWidget *image = gtk_image_new_from_file(notification->image_path);
		gtk_widget_add_events(image, GDK_BUTTON_RELEASE_MASK);
		gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(image),
				   FALSE, FALSE, 0);
	}

	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(eventbox));

	GtkWidget *labels = gtk_vbox_new(FALSE, 0);

	GtkWidget *title = gtk_label_new(NULL);

	/* Don't limit the size of the label in case it's the small one
	   or it doesn't have an image. */
	if (!IS_TYPE(notification, "small") && notification->image_path)
		gtk_widget_set_size_request(title, LABELS_WIDTH, -1);

	gtk_label_set_text(GTK_LABEL(title), notification->title);
	gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD);

	style = gtk_widget_get_style(title);
	pango_font_description_set_size(style->font_desc, 15*PANGO_SCALE);
	pango_font_description_set_weight(style->font_desc, PANGO_WEIGHT_BOLD);
	gtk_widget_modify_font(title, style->font_desc);

	GdkColor title_colour;
	gdk_color_parse(TITLE_COLOUR, &title_colour);
	gtk_widget_modify_fg(title, GTK_STATE_NORMAL, &title_colour);

	GtkWidget *title_align = gtk_alignment_new(0,0,0,0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(title_align), 20, 0, 20, 20);
	gtk_container_add(GTK_CONTAINER(title_align), title);
	gtk_box_pack_start(GTK_BOX(labels), GTK_WIDGET(title_align),
			   FALSE, FALSE, 0);

	GtkWidget *byline = gtk_label_new(NULL);
	gtk_label_set_text(GTK_LABEL(byline), notification->byline);

	/* Don't limit the size of the label in case it's the small one
	   or it doesn't have an image. */
	if (!IS_TYPE(notification, "small") && notification->image_path)
		gtk_widget_set_size_request(byline, LABELS_WIDTH, -1);

	gtk_label_set_justify(GTK_LABEL(byline), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(byline), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(byline), PANGO_WRAP_WORD);

	style = gtk_widget_get_style(byline);
	pango_font_description_set_size(style->font_desc, 12*PANGO_SCALE);
	pango_font_description_set_weight(style->font_desc, PANGO_WEIGHT_NORMAL);
	gtk_widget_modify_font(byline, style->font_desc);

	GdkColor byline_colour;
	gdk_color_parse(BYLINE_COLOUR, &byline_colour);
	gtk_widget_modify_fg(byline, GTK_STATE_NORMAL, &byline_colour);

	GtkWidget *byline_align = gtk_alignment_new(0,0,0,0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(byline_align), 5, 20, 20, 20);
	gtk_container_add(GTK_CONTAINER(byline_align), byline);
	gtk_box_pack_start(GTK_BOX(labels), GTK_WIDGET(byline_align),
			   FALSE, FALSE, 0);

	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);

	if (IS_TYPE(notification, "small") && notification->image_path) {
		GtkWidget *small_image = gtk_image_new_from_file(notification->image_path);
		gtk_widget_add_events(small_image, GDK_BUTTON_RELEASE_MASK);

		GtkWidget *small_image_align = gtk_alignment_new(0,0,0,0);
		gtk_alignment_set_padding(GTK_ALIGNMENT(small_image_align), 15, 0, 10, 0);
		gtk_container_add(GTK_CONTAINER(small_image_align), small_image);

		gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(small_image_align),
				   FALSE, FALSE, 0);
	}

	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(labels),
			   TRUE, TRUE, 0);

	/* Add the command button if there is a command */
	if (notification->command && strlen(notification->command) > 0) {
		GdkColor button_bg;
		gdk_color_parse(BUTTON_COLOUR, &button_bg);
		GtkWidget *arrow = gtk_image_new_from_file(RIGHT_ARROW);
		GtkWidget *button = gtk_event_box_new();
		gtk_widget_modify_bg(button, GTK_STATE_NORMAL, &button_bg);
		gtk_container_add(GTK_CONTAINER(button), arrow);
		gtk_widget_set_size_request(button, BUTTON_WIDTH, BUTTON_HEIGHT);
		gtk_signal_connect(GTK_OBJECT(button), "realize",
			     GTK_SIGNAL_FUNC(button_realize_cb), NULL);
		gtk_signal_connect(GTK_OBJECT(button), "enter-notify-event",
			     GTK_SIGNAL_FUNC(button_enter_cb), NULL);
		gtk_signal_connect(GTK_OBJECT(button), "leave-notify-event",
			     GTK_SIGNAL_FUNC(button_leave_cb), NULL);

		gtk_user_data_t *user_data = g_new0(gtk_user_data_t, 1);
		user_data->notification = notification;
		user_data->plugin_data = plugin_data;
		gtk_signal_connect(GTK_OBJECT(button), "button-release-event",
			     GTK_SIGNAL_FUNC(button_click_cb), user_data);

		gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(button),
				   FALSE, FALSE, 0);
	}

	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
			   TRUE, TRUE, 0);

	gtk_widget_show_all(win);

	/* TODO Positioning doesn't take into account the position of the
	   panel itself. */
	GdkWindow *gdk_win = gtk_widget_get_window(GTK_WIDGET(win));
	int win_pos_x = gdk_screen_width() - gdk_window_get_width(gdk_win) -
			WINDOW_MARGIN_RIGHT,
	    win_pos_y = gdk_screen_height() - gdk_window_get_height(gdk_win) -
			plugin_data->panel_height - WINDOW_MARGIN_BOTTOM;
	gtk_window_set_gravity(GTK_WINDOW(win), GDK_GRAVITY_SOUTH_EAST);
	gtk_window_move(GTK_WINDOW(win), win_pos_x, win_pos_y);

	/* Play the sound */
	if (notification->sound) {
		int bufsize = strlen("aplay") + strlen(notification->sound) + 2;
		gchar *aplay_cmd = g_new0(gchar, bufsize);
		g_sprintf(aplay_cmd, "aplay %s", notification->sound);
		launch_cmd(aplay_cmd, FALSE);
		g_free(aplay_cmd);
	}

	/*plugin_data->window_timeout = g_timeout_add(ON_TIME,
				(GSourceFunc) close_notification,
				(gpointer) plugin_data);*/
}


/* This will queue and show the kano-world registration and updater reminders
 * if needed.
 *
 * Expects plugin_data to be locked.
 */
static void show_reminders(kano_notifications_t *plugin_data)
{
	/* Both reminders only make sense when the user is online */
	if (!is_internet())
		return;

	notification_info_t *notif = NULL;
	if (!is_user_registered()) {
		notif = get_json_notification(REGISTER_REMINDER);
		plugin_data->queue = g_list_append(plugin_data->queue, notif);

	}

	/*if (is_update_available()) {
		notif = get_json_notification(UPDATE_REMINDER);
		plugin_data->queue = g_list_append(plugin_data->queue, notif);
	}*/


	if (g_list_length(plugin_data->queue) > 0) {
		notif = g_list_nth_data(plugin_data->queue, 0);
		show_notification_window(plugin_data, notif);
	}
}


/*
 * Destroy the notification window and free up the resources.
 *
 * If there was another notification queued up after this one, it will
 * show it.
 */
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
			/* Show the next one in the queue */
			notification_info_t *notification = g_list_nth_data(plugin_data->queue, 0);
			show_notification_window(plugin_data, notification);
		} else {
			/* If this was a las one in a row, queue additional
			   reminders. */
			if (!plugin_data->queue_has_reminders) {
				plugin_data->queue_has_reminders = TRUE;
				show_reminders(plugin_data);
			} else {
				plugin_data->queue_has_reminders = FALSE;
			}
		}

		g_mutex_unlock(&(plugin_data->lock));
	}

	return FALSE;
}


/*
 * Non-blocking way of launching a command.
 */
static void launch_cmd(const char *cmd, gboolean hourglass)
{
	GAppInfo *appinfo = NULL;
	gboolean ret = FALSE;

	appinfo = g_app_info_create_from_commandline(cmd, NULL,
				G_APP_INFO_CREATE_NONE, NULL);

	if (hourglass)
		kdesk_hourglass_start_appcmd((char *) cmd);

	if (appinfo == NULL) {
		perror("Command lanuch failed.");
		if (hourglass)
			kdesk_hourglass_end();
		return;
	}

	ret = g_app_info_launch(appinfo, NULL, NULL, NULL);
	if (!ret) {
		perror("Command lanuch failed.");
		if (hourglass)
			kdesk_hourglass_end();
	}
}


/*
 * The main loop of this widget. It sets up an IO watch for the pipe and
 * waits for incomming data. It will trigger different actions based on
 * the data received.
 *
 * WARNING: I've seen some deadlocks when doing too much within the
 *          handler itself. If the action takes a long time, it's better
 *          to schedule it for the GTK main loop to execute outside of
 *          this code path.
 */
static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data)
{
	kano_notifications_t *plugin_data = (kano_notifications_t *)data;

	gchar *line = NULL;
	gsize len, tpos;
	GIOStatus status;

	status = g_io_channel_read_line(source, &line, &len, &tpos, NULL);
	if (status == G_IO_STATUS_NORMAL) {
		/* Removes the newline character at the and of the line */
		line[tpos] = '\0';

		/* This has to come before the enabled check. */
		if (g_strcmp0(line, "enable") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->conf.enabled = TRUE;
			g_mutex_unlock(&(plugin_data->lock));
			save_conf(&(plugin_data->conf));
			g_free(line);
			return TRUE;
		}

		if (!plugin_data->conf.enabled) {
			g_free(line);
			return TRUE;
		}

		if (g_strcmp0(line, "disable") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->conf.enabled = FALSE;
			g_mutex_unlock(&(plugin_data->lock));
			save_conf(&(plugin_data->conf));
			g_free(line);
			return TRUE;
		}

		if (g_strcmp0(line, "allow_world_notifications") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->conf.allow_world_notifications = TRUE;
			g_mutex_unlock(&(plugin_data->lock));
			save_conf(&(plugin_data->conf));
			g_free(line);
			return TRUE;
		}

		if (g_strcmp0(line, "disallow_world_notifications") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->conf.allow_world_notifications = FALSE;
			g_mutex_unlock(&(plugin_data->lock));
			save_conf(&(plugin_data->conf));
			g_free(line);
			return TRUE;
		}

		if (g_strcmp0(line, "pause") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->paused = TRUE;
			g_mutex_unlock(&(plugin_data->lock));
			g_free(line);
			return TRUE;
		}

		if (g_strcmp0(line, "resume") == 0) {
			g_mutex_lock(&(plugin_data->lock));
			plugin_data->paused = FALSE;

			if (g_list_length(plugin_data->queue) > 0) {
				notification_info_t *first = g_list_nth_data(plugin_data->queue, 0);
				show_notification_window(plugin_data, first);
			}

			g_mutex_unlock(&(plugin_data->lock));
			g_free(line);
			return TRUE;
		}

		/* See if the notification is a JSON */
		notification_info_t *data = get_json_notification(line);

		if (!data)
			data = get_notification_by_id(line);

		g_free(line);

		if (data) {
			g_mutex_lock(&(plugin_data->lock));

			/* Don't queue world notifications in case they are
			   being filtered. This also ignores any incomming
			   notifications beyond the maximum limit set. */
			if ((IS_TYPE(data, "world") &&
			    !plugin_data->conf.allow_world_notifications) ||
			    g_list_length(plugin_data->queue) >=
			    MAX_QUEUE_LEN) {
				g_mutex_unlock(&(plugin_data->lock));
				return TRUE;
			}

			plugin_data->queue = g_list_append(plugin_data->queue, data);

			if (g_list_length(plugin_data->queue) <= 1 &&
			    !plugin_data->paused)
				show_notification_window(plugin_data, data);

			g_mutex_unlock(&(plugin_data->lock));
		}
	}

	return TRUE;
}

FM_DEFINE_MODULE(lxpanel_gtk, kano_notifications)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Kano Notifications"),
    .description = N_("Displays various types of alerts and notifications."),
    .new_instance = plugin_constructor,
    .one_per_system = TRUE,
    .expand_available = FALSE
};
