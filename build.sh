#!/bin/bash

set -e
set -x

git submodule init
git submodule update

python py_mini_racer/ffi/v8_build.py

python setup.py bdist_wheel


