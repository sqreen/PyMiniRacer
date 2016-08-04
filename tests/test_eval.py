#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS types tests """

import unittest

from py_mini_racer import py_mini_racer

class Test(unittest.TestCase):
    """ Test basic types """

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_invalid(self):

        with self.assertRaises(py_mini_racer.JSEvalException):
            self.mr.eval("invalid")

    def test_global(self):
        self.mr.eval('var xabc = 22;')
        self.assertEqual(22, self.mr.eval('xabc'))


    def test_fun(self):
        res = self.mr.eval('var x = function(y) {return y+1;}')

        self.assertEqual(2,   self.mr.eval('x(1)'))
        self.assertEqual(11,  self.mr.eval('x(10)'))
        self.assertEqual(101, self.mr.eval('x(100)'))

    def test_multiple_ctx(self):

        c1 = py_mini_racer.MiniRacer()
        c2 = py_mini_racer.MiniRacer()
        c3 = py_mini_racer.MiniRacer()

        c1.eval('var x = 1')
        c2.eval('var x = 2')
        c3.eval('var x = 3')
        self.assertEqual(c1.eval('(x)'), 1)
        self.assertEqual(c2.eval('(x)'), 2)
        self.assertEqual(c3.eval('(x)'), 3)

    def test_exception_thrown(self):
        context = py_mini_racer.MiniRacer()

        js_source = "var f = function() {throw 'error'};"

        context.eval(js_source)

        with self.assertRaises(py_mini_racer.JSEvalException):
            context.eval("f()")

    def test_cannot_parse(self):

        context = py_mini_racer.MiniRacer()

        js_source = "var f = function("

        with self.assertRaises(py_mini_racer.JSParseException):
            context.eval(js_source)


if __name__ == '__main__':
    import sys
    sys.exit(unittest.main())
