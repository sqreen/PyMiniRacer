#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Basic JS types tests """
import sys
import time
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
        self.mr.eval('var x = function(y) {return y+1;}')

        self.assertEqual(2, self.mr.eval('x(1)'))
        self.assertEqual(11, self.mr.eval('x(10)'))
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

    @unittest.skipIf(sys.version_info[0] < 3, "no assertRaisesRegex on Python 2")
    def test_cannot_parse(self):
        context = py_mini_racer.MiniRacer()
        js_source = "var f = function("

        with self.assertRaisesRegex(py_mini_racer.JSParseException, '.*Unknown JavaScript error during parse.*'):
            context.eval(js_source)

    def test_null_byte(self):

        context = py_mini_racer.MiniRacer()

        s = "\x00 my string!"

        # Try return a string including a null byte
        in_val = "var str = \"" + s + "\"; str;"
        result = context.eval(in_val)
        self.assertEqual(result, s)

    def test_timeout(self):
        timeout_ms = 100
        with self.assertRaises(py_mini_racer.JSTimeoutException):
            start_time = time.time()
            self.mr.eval('while(1) { }', timeout=timeout_ms)
            duration = time.time() - start_time
            assert timeout_ms <= duration * 1000 <= timeout_ms + 10

    def test_max_memory_soft(self):
        self.mr.set_soft_memory_limit(100000000)
        with self.assertRaises(py_mini_racer.JSOOMException):
            self.mr.eval('''let s = 1000;
                var a = new Array(s);
                a.fill(0);
                while(true) {
                    s *= 1.1;
                    let n = new Array(Math.floor(s));
                    n.fill(0);
                    a = a.concat(n);
                }''', max_memory=200000000)
        self.assertEqual(self.mr.was_soft_memory_limit_reached(), True)

    def test_max_memory_hard(self):
        with self.assertRaises(py_mini_racer.JSOOMException):
            self.mr.eval('''let s = 1000;
                var a = new Array(s);
                a.fill(0);
                while(true) {
                    s *= 1.1;
                    let n = new Array(Math.floor(s));
                    n.fill(0);
                    a = a.concat(n);
                }''', max_memory=200000000)

    def test_symbol(self):
        res = self.mr.eval('Symbol.toPrimitive')
        self.assertEqual(type(res), py_mini_racer.JSSymbol)

    def test_async(self):
        self.assertFalse(self.mr.eval("""
        var done = false;
        const shared = new SharedArrayBuffer(8);
        const view = new Int32Array(shared);

        const p = Atomics.waitAsync(view, 0, 0, 1); // 1 ms timeout
        p.value.then(() => { done = true; });
        done
        """))
        time.sleep(0.01)
        self.assertFalse(self.mr.eval("done"))
        self.assertTrue(self.mr.eval("done"))

    def test_fast_call(self):
        self.mr.eval("const test = function () { return 42; }")
        # this syntax is optimized and takes another execution path in the extension
        self.assertEqual(self.mr.eval("test()"), 42)
        # It looks like a fast call but it is not (it ends with '()' but no identifier)
        # should not fail and do a classical eval.
        self.assertEqual(self.mr.eval("1+test()"), 43)


if __name__ == '__main__':
    import sys
    sys.exit(unittest.main())
