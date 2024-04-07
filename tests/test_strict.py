from traceback import clear_frames

import pytest
from py_mini_racer import JSEvalException, JSUndefined, StrictMiniRacer


def test_basic_int(gc_check):
    mr = StrictMiniRacer()
    assert mr.execute("42") == 42

    gc_check.check(mr)


def test_basic_string(gc_check):
    mr = StrictMiniRacer()
    assert mr.execute('"42"') == "42"

    gc_check.check(mr)


def test_basic_hash(gc_check):
    mr = StrictMiniRacer()
    assert mr.execute("{}") == {}

    gc_check.check(mr)


def test_basic_array(gc_check):
    mr = StrictMiniRacer()
    assert mr.execute("[1, 2, 3]") == [1, 2, 3]

    gc_check.check(mr)


def test_call(gc_check):
    js_func = """var f = function(args) {
        return args.length;
    }"""

    mr = StrictMiniRacer()

    assert mr.eval(js_func) is JSUndefined
    assert mr.call("f", list(range(5))) == 5

    gc_check.check(mr)


def test_message(gc_check):
    mr = StrictMiniRacer()
    with pytest.raises(JSEvalException) as exc_info:
        mr.eval("throw new EvalError('Hello', 'someFile.js', 10);")

    clear_frames(exc_info.tb)
    gc_check.check(mr)
