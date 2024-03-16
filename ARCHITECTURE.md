# Architecture

This document contains some notes about the design of PyMiniRacer.

## Brief catalog of key components

### `docs/`

This is the [`mkdocs` ](https://www.mkdocs.org/) site for PyMiniRacer. To maximize
compatibility with standard open-source repository layout, this directory is just a
bunch of stubs which include files from the package root.

### `hatch_build.py`

This is a Hatch build hook which builds Python wheels, by calling `helpers/v8_build.py`.

### `helpers/v8_build.py`

This is the PyMiniRacer V8 build wrapper. Building V8 for many platforms (Windows, Mac,
glibc Linux, musl Linux) *and* architectures (x86_64, aarch64) is hard, especially since
V8 is primarily intended to be built by Google engineers on a somewhat different set of
of platforms (i.e., those Chrome runs on), and typically via cross-compilation from
relatively curated build hosts. So this file is complicated and full of `if` statements.

### `src/v8_py_frontend/`

This is a small frontend for V8, written in C++. It manages initialization, context,
marshals and unmarshals inputs and outputs through V8's type system, etc. The front-end
exposes simple functions and types which are friendly to the Python `ctypes` system.
These simple C++ functions in turn call the C++ V8 APIs.

As noted below, `v8_py_frontend` is *not* a Python extension (it does *not* include
`Python.h` or link `libpython`, and it does not touch Python types).

### (Compiled) `src/py_mini_racer/libmini_racer.so`, `src/py_mini_racer/mini_racer.dll`, `src/py_mini_racer/libmini_racer.dylib`

These files (*which one* depends on the platform) contain the compiled V8 build,
complete with the frontend from `src/v8_py_frontend`.

### (Compiled) `src/py_mini_racer/icudtl.dat`

This is a build-time-generated internationalization artifact, used
[at runtime by V8](https://v8.dev/docs/i18n) and thus shipped with PyMiniRacer.

### (Compiled) `src/py_mini_racer/snapshot_blob.bin`

This is a build-time-generated
[startup snapshot](https://v8.dev/blog/custom-startup-snapshots), used at runtime by V8
and thus shipped with PyMiniRacer. This is a snapshot of the JavaScript heap including
JavaScript built-ins, which accelerates JS engine startup.

### `src/py_mini_racer/`

This is the pure-Python implementation of PyMiniRacer. This loads the
(Python-independent) PyMiniRacer dynamic-link library (`.dll` on windows, `.so` on
Linux, `.dylib` on MacOS) and uses the Python `ctypes` system to call methods within it,
to manage V8 context and actually evaluate JavaScript code.

### `.github/workflows/pypi-build.yml`

This is the primary build script for PyMiniRacer, implemented as a GitHub Actions
workflow.

## Design decisions

These are listed in a topological sort, from most-fundamental to most-derived decisions.

In theory, answers to questions in the vein of "Why is it done this way?" belong in this
document.

### Minimize the interface with V8

V8 is extremely complex and is under continual, heavy development. Such development can
result in interface changes, which may in turn break PyMiniRacer.

To mitigate the risk of breakage with new V8 builds, we seek to minimize the "API
surface area" between PyMiniRacer and V8. This means we seek to limit "advanced" use of
both:

1. The V8 C++ API, and
1. The V8 build system (GN) and build options.

Our success at minimizing the interface with the V8 build system can be measured by the
length of `helpers/v8.build.py` (444 lines as of this writing!). Making V8 build on
multiple platforms takes a lot of trickery...

### Minimize the interface with the CPython API (don't make an extension)

For similar reasons (the CPython API is complex and always in flux, *although not as
much as V8*), *combined with* the proliferation of Python versions (many versions of
CPython, PyPy, etc), we'd rather avoid directly interfacing with the CPython API. Thus,
*instead of* an extension module (which includes `Python.h` and links against
`libpython`), we build an ordinary Python-independent C++ library, and use `ctypes` to
access it.

### Build V8 from source

The V8 project does not produce stable binary distributions, i.e., static or dynamic
libraries. (In Linux terms, this would probably look like `libv8` and `libv8-dev`.)
Instead, any project (like NodeJS, Chromium, or... PyMiniRacer!) which wants to
integrate V8 must first build it.

### Build V8 *with* our frontend (`v8_py_frontend`) as a snuck-in component

We could *just* get a static library (i.e., `libv8.a`) from the V8 build, and link that
into a dynamic-link library \[i.e., `libmini_racer.so`\]) ourselves.

However:

1. We do have *one more* C++ file to compile (the C++ code in `v8_py_frontend`)
1. Because we're not making a true Python extension module (see above), we aren't using
    Python's `setuptools` `Extension` infrastructure to perform a build.

This *does*, however, leave us needing *some* platform-independent C++ toolchain.

V8 already has such a toolchain, based on Ninja and Generated Ninja files (GN).

Rather than bringing in another toolchain, we sneak `v8_py_frontend` (which is, after
all, just one C++ file) into the V8 tree itself, as a "custom dep". We then instruct GN
to build it as if it were an ordinary part of V8.

The result is a dynamic-link library which contains an ordinary release build of V8,
plus our Python `ctypes`-friendly frontend.

### Build PyPI wheels

Because V8 takes so long to build (about 2-3 hours at present on the free GitHub Actions
runners, and >12 hours when emulating `aarch64` on them), we want to build wheels for
PyPI. We don't want folks to have to build V8 when they `pip install mini-racer`!.

We build wheels for many operating systems and architectures based on popular demand via
GitHib issues. Currently the list is
`{x86_64, aarch64} × {Debian Linux, Alpine Linux, Mac, Windows}` (but skipping Windows
`aarch64` for now since there is not yet either a GitHub Actions runner, or emulation
layer for it).

### Use the free GitHub Actions hosted runners

PyMiniRacer is not a funded project, so we run on the free GitHub Actions hosted
runners. These currently let us build for many key platforms (including via emulation).

This also lets contributors easily run the same build automation by simply forking the
PyMiniRacer repo and running the workflows (for free!) within their own forks.

### Use `sccache` to patch around build timeouts

As of this writing, the Linux `aarch64` builds run on emulation GitHub because has no
free hosted `aarch64` runners for Linux. This makes them so slow, they struggle to
complete at all. They take about 24 hours to run. The GitHub Actions
[job timeout is only 6 hours](https://docs.github.com/en/actions/learn-github-actions/usage-limits-billing-and-administration#usage-limits),
so we have to restart the jobs multiple times. We rely on
[`sccache`](https://github.com/mozilla/sccache) to catch the build up to prior progress.

It would in theory be less ugly to segment the build into small interlinked jobs of less
than 6 hours each so they each succeed, but for now it's simpler to just manually
restart the failed jobs, each time loading from the build cache and making progress,
until they finally succeed. Hopefully at some point GitHub will provide native `aarch64`
Linux runners, which will alleviate this problem.

### Use `uraimo/run-on-arch-action` (and not `cibuildwheel`)

So, we need to build wheels for multiple architectures. For Windows and Mac (`x86_64` on
Windows, and both `x86_64` and `aarch64` on Mac) we can can use GitHub hosted runners.
For Linux builds (Debian and Alpine, and `x86_64` and `aarch64`), we use the fantastic
GitHub Action workflow step
[`uraimo/run-on-arch-action`](https://github.com/uraimo/run-on-arch-action), which lets
us build a docker container on the fly and run it on QEMU.

Many modern Python projects which need to build wheels with native code use
[the `cibuildwheel` project](https://github.com/pypa/cBbuildwheel) to manange their
builds. However, `cibuildwheel` isn't a perfect fit here. Because we are building
Python-independent dynamic-link libraries instead of Python extension modules modules,
we aren't linking with any particular Python ABI. Thus we need *only*
`(operating systems × architectures)` builds, whereas `cibuildwheel` generates
`(operating systems × architecture × Python flavors × Python versions)` wheels.
[That's a ton of wheels](https://cibuildwheel.readthedocs.io/en/stable/options/#build-skip)!
Given that it takes hours to *days* to build PyMiniRacer for *one* target OS and
architecture, doing redundant builds is undesirable.

It might be possible to use `cibuildwheel` with PyMiniRacer by segmenting the build of
the dynamic-link library (i.e., `libmini_racer.so`) from the actual wheel build. That
is, we could have the following separate components:

1. Create a separate Github Actions workflow to build the `libmini_racer.so` binary
    (i.e., the hard part). Publish that as a release, using the GitHub release artifact
    management system as a distribution mechanism.
1. The wheel build step could then simply download a pre-built binary from the latest
    GitHub release. We could use `cibuildwheel` to manage this step. This would
    generate many redundant wheels (because the wheels we'd generate for, say, CPython
    3.9 and 3.10 would be identical), but it wouldn't matter because it would be cheap
    and automatic.

This is similar to how the Ruby [`mini_racer`](https://github.com/rubyjs/mini_racer) and
[`libv8-node`](https://github.com/rubyjs/libv8-node) projects, which inspired
PyMiniRacer, work together today.

To sum up, to use `cibuildwheel`, we would still need our own *separate*
multi-architecture build workflow for V8, *ahead of* the `cibuildwheel` step. So
`cibuildwheel` could potentially simplify the actual wheel distribution for us, but it
wouldn't simplify the overall workflow management.
