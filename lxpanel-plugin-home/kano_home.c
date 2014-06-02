/*
* kano_home.c
*
* Copyright (C) 2014 Kano Computing Ltd.
* License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
*
*/

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

#include <lxpanel/plugin.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define ICON_FILE "/usr/share/kano-widgets/icons/home-widget.png"
#define PLUGIN_TOOLTIP "Home"


Panel *panel;

static gboolean minimise_windows(GtkWidget *, GdkEventButton *);


static int plugin_constructor(Plugin *p, char **fp)
{
    (void)fp;

    panel = p->panel;

    /* need to create a widget to show */
    p->pwid = gtk_event_box_new();

    /* create an icon */
    GtkWidget *icon = gtk_image_new_from_file(ICON_FILE);

    /* set border width */
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);

    /* add the label to the container */
    gtk_container_add(GTK_CONTAINER(p->pwid), GTK_WIDGET(icon));

    /* our widget doesn't have a window... */
    gtk_widget_set_has_window(p->pwid, FALSE);
    gtk_signal_connect(GTK_OBJECT(p->pwid), "button-press-event",GTK_SIGNAL_FUNC(minimise_windows), p);

    /* Set a tooltip to the icon to show when the mouse sits over the it */
    GtkTooltips *tooltips;
    tooltips = gtk_tooltips_new();
    gtk_tooltips_set_tip(tooltips, GTK_WIDGET(icon), PLUGIN_TOOLTIP, NULL);

    gtk_widget_set_sensitive(icon, TRUE);

    /* show our widget */
    gtk_widget_show_all(p->pwid);

    return 1;
}

static void plugin_destructor(Plugin *p)
{
    (void)p;
}

static gboolean minimise_windows(GtkWidget *widget, GdkEventButton *event)
{
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
PluginClass kano_home_plugin_class = {
    // this is a #define taking care of the size/version variables
    PLUGINCLASS_VERSIONING,

    // type of this plugin
    type : "kano_home",
    name : N_("Kano Home"),
    version: "1.0",
    description : N_("Minimise open windows."),

    // we can have many running at the same time
    one_per_system : FALSE,

    // can't expand this plugin
    expand_available : FALSE,

    // assigning our functions to provided pointers.
    constructor : plugin_constructor,
    destructor  : plugin_destructor,
    config : plugin_configure,
    save : plugin_save_configuration
};
