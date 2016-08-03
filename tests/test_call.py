#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS call functions """

import unittest

from py_mini_racer import py_mini_racer


class TestEval(unittest.TestCase):
    """ Test calling a function """

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_call_js(self):

        js_func = """var f = function(args) {
            return args.length;
        }"""

        self.mr.eval(js_func)

        self.assertEqual(self.mr.call('f', list(range(5))), 5)
        self.assertEqual(self.mr.call('f', list(range(10))), 10)
        self.assertEqual(self.mr.call('f', list(range(20))), 20)
