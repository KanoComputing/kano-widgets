#!/usr/bin/python
#
#  Tests widget by sending multiple chained notifications.
#  You need to install dogtail like this:
#
#    $ sudo apt-get install python-dogtail libglib2.0-bin
#

import unittest

from dogtail import rawinput

import os
import sys
import time

import api

class TestNotificationsPythonApi(unittest.TestCase):

    daemon_name='kano-notifications-daemon'

    @classmethod
    def setUp(cls):
        if os.getenv('DISPLAY'):
            os.system('pkill -f {}'.format(cls.daemon_name))
            os.system('{} &'.format(cls.daemon_name))
            time.sleep(1)

    @classmethod
    def tearDown(cls):
        if os.getenv('DISPLAY'):
            os.system('pkill -f {}'.format(cls.daemon_name))
            time.sleep(1)

    @classmethod
    def tearDownClass(cls):
        if os.getenv('DISPLAY'):
            '''double make sure the notifications daemon is dead on tests completion'''
            os.system('pkill -f {}'.format(cls.daemon_name))
            time.sleep(1)
    
    def _import_api(self):
        try:
            sys.path.append('../lxpanel-plugin-notifications')
            import notifications
            return notifications
        except:
            return None

    def _get_notification_count(self):
        count=-1
        try:
            count=int(os.popen('xwininfo -root -tree | grep -c {}'.format(self.daemon_name)).read())
            if count:
                count -= 1
        except:
            pass
        
        return count
                
    def test_send_one_notification(self):
        self.assertIsNotNone(os.getenv('DISPLAY'))

        notifications=api.import_notification_api()
        self.assertTrue(notifications)
        
        # send a notification popup to the desktop
        notifications.display_generic_notification('test_send_one_notification', 'sample test line')
        time.sleep(1)

        # make sure there is a notification window
        self.assertEqual(self._get_notification_count(), 1, msg='Notification does not seem to popup')

        # FIXME: coordinates will not work unless 1980x1080 display
        rawinput.click(780, 690)
        time.sleep(5)

        # FIXME: for some reason the daemon always shows a notification "please remember to register"
        self.assertEqual(self._get_notification_count(), 0, msg='Notification does not disappear')

    @unittest.skipIf('-fast' in sys.argv, 'Skipping because tests are in fast mode')
    def test_send_many_notifications(self):
        self.assertIsNotNone(os.getenv('DISPLAY'))

        notifications=api.import_notification_api()
        self.assertTrue(notifications)
        
        num_popups=15

        # send a notification popup to the desktop one after the next
        for i in range(0, num_popups):
            notifications.display_generic_notification('test_send_many_notifications {}'.format(i), 'sample test line')
            time.sleep(0.5)
            
        # click on each stacked notification one by one
        for i in range(0, num_popups):
            self.assertEqual(self._get_notification_count(), 1)

            # FIXME: coordinates will not work unless 1980x1080
            rawinput.click(780, 690)
            time.sleep(0.5)

        # make sure all notifications popups are gone
        self.assertEqual(self._get_notification_count(), 0, msg='Notifications popups still found')



if __name__ == '__main__':
    unittest.main()
