#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import pip
import sys
import codecs
import pkg_resources

from subprocess import check_call
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

import py_mini_racer
from py_mini_racer.extension.v8_build import build_v8

V8_PATH = os.environ.get("PY_MINI_RACER_V8_PATH")

with codecs.open('README.rst', 'r', encoding='utf8') as readme_file:
    readme = readme_file.read()

    # Convert local image links by their github equivalent
    readme = readme.replace(".. image:: data/",
                            ".. image:: https://github.com/sqreen/PyMiniRacer/raw/master/data/")

with codecs.open('HISTORY.rst', 'r', encoding='utf8') as history_file:
    history = history_file.read().replace('.. :changelog:', '')


def _parse_requirements(filepath):
    pip_version = list(map(int, pkg_resources.get_distribution('pip').version.split('.')[:2]))
    if pip_version >= [10, 0]:
        from pip._internal.download import PipSession
        from pip._internal.req import parse_requirements
        raw = parse_requirements(filepath, session=PipSession())
    elif pip_version >= [6, 0]:
        from pip.download import PipSession
        from pip.req import parse_requirements
        raw = parse_requirements(filepath, session=PipSession())
    else:
        from pip.req import parse_requirements
        raw = parse_requirements(filepath)

    return [str(i.req) for i in raw]


requirements = _parse_requirements('requirements/prod.txt')
setup_requires = _parse_requirements('requirements/setup.txt')
test_requirements = _parse_requirements('requirements/test.txt')


def get_target():
    return os.environ.get('V8_TARGET', 'native')


def local_path(path):
    """ Return path relative to this file
    """
    current_path = dirname(__file__)
    return abspath(join(current_path, path))


V8_LIB_DIRECTORY = local_path('py_mini_racer/extension/v8/v8')
V8_STATIC_LIBRARIES = ['libv8_base.a', 'libv8_libbase.a', 'libv8_libplatform.a',
                       'libv8_nosnapshot.a']


def is_v8_built():
    """ Check if v8 has been built
    """
    if V8_PATH:
        return True
    return all(isfile(static_filepath) for static_filepath in get_raw_static_lib_path())


def is_depot_tools_checkout():
    """ Check if the depot tools submodule has been checkouted
    """
    return isdir(local_path('vendor/depot_tools'))


def libv8_object(object_name):
    """ Return a path for object_name which is OS independent
    """
    filename = join(V8_LIB_DIRECTORY, "out/{}/".format(get_target()),
                    object_name)
    if not isfile(filename):
        filename = join(V8_LIB_DIRECTORY,
                        "out/{}/obj.target/tools/gyp/".format(get_target()),
                        object_name)

    if not isfile(filename):
        filename = join(V8_LIB_DIRECTORY,
                        "out/{}/".format(get_target()),
                        object_name)

    return filename


def get_raw_static_lib_path():
    """ Return the list of the static libraries files ONLY, use
    get_static_lib_paths to get the right compilation flags
    """
    return [libv8_object(static_file) for static_file in V8_STATIC_LIBRARIES]


def get_static_lib_paths():
    """ Return the required static libraries path
    """
    libs = []
    is_linux = sys.platform.startswith('linux')
    if is_linux:
        libs += ['-Wl,--start-group']
    libs += get_raw_static_lib_path()
    if is_linux:
        libs += ['-Wl,--end-group']
    return libs

EXTRA_LINK_ARGS = [
    '-ldl',
    '-fstack-protector',
]


# Per platform customizations
if sys.platform[:6] == "darwin":
    EXTRA_LINK_ARGS += ['-stdlib=libstdc++', '-lpthread']
elif sys.platform.startswith('linux'):
    EXTRA_LINK_ARGS += ['-lrt']


PY_MINI_RACER_EXTENSION = Extension(
    name="py_mini_racer._v8",
    sources=['py_mini_racer/extension/mini_racer_extension.cc'],
    include_dirs=[V8_LIB_DIRECTORY, join(V8_LIB_DIRECTORY, 'include')],
    extra_objects=get_static_lib_paths(),
    extra_compile_args=['-std=c++0x', '-rdynamic', '-fpermissive', '-fno-common'],
    extra_link_args=EXTRA_LINK_ARGS
)


class MiniRacerBuildExt(build_ext):

    def build_extension(self, ext):
        """ Compile manually the py_mini_racer extension, bypass setuptools
        """
        try:
            if not is_v8_built():
                self.run_command('build_v8')

            self.debug = True
            if V8_PATH:
                dest_filename = join(self.build_lib, "py_mini_racer")
                copy_file(V8_PATH, dest_filename, verbose=self.verbose, dry_run=self.dry_run)
            else:
                build_ext.build_extension(self, ext)

        except Exception as e:
            # Alter message
            err_msg = """py_mini_racer failed to build, ensure you have an up-to-date pip (>= 8.1) to use the wheel instead
            To update pip: 'pip install -U pip'
            See also: https://github.com/sqreen/PyMiniRacer#binary-builds-availability

            Original error: %s"""

            raise Exception(err_msg % repr(e))


class MiniRacerBuildV8(Command):

    description = 'Compile vendored v8'
    user_options = [
    ]

    def initialize_options(self):
        """Set default values for options."""

    def finalize_options(self):
        """Post-process options."""
        pass

    def run(self):
        if V8_PATH:
            return

        if not is_depot_tools_checkout():
            print("cloning depot tools submodule")
            # Clone the depot_tools repository, easier than using submodules
            check_call(['git', 'init'])
            check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', 'vendor/depot_tools'])

        if not is_v8_built():
            print("building v8")
            target = os.environ.get('V8_TARGET', 'native')
            build_v8(target)
        else:
            print("v8 is already built")

setup(
    name='py_mini_racer',
    version=py_mini_racer.__version__,
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
    setup_requires=setup_requires,
    install_requires=requirements,
    license="ISCL",
    zip_safe=False,
    keywords='py_mini_racer',
    classifiers=[
        'Development Status :: 2 - Pre-Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: ISC License (ISCL)',
        'Natural Language :: English',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
    ],
    test_suite='tests',
    tests_require=test_requirements,
    cmdclass={
        'build_ext': MiniRacerBuildExt,
        'build_v8': MiniRacerBuildV8,
    }
)
