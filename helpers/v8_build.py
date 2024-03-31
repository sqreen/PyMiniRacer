from argparse import ArgumentParser
from errno import EEXIST
from functools import lru_cache
from logging import DEBUG, basicConfig, getLogger
from os import environ, makedirs, pathsep, remove, symlink, unlink
from os.path import abspath, dirname, exists, isdir, isfile
from os.path import join as pathjoin
from platform import machine
from re import match
from shlex import join as shlexjoin
from shutil import copyfile, rmtree
from subprocess import check_call
from sys import executable, platform

from packaging.tags import platform_tags

basicConfig()
LOGGER = getLogger(__name__)
LOGGER.setLevel(DEBUG)
ROOT_DIR = dirname(abspath(__file__))
V8_VERSION = "branch-heads/12.3"


def local_path(path="."):
    """Return path relative to this file."""
    return abspath(pathjoin(ROOT_DIR, path))


@lru_cache(maxsize=None)
def is_win():
    return platform.startswith("win")


@lru_cache(maxsize=None)
def is_linux():
    return platform == "linux"


@lru_cache(maxsize=None)
def is_mac():
    return platform == "darwin"


class UnknownArchError(RuntimeError):
    def __init__(self, arch):
        super().__init__(f"Unknown arch {arch!r}")


@lru_cache(maxsize=None)
def get_v8_target_cpu():
    m = machine().lower()
    if m in ("arm64", "aarch64"):
        return "arm64"
    if m == "arm":
        return "arm"
    if (not m) or (match("(x|i[3-6]?)86$", m) is not None):
        return "ia32"
    if m in ("x86_64", "amd64"):
        return "x64"
    if m == "s390x":
        return "s390x"
    if m == "ppc64":
        return "ppc64"

    raise UnknownArchError(m)


@lru_cache(maxsize=None)
def get_dll_filename():
    if is_mac():
        return "libmini_racer.dylib"

    if is_win():
        return "mini_racer.dll"

    return "libmini_racer.so"


@lru_cache(maxsize=None)
def get_data_files_list():
    """List the files which v8 builds and then needs at runtime."""

    return (
        # V8 i18n data:
        "icudtl.dat",
        # V8 fast-startup snapshot; a dump of the heap after loading built-in JS
        # modules:
        "snapshot_blob.bin",
        # And obviously, the V8 build itself:
        get_dll_filename(),
    )


@lru_cache(maxsize=None)
def is_musl():
    # Alpine uses musl for libc, instead of glibc. This breaks many assumptions in the
    # V8 build, so we have to reconfigure various things when running on musl libc.
    # Determining if we're on musl (or Alpine) is surprisingly complicated; the best
    # way seems to be to check the dynamic linker ependencies of the current Python
    # executable for musl! packaging.tags.platform_tags (which is used by pip et al)
    # does this for us:
    return any("musllinux" in t for t in platform_tags())


@lru_cache(maxsize=None)
def get_platform_tag():
    """Return a pip platform tag indicating compatibility of the mini_racer binary.

    See https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/.
    """

    if is_mac():
        # pip seems finicky about platform tags with larger macos versions, so just
        # tell arm64 is 11.0 and everything is is 10.9:
        if get_v8_target_cpu() == "arm64":
            return "macosx_11_0_arm64"

        return "macosx_10_9_x86_64"

    # return the first, meaning the most-specific, platform tag:
    return next(platform_tags())


@lru_cache(maxsize=None)
def get_workspace_path():
    return local_path(pathjoin("..", "v8_workspace"))


@lru_cache(maxsize=None)
def get_depot_tools_path():
    return pathjoin(get_workspace_path(), "depot_tools")


@lru_cache(maxsize=None)
def get_v8_path():
    return pathjoin(get_workspace_path(), "v8")


def unlink_if_exists(f):
    if exists(f):
        unlink(f)


def run(*args, cwd, depot_tools_first=True):
    LOGGER.debug("Calling: '%s' from working directory %s", shlexjoin(args), cwd)
    env = environ.copy()

    if depot_tools_first:
        env["PATH"] = pathsep.join([get_depot_tools_path(), environ["PATH"]])
    else:
        env["PATH"] = pathsep.join([environ["PATH"], get_depot_tools_path()])

    env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    # vpython is V8's Python environment manager; it downloads Python binaries
    # dynamically. This doesn't work on Alpine (because it downloads a glibc binary,
    # but needs a musl binary), so let's just disable it on all environments:
    env["VPYTHON_BYPASS"] = "manually managed python not supported by chrome operations"
    # Goma is a remote build system which we aren't using. depot_tools/autoninja.py
    # tries to run the goma client, which is checked into depot_tools as a glibc binary.
    # This fails on musl (on Alpine), so let's just disable the thing:
    env["GOMA_DISABLED"] = "1"

    return check_call(args, env=env, cwd=cwd)


