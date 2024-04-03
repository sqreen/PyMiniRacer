""" Basic JS types tests """

from datetime import datetime, timezone
from json import dumps
from time import time

import pytest
from py_mini_racer import (
    JSEvalException,
    JSFunction,
    JSObject,
    JSSymbol,
    JSUndefined,
    MiniRacer,
)


def _test_round_trip(mr, val):
    a = mr.eval("[]")
    a.append(val)  # force conversion into a JS type
    assert a[0] == val  # get it back again and verify it


class Validator:
    def __init__(self, *, round_trip=True):
        self.mr = MiniRacer()
        self.round_trip = round_trip

    def validate(self, py_val):
        testee = py_val
        js_str = dumps(py_val)

        parsed = self.mr.execute(js_str)
        assert testee == parsed

        if self.round_trip:
            _test_round_trip(self.mr, py_val)


def test_undefined():
    mr = MiniRacer()
    undef = mr.eval("undefined")
    assert undef is JSUndefined
    assert undef == JSUndefined
    assert not undef
    _test_round_trip(mr, undef)


def test_str():
    v = Validator()
    v.validate("'a string'")
    v.validate("'a ' + 'string'")
    v.validate("string with null \0 byte")


def test_unicode():
    ustr = "\N{GREEK CAPITAL LETTER DELTA}"
    mr = MiniRacer()
    res = mr.eval("'" + ustr + "'")
    assert ustr == res
    _test_round_trip(mr, ustr)


def test_numbers():
    v = Validator()
    v.validate(1)
    v.validate(1.0)
    v.validate(2**16)
    v.validate(2**31 - 1)
    v.validate(2**31)
    v.validate(2**33)


def test_arrays():
    v = Validator(round_trip=False)
    v.validate([1])
    v.validate([])
    v.validate([1, 2, 3])
    # Nested
    v.validate([1, 2, ["a", 1]])


def test_none():
    v = Validator()
    v.validate(None)


def test_hash():
    v = Validator(round_trip=False)
    v.validate({})
    v.validate("{}")
    v.validate({"a": 1})
    v.validate({" ": {"z": "www"}})


def test_complex():
    v = Validator(round_trip=False)
    v.validate(
        {
            "1": [
                1,
                2,
                "qwe",
                {"z": [4, 5, 6, {"eqewr": 1, "zxczxc": "qweqwe", "z": {"1": 2}}]},
            ],
            "qwe": 1,
        }
    )


def test_object():
    mr = MiniRacer()
    res = mr.eval("var a = {}; a")
    assert isinstance(res, JSObject)
    assert res.__hash__() is not None
    _test_round_trip(mr, res)


def test_timestamp():
    val = int(time())
    mr = MiniRacer()
    res = mr.eval("var a = new Date(%d); a" % (val * 1000))
    assert res == datetime.fromtimestamp(val, timezone.utc)
    _test_round_trip(mr, res)


def test_symbol():
    mr = MiniRacer()
    res = mr.eval('Symbol("my_symbol")')
    assert isinstance(res, JSSymbol)
    assert res.__hash__() is not None
    _test_round_trip(mr, res)


def test_function():
    mr = MiniRacer()
    res = mr.eval("function func() {}; func")
    assert isinstance(res, JSFunction)
    assert res.__hash__() is not None
    _test_round_trip(mr, res)


def test_date():
    mr = MiniRacer()
    res = mr.eval("var a = new Date(Date.UTC(2014, 0, 2, 3, 4, 5)); a")
    assert res == datetime(2014, 1, 2, 3, 4, 5, tzinfo=timezone.utc)
    _test_round_trip(mr, res)


def test_exception():
    js_source = """
    var f = function(arg) {
        throw 'error: '+arg
        return nil
    }"""

    mr = MiniRacer()
    mr.eval(js_source)

    with pytest.raises(JSEvalException) as exc_info:
        mr.eval("f(42)")

    assert "error: 42" in exc_info.value.args[0]


def test_array_buffer():
    js_source = """
    var b = new ArrayBuffer(1024);
    var v = new Uint8Array(b);
    v[0] = 0x42;
    b
    """
    mr = MiniRacer()
    ret = mr.eval(js_source)
    assert len(ret) == 1024
    assert ret[0:1].tobytes() == b"\x42"


def test_array_buffer_view():
    js_source = """
    var b = new ArrayBuffer(1024);
    var v = new Uint8Array(b, 1, 1);
    v[0] = 0x42;
    v
    """
    mr = MiniRacer()
    ret = mr.eval(js_source)
    assert len(ret) == 1
    assert ret.tobytes() == b"\x42"


def test_shared_array_buffer():
    js_source = """
    var b = new SharedArrayBuffer(1024);
    var v = new Uint8Array(b);
    v[0] = 0x42;
    b
    """
    mr = MiniRacer()
    ret = mr.eval(js_source)
    assert len(ret) == 1024
    assert ret[0:1].tobytes() == b"\x42"
    ret[1:2] = b"\xFF"
    assert mr.eval("v[1]") == 0xFF
