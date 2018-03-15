===============================
Python Mini Racer
===============================

.. image:: https://img.shields.io/pypi/v/py_mini_racer.svg
        :target: https://pypi.python.org/pypi/py_mini_racer

Minimal, modern embedded V8 for Python.

* Free software: ISC license

.. image:: data/py_mini_racer.png

Features
--------

* Unicode support
* Thread safe
* Re-usable contexts
* Binary object is Python agnostic

MiniRacer can be easily used by Django or Flask projects to minify assets, run
babel or compile CoffeeScript.

Examples
--------

py_mini_racer is straightforward to use:

.. code-block:: python

    >>> from py_mini_racer import py_mini_racer
    >>> ctx = py_mini_racer.MiniRacer()
    >>> ctx.eval('1+1')
    2
    >>> ctx.eval("var x = {company: 'Sqreen'}; x.company")
    u'Sqreen'
    >>> print ctx.eval(u"'\N{HEAVY BLACK HEART}'")
    ❤
    >>> ctx.eval("var fun = () => ({ foo: 1 });")
    >>> ctx.call("fun")
    {u'foo': 1}

Variables are kept inside of a context:

.. code-block:: python

    >>> ctx.eval("x.company")
    u'Sqreen'


You can give directly a custom JSON encoder when sending non-JSON encodable
parameters:

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
    u'2017-03-31T16:51:02.474118'


PyMiniRacer is ES6 capable:

.. code-block:: python

    >>> ctx.eval("[1,2,3].includes(5)")
    False

V8 heap information can be retrieved:

.. code-block:: python

    >>> ctx.heap_stats()
    {u'total_physical_size': 1613896,
     u'used_heap_size': 1512520,
     u'total_heap_size': 3997696,
     u'total_heap_size_executable': 3145728,
     u'heap_size_limit': 1501560832}


Compatibility
-------------

PyMiniRacer is compatible with Python 2 and Python 3. Wheels are generated for python 2.7 and python 3.4 to python 3.6.

Binary builds availability
--------------------------

The PyMiniRacer binary builds have been tested on x86_64 with:

* OSX 10.10
* Ubuntu >= 12.04
* Debian >= 7
* CentOS >= 6

You need pip >= 8.1 to install the wheels - you can check and upgrade yours in
this way:

.. code-block:: bash

    $ pip --version
    $ pip install --upgrade pip

It should work on any Linux with a libc >= 2.12 and a wheel compatible pip (>=
8.1).

If you have a up-to-date pip and it doesn't use a wheel, you might have an environment for which no wheel is built. Please open an issue.

Installation
------------

We built Python wheels (prebuilt binaries) for OSX 64 bits and Linux 64 bits -
most recent distributions. You need pip >= 1.4 and setuptools >= 0.8.

.. code:: bash

    $ pip install py-mini-racer

Build
-----

You can build v8 with the command:

.. code:: bash

    $ python setup.py build_v8

You can also build the ctype extension:

.. code:: bash

    $ python setup.py build_ext

Which automatically builds v8.

You can generate a wheel with the command:

.. code:: bash

    $ python setup.py bdist_wheel

which builds v8, the extension, and generates a wheel.

Notes for building on OSX
'''''''''''''''''''''''''

The legacy Python binary builds (OSX 10.6) need to be downloaded from:
    https://www.python.org/downloads/

They will allow to build a wheel compatible with former OSX versions.

Tests
-----

If you want to run the tests, you need to build V8 first, then launch:

.. code:: bash

    $ python setup.py test --addopts tests

Credits
-------

Built with love by Sqreen_.

.. _Sqreen: https://www.sqreen.io

PyMiniRacer launch was described in `this blog post`_.

.. _`this blog post`: https://blog.sqreen.io/embedding-javascript-into-python/

PyMiniRacer is inspired by mini_racer_, built for the Ruby world by Sam Saffron.

.. _`mini_racer`: https://github.com/SamSaffron/mini_racer

Tools used in rendering this package:

*  Cookiecutter_
*  `cookiecutter-pypackage`_

.. _Cookiecutter: https://github.com/audreyr/cookiecutter
.. _`cookiecutter-pypackage`: https://github.com/audreyr/cookiecutter-pypackage

Todo
----

Export V8 version.
Fix circular structures export.
