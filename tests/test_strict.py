from gc import collect
from traceback import clear_frames

import pytest
from py_mini_racer import JSEvalException, JSUndefined, StrictMiniRacer


def test_basic_int():
    mr = StrictMiniRacer()
    assert mr.execute("42") == 42

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001


def test_basic_string():
    mr = StrictMiniRacer()
    assert mr.execute('"42"') == "42"

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001


def test_basic_hash():
    mr = StrictMiniRacer()
    assert mr.execute("{}") == {}

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001


def test_basic_array():
    mr = StrictMiniRacer()
    assert mr.execute("[1, 2, 3]") == [1, 2, 3]

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001


def test_call():
    js_func = """var f = function(args) {
        return args.length;
    }"""

    mr = StrictMiniRacer()

    assert mr.eval(js_func) is JSUndefined
    assert mr.call("f", list(range(5))) == 5

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001


def test_message():
    mr = StrictMiniRacer()
    with pytest.raises(JSEvalException) as exc_info:
        mr.eval("throw new EvalError('Hello', 'someFile.js', 10);")

    clear_frames(exc_info.tb)
    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001
