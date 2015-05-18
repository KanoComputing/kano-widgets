/*
 * config.h
 *
 * Copyright (C) 2015 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
 *
 * Code related to the configuration of the notifications widget.
 *
 */

#include <glib/gi18n.h>
#include <glib.h>
#include <gio/gio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>

#include "parson/parson.h"

#include "notifications.h"

#ifndef notif_config_h
#define notif_config_h

#define FIFO_FILENAME ".kano-notifications.fifo"
#define CONF_FILENAME ".kano-notifications.conf"


gchar *get_fifo_filename(void);
gchar *get_conf_filename(void);

int save_conf(struct notification_conf *conf);
void load_conf(struct notification_conf *conf);

gboolean is_user_registered();
gboolean is_internet();

#endif
