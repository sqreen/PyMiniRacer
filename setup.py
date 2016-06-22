#!/usr/bin/env python
# -*- coding: utf-8 -*-

import pip

from pip.req import parse_requirements

try:
    from setuptools import setup
except ImportError:
    from distutils.core import setup


with open('README.rst') as readme_file:
    readme = readme_file.read()

with open('HISTORY.rst') as history_file:
    history = history_file.read().replace('.. :changelog:', '')

parsed_requirements = parse_requirements(
    'requirements/prod.txt',
    session=pip.download.PipSession()
)

parsed_setup_requirements = parse_requirements(
    'requirements/setup.txt',
    session=pip.download.PipSession()
)

parsed_test_requirements = parse_requirements(
    'requirements/test.txt',
    session=pip.download.PipSession()
)


requirements = [str(ir.req) for ir in parsed_requirements]
setup_requires = [str(sr.req) for sr in parsed_setup_requirements]
test_requirements = [str(tr.req) for tr in parsed_test_requirements]


setup(
    name='py_mini_racer',
    version='0.1.0',
    description="Minimal, modern embedded V8 for Python.",
    long_description=readme + '\n\n' + history,
    author="Boris FELD",
    author_email='boris@sqreen.io',
    url='https://github.com/sqreen/py_mini_racer',
    packages=[
        'py_mini_racer',
        'py_mini_racer.ffi'
    ],
    package_dir={'py_mini_racer':
                 'py_mini_racer'},
    include_package_data=True,
    setup_requires=setup_requires,
    cffi_modules=["py_mini_racer/ffi/mini_racer_build.py:ffi"],
    install_requires=requirements,
    license="ISCL",
    zip_safe=False,
    keywords='py_mini_racer',
    classifiers=[
        'Development Status :: 2 - Pre-Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: ISC License (ISCL)',
        'Natural Language :: English',
        "Programming Language :: Python :: 2",
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
    ],
    test_suite='tests',
    tests_require=test_requirements
)
