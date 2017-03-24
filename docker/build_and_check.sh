#!/bin/bash
set -e
set -x

# Build the wheel
python setup.py bdist_wheel

# Create a virtualenv
virtualenv /tmp/venv/

# Install py_mini_racer
/tmp/venv/bin/pip install --find-links=./dist/ py_mini_racer

cd /tmp

# Run basic tests
/tmp/venv/bin/python -c "from py_mini_racer import py_mini_racer; mr = py_mini_racer.MiniRacer(); mr.eval(\"'a string'\")"

# Run the tests
/tmp/venv/bin/pip install -r /code/requirements/test.txt
cp -Rv /code/tests /tmp/
/tmp/venv/bin/py.test /tmp/tests

ls /code/dist
