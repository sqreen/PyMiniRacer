.. image:: https://img.shields.io/pypi/v/py_mini_racer.svg
        :target: https://pypi.python.org/pypi/py_mini_racer

.. image:: https://dev.azure.com/sqreenci/PyMiniRacer/_apis/build/status/sqreen.PyMiniRacer?branchName=master
        :target: https://dev.azure.com/sqreenci/PyMiniRacer/_build/latest?definitionId=10&branchName=master

.. image:: https://img.shields.io/badge/License-ISC-blue.svg
        :target: https://opensource.org/licenses/ISC

Minimal, modern embedded V8 for Python.

.. image:: data/py_mini_racer.png
        :align: center

Features
--------

* Latest ECMAScript support
* Web Assembly support
* Unicode support
* Thread safe
* Re-usable contexts

MiniRacer can be easily used by Django or Flask projects to minify assets, run
babel or WASM modules.

Examples
--------

MiniRacer is straightforward to use:

.. code-block:: python

    >>> from py_mini_racer import MiniRacer
    >>> ctx = MiniRacer()
    >>> ctx.eval("1+1")
    2
    >>> ctx.eval("var x = {company: 'Sqreen'}; x.company")
    'Sqreen'
    >>> print(ctx.eval("'\N{HEAVY BLACK HEART}'"))
    ❤
    >>> ctx.eval("var fun = () => ({ foo: 1 });")

Variables are kept inside of a context:

.. code-block:: python

    >>> ctx.eval("x.company")
    'Sqreen'


While ``eval`` only supports returning primitive data types such as
strings, ``call`` supports returning composite types such as objects:

.. code-block:: python

    >>> ctx.call("fun")
    {'foo': 1}


Composite values are serialized using JSON.
Use a custom JSON encoder when sending non-JSON encodable parameters:

.. code-block:: python

    import json

    from datetime import datetime

    class CustomEncoder(json.JSONEncoder):

            def default(self, obj):
                if isinstance(obj, datetime):
                    return obj.isoformat()

                return json.JSONEncoder.default(self, obj)


.. code-block:: python

    >>> ctx.eval("var f = function(args) { return args; }")
    >>> ctx.call("f", datetime.now(), encoder=CustomEncoder)
    '2017-03-31T16:51:02.474118'


MiniRacer is ES6 capable:

.. code-block:: python

    >>> ctx.execute("[1,2,3].includes(5)")
    False

V8 heap information can be retrieved:

.. code-block:: python

    >>> ctx.heap_stats()
    {'total_physical_size': 1613896,
     'used_heap_size': 1512520,
     'total_heap_size': 3997696,
     'total_heap_size_executable': 3145728,
     'heap_size_limit': 1501560832}


A WASM example is available in the `tests`_.

.. _`tests`: https://github.com/sqreen/PyMiniRacer/blob/master/tests/test_wasm.py


Compatibility
-------------

PyMiniRacer is compatible with Python 2 & 3 and based on ctypes.

The binary builds have been tested on x86_64 with:

* macOS >= 10.13
* Ubuntu >= 16.04
* Debian >= 9
* CentOS >= 7
* Alpine >= 3.11
* Windows 10

It should work on any Linux with a libc >= 2.12 and a wheel compatible pip (>= 8.1).

If you're running Alpine Linux, you may need to install required dependencies manually using the following command:

.. code-block:: bash

    $ apk add libgcc libstdc++

If you have a up-to-date pip and it doesn't use a wheel, you might have an environment for which no wheel is built. Please open an issue.

Installation
------------

We built Python wheels (prebuilt binaries) for macOS 64 bits, Linux 64 bits and Windows 64 bits.

.. code:: bash

    $ pip install py-mini-racer

Build
-----

**Warning**: building this package from source takes several GB of disk space and takes ~60 minutes.

First check that your current Python executable is version 2.7. This is required
by the V8 build system.

.. code:: bash

    $ python --version
    Python 2.7.16

You can build the extension with the following command:

.. code:: bash

    $ python helpers/v8_build.py

You can generate a wheel for whatever Python version with the command:

.. code:: bash

    $ python3 helpers/build_package.py wheel dist

It will then build V8, the extension, and generates a wheel for your current
Python version. The V8 builds are cached in the ``py_mini_racer/extension/v8/``
directory.

Notes for building on macOS
'''''''''''''''''''''''''''

The legacy Python binary builds (OSX 10.6) need to be downloaded from:
    https://www.python.org/downloads/

They will allow to build a wheel compatible with former OSX versions.

Tests
-----

If you want to run the tests, you need to build the extension first, first install pytest:

.. code-block:: bash

    $ python -m pip install pytest

Then launch:

.. code:: bash

    $ python -m pytest tests

Credits
-------

Built with love by Sqreen_.

.. _Sqreen: https://www.sqreen.com

PyMiniRacer launch was described in `this blog post`_.

.. _`this blog post`: https://blog.sqreen.com/embedding-javascript-into-python/

PyMiniRacer is inspired by mini_racer_, built for the Ruby world by Sam Saffron.

.. _`mini_racer`: https://github.com/SamSaffron/mini_racer

`Cookiecutter-pypackage`_ was used as this package skeleton.

.. _`Cookiecutter-pypackage`: https://github.com/audreyr/cookiecutter-pypackage
