#!/usr/bin/env python
# -*- coding: utf-8 -*-

import pip
import shlex

from os import mkdir
from pip.req import parse_requirements

try:
    from setuptools import setup, Extension
    from setuptools.command.build_ext import build_ext
except ImportError:
    from distutils.core import setup, Extension
    from distutils.command.build_ext import build_ext

from distutils.spawn import spawn


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


COMPILE_CMD = "clang++ -c src/mini_racer_extension.cc -I py_mini_racer/ffi/v8/v8 \
        -D_XOPEN_SOURCE -D_DARWIN_C_SOURCE                     \
        -D_DARWIN_UNLIMITED_SELECT -D_REENTRANT                \
        -Wall -g -rdynamic -std=c++0x -fpermissive -fno-common \
        -o {output_dir}/mini_racer.o"

LINK_CMD = "clang++ -dynamic -bundle src/mini_racer.o          \
    -stdlib=libstdc++                                          \
    -fstack-protector                                          \
    -Wl,-undefined,dynamic_lookup                              \
    -Wl,-multiply_defined,suppress                             \
    -lobjc -lpthread  -lpthread -ldl -lobjc                    \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_base.a          \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_libbase.a       \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_libplatform.a   \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_nosnapshot.a    \
    -o {ext_path}"


class MiniRacerBuildExt(build_ext):

    def build_extension(self, ext):
        """ Compile manually the py_mini_racer extension, bypass setuptools
        """
        output_dir = self.build_temp
        ext_path = self.get_ext_fullpath(ext.name)
        mkdir(output_dir)
        spawn(shlex.split(COMPILE_CMD.format(output_dir=output_dir)))
        spawn(shlex.split(LINK_CMD.format(ext_path=ext_path)))


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
    ext_modules=[
        Extension('mini_racer', sources=['src/mini_racer_extension.cc']),
    ],
    package_dir={'py_mini_racer':
                 'py_mini_racer'},
    include_package_data=True,
    # package_data={'py_mini_racer': ['mini_racer_extension.bundle']},
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
        "Programming Language :: Python :: 2",
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
    ],
    test_suite='tests',
    tests_require=test_requirements,
    cmdclass={
        'build_ext': MiniRacerBuildExt
    }
)
