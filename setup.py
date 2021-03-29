#!/usr/bin/env python
# -*- coding: utf-8 -*-

import io
import os

try:
    from setuptools import setup
except ImportError:
    from distutils.core import setup


ROOT_DIR = os.path.dirname(os.path.abspath(__file__))

about = {}  # type: ignore

# Read the version from the source
with io.open(os.path.join(ROOT_DIR, "py_mini_racer", "__about__.py"), encoding="utf-8") as f:
    exec(f.read(), about)

with io.open(os.path.join(ROOT_DIR, 'README.rst'), 'r', encoding='utf8') as readme_file:
    readme = readme_file.read()

    # Convert local image links by their github equivalent
    readme = readme.replace(".. image:: data/",
                            ".. image:: https://github.com/sqreen/PyMiniRacer/raw/master/data/")

with io.open(os.path.join(ROOT_DIR, 'HISTORY.rst'), 'r', encoding='utf8') as history_file:
    history = history_file.read().replace('.. :changelog:', '')


setup(
    name='py_mini_racer',
    version=about["__version__"],
    description="Minimal, modern embedded V8 for Python.",
    long_description=readme + '\n\n' + history,
    long_description_content_type='text/x-rst',
    author=about["__author__"],
    author_email=about["__email__"],
    url='https://github.com/sqreen/PyMiniRacer',
    packages=[
        'py_mini_racer',
    ],
    package_dir={'py_mini_racer':
                 'py_mini_racer'},
    include_package_data=True,
    license="ISCL",
    zip_safe=False,
    keywords='py_mini_racer',
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: ISC License (ISCL)',
        'Natural Language :: English',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3'
    ],
    test_suite='tests',
    tests_require=[
        "tox",
        "six",
        "pytest",
    ]
)
