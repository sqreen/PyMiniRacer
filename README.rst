===============================
Python Mini Racer
===============================

.. image:: https://img.shields.io/pypi/v/py_mini_racer.svg
        :target: https://pypi.python.org/pypi/py_mini_racer

.. image:: https://img.shields.io/travis/sqreen/py_mini_racer.svg
        :target: https://travis-ci.org/sqreen/py_mini_racer

.. image:: https://readthedocs.org/projects/py_mini_racer/badge/?version=latest
        :target: https://readthedocs.org/projects/py_mini_racer/?badge=latest
        :alt: Documentation Status


Minimal, modern embedded V8 for Python.

* Free software: ISC license
* Documentation: https://py_mini_racer.readthedocs.org.

Features
--------

* TODO

Credits
---------

Tools used in rendering this package:

*  Cookiecutter_
*  `cookiecutter-pypackage`_

.. _Cookiecutter: https://github.com/audreyr/cookiecutter
.. _`cookiecutter-pypackage`: https://github.com/audreyr/cookiecutter-pypackage

Build
--------

You can build v8 with the command "python setup.py build_v8".

You can also build the ctype extension with "python setup.py build_ext" which automatically build v8.

You can generate a wheel with the command "python setup.py bdist_wheel" which build the extension and v8.

Tests
--------

If you want to run the tests, you need to build V8 first, then launch:

.. code:: bash

    python setup.py test --addopts tests
