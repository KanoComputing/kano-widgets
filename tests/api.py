#!/usr/bin/python
#
# Copyright (C) 2014, 2016 Kano Computing Ltd.
# License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
#

import sys

def import_notification_api():
    sys.path.append('../lxpanel-plugin-notifications')
    import notifications
    return notifications
