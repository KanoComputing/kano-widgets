/*
 * ui.h
 *
 * Copyright (C) 2015 Kano Computing Ltd.
 * License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
 *
 * The GTK UI code related to the popup windows.
 *
 */

#include <glib.h>

#include "notifications.h"

#ifndef notif_ui_h
#define notif_ui_h

#define X_BUTTON "/usr/share/kano-widgets/icons/x-button.png"

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

#define TITLE_COLOUR "#323232"
#define BYLINE_COLOUR "#6e6e6e"

#define EXTRA_BUTTON_LABEL_COLOUR "#ffffff"

#define ON_TIME 6000

#define REGISTER_REMINDER \
	"{" \
		"\"title\": \"Kano World\", " \
		"\"byline\": \"Remember to register\", " \
		"\"image\": \"/usr/share/kano-profile/media/images/notification/280x170/register.png\", " \
		"\"sound\": null, " \
		"\"command\": \"kano-login 3\" " \
	"}"

/* This struct is used exclusively for passing user data to GTK signals. */
typedef struct {
	notification_info_t *notification;
	gchar *command;
	kano_notifications_t *plugin_data;
} gtk_user_data_t;

void launch_cmd(const char *cmd, gboolean hourglass);
void show_notification_window(kano_notifications_t *plugin_data,
				     notification_info_t *notification);
gboolean close_notification(kano_notifications_t *plugin_data);

#endif