def ensure_depot_tools():
    if isdir(get_depot_tools_path()):
        LOGGER.debug("Using already cloned depot tools")
        return

    LOGGER.debug("Cloning depot tools")
    makedirs(f"{get_workspace_path()}", exist_ok=True)
    run(
        "git",
        "clone",
        "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
        cwd=get_workspace_path(),
    )

    # depot_tools will auto-update when we run various commands. This creates extra
    # dependencies, e.g., on goma (which has trouble running on Alpine due to musl).
    # We just created a fresh single-use depot_tools checkout. There is no reason to
    # update it, so let's just disable that functionality:
    open(pathjoin(get_depot_tools_path(), ".disable_auto_update"), "w").close()

    if is_win():
        # Create git.bit and maybe other shortcuts used by the Windows V8 build tools:
        run(
            pathjoin(get_depot_tools_path(), "bootstrap", "win_tools.bat"),
            cwd=get_depot_tools_path(),
        )


def ensure_v8_src(revision):
    """Ensure that v8 src are present and up-to-date."""

    # We create our own .gclient config instead of creating it via fetch.py so we can
    # control (non-)installation of a sysroot.
    gclient_file = pathjoin(get_workspace_path(), ".gclient")
    if not isfile(gclient_file):
        makedirs(get_workspace_path(), exist_ok=True)
        if is_musl():
            # Prevent fetching of a useless Debian sysroot on Alpine.
            # We disable use of the sysroot below (see "use_sysroot"), so this is just
            # an optimization to preempt the download.
            # (Note that "musl" is not a valid OS in the depot_tools deps system;
            # "musl" here is just a placeholder to mean "*not* the thing you think is
            # called 'linux'".)
            # Syntax from https://source.chromium.org/chromium/chromium/src/+/main:docs/ios/running_against_tot_webkit.md
            target_os = """\
target_os = ["musl"]
target_os_only = "True"
"""
        else:
            target_os = ""

        with open(gclient_file, "w") as f:
            f.write(
                f"""\
solutions = [
  {{ "name"        : "v8",
    "url"         : "https://chromium.googlesource.com/v8/v8.git",
    "deps_file"   : "DEPS",
    "managed"     : False,
    "custom_deps" : {{}},
    "custom_vars": {{}},
  }},
]
{target_os}\
"""
            )

    run(
        executable,
        pathjoin(get_depot_tools_path(), "gclient.py"),
        "sync",
        "--revision",
        f"v8@{revision}",
        cwd=get_workspace_path(),
    )

    ensure_symlink(
        local_path(pathjoin("..", "src", "v8_py_frontend")),
        pathjoin(get_v8_path(), "custom_deps", "mini_racer"),
    )


def apply_patch(path, patch_filename):
    applied_patches_filename = local_path(".applied_patches")

    if not exists(applied_patches_filename):
        open(applied_patches_filename, "w").close()

    with open(applied_patches_filename, "r+") as f:
        applied_patches = set(f.read().splitlines())

        if patch_filename in applied_patches:
            return

        run(
            "patch",
            "-p0",
            "-i",
            patch_filename,
            cwd=path,
        )

        f.write(patch_filename + "\n")


