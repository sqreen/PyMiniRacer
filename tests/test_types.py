#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS types tests """

import unittest
import json

import py_mini_racer

class Test(unittest.TestCase):
    """ Test basic types """

    def valid(self, py_val):
        js_str = json.dumps(py_val)
        parsed = self.mr.eval(js_str)
        self.assertEqual(py_val, parsed)

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_str(self):
        self.valid("'a string'")
        self.valid("'a ' + 'string'")

    def test_numbers(self):
        self.valid(1)
        self.valid(1.0)
        self.valid(2**16)
        self.valid(2**31-1)
        # FIXME:
        self.skipTest("too big numbers follow")
        # self.valid(2**31)
        # self.valid(2**33)

    def test_arrays(self):
        self.valid([1])
        self.valid([])
        self.valid([1,2,3])
        # Nested
        self.valid([1,2,['a', 1]])

    def test_none(self):
        self.valid(None)

    def test_hash(self):
        self.valid({})
        self.valid('{}')
        self.valid({'a': 1})
        # FIXME: keys of stringified integers are eval'd as integers
        # self.valid({'2': 1})
        # Nested
        self.valid({" ": {'z': 'www'}})


    def test_complex(self):

        self.valid({
            1: [
                1, 2, 'qwe', {
                    'z': [
                        4,5,6, {
                            'eqewr': 1,
                            'zxczxc': 'qweqwe',
                            'z': { 1: 2 }
                        }
                    ]
                }
            ], 'qwe': 1
        })


if __name__ == '__main__':
    import sys
    sys.exit(unittest.main())
