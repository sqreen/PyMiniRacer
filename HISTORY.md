# History

## 0.11.0 (2024-04-03)

- Added a `MutableMapping` (`dict`-like) interface for all derivatives of JS Objects,
    and a `MutableSequence` (`list`-like) interface for JS Arrays. You can now use
    Pythonic idioms to read and write Object properties and Array elements in Python,
    including recursively (i.e., you can read Objects embedded in other objects, and
    embed your own).

- Added ability to directly call `JSFunction` objects from Python. E.g.,
    `mr.eval("a => a*a")(4)` parses the given number-squaring code into a function,
    returns a handle to that function to Python, calls it with the number `4`, and
    recieves the result of `16`.

- Added a `JSUndefined` Python object to model JavaScript `undefined`. This is needed to
    properly implement the above interface for reading Object and Array elements.
    *Unfortunately, this may present a breaking change for users who assume JavaScript
    `undefined` is modeled as Python `None`.*

- Removed an old optimization for `eval` on simple no-argument function calls (i.e.,
    `myfunc()`). The optimization only delivered about a 17% speedup on no-op calls (and
    helped relatively *less* on calls which actually did work), and for the purpose of
    optimizing repeated calls to the same function, it's now redundant with extracting
    and calling the function from Python, e.g., `mr.eval("myfunc")()`.

- Hardening (meaning "fixing potential but not-yet-seen bugs") related to freeing
    `BinaryValue` instances (which convey data from C++ to Python).

- More hardening related to race conditions on teardown of the `MiniRacer` object in the
    unlikely condition that `eval` operations are still executing on the C++ side, and
    abandoned on the Python side, when Python attempts to garbage collect the
    `MiniRacer` object.

## 0.10.0 (2024-03-31)

- Updated to V8 12.3 from V8 12.2 now that Chromium stable is on 12.3.

- Added Python-side support for JS Promises. You can now return a JS Promise from code
    executed by `MiniRacer.eval`, and PyMiniRacer will convert it to a Python object
    which has a blocking `promise.get()` method, and also supports `await promise` in
    `async` Python functions.

- Added a `setTimeout` and `clearTimeout`. These common functions live in the Web API
    standard, not the ECMAScript standard, and thus don't come with V8, but they're so
    ubiquitious we now ship an implemention with `PyMiniRacer`.

## 0.9.0 (2024-03-30)

- Revamped JS execution model to be out-of-thread. Python/C++ interaction now happens
    via callbacks.

- Consequently, Control+C (`KeyboardInterrupt`) now interrupts JS execution.

- Hardened C++-side thread safety model, resolving potential race conditions introduced
    in v0.8.1 (but not actually reported as happening anywhere).

- Further improved JS exception reporting; exception reports now show the offending code
    where possible.

- Introduced `timeout_sec` parameter to `eval`, `call`, and `execute` to replace the
    `timeout`, which unfortunately uses milliseconds (unlike the Python standard
    library). In the future we may emit deprecation warnings for use of `timeout`.

## 0.8.1 (2024-03-23)

