#!/usr/bin/env python
# -*- coding: utf-8 -*-

import unittest

from py_mini_racer import py_mini_racer


class TestHeap(unittest.TestCase):
    """ Test heap stats """

    def setUp(self):

        self.mr = py_mini_racer.MiniRacer()

    def test_heap_stats(self):
        self.assertGreater(self.mr.heap_stats()["used_heap_size"], 0)
        self.assertGreater(self.mr.heap_stats()["total_heap_size"], 0)
