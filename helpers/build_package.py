import os
import sys
import glob

from setuptools.build_meta import (
    get_requires_for_build_wheel,
    get_requires_for_build_sdist,
    prepare_metadata_for_build_wheel,
    build_wheel as setuptools_build_wheel,
    build_sdist as setuptools_build_sdist,
)

from v8_build import build_v8


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    config_settings = config_settings or {}
    # Our wheel is compatible both with python 2 & python 3
    config_settings["--global-option"] = options = ["--python-tag", "py2.py3"]
    # Clean previous version of the lib
    for pattern in ("py_mini_racer/extension/*.so", "py_mini_racer/extension/*.dylib", "py_mini_racer/extension/*.dll"):
        for filename in glob.glob(pattern):
            print("deleting {}".format(filename))
            os.unlink(filename)
    # Build V8
    build_v8("py_mini_racer_shared_lib")
    # Build the wheel
    if os.name == "posix" and sys.platform == "darwin":
        options.extend(["--plat-name", "macosx_10_9_x86_64"])
    elif sys.platform == "win32":
        options.extend(["--plat-name", "win_amd64"])
    else:
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
