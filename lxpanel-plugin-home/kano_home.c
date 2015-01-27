/*
* kano_home.c
*
* Copyright (C) 2014 Kano Computing Ltd.
* License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
*
*/

#include <glib/gi18n.h>
#include <lxpanel/plugin.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define ICON_FILE "/usr/share/kano-widgets/icons/home-widget.png"
#define PLUGIN_TOOLTIP "Home"


static gboolean minimise_windows(GtkWidget *, GdkEventButton *);


static GtkWidget *plugin_constructor(LXPanel *panel, config_setting_t *settings)
{
    (void)panel;
    (void)settings;

    /* need to create a widget to show */
    GtkWidget *pwid = gtk_event_box_new();

    /* create an icon */
    GtkWidget *icon = gtk_image_new_from_file(ICON_FILE);

    /* set border width */
    gtk_container_set_border_width(GTK_CONTAINER(pwid), 0);

    /* add the label to the container */
    gtk_container_add(GTK_CONTAINER(pwid), GTK_WIDGET(icon));

    /* our widget doesn't have a window... */
    gtk_widget_set_has_window(pwid, FALSE);
    gtk_signal_connect(GTK_OBJECT(pwid), "button-press-event", GTK_SIGNAL_FUNC(minimise_windows), pwid);

    /* Set a tooltip to the icon to show when the mouse sits over the it */
    GtkTooltips *tooltips;
    tooltips = gtk_tooltips_new();
    gtk_tooltips_set_tip(tooltips, GTK_WIDGET(icon), PLUGIN_TOOLTIP, NULL);

    gtk_widget_set_sensitive(icon, TRUE);

    /* show our widget */
    gtk_widget_show_all(pwid);

    return pwid;
}

static gboolean minimise_windows(GtkWidget *widget, GdkEventButton *event)
{
    (void)widget;

    if (event->button != 1)
        return FALSE;

    WnckScreen* screen;
    GList* window_l;
    screen = wnck_screen_get_default();
    wnck_screen_force_update(screen);

    for (window_l = wnck_screen_get_windows(screen); window_l != NULL; window_l = window_l->next)
    {
        WnckWindow *window = WNCK_WINDOW(window_l->data);
        wnck_window_minimize(window);
    }

    return TRUE;
}

FM_DEFINE_MODULE(lxpanel_gtk, kano_home)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Kano Home"),
    .description = N_("Minimise open windows."),
    .new_instance = plugin_constructor,
    .one_per_system = FALSE
};
