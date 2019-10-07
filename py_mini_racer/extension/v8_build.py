# -*- coding: utf-8 -*-
import errno

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

V8_VERSION = "6.7.288.46"


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

def gen_makefiles(path):
    opts = {
        'is_component_build': 'false',
        'v8_monolithic': 'true',
        'use_gold': 'false',
        'use_allocator_shim': 'false',
        'is_debug': 'false',
        'symbol_level': '0',
        'strip_debug_info': 'true',
        'v8_use_external_startup_data': 'false',
        'v8_enable_i18n_support': 'false',
        'v8_static_library': 'true',
        'v8_experimental_extra_library_files': '[]',
        'v8_extra_library_files': '[]'
    }
    joined_opts = ' '.join('{}={}'.format(a, b) for (a, b) in opts.items())

    with chdir(path):
        call('./tools/dev/v8gen.py -vv x64.release -- ' + joined_opts)

def make(path, cmd_prefix):
    """ Create a release of v8
    """
    with chdir(path):
        call("{} ninja -vv -C out.gn/x64.release -j {} v8_monolith"
             .format(cmd_prefix, 4))

def patch_v8():
    """ Apply patch on v8
    """
    path = local_path('v8/v8')
    patches_paths = PATCHES_PATH
    apply_patches(path, patches_paths)


def symlink_force(target, link_name):
    try:
        os.symlink(target, link_name)
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove(link_name)
            os.symlink(target, link_name)
        else:
            raise e


def fixup_libtinfo(dir):
    dirs = ['/lib64', '/usr/lib64', '/lib', '/usr/lib']

    v5_locs = ["{}/libtinfo.so.5".format(d) for d in dirs]
    found_v5 = next((f for f in v5_locs if os.path.isfile(f)), None)
    if found_v5 and os.stat(found_v5).st_size > 100:
        return ''

    v6_locs = ["{}/libtinfo.so.6".format(d) for d in dirs]
    found_v6 = next((f for f in v6_locs if os.path.isfile(f)), None)
    if not found_v6:
        return ''

    symlink_force(found_v6, join(dir, 'libtinfo.so.5'))
    return "LD_LIBRARY_PATH='{}:{}'"\
        .format(dir, os.getenv('LD_LIBRARY_PATH', ''))


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


def build_v8():
    ensure_v8_src()
    patch_v8()
    checkout_path = local_path('v8/v8')
    cmd_prefix = fixup_libtinfo(checkout_path)
    gen_makefiles(checkout_path)
    make(checkout_path, cmd_prefix)


if __name__ == '__main__':
    build_v8()
