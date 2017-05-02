# kano.notifications
#
# Copyright (C) 2014 Kano Computing Ltd.
# License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
#
# The python API for system notifications

import os
import json
from kano.logging import logger
from kano.timeout import timeout, TimeoutError
from kano.utils import get_user_unsudoed, get_home_by_username


_NOTIFICATION_PIPE = os.path.join(get_home_by_username(get_user_unsudoed()),
                                  '.kano-notifications.fifo')

_CONF_PATH = os.path.join(get_home_by_username(get_user_unsudoed()),
                          '.kano-notifications.conf')


def enable():
    """ Turns the notifications on

    Nothing will happen if they're on already.
    If there were queued notifications while it was in 'disable' mode,
    they will be displayed now in sequence.
    """

    _send_to_widget("enable")


def disable():
    """ Turns the notifications off completely

    All notifications subsequent notifications will be ignored by the widget.
    """

    _send_to_widget("disable")


def is_enabled():
    """ Query the notification config to see whether they're enabled.

    :returns: Boolean value
    """

    if os.path.isfile(_CONF_PATH):
        with open(_CONF_PATH, "r") as conf_file:
            conf = json.load(conf_file)
            if "enabled" in conf:
                return conf["enabled"]

    return False


def allow_world_notifications():
    """ Turns off the filtering of notifications from kano world. """

    _send_to_widget("allow_world_notifications")


def disallow_world_notifications():
    """ Turns on the filtering of notifications from kano world. """

    _send_to_widget("disallow_world_notifications")


def world_notifications_allowed():
    """ Query the notification config to see whether world notifications are on.

    :returns: Boolean value
    """

    if os.path.isfile(_CONF_PATH):
        with open(_CONF_PATH, "r") as conf_file:
            conf = json.load(conf_file)
            if "allow_world_notifications" in conf:
                return conf["allow_world_notifications"]

    return False


def pause():
    """ Hold off displaying notification until further notice

    Any notification that will be triggered after a call to this
    function will be put into a pipeline and displayed after a
    `resume()` call was made.
    """

    _send_to_widget("pause")


def resume():
    """ Continue displaying notifications

    If there were any notifications triggered since the last pause,
    they will be all displayed after this function has been called.
    """

    _send_to_widget("resume")


def display_notification_by_id(notification_id):
    """ Display a specific, kano-profile related notification

    This function can only be used to display the notifications that
    are predefined in kano-profile. You need to know the id string of
    the one you'd like to show.

    :param notification_id: the id string of the notification
    """

    _send_to_widget(notification_id)


def display_generic_notification(title, byline, image=None, command=None,
                                 sound=None, ntype=None, urgency=None):
    """ Display a generic notification

    Using this function you can customise all the parameters of the
    notification as you wish. The only required ones are the title and
    the byline.

    :param title: the desired title of the notification
    :param byline: short description (under 50 characters)
    :param image: a path to an image (must be 280x170 in size)
    :param command: an action that the user can optionaly launch
    :param sound: an absolute path to a wav file to play with the notification
    :param ntype: the type of a notification
    :param urgency: the urgency level (0: low, 1: normal, 2: critical)
    """

    notification_data = {
        "title": title,
        "byline": byline,
        "image": image,
        "command": command,
        "sound": sound,
        "type": ntype,
        "urgency": urgency
    }

    _send_to_widget(json.dumps(notification_data))


def _send_to_widget(message):
  try:
    _do_send_to_widget(message)
  except TimeoutError:
    pass
  
@timeout(2)
def _do_send_to_widget(message):
    """ Dispatch a message to the notification pipe

    :param notification: the message string
    """

    if not os.path.exists(_NOTIFICATION_PIPE):
        msg = "The notification pipe not found at {}".format(_NOTIFICATION_PIPE)
        logger.warn(msg)

    # Open the fifo in append mode, as if it is not present notifications are queued in a flat file
    with open(_NOTIFICATION_PIPE, 'a') as fifo:
        msg = 'Sending "{}" to the notifications widget'.format(message)
        logger.debug(msg)
        fifo.write(message + '\n')
