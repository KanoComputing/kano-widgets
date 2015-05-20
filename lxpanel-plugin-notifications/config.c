/*
 * config.c
 *
 * Copyright (C) 2015 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
 *
 */

#include "config.h"

#include <glib/gi18n.h>
#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <stdlib.h>

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
	int buff_len;
	buff_len = strlen(homedir) + strlen(FIFO_FILENAME) + sizeof(char) * 2;

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
	int buff_len;
	buff_len = strlen(homedir) + strlen(CONF_FILENAME) + sizeof(char) * 2;

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
 * Check whether the user is registered in kano world
 *
 * This will open the profile json file and see whether it has the
 * 'kanoworld_id' key.
 */
gboolean is_user_registered()
{
	const gchar *pf_tmp = "/home/%s/.kanoprofile/profile/profile.json";
	size_t bufsize;

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
	return (id != NULL) && strlen(id) > 0;
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

			conf->enabled = json_object_get_boolean(root,
								"enabled");
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
 * Check whether there's internet available.
 */
gboolean is_internet()
{
	return system("is_internet") == 0;
}
