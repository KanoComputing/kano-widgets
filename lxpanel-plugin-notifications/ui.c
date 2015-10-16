/*
 * ui.c
 *
 * Copyright (C) 2015 Kano Computing Ltd.
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
#include <stdlib.h>

#include <kdesk-hourglass.h>

#include "ui.h"
#include "notifications.h"
#include "config.h"

#define LED_START_CMD "sudo -b kano-speakerleds notification start"
#define LED_STOP_CMD "sudo kano-speakerleds notification stop"
/*
 * Non-blocking way of launching a command.
 */
void launch_cmd(const char *cmd, gboolean hourglass)
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
 * Launch the command that is associated with the notification.
 */
static gboolean eventbox_click_cb(GtkWidget *w, GdkEventButton *event,
				gtk_user_data_t *user_data)
{
	/* User clicked on this world notification command.
	   Save this event in Kano Tracker. It will be audited
	   in the form: "world-notification <byline>" to keep a counter. */
	/* TODO: This no longer makes sense we need to replace it with something else
	   since the old tracker doesn't work any more. */
	/* char *tracker_cmd_prolog = "kano-profile-cli increment_app_runtime "
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
	}*/

	/* Launch the application pointed to by the "command"
	   notification field */
	launch_cmd(user_data->notification->command, TRUE);
	hide_notification_window(user_data->plugin_data);
	g_free(user_data);

	/* Notification tracking is done after processing the visual work,
	   to avoid UIX delays */
	/* TODO: to be removed or refactored */
	/*if (tracker_cmd) {
		launch_cmd(tracker_cmd, FALSE);
		g_free(tracker_cmd);
	}*/

	return TRUE;
}

/*
 * A callback for when the user clicks on the closing button.
 */
static gboolean close_button_click_cb(GtkWidget *w, GdkEventButton *event,
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
 * The command button's hover in/out callback. Changes the colour of it.
 */
static gboolean button_hover_cb(GtkWidget *widget, GdkEvent *event,
				gchar *bg_colour)
{
	GdkColor c;
	gdk_color_parse(bg_colour, &c);
	gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &c);
	return TRUE;
}

/* Sets the appropriate callbacks to the object for the cursor
 * to change to hand when hovering over it.
 */
static void set_hover_callbacks(GtkWidget *w, gchar *default_bg,
				gchar *hover_bg)
{
	gtk_signal_connect(GTK_OBJECT(w), "realize",
		     GTK_SIGNAL_FUNC(button_realize_cb), NULL);
	gtk_signal_connect(GTK_OBJECT(w), "enter-notify-event",
		     GTK_SIGNAL_FUNC(button_hover_cb), hover_bg);
	gtk_signal_connect(GTK_OBJECT(w), "leave-notify-event",
		     GTK_SIGNAL_FUNC(button_hover_cb), default_bg);
}

/* Creates the closing X button widget for the bottom right corner of
 * the notification window.
 */
static GtkWidget *construct_x_button_widget(kano_notifications_t *plugin_data)
{
	GdkColor button_bg;
	gdk_color_parse(BUTTON_COLOUR, &button_bg);

	GtkWidget *arrow = gtk_image_new_from_file(X_BUTTON);

	GtkWidget *x_button = gtk_event_box_new();
	gtk_widget_modify_bg(x_button, GTK_STATE_NORMAL, &button_bg);
	gtk_container_add(GTK_CONTAINER(x_button), arrow);
	gtk_widget_set_size_request(x_button, BUTTON_WIDTH, BUTTON_HEIGHT);

	set_hover_callbacks(x_button, BUTTON_COLOUR, BUTTON_HIGHLIGHTED_COLOUR);

	gtk_signal_connect(GTK_OBJECT(x_button), "button-release-event",
		     GTK_SIGNAL_FUNC(close_button_click_cb), plugin_data);

	return x_button;
}

static gboolean launch_button_cb(GtkWidget *w, GdkEventButton *event,
				gtk_user_data_t *user_data)
{
	if (user_data->command)
		launch_cmd(user_data->command, TRUE);

	hide_notification_window(user_data->plugin_data);
	g_free(user_data);

	return TRUE;
}


