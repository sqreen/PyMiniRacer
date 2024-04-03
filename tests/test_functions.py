""" Tests JSFunctions """


import pytest
from py_mini_racer import (
    JSEvalException,
    JSTimeoutException,
    MiniRacer,
)


def test_function():
    mr = MiniRacer()
    func = mr.eval("(a) => a")
    assert func(42) == 42
    arr = mr.eval("[41, 42]")
    assert list(func(arr)) == list(func(arr))
    thing = mr.eval(
        """\
class Thing {
    constructor(a) {
        this.blob = a;
    }

    stuff(extra) {
        return this.blob + extra;
    }
}
new Thing('start');
"""
    )
    stuff = thing["stuff"]
    assert stuff("end", this=thing) == "startend"


def test_exceptions():
    mr = MiniRacer()
    func = mr.eval(
        """\
function func(a, b, c) {
    throw new Error('asdf');
}
func
"""
    )

    with pytest.raises(JSEvalException) as exc_info:
        func()

    assert (
        exc_info.value.args[0]
        == """\
<anonymous>:2: Error: asdf
    throw new Error('asdf');
    ^

Error: asdf
    at func (<anonymous>:2:11)
"""
    )


def test_timeout():
    mr = MiniRacer()
    func = mr.eval("() => { while(1) { } }")
    with pytest.raises(JSTimeoutException) as exc_info:
        func(timeout_sec=1)

    assert exc_info.value.args[0] == "JavaScript was terminated by timeout"
