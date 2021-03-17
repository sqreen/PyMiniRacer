# -*- coding: utf-8 -*-
import os
import unittest

from py_mini_racer import MiniRacer

test_dir = os.path.dirname(os.path.abspath(__file__))


class WASMTest(unittest.TestCase):

    def setUp(self):
        self.mr = MiniRacer()

    def test_add(self):
        fn = os.path.join(test_dir, "add.wasm")

        size = os.path.getsize(fn)
        moduleRaw = self.mr.eval("""
        const moduleRaw = new SharedArrayBuffer({});
        moduleRaw
        """.format(size))

        with open(fn, "rb") as f:
            self.assertEqual(f.readinto(moduleRaw), size)

        self.mr.eval("""
        var res = null;
        WebAssembly.instantiate(new Uint8Array(moduleRaw)).then(result => {
            res = result.instance;
        }).catch(result => { res = result.message; });
        """)

        while not self.mr.eval("res"):
            pass

        self.assertTrue(self.mr.eval("typeof res !== 'string'"))
        self.assertEqual(self.mr.eval("res.exports.addTwo(1, 2)"), 3)