static GtkWidget *construct_extra_button(gchar *label, gchar *colour,
					 gchar *hover, gchar *command,
					 kano_notifications_t *plugin_data)
{
	GdkColor button_bg, label_colour;
	GtkWidget *button = gtk_event_box_new();

	if (colour) {
		gdk_color_parse(colour, &button_bg);
		if (!hover)
			hover = colour;
		set_hover_callbacks(button, colour, hover);
	} else {
		gdk_color_parse(BUTTON_COLOUR, &button_bg);
		set_hover_callbacks(button, BUTTON_COLOUR,
				    BUTTON_HIGHLIGHTED_COLOUR);
	}
	gtk_widget_modify_bg(button, GTK_STATE_NORMAL, &button_bg);

	GtkWidget *label_widget = gtk_label_new(NULL);
	gtk_label_set_text(GTK_LABEL(label_widget), label);

	/* Set the label's font size and weight */
	GtkStyle *style = gtk_widget_get_style(label_widget);
	pango_font_description_set_size(style->font_desc, 11*PANGO_SCALE);
	pango_font_description_set_weight(style->font_desc, PANGO_WEIGHT_BOLD);
	gtk_widget_modify_font(label_widget, style->font_desc);

	gdk_color_parse(EXTRA_BUTTON_LABEL_COLOUR, &label_colour);
	gtk_widget_modify_fg(label_widget, GTK_STATE_NORMAL, &label_colour);

	/* Center the label within the eventbox */
	GtkWidget *label_align = gtk_alignment_new(0.5, 0.5, 0, 0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(label_align), 0, 0, 22, 22);
	gtk_container_add(GTK_CONTAINER(label_align), label_widget);

	gtk_container_add(GTK_CONTAINER(button), label_align);

	//gtk_widget_set_size_request(button, -1, BUTTON_HEIGHT);

	gtk_user_data_t *user_data = g_new0(gtk_user_data_t, 1);
	user_data->notification = NULL;
	user_data->plugin_data = plugin_data;
	user_data->command = command;

	gtk_signal_connect(GTK_OBJECT(button), "button-release-event",
		     GTK_SIGNAL_FUNC(launch_button_cb), user_data);

	return button;
}

static GtkWidget *construct_extra_buttons(kano_notifications_t *plugin_data,
					  notification_info_t *n)
{
	GtkWidget *buttons = gtk_vbox_new(FALSE, 0);

	if (n->button1_label) {
		GtkWidget *button1 = construct_extra_button(n->button1_label,
					n->button1_colour, n->button1_hover,
					n->button1_command, plugin_data);
		gtk_box_pack_start(GTK_BOX(buttons), button1, TRUE, TRUE, 0);
	}

	if (n->button2_label) {
		GtkWidget *button2 = construct_extra_button(n->button2_label,
					n->button2_colour, n->button2_hover,
					n->button2_command, plugin_data);
		gtk_box_pack_start(GTK_BOX(buttons), button2, TRUE, TRUE, 0);
	}

	return buttons;
}

/*
 * Constructs the notification window and display's it.
 *
 * This function also sets up a timer that will destroy the window after
 * a set period of time. It's expected that no notification is being
 * shown at the time of this function call.
 */
void show_notification_window(kano_notifications_t *plugin_data,
				     notification_info_t *notification)
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
	plugin_data->window = win;

	GtkStyle *style;
	GtkWidget *eventbox = gtk_event_box_new();

	gtk_user_data_t *user_data = g_new0(gtk_user_data_t, 1);
	user_data->notification = notification;
	user_data->plugin_data = plugin_data;
	user_data->command = NULL;

	if (notification->command && strlen(notification->command) > 0) {
		gtk_signal_connect(GTK_OBJECT(eventbox), "button-release-event",
				   GTK_SIGNAL_FUNC(eventbox_click_cb), user_data);
	}

	GdkColor white;
	gdk_color_parse("white", &white);
	gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(eventbox), GTK_WIDGET(box));

	/* A notification with a small image on the side rather than the
	   large one at the top.
	*/
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

	/* Create the buttons on the right side of the notification */
	GtkWidget *buttons_widget;
	if (IS_TYPE(notification, "small") &&
	    (notification->button1_label || notification->button2_label)) {
		buttons_widget = construct_extra_buttons(plugin_data,
							 notification);
	} else {
		buttons_widget = construct_x_button_widget(plugin_data);
	}
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(buttons_widget),
			   FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
			   TRUE, TRUE, 0);

	gtk_widget_show_all(win);

	/* TODO Positioning doesn't take into account the position of the
	   panel itself. */
	GdkWindow *gdk_win = gtk_widget_get_window(GTK_WIDGET(win));
	int win_pos_x = (gdk_screen_width() - gdk_window_get_width(gdk_win))/2,
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

	/* Change speaker LED colour for notification. */

	{
	        int bufsize = strlen(LED_START_CMD)+strlen(notification->unparsed)+4;
		gchar *notify_cmd = g_new0(gchar, bufsize);
		g_snprintf(notify_cmd, bufsize, LED_START_CMD " '%s'", notification->unparsed);
		launch_cmd(notify_cmd, FALSE);
		g_free(notify_cmd);
	}


	plugin_data->window_timeout = g_timeout_add(ON_TIME,
				(GSourceFunc) close_notification,
				(gpointer) plugin_data);
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
   	        notif = get_json_notification(REGISTER_REMINDER, FALSE);
		plugin_data->queue = g_list_append(plugin_data->queue, notif);

	}

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
gboolean close_notification(kano_notifications_t *plugin_data)
{
	/* Change speaker LED colour back after notification. 
	 * We use system() so we don't kill next led command for the next notification.
	 */

	system(LED_STOP_CMD);

	if (plugin_data->window != NULL) {
		g_mutex_lock(&(plugin_data->lock));

		gtk_widget_destroy(plugin_data->window);
		plugin_data->window = NULL;

		notification_info_t *notification = g_list_nth_data(plugin_data->queue, 0);
		plugin_data->queue = g_list_remove(plugin_data->queue, notification);
		free_notification(notification);


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
