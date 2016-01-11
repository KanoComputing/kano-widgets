#!/usr/bin/python
#
# Copyright (C) 2014, 2016 Kano Computing Ltd.
# License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
#
#  Tests that the internal notification python API can be loaded
#

import unittest
import sys
import api

class TestPythonApi(unittest.TestCase):
    
    def test_python_api(self):
        notification_api=api.import_notification_api()
        self.assertIsNotNone(notification_api)


if __name__ == '__main__':
    unittest.main()
