#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Growing and reducing arrays """

import unittest
import json
import time

from datetime import datetime

from py_mini_racer import py_mini_racer


class Test(unittest.TestCase):
    """ Test basic types """


    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_growing_array(self):

        js = """
    var global_array = [
        {
            get first() {
                for(var i=0; i<100; i++) {
                    global_array.push(0x41);
                }
            }
        }
    ];

    // when accessed, the first element will make the array grow by 100 items.
    global_array;
    """

        res = self.mr.eval(js)
        # Initial array size was 100
        self.assertEqual(res, [{'first': None}])


    def test_shrinking_array(self):
            js = """
    var global_array = [
        {
            get first() {
                for(var i=0; i<100; i++) {
                    global_array.pop();
                }
            }
        }
    ];

    // build a 200 elements array
    for(var i=0; i < 200; i++)
        global_array.push(0x41);

    // when the first item will be accessed, it should remove 100 items.

    global_array;
            """

            # The final array should have:
            #   The initial item
            #   The next 100 items (value 0x41)
            #   The last 100 items which have been removed when the initial key was accessed
            array = [{'first': None}] + \
                    [0x41] * 100      + \
                    [None] * 100

            res = self.mr.eval(js)
            self.assertEqual(res, array)


if __name__ == '__main__':
    import sys
    sys.exit(unittest.main())
