[![PyPI status indicator](https://img.shields.io/pypi/v/mini_racer.svg)](https://pypi.python.org/pypi/mini_racer)
[![Github workflow status indicator](https://github.com/bpcreech/PyMiniRacer/actions/workflows/build.yml/badge.svg)](https://github.com/bpcreech/PyMiniRacer/actions/workflows/build.yml)
[![ISC License](https://img.shields.io/badge/License-ISC-blue.svg)](https://opensource.org/licenses/ISC)

Minimal, modern embedded V8 for Python.

![MiniRacer logo: a V8 with a very snakey 8](py_mini_racer.png)

[Full documentation](https://bpcreech.com/PyMiniRacer/).

## Features

- Latest ECMAScript support
- Web Assembly support
- Unicode support
- Thread safe
- Re-usable contexts

MiniRacer can be easily used by Django or Flask projects to minify assets, run babel or
WASM modules.

## New home! (As of March 2024)

PyMiniRacer was created by [Sqreen](https://github.com/sqreen), and originally lived at
<https://github.com/sqreen/PyMiniRacer> with the PyPI package
[`py-mini-racer`](https://pypi.org/project/py-mini-racer/).

As of March 2024, after a few years without updates, [I](https://bpcreech.com) have
reached out to the original Sqreen team. We agreed that I should fork PyMiniRacer,
giving it a new home at <https://github.com/bpcreech/PyMiniRacer> with a new PyPI
package [`mini-racer`](https://pypi.org/project/mini-racer/) (*note: no `py-`*). It now
has [a new version](https://bpcreech.com/PyMiniRacer/history/#070-2024-03-06) for the
first time since 2021!

## Examples

MiniRacer is straightforward to use:

```sh
    $ pip install mini-racer
```

and then:

```python
    $ python3
    >>> from py_mini_racer import MiniRacer
    >>> ctx = MiniRacer()
    >>> ctx.eval("1+1")
    2
    >>> ctx.eval("var x = {company: 'Sqreen'}; x.company")
    'Sqreen'
    >>> print(ctx.eval("'❤'"))
    ❤
    >>> ctx.eval("var fun = () => ({ foo: 1 });")
```

Variables are kept inside of a context:

```python
    >>> ctx.eval("x.company")
    'Sqreen'
```

You can evaluate whole scripts within JavaScript, or define and return JavaScript
function objects and call them from Python (*new in v0.11.0*):

```python
    >>> square = ctx.eval("a => a*a")
    >>> square(4)
    16
```

JavaScript Objects and Arrays are modeled in Python as dictionaries and lists (or, more
precisely,
[`MutableMapping`](https://docs.python.org/3/library/collections.abc.html#collections.abc.MutableMapping)
and
[`MutableSequence`](https://docs.python.org/3/library/collections.abc.html#collections.abc.MutableSequence)
instances), respectively (*new in v0.11.0*):

```python
    >>> obj = ctx.eval("var obj = {'foo': 'bar'}; obj")
    >>> obj["foo"]
    'bar'
    >>> list(obj.keys())
    ['foo']
    >>> arr = ctx.eval("var arr = ['a', 'b']; arr")
    >>> arr[1]
    'b'
    >>> 'a' in arr
    True
    >>> arr.append(obj)
    >>> ctx.eval("JSON.stringify(arr)")
    '["a","b",{"foo":"bar"}]'
```

Meanwhile, `call` uses JSON to transfer data between JavaScript and Python, and converts
data in bulk:

```python
    >>> ctx.call("fun")
    {'foo': 1}
```

Composite values are serialized using JSON. Use a custom JSON encoder when sending
non-JSON encodable parameters:

```python
    import json

    from datetime import datetime

    class CustomEncoder(json.JSONEncoder):

            def default(self, obj):
                if isinstance(obj, datetime):
                    return obj.isoformat()

                return json.JSONEncoder.default(self, obj)
```

```python
    >>> ctx.eval("var f = function(args) { return args; }")
    >>> ctx.call("f", datetime.now(), encoder=CustomEncoder)
    '2017-03-31T16:51:02.474118'
```

MiniRacer is ES6 capable:

```python
    >>> ctx.execute("[1,2,3].includes(5)")
    False
```

MiniRacer supports asynchronous execution using JS `Promise` instances (*new in
v0.10.0*):

```python
    >>> promise = ctx.eval(
    ...     "new Promise((res, rej) => setTimeout(() => res(42), 10000))")
    >>> promise.get()  # blocks for 10 seconds, and then:
    42
```

You can use JS `Promise` instances with Python `async` (*new in v0.10.0*):

```python
    >>> import asyncio
    >>> async def demo():
    ...     promise = ctx.eval(
    ...         "new Promise((res, rej) => setTimeout(() => res(42), 10000))")
    ...     return await promise
    ... 
    >>> asyncio.run(demo())  # blocks for 10 seconds, and then:
    42
```

JavaScript `null` and `undefined` are modeled in Python as `None` and `JSUndefined`,
respectively:

```python
    >>> list(ctx.eval("[undefined, null]"))
    [JSUndefined, None]
```

MiniRacer supports [the ECMA `Intl` API](https://tc39.es/ecma402/):

```python
    # Indonesian dates!
    >>> ctx.eval('Intl.DateTimeFormat(["ban", "id"]).format(new Date())')
    '16/3/2024'
```

V8 heap information can be retrieved:

```python
    >>> ctx.heap_stats()
    {'total_physical_size': 1613896,
     'used_heap_size': 1512520,
     'total_heap_size': 3997696,
     'total_heap_size_executable': 3145728,
     'heap_size_limit': 1501560832}
```

A WASM example is available in the
[`tests`](https://github.com/bpcreech/PyMiniRacer/blob/master/tests/test_wasm.py).

## Compatibility

PyMiniRacer is compatible with Python 3.8-3.12 and is based on `ctypes`.

PyMiniRacer is distributed using [wheels](https://pythonwheels.com/) on
[PyPI](https://pypi.org/). The wheels are intended to provide compatibility with:

| OS                              | x86_64 | aarch64 |
| ------------------------------- | ------ | ------- |
| macOS ≥ 10.9                    | ✓      | ✓       |
| Windows ≥ 10                    | ✓      | ✖       |
| Ubuntu ≥ 20.04                  | ✓      | ✓       |
| Debian ≥ 11                     | ✓      | ✓       |
| RHEL ≥ 8                        | ✓      | ✓       |
| other Linuxes with glibc ≥ 2.31 | ✓      | ✓       |
| Alpine ≥ 3.19                   | ✓      | ✓       |
| other Linux with musl ≥ 1.2     | ✓      | ✓       |

If you have a up-to-date pip and it doesn't use a wheel, you might have an environment
for which no wheel is built. Please open an issue.

## Developing and releasing PyMiniRacer

See [the contribution guide](CONTRIBUTING.md).

## Credits

Built with love by [Sqreen](https://www.sqreen.com).

PyMiniRacer launch was described in
[`this blog post`](https://web.archive.org/web/20230526172627/https://blog.sqreen.com/embedding-javascript-into-python/).

PyMiniRacer is inspired by [mini_racer](https://github.com/SamSaffron/mini_racer), built
for the Ruby world by Sam Saffron.

In 2024, PyMiniRacer was revived, and adopted by [Ben Creech](https://bpcreech.com).
Upon discussion with the original Sqreen authors, we decided to re-launch PyMiniRacer as
a fork under <https://github.com/bpcreech/PyMiniRacer> and
<https://pypi.org/project/mini-racer/>.
