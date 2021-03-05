#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS types tests """

import os
import unittest
from io import open

from py_mini_racer import py_mini_racer


class Test(unittest.TestCase):
    """ Test basic types """

    def test_babel(self):

        context = py_mini_racer.MiniRacer()

        path = os.path.join(os.path.dirname(__file__), 'fixtures/babel.js')
        babel_source = open(path, "r", encoding='utf-8').read()
        source = """
          var self = this;
          %s
          babel.eval = function(code) {
            return eval(babel.transform(code)["code"]);
          }
        """ % babel_source
        context.eval(source)
        self.assertEqual(64, context.eval("babel.eval(((x) => x * x)(8))"))


if __name__ == '__main__':
    import sys
    sys.exit(unittest.main())