- A series of C++ changes which should not impact the behavior of PyMiniRacer:
- Refactoring how we use V8 by inverting the control flow. Before we had function
    evaluations which ran and drained the message loop. Now we have an always-running
    message loop into which we inject function evaluations. This seems to be the
    preferred way to use V8. This is not expected to cause any behavior changes (but, in
    tests, makes
    [microtask competion](https://developer.mozilla.org/en-US/docs/Web/API/HTML_DOM_API/Microtask_guide)
    more consistent).
- Refactoring the C++ implementation into multiple components to make startup and
    teardown logic more robust.
- Added tests for the existing fast-function-call path.
- Also, simplified Python conversion of C++ evaluation results.

## 0.8.0 (2024-03-18)

- General overhaul of C++ implementation to better adhere to modern best practice. This
    should have no visible impact except for the following notes...
- Exposed the hard memory limit as a context-specific (as opposed to `eval`-specific)
    limit, since that's how it worked all along anyway. The `max_memory` `eval` argument
    still works for backwards compatibility purposes.
- Correct message type of some exceptions to `str` instead of `bytes` (they should all
    be `str` now).
- Added better messages for JS parse errors.
- Added backtraces for more JS errors.
- Added some really basic Python typing.

## 0.7.0 (2024-03-06)

- Update V8 to 12.2
- Drop Python 2 support
- Fix small Python 3.12 issue and add testing for Python 3.9-3.12
- Add aarch64 support for Mac and Linux
- Revamp DLL loading to be compliant with Python 3.9-style resource loading. This may
    present a small breaking change for advanced usage; the `EXTENSION_PATH` and
    `EXTENSION_NAME` module variables, and `MiniRacer.v8_flags` and `MiniRacer.ext`
    class variable have all been removed.
- Add support for the [ECMAScript internalization API](https://v8.dev/docs/i18n) and
    thus [the ECMA `Intl` API](https://tc39.es/ecma402/)
- Use [fast startup snapshots](https://v8.dev/blog/custom-startup-snapshots)
- Switch from setuptools to Hatch
- Switch from tox to Hatch
- Switch from flake8 and isort to Hatch's wrapper of Ruff
- Switch from Sphinx to mkdocs (and hatch-mkdocs)
- Switch from unittest to pytest
- Add ARCHITECTURE.md and lots of code comments

## 0.6.0 (2020-04-20)

- Update V8 to 8.9
- Optimize function calls without arguments
- Switch V8 to single threaded mode to avoid crashes after fork
- Switch to strict mode by default
- Revamp documentation

## 0.5.0 (2020-02-25)

- Update V8 to 8.8

## 0.4.0 (2020-09-22)

- Universal wheels for Linux, Mac and Windows
- Fallback to source package for Alpine Linux

## 0.3.0 (2020-06-29)

- Introduce a strict mode
- Fix array conversion when size changes dynamically (CVE-2020-25489)

## 0.2.0 (2020-03-11)

- Support for Alpine Linux
- Avoid pip private modules in setup.py

## 0.2.0b1 (2020-01-09)

- Support for Windows 64 bits
- Support for Python 3.8
- Upgrade V8 to 7.8
- Support soft memory limits

## 0.1.18 (2019-01-04)

- Support memory and time limits

## 0.1.17 (2018-19-12)

- Upgrade libv8
- Fix a memory leak

## 0.1.16 (2018-07-11)

- Add wheel for Python without PyMalloc

## 0.1.15 (2018-06-18)

- Add wheel for Python 3.7

## 0.1.14 (2018-05-25)

- Add support for pip 10
- Update package metadata

## 0.1.13 (2018-03-15)

- Add heap_stats function
- Fix issue with returned strings containing null bytes

## 0.1.12 (2018-17-04)

- Remove dependency to enum

## 0.1.11 (2017-07-11)

- Add compatibility for centos6

## 0.1.10 (2017-03-31)

- Add the possibility to pass a custom JSON encoder in call.

## 0.1.9 (2017-03-24)

- Fix the compilation for Ubuntu 12.04 and glibc \< 2.17.

## 0.1.8 (2017-03-02)

- Update targets build for better compatibility with old Mac OS X and linux platforms.

## 0.1.7 (2016-10-04)

- Improve general performances of the JS execution.
- Add the possibility to build a different version of V8 (for example with debug
    symbols).
- Fix a conflict that could happens between statically linked libraries and dynamic
    ones.

## 0.1.6 (2016-08-12)

- Add error message when py_mini_racer sdist fails to build asking to update pip in
    order to download the pre-compiled wheel instead of the source distribution.

## 0.1.5 (2016-08-04)

- Build py_mini_racer against a static Python. When built against a shared library
    python, it doesn't work with a static Python.

## 0.1.4 (2016-08-04)

- Ensure JSEvalException message is converted to unicode

## 0.1.3 (2016-08-04)

- Fix extension loading for python3
- Add a make target for building distributions (sdist + wheels)
- Fix eval conversion for python 3

## 0.1.2 (2016-08-03)

- Fix date support
- Fix Dockerfile for generating python3 wheels

## 0.1.1 (2016-08-02)

- Fix sdist distribution.

## 0.1.0 (2016-08-01)

- First release on PyPI.
