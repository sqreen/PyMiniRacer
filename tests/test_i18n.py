import json
import unittest

from datetime import datetime

from py_mini_racer import py_mini_racer


class Testi18n(unittest.TestCase):

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_i18n(self):

        js_func = """
        const count = 26254.39;
        const date = new Date("2012-05-24");

        function log(locale) {
          return `${new Intl.DateTimeFormat(locale).format(date)} ${new Intl.NumberFormat(locale).format(count)}`;
        }
        """
        self.mr.eval(js_func)

        self.assertEqual(self.mr.call('log', 'en-US'), u"5/24/2012 26,254.39")
