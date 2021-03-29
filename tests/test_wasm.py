# -*- coding: utf-8 -*-
import os
import unittest

from py_mini_racer import MiniRacer

test_dir = os.path.dirname(os.path.abspath(__file__))


class WASMTest(unittest.TestCase):
    """
    Test executing a WASM module.
    """

    def setUp(self):
        self.mr = MiniRacer()

    def test_add(self):
        fn = os.path.join(test_dir, "add.wasm")

        # 1. Allocate a buffer to hold the WASM module code
        size = os.path.getsize(fn)
        moduleRaw = self.mr.eval("""
        const moduleRaw = new SharedArrayBuffer({});
        moduleRaw
        """.format(size))

        # 2. Read the WASM module code
        with open(fn, "rb") as f:
            self.assertEqual(f.readinto(moduleRaw), size)

        # 3. Instantiate the WASM module
        self.mr.eval("""
        var res = null;
        WebAssembly.instantiate(new Uint8Array(moduleRaw)).then(result => {
            res = result.instance;
        }).catch(result => { res = result.message; });
        """)

        # 4. Wait for WASM module instantiation
        while not self.mr.eval("res"):
            pass

        self.assertTrue(self.mr.eval("typeof res !== 'string'"))

        # 5. Execute a WASM function
        self.assertEqual(self.mr.eval("res.exports.addTwo(1, 2)"), 3)
