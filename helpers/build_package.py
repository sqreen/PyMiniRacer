import glob
import os
import shutil
import sys

from setuptools.build_meta import (
    build_sdist as setuptools_build_sdist,
    build_wheel as setuptools_build_wheel,
    get_requires_for_build_sdist,
    get_requires_for_build_wheel,
    prepare_metadata_for_build_wheel,
)
from v8_build import build_v8


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    config_settings = config_settings or {}
    # Our wheel is compatible both with python 2 & python 3
    config_settings["--global-option"] = options = ["--python-tag", "py2.py3"]
    # Clean previous version of the lib
    for pattern in ("py_mini_racer/*.so", "py_mini_racer/*.dylib", "py_mini_racer/*.dll"):
        for filename in glob.glob(pattern):
            print("removing {}".format(filename))
            os.unlink(filename)
    # Build V8
    build_v8("py_mini_racer_shared_lib")
    # Build the wheel
    if os.name == "posix" and sys.platform == "darwin":
        shutil.copyfile("py_mini_racer/extension/out/libmini_racer.dylib", "py_mini_racer/libmini_racer.dylib")
        options.extend(["--plat-name", "macosx_10_10_x86_64"])
    elif sys.platform == "win32":
        shutil.copyfile("py_mini_racer/extension/out/mini_racer.dll", "py_mini_racer/mini_racer.dll")
        options.extend(["--plat-name", "win_amd64"])
    else:
        shutil.copyfile("py_mini_racer/extension/out/libmini_racer.so", "py_mini_racer/libmini_racer.glibc.so")
        options.extend(["--plat-name", "manylinux1_x86_64"])
    return setuptools_build_wheel(wheel_directory, config_settings=config_settings, metadata_directory=metadata_directory)


def build_sdist(sdist_directory, config_settings=None):
    return setuptools_build_sdist(sdist_directory)


__all__ = [
    "get_requires_for_build_wheel",
    "get_requires_for_build_sdist",
    "prepare_metadata_for_build_wheel",
    "build_wheel",
    "build_sdist",
]

if __name__ == "__main__":
    if sys.argv[1] == "wheel":
        build_wheel(sys.argv[2])
    else:
        build_sdist(sys.argv[2])
