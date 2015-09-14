/*
 * kano_notifications.c
 *
 * Copyright (C) 2014, 2015 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
 *
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>

#include "parson/parson.h"
#include "config.h"
#include "notifications.h"
#include "ui.h"


#define CHEER_SOUND "/usr/share/kano-media/sounds/kano_level_up.wav"

#define MAX_QUEUE_LEN 50

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

#define RULES_BASE_PATH "/usr/share/kano-profile/rules/%s/%s.json"

#define KANO_PROFILE_CMD "kano-profile-gui"
#define KANO_LOGIN_CMD "kano-login 3"

static gboolean io_watch_cb(GIOChannel *source, GIOCondition cond, gpointer data);

static void cleanup(gpointer data);

/*
 * Main body - does initialisation, most actual work runs in io_watch_cb.
 * 
 */
int main(int argc, char *argv[])
{
	/* allocate our private structure instance */
	kano_notifications_t *plugin_data = g_new0(kano_notifications_t, 1);

	plugin_data->panel_height = 44; //FIXME - make this configurable
	gtk_init (&argc, &argv);


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


    
	gtk_main ();

	cleanup(plugin_data);

}

/*
 * Free up resources.
 */
static void cleanup(gpointer data)
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
static notification_info_t *get_notification_by_id(gchar *id, gboolean free_unparsed)
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

		/* pass ownership of the input */
		data->free_unparsed = free_unparsed;
		data->unparsed = id;
		
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

	/* badge, environment, or avatar */
	if (length < 3) {
		g_strfreev(tokens);
		return NULL;
	}

	notification_info_t *data = g_new0(notification_info_t, 1);

	/* pass ownership of the input */
	data->free_unparsed = free_unparsed;
	data->unparsed = id;

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
notification_info_t *get_json_notification(gchar *json_data, gboolean free_unparsed)
{
	JSON_Value *root_value = NULL;
	JSON_Object *root = NULL;
	const char *title = NULL,
		   *byline = NULL,
		   *image_path = NULL,
		   *command = NULL,
		   *sound = NULL,
		   *type = NULL,
		   *button1_label = NULL,
		   *button1_colour = NULL,
		   *button1_command = NULL,
		   *button1_hover = NULL,
		   *button2_label = NULL,
		   *button2_colour = NULL,
		   *button2_command = NULL,
		   *button2_hover = NULL;

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
	button1_label = json_object_get_string(root, "button1_label");
	button1_colour = json_object_get_string(root, "button1_colour");
	button1_hover = json_object_get_string(root, "button1_hover");
	button1_command = json_object_get_string(root, "button1_command");
	button2_label = json_object_get_string(root, "button2_label");
	button2_colour = json_object_get_string(root, "button2_colour");
	button2_command = json_object_get_string(root, "button2_command");
	button2_hover = json_object_get_string(root, "button2_hover");

	notification_info_t *data = g_new0(notification_info_t, 1);

	data->unparsed = json_data;
	data->free_unparsed = free_unparsed;

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

	if (button1_label) {
		data->button1_label = g_new0(gchar, strlen(button1_label) + 1);
		g_strlcpy(data->button1_label, button1_label,
			  strlen(button1_label) + 1);

		if (button1_colour) {
			data->button1_colour = g_new0(gchar, strlen(button1_colour) + 1);
			g_strlcpy(data->button1_colour, button1_colour,
				  strlen(button1_colour) + 1);
		}

		if (button1_hover) {
			data->button1_hover = g_new0(gchar, strlen(button1_hover) + 1);
			g_strlcpy(data->button1_hover, button1_hover,
				  strlen(button1_hover) + 1);
		}

		if (button1_command) {
			data->button1_command = g_new0(gchar, strlen(button1_command) + 1);
			g_strlcpy(data->button1_command, button1_command,
				  strlen(button1_command) + 1);
		}
	}

	if (button2_label) {
		data->button2_label = g_new0(gchar, strlen(button2_label) + 1);
		g_strlcpy(data->button2_label, button2_label,
			  strlen(button2_label) + 1);

		if (button2_colour) {
			data->button2_colour = g_new0(gchar, strlen(button2_colour) + 1);
			g_strlcpy(data->button2_colour, button2_colour,
				  strlen(button2_colour) + 1);
		}

		if (button2_hover) {
			data->button2_hover = g_new0(gchar, strlen(button2_hover) + 1);
			g_strlcpy(data->button2_hover, button2_hover,
				  strlen(button2_hover) + 1);
		}

		if (button2_command) {
			data->button2_command = g_new0(gchar, strlen(button2_command) + 1);
			g_strlcpy(data->button2_command, button2_command,
				  strlen(button2_command) + 1);
		}
	}

	json_value_free(root_value);

	return data;
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
		notification_info_t *data = get_json_notification(line, TRUE);

		if (!data)
		  data = get_notification_by_id(line, TRUE);

		/* if data is valid, we pass ownership of 'line' to it for later use.
		   It is then freed when 'data' is freed */
		if(!data)
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
