# -*- coding: utf-8 -*-"
import argparse
import errno
import sys
import logging
import os
import os.path
import subprocess
import multiprocessing

from glob import glob
from os.path import join, dirname, abspath
from contextlib import contextmanager
from distutils.dir_util import copy_tree


logging.basicConfig()
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)

V8_VERSION = "branch-heads/7.8"


def local_path(path="."):
    """ Return path relative to this file
    """
    current_path = dirname(__file__)
    return abspath(join(current_path, path))


VENDOR_PATH = local_path('../../vendor/depot_tools')
PATCHES_PATH = local_path('../../patches')


def call(cmd):
    LOGGER.debug("Calling: '%s' from working directory %s", cmd, os.getcwd())
    current_env = os.environ
    depot_tools_env = "".join([VENDOR_PATH, os.pathsep, os.environ['PATH']])
    current_env['PATH'] = depot_tools_env
    current_env['DEPOT_TOOLS_WIN_TOOLCHAIN'] = '0'
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


def prepare_workdir():
    link_directories = ["build_overrides", "buildtools", "testing",
                   "third_party", "tools"]
    with chdir(local_path()):
        for item in link_directories:
            if not os.path.exists(item):
                symlink_force(os.path.join("v8", item), item)
        if os.path.islink("build"):
            os.unlink("build")
        copy_tree(os.path.join("v8", "build"), "build")


def ensure_v8_src(revision):
    """ Ensure that v8 src are presents and up-to-date
    """
    path = local_path()

    if not os.path.isfile(local_path(".gclient")):
        fetch_v8(path)
    else:
        update_v8(path)

    checkout_v8_version(local_path("v8"), revision)
    dependencies_sync(path)


def fetch_v8(path):
    """ Fetch v8
    """
    with chdir(abspath(path), make=True):
        call("fetch --nohooks v8")


def update_v8(path):
    """ Update v8 repository
    """
    with chdir(path):
        call("gclient fetch")


def checkout_v8_version(path, revision):
    """ Ensure that we have the right version
    """
    with chdir(path):
        call("git checkout {} -- .".format(revision))


def dependencies_sync(path):
    """ Sync v8 build dependencies
    """
    with chdir(path):
        call("gclient sync")

def run_hooks(path):
    """ Run v8 build hooks
    """
    with chdir(path):
        call("gclient runhooks")

def gen_makefiles(build_path):
    with chdir(local_path()):
        call("gn gen {}".format(local_path(build_path)))

def make(build_path, target, cmd_prefix=""):
    """ Create a release of v8
    """
    with chdir(local_path()):
        call("{} ninja -vv -C {} {}".format(cmd_prefix, local_path(build_path), target))

def patch_v8():
    """ Apply patch on v8
    """
    path = local_path("v8")
    patches_paths = PATCHES_PATH
    apply_patches(path, patches_paths)


def symlink_force(target, link_name):
    LOGGER.debug("Creating symlink to %s on %s", target, link_name)
    if hasattr(os, "symlink"):
        try:
            os.symlink(target, link_name)
        except OSError as e:
            if e.errno == errno.EEXIST:
                os.remove(link_name)
                os.symlink(target, link_name)
            else:
                raise e
    else:
        call(["mklink", "/d", abspath(link_name), abspath(target)])


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

        patched = False
        with open('.applied_patches', 'r+') as applied_patches_file:
            applied_patches = set(applied_patches_file.read().splitlines())

            for patch in glob(join(patches_path, '*.patch')):
                if patch not in applied_patches:
                    call("patch -p1 -N < {}".format(patch))
                    applied_patches_file.write(patch + "\n")
                    patched = True

        if patched:
            run_hooks(path)


def build_v8(target=None, build_path=None, revision=None):
    if target is None:
        target = "v8"
    if build_path is None:
        # Must be relative to local_path()
        build_path = "out"
    if revision is None:
        revision = V8_VERSION
    ensure_v8_src(revision)
    patch_v8()
    prepare_workdir()
    checkout_path = local_path("v8")
    cmd_prefix = fixup_libtinfo(checkout_path)
    gen_makefiles(build_path)
    make(build_path, target, cmd_prefix)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", default="v8", help="Ninja target")
    parser.add_argument("--build-path", default="out", help="Build destination directory (relative to the path)")
    parser.add_argument("--v8-revision", default=V8_VERSION)
    args = parser.parse_args()
    build_v8(target=args.target, build_path=args.build_path, revision=args.v8_revision)
