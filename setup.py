#!/usr/bin/env python
# -*- coding: utf-8 -*-

import pip
import sys

from os.path import dirname, abspath, join, isfile
from pip.req import parse_requirements

try:
    from setuptools import setup, Extension, Command
    from setuptools.command.build_ext import build_ext
except ImportError:
    from distutils.core import setup, Extension, Command
    from distutils.command.build_ext import build_ext

from py_mini_racer.extension.v8_build import build_v8


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


def local_path(path):
    """ Return path relative to this file
    """
    current_path = dirname(__file__)
    return abspath(join(current_path, path))


V8_LIB_DIRECTORY = local_path('py_mini_racer/extension/v8/v8')
V8_STATIC_LIBRARIES = ['libv8_base.a', 'libv8_libbase.a', 'libv8_libplatform.a',
                       'libv8_nosnapshot.a']


COMPILE_CMD = "clang++ -c py_mini_racer/extension/mini_racer_extension.cc -I {v8_lib_dir} \
        -D_XOPEN_SOURCE -D_DARWIN_C_SOURCE                     \
        -D_DARWIN_UNLIMITED_SELECT -D_REENTRANT                \
        -Wall -g -rdynamic -std=c++0x -fpermissive -fno-common \
        -o {output_dir}/mini_racer.o"

# MAC  =         -D_XOPEN_SOURCE -D_DARWIN_C_SOURCE                     \
#         -D_DARWIN_UNLIMITED_SELECT -D_REENTRANT                \
#         -Wall -std=c++0x

# -g debug

LINK_CMD = "clang++ -dynamic -bundle {output_dir}/mini_racer.o \
    -stdlib=libstdc++                                          \
    -fstack-protector                                          \
    -Wl,-undefined,dynamic_lookup                              \
    -Wl,-multiply_defined,suppress                             \
    -lobjc -lpthread -ldl                    \
    {v8_static_libraries} \
    -o {ext_path}"

# MAC -bundle -lobjc


def is_v8_build():
    """ Check if v8 has been built
    """
    return all(isfile(static_filepath) for static_filepath in get_static_lib_paths())


def get_static_lib_paths():
    """ Return the required static libraries path
    """
    return [join(V8_LIB_DIRECTORY, "out/native/", static_file) for static_file in V8_STATIC_LIBRARIES]


EXTRA_LINK_ARGS = [
    '-ldl',
    '-fstack-protector'
]


# Per platform customizations
if sys.platform[:6] == "darwin":
    EXTRA_LINK_ARGS += ['-stdlib=libstdc++', '-lpthread']
elif sys.platform.startswith('linux'):
    EXTRA_LINK_ARGS += ['-static-libgcc', '-static-libstdc++']


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
        self.run_command('build_v8')

        self.debug = True

        build_ext.build_extension(self, ext)

        # output_dir = self.build_temp
        # ext_path = self.get_ext_fullpath(ext.name)
        # try:
        #     mkdir(output_dir)
        # except OSError:
        #     pass

        # compile_cmd = COMPILE_CMD.format(output_dir=output_dir, v8_lib_dir=V8_LIB_DIRECTORY)
        # spawn(shlex.split(compile_cmd))

        # link_cmd = LINK_CMD.format(ext_path=ext_path,
        #                            v8_static_libraries=" ".join(get_static_lib_paths()),
        #                            output_dir=output_dir)
        # spawn(shlex.split(link_cmd))


class MiniRacerBuildV8(Command):

    description = 'Compile vendored v8'
    user_options = []

    def initialize_options(self):
        """Set default values for options."""
        pass

    def finalize_options(self):
        """Post-process options."""
        pass

    def run(self):
        if not is_v8_build():
            build_v8()

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
        'py_mini_racer.extension'
    ],
    ext_modules=[PY_MINI_RACER_EXTENSION],
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
        'build_ext': MiniRacerBuildExt,
        'build_v8': MiniRacerBuildV8
    }
)
