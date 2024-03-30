""" Test .eval() method """

from contextlib import contextmanager
from time import sleep, time

import pytest
from py_mini_racer import (
    JSEvalException,
    JSOOMException,
    JSParseException,
    JSSymbol,
    JSTimeoutException,
    MiniRacer,
)


def test_invalid():
    mr = MiniRacer()

    with pytest.raises(JSEvalException) as exc_info:
        mr.eval("invalid")

    assert (
        exc_info.value.args[0]
        == """\
<anonymous>:1: ReferenceError: invalid is not defined
invalid
^

ReferenceError: invalid is not defined
    at <anonymous>:1:1
"""
    )


def test_eval():
    mr = MiniRacer()
    assert mr.eval("42") == 42


@contextmanager
def expect_calls(mr, *, full=0, fast=0):
    full_before = mr._full_eval_call_count()  # noqa: SLF001
    fast_before = mr._function_eval_call_count()  # noqa: SLF001

    yield

    full_after = mr._full_eval_call_count()  # noqa: SLF001
    fast_after = mr._function_eval_call_count()  # noqa: SLF001

    assert full_after - full_before == full
    assert fast_after - fast_before == fast


def test_blank():
    mr = MiniRacer()
    with expect_calls(mr, full=3):
        assert mr.eval("") is None
        assert mr.eval(" ") is None
        assert mr.eval("\t") is None


def test_global():
    mr = MiniRacer()
    mr.eval("var xabc = 22;")
    assert mr.eval("xabc") == 22


def test_fun():
    mr = MiniRacer()
    mr.eval("var x = function(y) {return y+1;}")

    assert mr.eval("x(1)") == 2
    assert mr.eval("x(10)") == 11
    assert mr.eval("x(100)") == 101


def test_multiple_ctx():
    c1 = MiniRacer()
    c2 = MiniRacer()
    c3 = MiniRacer()

    c1.eval("var x = 1")
    c2.eval("var x = 2")
    c3.eval("var x = 3")
    assert c1.eval("(x)") == 1
    assert c2.eval("(x)") == 2
    assert c3.eval("(x)") == 3


def test_exception_thrown():
    context = MiniRacer()

    js_source = "var f = function() {throw new Error('blah')};"

    context.eval(js_source)

    # Add extra junk (+'') to avoid our fast function call path which produces a
    # slightly different traceback:
    with pytest.raises(JSEvalException) as exc_info:
        context.eval("f()+''")

    assert (
        exc_info.value.args[0]
        == """\
<anonymous>:1: Error: blah
var f = function() {throw new Error('blah')};
                    ^

Error: blah
    at f (<anonymous>:1:27)
    at <anonymous>:1:1
"""
    )


def test_fast_call_exception_thrown():
    mr = MiniRacer()

    with expect_calls(mr, full=1, fast=1):
        js_source = "var f = function() {throw new Error('blah')};"

        mr.eval(js_source)

        # This should go into our fast function call route
        with pytest.raises(JSEvalException) as exc_info:
            mr.eval("f()")

        assert (
            exc_info.value.args[0]
            == """\
<anonymous>:1: Error: blah
var f = function() {throw new Error('blah')};
                    ^

Error: blah
    at f (<anonymous>:1:27)
"""
        )


def test_string_thrown():
    mr = MiniRacer()

    js_source = "var f = function() {throw 'blah'};"

    with expect_calls(mr, full=1, fast=1):
        mr.eval(js_source)

        with pytest.raises(JSEvalException) as exc_info:
            mr.eval("f()")

        # When you throw a plain string (not wrapping it in a `new Error(...)`), you
        # get no backtrace:
        assert (
            exc_info.value.args[0]
            == """\
<anonymous>:1: blah
var f = function() {throw 'blah'};
                    ^
"""
        )


def test_cannot_parse():
    mr = MiniRacer()
    js_source = "var f = function("

    with pytest.raises(JSParseException) as exc_info:
        mr.eval(js_source)

    assert (
        exc_info.value.args[0]
        == """\
<anonymous>:1: SyntaxError: Unexpected end of input
var f = function(
                 ^

SyntaxError: Unexpected end of input
"""
    )


def test_null_byte():
    mr = MiniRacer()

    s = "\x00 my string!"

    # Try return a string including a null byte
    in_val = 'var str = "' + s + '"; str;'
    result = mr.eval(in_val)
    assert result == s


def test_timeout():
    timeout = 0.1
    start_time = time()

    mr = MiniRacer()
    with pytest.raises(JSTimeoutException) as exc_info:
        mr.eval("while(1) { }", timeout_sec=timeout)

    duration = time() - start_time
    # Make sure it timed out on time, and allow a giant leeway (because aarch64
    # emulation tests are surprisingly slow!)
    assert timeout <= duration <= timeout + 5

    assert exc_info.value.args[0] == "JavaScript was terminated by timeout"


def test_timeout_ms():
    # Same as above but with the deprecated timeout millisecond argument
    timeout = 0.1
    start_time = time()

    mr = MiniRacer()
    with pytest.raises(JSTimeoutException) as exc_info:
        mr.eval("while(1) { }", timeout=int(timeout * 1000))

    duration = time() - start_time
    # Make sure it timed out on time, and allow a giant leeway (because aarch64
    # emulation tests are surprisingly slow!)
    assert timeout <= duration <= timeout + 5

    assert exc_info.value.args[0] == "JavaScript was terminated by timeout"


