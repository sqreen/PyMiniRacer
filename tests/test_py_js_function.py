"""Test Python functions exposed as JS."""

from asyncio import gather
from asyncio import run as asyncio_run
from asyncio import sleep as asyncio_sleep
from time import time

import pytest
from py_mini_racer import (
    JSPromise,
    JSPromiseError,
    MiniRacer,
)


def test_basic(gc_check):
    mr = MiniRacer()

    data = []

    async def append(*args):
        data.append(args)
        return "foobar"

    async def run():
        async with mr.wrap_py_function(append) as jsfunc:
            # "Install" our JS function on the global "this" object:
            mr.eval("x => this.func = x")(jsfunc)

            assert await mr.eval("this.func(42)") == "foobar"
            assert await mr.eval('this.func("blah")') == "foobar"

            assert data == [(42,), ("blah",)]

    for _ in range(100):
        data[:] = []
        asyncio_run(run())

    gc_check.check(mr)


def test_exception(gc_check):
    # Test a Python callback which raises exceptions
    mr = MiniRacer()

    data = []

    async def append(*args):
        del args
        boo = "boo"
        raise RuntimeError(boo)

    async def run():
        async with mr.wrap_py_function(append) as jsfunc:
            # "Install" our JS function on the global "this" object:
            mr.eval("x => this.func = x")(jsfunc)

            with pytest.raises(JSPromiseError) as exc_info:
                await mr.eval("this.func(42)")

            assert exc_info.value.args[0].startswith(
                """\
JavaScript rejected promise with reason: Error: Error running Python function:
Traceback (most recent call last):
"""
            )

            assert exc_info.value.args[0].endswith(
                """\

    at <anonymous>:1:6
"""
            )

    for _ in range(100):
        data[:] = []
        asyncio_run(run())

    gc_check.check(mr)


def test_slow(gc_check):
    # Test a Python callback which runs slowly, but is faster in parallel.
    mr = MiniRacer()

    data = []

    async def append(*args):
        await asyncio_sleep(1)
        data.append(args)
        return "foobar"

    async def run():
        async with mr.wrap_py_function(append) as jsfunc:
            # "Install" our JS function on the global "this" object:
            mr.eval("x => this.func = x")(jsfunc)

            pending = [mr.eval("this.func(42)") for _ in range(100)]

            assert await gather(*pending) == ["foobar"] * 100

            assert (
                data
                == [
                    (42,),
                ]
                * 100
            )

    start = time()
    asyncio_run(run())
    # The above should run in just over a second.
    # Just verify it didn't take 100 seconds (i.e., that things didn't execute
    # sequentially):
    assert time() - start < 10

    gc_check.check(mr)


def test_call_on_exit(gc_check):
    """Checks that calls from JS made while we're trying to tear down the wrapped
    function are ignored and don't break anything."""

    mr = MiniRacer()

    data = []

    async def append(*args):
        data.append(args)
        return "foobar"

    async def run():
        async with mr.wrap_py_function(append) as jsfunc:
            # "Install" our JS function on the global "this" object:
            mr.eval("x => this.func = x")(jsfunc)

            # Note: we don't await the promise, meaning we just start a call and never
            # finish it:
            assert isinstance(mr.eval("this.func(42)"), JSPromise)

            # Generally (subject to race conditions) at this point the
            # callback initiated by the above this.func(42) will be half-received:
            # _Context.wrap_py_function.on_called will have gotten the callback, and
            # will have told asyncio to deal with it on the loop thread. We will
            # generally *not* have yet processed the call.
            # After this line, we start tearing down the mr.wrap_py_function context
            # manager, which entails stopping the call processor.
            # Let's make sure we don't fall over ourselves (it's fair to either process
            # the last straggling calls, or ignore them, but make sure we don't hang).

    for _ in range(100):
        data[:] = []
        asyncio_run(run())

    gc_check.check(mr)
