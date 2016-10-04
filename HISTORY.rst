.. :changelog:

History
-------

UNRELEASED
---------------------

0.1.7 (2016-10-04)
---------------------

* Improve general performances of the JS execution.
* Add the possibility to build a different version of V8 (for example with debug symbols).
* Fix a conflict that could happens between statically linked libraries and dynamic ones.

0.1.6 (2016-08-12)
---------------------

* Add error message when py_mini_racer sdist fails to build asking to update pip in order to download the pre-compiled wheel instead of the source distribution.

0.1.5 (2016-08-04)
---------------------

* Build py_mini_racer against a static Python. When built against a shared library python, it doesn't work with a static Python.

0.1.4 (2016-08-04)
---------------------

* Ensure JSEvalException message is converted to unicode

0.1.3 (2016-08-04)
---------------------

* Fix extension loading for python3
* Add a make target for building distributions (sdist + wheels)
* Fix eval conversion for python 3

0.1.2 (2016-08-03)
---------------------

* Fix date support
* Fix Dockerfile for generating python3 wheels


0.1.1 (2016-08-02)
---------------------

* Fix sdist distribution.


0.1.0 (2016-08-01)
---------------------

* First release on PyPI.