def test_max_memory_soft():
    mr = MiniRacer()
    mr.set_soft_memory_limit(100000000)
    mr.set_hard_memory_limit(100000000)
    with pytest.raises(JSOOMException) as exc_info:
        mr.eval(
            """\
let s = 1000;
var a = new Array(s);
a.fill(0);
while(true) {
    s *= 1.1;
    let n = new Array(Math.floor(s));
    n.fill(0);
    a = a.concat(n);
}
"""
        )

    assert mr.was_soft_memory_limit_reached()
    assert mr.was_hard_memory_limit_reached()
    assert exc_info.value.args[0] == "JavaScript memory limit reached"


def test_max_memory_hard():
    mr = MiniRacer()
    mr.set_hard_memory_limit(100000000)
    with pytest.raises(JSOOMException) as exc_info:
        mr.eval(
            """\
let s = 1000;
var a = new Array(s);
a.fill(0);
while(true) {
    s *= 1.1;
    let n = new Array(Math.floor(s));
    n.fill(0);
    a = a.concat(n);
}"""
        )

    assert not mr.was_soft_memory_limit_reached()
    assert mr.was_hard_memory_limit_reached()
    assert exc_info.value.args[0] == "JavaScript memory limit reached"


def test_max_memory_hard_eval_arg():
    # Same as above but passing the argument into the eval method (which is a
    # deprecated thing to do because the parameter is really affine to the
    # MiniRacer object)
    mr = MiniRacer()
    with pytest.raises(JSOOMException) as exc_info:
        mr.eval(
            """\
let s = 1000;
var a = new Array(s);
a.fill(0);
while(true) {
    s *= 1.1;
    let n = new Array(Math.floor(s));
    n.fill(0);
    a = a.concat(n);
}""",
            max_memory=200000000,
        )

    assert exc_info.value.args[0] == "JavaScript memory limit reached"


def test_symbol():
    mr = MiniRacer()
    res = mr.eval("Symbol.toPrimitive")
    assert isinstance(res, JSSymbol)


def test_microtask():
    # PyMiniRacer uses V8 microtasks (things, like certain promise callbacks, which run
    # immediately after an evaluation ends).
    # By default, V8 runs any microtasks before it returns control to PyMiniRacer.
    # Let's test that they actually work.
    # PyMiniRacer does not expose the web standard `window.queueMicrotask` (because it
    # does not expose a `window` to begin with). We can, however, trigger a microtask
    # by triggering one as a side effect of a `then` on a resolved promise:
    mr = MiniRacer()
    assert not mr.eval(
        """
let p = Promise.resolve();

var done = false;

p.then(() => {done = true});

done
"""
    )
    assert mr.eval("done")


def test_polling():
    mr = MiniRacer()
    assert not mr.eval(
        """
var done = false;
setTimeout(() => { done = true; }, 1000);
done
"""
    )
    assert not mr.eval("done")
    start = time()
    # Give the 1-second wait 10 seconds to finish. (Emulated aarch64 tests are
    # surprisingly slow!)
    while time() - start < 10 and not mr.eval("done"):
        sleep(0.1)
    assert mr.eval("done")


def test_settimeout():
    mr = MiniRacer()
    mr.eval(
        """
var results = [];
let a = setTimeout(() => { results.push("a"); }, 2000);
let b = setTimeout(() => { results.push("b"); }, 3000);
let c = setTimeout(() => { results.push("c"); }, 1000);
let d = setTimeout(() => { results.push("d"); }, 4000);
clearTimeout(b)
"""
    )
    start = time()
    # Give the 1-second wait 10 seconds to finish. (Emulated aarch64 tests are
    # surprisingly slow!)
    while time() - start < 10 and mr.eval("results.length") != 3:
        sleep(0.1)
    assert mr.eval("results.length") == 3
    assert mr.eval("results[0]") == "c"
    assert mr.eval("results[1]") == "a"
    assert mr.eval("results[2]") == "d"


def test_promise_sync():
    mr = MiniRacer()
    promise = mr.eval(
        """
new Promise((res, rej) => setTimeout(() => res(42), 1000)); // 1 s timeout
"""
    )
    start = time()
    # Give the 1-second wait 10 seconds to finish. (Emulated aarch64 tests are
    # surprisingly slow!)
    result = promise.get(timeout=10)
    assert time() - start > 0.5
    assert result == 42


@pytest.mark.asyncio
async def test_promise_async():
    mr = MiniRacer()
    promise = mr.eval(
        """
new Promise((res, rej) => setTimeout(() => res(42), 1000)); // 1 s timeout
"""
    )
    start = time()
    result = await promise
    assert time() - start > 0.5
    # Give the 1-second wait 10 seconds to finish. (Emulated aarch64 tests are
    # surprisingly slow!)
    assert time() - start < 10
    assert result == 42


def test_resolved_promise_sync():
    mr = MiniRacer()
    val = mr.eval("Promise.resolve(6*7)").get()
    assert val == 42


@pytest.mark.asyncio
async def test_resolved_promise_async():
    mr = MiniRacer()
    val = await mr.eval("Promise.resolve(6*7)")
    assert val == 42


def test_fast_call():
    mr = MiniRacer()
    with expect_calls(mr, full=2, fast=1):
        mr.eval("var test = function () { return 42; }")

        # This syntax is optimized and takes another execution path in the extension
        # Note: the fast call optimization only works with functions defined using
        # "var", of "function test() { ... }", which place the definition on the global
        # "this". It doesn't work with functions defined as "let" or "const" because
        # they place the function onto block scope, where our optimization cannot find
        # them.
        assert mr.eval("test()") == 42

        # It looks like a fast call but it is not (it ends with '()' but no identifier)
        # should not fail and do a classical evaluation.
        assert mr.eval("1+test()") == 43
