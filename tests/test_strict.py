import unittest

from py_mini_racer import py_mini_racer


class StrictTestCase(unittest.TestCase):
    """Test StrictMiniRacer"""

    def setUp(self):
        self.mr = py_mini_racer.StrictMiniRacer()

    def test_basic_int(self):
        self.assertEqual(42, self.mr.execute("42"))

    def test_basic_string(self):
        self.assertEqual("42", self.mr.execute('"42"'))

    def test_basic_hash(self):
        self.assertEqual({}, self.mr.execute('{}'))

    def test_basic_array(self):
        self.assertEqual([1, 2, 3], self.mr.execute('[1, 2, 3]'))

    def test_call(self):
        js_func = """var f = function(args) {
            return args.length;
        }"""

        self.assertIsNone(self.mr.eval(js_func))
        self.assertEqual(self.mr.call('f', list(range(5))), 5)

    def test_message(self):
        with self.assertRaises(py_mini_racer.JSEvalException):
            self.mr.eval("throw new EvalError('Hello', 'someFile.js', 10);")
