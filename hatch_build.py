from os.path import dirname
from sys import path as syspath
from typing import Iterable

from hatchling.builders.config import BuilderConfig
from hatchling.builders.hooks.plugin.interface import BuildHookInterface
from packaging.tags import Tag

# During env initialization the PYTHONPATH doesn't include our helpers, so
# give it some help:
syspath.append(dirname(__file__))
from helpers.v8_build import build_v8, clean_v8, get_platform_tag  # noqa: E402


class V8BuildHook(BuildHookInterface[BuilderConfig]):
    def clean(self, versions: Iterable[str]) -> None:
        del versions

        clean_v8("src/py_mini_racer")

    def initialize(self, version: str, build_data):
        del version

        artifacts = build_v8(out_path="src/py_mini_racer")

        build_data.setdefault("force_include", {}).update(artifacts)

        # From https://stackoverflow.com/questions/76450587/python-wheel-that-includes-shared-library-is-built-as-pure-python-platform-indep
        # We have to tell Hatch that we're building a non-pure-Python wheel, even
        # though there are no extension modules (instead, there is native code in a
        # dynamic-link library in the package):
        build_data["pure_python"] = False

        # Because we aren't building an extension module (just pure Python and a
        # Python-independent DLL), any single wheel we create is broadly compatible
        # with different Python interpreters. Just mark py3 with any ABI:
        tag = Tag("py3", "none", get_platform_tag())

        build_data["tag"] = str(tag)
