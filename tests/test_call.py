#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS call functions """

import json
import unittest
from datetime import datetime

from py_mini_racer import py_mini_racer


class TestEval(unittest.TestCase):
    """ Test calling a function """

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_call_js(self):

        js_func = """var f = function() {
            return arguments.length;
        }"""

        self.mr.eval(js_func)

        self.assertEqual(self.mr.call('f'), 0)
        self.assertEqual(self.mr.call('f', *list(range(5))), 5)
        self.assertEqual(self.mr.call('f', *list(range(10))), 10)
        self.assertEqual(self.mr.call('f', *list(range(20))), 20)

    def test_call_custom_encoder(self):

        # Custom encoder for dates
        class CustomEncoder(json.JSONEncoder):

            def default(self, obj):
                if isinstance(obj, datetime):
                    return obj.isoformat()

                return json.JSONEncoder.default(self, obj)

        now = datetime.now()
        js_func = """var f = function(args) {
            return args;
        }"""
        self.mr.eval(js_func)

        self.assertEqual(self.mr.call('f', now, encoder=CustomEncoder),
                         now.isoformat())
