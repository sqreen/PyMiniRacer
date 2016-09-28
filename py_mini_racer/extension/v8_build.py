# -*- coding: utf-8 -*-
import logging
import os
import os.path
import subprocess
import multiprocessing

from glob import glob
from os.path import join, dirname, abspath
from contextlib import contextmanager


logging.basicConfig()
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)

V8_VERSION = "5.1.281.67"


def local_path(path):
    """ Return path relative to this file
    """
    current_path = dirname(__file__)
    return abspath(join(current_path, path))


VENDOR_PATH = local_path('../../vendor/depot_tools')
PATCHES_PATH = local_path('../../patches')


def call(cmd):
    LOGGER.debug("Calling: '%s' from working directory %s", cmd, os.getcwd())
    current_env = os.environ
    depot_tools_env = '{}:{}'.format(VENDOR_PATH, os.environ['PATH'])
    current_env['PATH'] = depot_tools_env
    return subprocess.check_call(cmd, shell=True, env=current_env)


@contextmanager
def chdir(new_path, make=False):
    old_path = os.getcwd()

    if make is True:
        try:
            os.mkdir(new_path)
        except OSError:
            pass

    try:
        yield os.chdir(new_path)
    finally:
        os.chdir(old_path)


def ensure_v8_src():
    """ Ensure that v8 src are presents and up-to-date
    """
    path = local_path('v8')

    if not os.path.isdir(path):
        fetch_v8(path)
    else:
        update_v8(path)

    checkout_v8_version(local_path("v8/v8"), V8_VERSION)
    dependencies_sync(path)


def fetch_v8(path):
    """ Fetch v8
    """
    with chdir(abspath(path), make=True):
        call("fetch v8")


def update_v8(path):
    """ Update v8 repository
    """
    with chdir(path):
        call("gclient fetch")


def checkout_v8_version(path, version):
    """ Ensure that we have the right version
    """
    with chdir(path):
        call("git checkout {} -- .".format(version))


def dependencies_sync(path):
    """ Sync v8 build dependencies
    """
    with chdir(path):
        call("gclient sync")


def gyp_defines():
    defines = []

    # Do not use an external snapshot as we don't really care for binary size
    # and so we don't need to ship the blobs
    defines.append('v8_use_external_startup_data=0')

    # Do not use the GPLv3 ld.gold binary on Linux
    defines.append('linux_use_bundled_gold=0')

    return 'GYP_DEFINES="{}"'.format(" ".join(defines))


def make_flags():
    """ Returns compilation flags
    """
    flags = []

    # Activate parallel build process
    flags.append('-j {:d}'.format(multiprocessing.cpu_count()))

    # Disable i18n
    flags.append('i18nsupport=off')

    # Disable werror as this version of v8 is getting difficult to maintain
    # with it on
    flags.append('werror=no')

    flags.append(gyp_defines())

    return ' '.join(flags)


def make(path, flags, target='native'):
    """ Create a release of v8
    """
    with chdir(path):
        call("make {} {}".format(target, flags))


def patch_v8():
    """ Apply patch on v8
    """
    path = local_path('v8/v8')
    patches_paths = PATCHES_PATH
    apply_patches(path, patches_paths)


def apply_patches(path, patches_path):
    with chdir(path):

        if not os.path.isfile('.applied_patches'):
            open('.applied_patches', 'w').close()

        with open('.applied_patches', 'r+') as applied_patches_file:
            applied_patches = set(applied_patches_file.read().splitlines())

            for patch in glob(join(patches_path, '*.patch')):
                if patch not in applied_patches:
                    call("patch -p1 -N < {}".format(patch))

                    applied_patches_file.write(patch + "\n")


def build_v8(target):
    ensure_v8_src()
    patch_v8()
    flags = make_flags()
    make(local_path('v8/v8'), flags, target)


if __name__ == '__main__':
    build_v8('native')