def run_build(build_dir):
    """Run the actual v8 build."""

    # As explained in the design principles in ARCHITECTURE.md, we want to reduce the
    # surface area of the V8 build system which PyMiniRacer depends upon. To accomodate
    # that goal, we run with as few non-default build options as possible.

    # The standard developer guide for V8 suggests we use the v8/tools/dev/v8gen.py
    # tool to both generate args.gn, and run gn to generate Ninja build files.

    # Unfortunately, v8/tools/dev/v8gen.py is unhappy about being run on non-amd64
    # architecture (it seems to think we're always cross-compiling from amd64). Thus
    # we reproduce what it does, which for our simple case devolves to just generating
    # args.gn with minimal arguments, and running "gn gen ...".

    opts = {
        # These following settings are based those found for the "x64.release"
        # configuration. This can be verified by running:
        # tools/mb/mb.py lookup -b x64.release -m developer_default
        # ... from within the v8 directory.
        "dcheck_always_on": "false",
        "is_debug": "false",
        "target_cpu": f'"{get_v8_target_cpu()}"',
        "v8_target_cpu": f'"{get_v8_target_cpu()}"',
        # We sneak our C++ frontend into V8 as a symlinked "custom_dep" so
        # that we can reuse the V8 build system to make our dynamic link
        # library:
        "v8_custom_deps": '"//custom_deps/mini_racer"',
    }

    if (is_linux() and get_v8_target_cpu() == "arm64") or is_musl():
        # The V8 build process includes its own clang binary, but not for aarch64 on
        # Linux glibc, and not for Alpine (musl) at all.
        # Per tools/dev/gm.py, use the the system clang instead:
        opts["clang_base_path"] = '"/usr"'

        opts["clang_use_chrome_plugins"] = "false"
        # Because we use a different clang, more warnings pop up. Ignore them:
        opts["treat_warnings_as_errors"] = "false"

        # V8 currently uses a clang flag -split-threshold-for-reg-with-hint=0 which
        # doen't exist on Alpine's mainline llvm yet. Disable it:
        if is_musl():
            apply_patch(
                get_v8_path(),
                local_path("split-threshold-for-reg-with-hint.patch"),
            )

    if is_musl() and get_v8_target_cpu() == "arm64":
        # The V8 build unhelpfully sets the clang flag --target=aarch64-linux-gnu
        # on musl. The --target flag is useful when we're cross-compiling (which we're
        # not) and we aren't on aarch64-linux-gnu, we're actually on what clang calls
        # aarch64-alpine-linux-musl.
        # This patch just disables the spurious cflags and ldflags:
        apply_patch(
            get_v8_path(),
            local_path("no-aarch64-linux-gnu-target.patch"),
        )

    if is_musl():
        # On various OSes, the V8 build process brings in a whole copy of the sysroot
        # (/usr/include, /usr/lib, etc). Unfortunately on Alpine it tries to use a
        # Debian sysroot, which doesn't work. Disable it:
        opts["use_sysroot"] = "false"

        # V8 includes its own libc++ whose headers don't seem to work on Alpine:
        opts["use_custom_libcxx"] = "false"

    # We optionally use SCCACHE to speed up builds (or restart them on failure):
    sccache_path = environ.get("SCCACHE_PATH")
    if sccache_path is not None:
        opts["cc_wrapper"] = f'"{sccache_path}"'

    makedirs(build_dir, exist_ok=True)

    with open(pathjoin(build_dir, "args.gn"), "w") as f:
        f.write("# This file is auto-generated by v8_build.py")
        f.write("\n".join(f"{n}={v}" for n, v in opts.items()))
        f.write("\n")

    # Now generate Ninja build files:
    if is_musl():
        # depot_tools doesn't include a musl-compatible GN, so use the system one:
        gn_bin = ("/usr/bin/gn",)
    else:
        gn_bin = (
            executable,
            pathjoin(get_depot_tools_path(), "gn.py"),
        )

    run(
        *gn_bin,
        "gen",
        build_dir,
        "--check",
        cwd=get_v8_path(),
    )

    # Finally, actually do the build:
    if is_musl():
        # depot_tools doesn't include a musl-compatible ninja, so use the system one:
        ninja_bin = ("/usr/bin/ninja",)
    else:
        ninja_bin = (
            executable,
            pathjoin(get_depot_tools_path(), "ninja.py"),
        )

    run(
        *ninja_bin,
        # "-vv",  # this is so spammy GitHub Actions struggles to show all the output
        "-C",
        build_dir,
        pathjoin("custom_deps", "mini_racer"),
        cwd=get_v8_path(),
    )


def ensure_symlink(target, link_name):
    LOGGER.debug("Creating symlink to %s on %s", target, link_name)
    try:
        symlink(target, link_name)
    except OSError as e:
        if e.errno == EEXIST:
            remove(link_name)
            symlink(target, link_name)
        else:
            raise


def build_v8(
    out_path,
    *,
    revision=None,
    fetch_only=False,
    skip_fetch=False,
):
    revision = revision or V8_VERSION

    ensure_depot_tools()

    if not skip_fetch:
        ensure_v8_src(revision)

    if fetch_only:
        return

    build_dir = pathjoin(get_v8_path(), "out.gn", "build")

    run_build(build_dir)

    # Fish out the build artifacts:
    makedirs(out_path, exist_ok=True)

    # Create a map of actual files to in-package filenames, for Hatch to use
    # when building the wheel:
    artifacts = {}

    for f in get_data_files_list():
        src = pathjoin(build_dir, f)
        dst = pathjoin(out_path, f)

        LOGGER.debug("Copying build artifact %s to %s", src, dst)
        unlink_if_exists(dst)
        copyfile(src, dst)

        artifacts[dst] = pathjoin("py_mini_racer", f)

    LOGGER.debug("Build complete!")

    return artifacts


def clean_v8(out_path):
    for f in get_data_files_list():
        unlink_if_exists(pathjoin(out_path, f))

    rmtree(get_workspace_path(), ignore_errors=True)


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument(
        "--out-path",
        default=pathjoin("src", "py_mini_racer"),
        help="Build destination directory",
    )
    parser.add_argument("--v8-revision", default=V8_VERSION)
    parser.add_argument("--fetch-only", action="store_true", help="Only fetch V8")
    parser.add_argument("--skip-fetch", action="store_true", help="Do not fetch V8")
    args = parser.parse_args()
    build_v8(
        out_path=args.out_path,
        revision=args.v8_revision,
        fetch_only=args.fetch_only,
        skip_fetch=args.skip_fetch,
    )
