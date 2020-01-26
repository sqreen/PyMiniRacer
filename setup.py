#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys
import io
import pkg_resources
import traceback

from itertools import chain
from subprocess import check_call, check_output, STDOUT
from os.path import dirname, abspath, join, isfile, isdir, basename

from distutils.file_util import copy_file

try:
    from setuptools import setup, Extension, Command
    from setuptools.command.build_ext import build_ext
    from setuptools.command.install import install
except ImportError:
    from distutils.core import setup, Extension, Command
    from distutils.command.build_ext import build_ext
    from distutils.command.install import install

from py_mini_racer import __version__
from py_mini_racer.extension.v8_build import build_v8, \
    local_path as local_path_v8


with io.open('README.rst', 'r', encoding='utf8') as readme_file:
    readme = readme_file.read()

    # Convert local image links by their github equivalent
    readme = readme.replace(".. image:: data/",
                            ".. image:: https://github.com/sqreen/PyMiniRacer/raw/master/data/")

with io.open('HISTORY.rst', 'r', encoding='utf8') as history_file:
    history = history_file.read().replace('.. :changelog:', '')


def check_python_version():
    """ Check that the python executable is Python 2.7.
    """
    output = check_output(['python', '--version'], stderr=STDOUT)
    if not output.strip().decode().startswith('Python 2.7'):
        msg = """py_mini_racer cannot build V8 in the current configuration.
        The V8 build system requires the python executable to be Python 2.7.
        See also: https://github.com/sqreen/PyMiniRacer#build"""
        raise Exception(msg)

def lib_filename(name, static=False):
    if os.name == "posix" and sys.platform == "darwin":
        prefix = "lib"
        if static:
            ext = ".a"
        else:
            ext = ".dylib"
    elif sys.platform == "win32":
        prefix = ""
        if static:
            ext = ".lib"
        else:
            ext = ".dll"
    else:
        prefix = "lib"
        if static:
            ext = ".a"
        else:
            ext = ".so"
    return prefix + name + ext


class V8Extension(Extension):

    def __init__(self, dest_module, target, lib, sources=[], **kwa):
        Extension.__init__(self, dest_module, sources=sources, **kwa)
        self.target = target
        self.lib = lib


class MiniRacerBuildExt(build_ext):

    def get_ext_filename(self, name):
        # XXX the filename is the same for all platforms for now
        ext = ".so"
        parts = name.split(".")
        last = parts.pop(-1) + ext
        return os.path.join(*(parts + [last]))

    def build_extensions(self):
        self.debug = True
        try:
            for ext in self.extensions:
                src = os.path.join(ext.lib)
                dest = self.get_ext_fullpath(ext.name)
                if not os.path.isfile(dest) and not os.path.isfile(src):
                    check_python_version()
                    print("building {}".format(ext.target))
                    build_v8(ext.target)
                if not os.path.isfile(dest):
                    dest_dir = os.path.dirname(dest)
                    if not os.path.exists(dest_dir):
                        os.makedirs(dest_dir)
                    copy_file(src, dest)
                else:
                    print("extension was already built")
        except Exception as e:
            traceback.print_exc()

            # Alter message
            err_msg = """py_mini_racer failed to build, ensure you have an up-to-date pip (>= 8.1) to use the wheel instead
            To update pip: 'pip install -U pip'
            See also: https://github.com/sqreen/PyMiniRacer#binary-builds-availability

            Original error: %s"""

            raise Exception(err_msg % repr(e))


PY_MINI_RACER_EXTENSION = V8Extension(
    "py_mini_racer._v8",
    "py_mini_racer_shared_lib",
    local_path_v8(os.path.join("out", lib_filename("mini_racer")))
)


setup(
    name='py_mini_racer',
    version=__version__,
    description="Minimal, modern embedded V8 for Python.",
    long_description=readme + '\n\n' + history,
    author='Sqreen',
    author_email='hey@sqreen.io',
    url='https://github.com/sqreen/PyMiniRacer',
    packages=[
        'py_mini_racer',
        'py_mini_racer.extension'
    ],
    ext_modules=[PY_MINI_RACER_EXTENSION],
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
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
    ],
    test_suite='tests',
    tests_require=[
        "tox",
        "six",
        "pytest",
    ],
    cmdclass={
        "build_ext": MiniRacerBuildExt,
    }
)
