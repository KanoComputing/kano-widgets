#!/usr/bin/python
#
#

import sys

def import_notification_api():
    sys.path.append('../lxpanel-plugin-notifications')
    import notifications
    return notifications
