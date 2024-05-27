# Architecture

This document contains some notes about the design of PyMiniRacer.

## Security goals

**First and foremost, PyMiniRacer makes no guarantees or warrantees, as noted in the
license.** This section documents the security *goals* of PyMiniRacer. Anything that
doesn't meet these goals should be considered to be a bug (but with no warrantee or even
a guaranteed path to remediation).

### PyMiniRacer should *be able to* run untrusted JavaScript code

The ability for PyMiniRacer to run untrusted JavaScript code
[was an original design goal for Sqreen](https://news.ycombinator.com/item?id=39754885#39813985)
in developing PyMiniRacer, and continues to be a design goal today.

To that end, PyMiniRacer provides:

1. The innate sandboxing properties of V8. V8 is trusted by billions of folks to run
    untrusted JavaScript every day, as a part of Chrome and other web browsers. It has
    many features like the [security sandbox](https://v8.dev/blog/sandbox) and
    undergoes close security scrutiny.

1. The ability to create multiple `MiniRacer` instances which each have separate V8
    isolates, to separate different blobs of untrusted code from each other.

1. Optional timeouts and memory constraints on code being executed.

Caveats:

1. The continual security research is V8 under yields a corresponding
    [stream of vulnerability reports](https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=v8).
    

1. ... and while V8 *as embedded in a web browser* will typically receive (funded!)
    updates to correct those vulnerabilities, PyMiniRacer is unlikely to see as
    aggressive and consistent an update schedule.

1. ... and of course PyMiniRacer itself may have vulnerabilities.
    [This has happened before](https://nvd.nist.gov/vuln/detail/CVE-2020-25489).

1. ... and even if PyMiniRacer is updated to accomodate a vulnerability fix in itself or
    V8, it is incumbent upon Python applications which integrate it to actually
    redeploy with the new PyMiniRacer version.

If running potentially adversarial JavaScript code in a high-security environment, it
might be a better choice to run code using a purpose-built isolation environment such as
containers on [gVisor](https://gvisor.dev/), than to rely on PyMiniRacer for isolation.

### JavaScript-to-Python callbacks may breach any isolation boundary

The `MiniRacer.wrap_py_function` method allows PyMiniRacer users to expose Python
functions *they write* to JavaScript. This creates an extension framework which
essentially breaches the isolation boundary provided by V8.

This feature should only be used if the underlying JavaScript code *is* trusted, or if
the author is certain the exposed Python function is safe for calls from untrusted code.
(I.e., if you expose a Python function which allows reading arbitrary files from disk,
this would obviously be bad if the JavaScript code which may call it is itself
untrusted.)

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
of platforms (i.e., those Chrome runs on), and typically via cross-compiled from
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

### `.github/workflows/build.yml`

This is the primary build script for PyMiniRacer, implemented as a GitHub Actions
workflow.

## Design decisions

These are listed in a topological sort, from most-fundamental to most-derived decisions.

In theory, answers to questions in the vein of "Why is it done this way?" belong in this
section.

### Minimize the interface with V8

V8 is extremely complex and is under continual, heavy development. Such development can
result in interface changes, which may in turn break PyMiniRacer.

To mitigate the risk of breakage with new V8 builds, we seek to minimize the "API
surface area" between PyMiniRacer and V8. This means we seek to limit "advanced" use of
both:

1. The V8 C++ API, and
1. The V8 build system (GN) and build options.

Our success at minimizing the interface with the V8 build system can be measured by:

1. The number of times the text `v8::` appears in `src/v8_py_frontend`, and
1. The length of `helpers/v8.build.py` (467 lines as of this writing!). Making V8 build
    on multiple platforms takes a lot of trickery...

### Build V8 from source

The V8 project does not produce stable binary distributions, i.e., static or dynamic
libraries. (In Linux terms, this would probably look like dpkgs and rpms with names like
`libv8` and `libv8-dev`.) Instead, any project (like NodeJS, Chromium, or...
PyMiniRacer!) which wants to integrate V8 must first build it.

### Build PyPI wheels

Because V8 takes so long to build (about 2-3 hours at present on the free GitHub Actions
runners, and >12 hours when emulating `aarch64` on them), we want to build wheels for
PyPI. We don't want folks to have to build V8 when they `pip install mini-racer`!

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

### Don't interface with the CPython API (don't make an extension)

We'd rather avoid directly interfacing with the CPython API, for a couple reasons:

1. **API flux**: Similar to the above note about V8, the CPython API is complex and
    always in flux, *although not as much as V8*).
1. **Version proliferation**: there are a ton of active Python versions (as of this
    writing, PyMiniRacer supports 3.8, 3.9, 3.10, 3.11, and 3.12, and also there's
    CPython and PyPy). PyMiniRacer *already* includes builds for 7 target architectures
    (see above); if we factor in 5x Python versions and 2x Python interpreters, we will
    wind up with 70 wheels, all on a free GitHub Actions runner!

So, *instead of* an extension module (which includes `Python.h` and links against
`libpython`), we build an ordinary Python-independent C++ library, and use `ctypes` to
access it.

Consequently, `libmini_racer.so` isn't specific to Python, and the code barely mentions
Python. One could in theory use it from any other language which knows how to call C
APIs, such as Java, Go, C#, ... or just C. No one does so as of this writing.

### Use `uraimo/run-on-arch-action`

So, we need to build wheels for multiple architectures. For Windows and Mac (`x86_64` on
Windows, and both `x86_64` and `aarch64` on Mac) we can can use GitHub hosted runners
as-is. For Linux builds (Debian and Alpine, and `x86_64` and `aarch64`), we use the
fantastic GitHub Action workflow step
[`uraimo/run-on-arch-action`](https://github.com/uraimo/run-on-arch-action), which lets
us build a docker container on the fly and run it on QEMU.

### Don't use `cibuildwheel`

Many modern Python projects which need to build wheels with native code use
[the `cibuildwheel` project](https://github.com/pypa/cBbuildwheel) to manange their
builds. However, `cibuildwheel` isn't a perfect fit here. Because we are building
Python-independent dynamic-link libraries instead of Python extension modules modules
for the reasons noted above, we aren't linking with any particular Python ABI. Thus we
need *only* `(operating systems × architectures)` builds, whereas `cibuildwheel`
generates `(operating systems × architecture × Python flavors × Python versions)`
wheels.
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

### Use `sccache` to patch around build timeouts

As of this writing, the Linux `aarch64` builds run on emulation becaues GitHub Actions
has no free hosted `aarch64` runners for Linux. This makes them so slow, they struggle
to complete at all. They take about 24 hours to run. The GitHub Actions
[job timeout is only 6 hours](https://docs.github.com/en/actions/learn-github-actions/usage-limits-billing-and-administration#usage-limits),
so we have to restart the jobs multiple times. We rely on
[`sccache`](https://github.com/mozilla/sccache) to catch the build up to prior progress.

It would in theory be less ugly to segment the build into small interlinked jobs of less
than 6 hours each so they each succeed, but for now it's simpler to just manually
restart the failed jobs, each time loading from the build cache and making progress,
until they finally succeed. Hopefully at some point GitHub will provide native `aarch64`
Linux runners, which will alleviate this problem.

Hopefully,
[per this GitHub community discussion thread](https://github.com/orgs/community/discussions/19197),
we will get a free Linux `aarch64` runner in 2024 and can dispense with
cross-architecture emulation.

### Build V8 *with* our frontend (`v8_py_frontend`) as a snuck-in component

We could *just* get a static library (i.e., `libv8.a`) from the V8 build, and link that
into a dynamic-link library (i.e., `libmini_racer.so`) ourselves.

However:

1. We do have *more* C++ files to compile (the C++ code in `src/v8_py_frontend`)
1. Because we're not making a true Python extension module (see above), we aren't using
    Python's `setuptools` `Extension` infrastructure to perform a build.

This leaves us needing *some* platform-independent C++ toolchain.

V8 already has such a toolchain, based on Ninja and Generated Ninja files (GN). We
already have to set it up to build V8 from source (see above for why!).

Rather than bringing in yet another toolchain, we sneak `v8_py_frontend` into the V8
tree itself, as a "custom dep". We then instruct GN to build it as if it were an
ordinary part of V8.

The result is a dynamic-link library which contains an ordinary release build of V8,
plus our Python `ctypes`-friendly frontend.

### Buggy or adversarial JavaScript shouldn't be able to crash or otherwise disrupt things

Per the security goals above, we want PyMiniRacer to be able to run untrusted JavaScript
code safely. This means we can't trust JavaScript to "behave". Intentionally bad (i.e,
adversarial) or **un**intentionally bad (i.e., buggy) JavaScript should not be able to:

1. Crash PyMiniRacer,
1. Read arbitrary memory, or
1. Use infinite CPU or memory resources

For the latter, the PyMiniRacer Python API exposes optional constraints on memory usage
as well as timeouts. The former two rules are enforced by the design of the C++ side of
PyMiniRacer, and of course V8 itself.

### Don't trust JavaScript with memory management of C++ objects

JavaScript is a garbage-collected language, and like many such languages it
[offers](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/FinalizationRegistry)
best-effort finalizer functionality, into which you can inject code which gets called
when the runtime is disposing of an object.

However, with
[V8-based JavaScript](https://stackoverflow.com/questions/24107280/v8-weakcallback-never-gets-called),
actually relying on this functionality to trigger callbacks to C++ to clean things up is
heartily discouraged. Exploratory attempts to make this with PyMiniRacer actually didn't
work at all.

Even if we *could* get V8 to call us back reliably to tear down objects (e.g., by
exposing an explicit teardown function to JavaScript), it would be hard to create a
design which does so safely. V8 (per our security goals) may be running adversarial
JavaScript which might try and use a reference after we free it, exploiting a
use-after-free bug.

### Any raw C++ object pointers and references given to JavaScript must outlive the `v8::Isolate`

Due to the above rule, we can't rely on V8 to tell us when it's *done with* any
references we give it, until the `v8::Isolate` is torn down. So clearly the only thing
we *can* do is ensure any raw pointers or references we hand to V8 are valid until after
the `v8::Isolate` is torn down.

### Use JavaScript integer IDs to track any allocated objects on the C++ side

The above said, we still have cases where we want objects to live shorter lifecycles
than the `v8::Isolate` itself. E.g., a function callback from JavaScript to C++ (and
thus to Python) might only be used as a single `Promise.then` callback. If a
long-running program were to create tons of `Promise`s, we'd want to garbage collect the
callbacks as we go, without waiting for the whole `v8::Isolate` to exit.

We can treat that case by "laundering" our raw C++ pointers and references through C++
maps (i.e., `std::unordered_map<uint64_t, std::shared_ptr<T>>`), giving V8 JavaScript
only IDs into the map. We can convert IDs back into C++ pointers when JavaScript calls
us back, after checking that they're still valid. (And we use `std::shared_ptr` to avoid
tear-down race conditions wherein a map entry is removed in one thread while we're still
using an object in another.)

In this manner, the C++ side can be authoritative about when objects are torn down. It
can delete C++ objects and remove them from the map whenever it sees fit. If JavaScript
tries to use the ID after that point, such usage can be easily spotted and safely
ignored.

### Buggy Python shouldn't be able to crash C++

Similar to, but with a lower priority than the above rule regarding bad *JavaScript*,
bad *Python* should not be able to crash the Python interpretter through PyMiniRacer.
This is a common design principle for Python; bad code should *not* result in
segmentation faults, sending developers scrambling to C/C++ debugging of core files,
etc. Extension modules should uphold this principle.

This applies only to **un**intentionally bad (i.e., buggy) Python code. PyMiniRacer does
not and cannot protect itself from intentionally bad (i.e., adversarial) Python code. A
determined Python programmer can always crash Python with ease without any help from
PyMiniRacer. Try it!: `import ctypes; ctypes.cast(0x1, ctypes.c_char_p).value`

### Minimize trust of Python in *automatic* memory management of C++ objects

Python is also a garbage-collected language, and like JavaScript, it
[offers](https://docs.python.org/3/reference/datamodel.html#object.__del__) best-effort
finalizer functionality.

Like in JavaScript code, relying on Python's finalizer functionality is
[heartily discouraged](https://stackoverflow.com/questions/6104535/i-dont-understand-this-python-del-behaviour#answer-6104568).
We can, at best, use `__del__` as a shortcut signaling we can go ahead and free
something to help reduce memory usage, but we shouldn't rely on it.

Since, unlike JavaScript, we *do* trust Python code, we can create explicit Python APIs
to manage object lifecycle. The Pythonic way to do that is with
[context managers](https://docs.python.org/3/reference/datamodel.html#context-managers).

Thus, for example, the MiniRacer Python `_Context` object, which wraps exactly one C++
`MiniRacer::Context` object, provides *both* a `__del__` finalizer for easy cleanup
which always works "eventually", and an explicit context manager interface for
PyMiniRacer users who want strong guarantees about teardown.

### Minimize trust of Python in handing C/C++ pointers

The `ctypes` module lets Python directly wrangle C/C++ pointers. This can be used to
send, receive, and mutate data shared between Python and C.

This is obviously somewhat dangerous. Array overruns are an obvious problem.
Use-after-free is more insidious: imagine the C++ side of PyMiniRacer returns a pointer
to an object to Python, Python stores that pointer, the C++ frees the object, and then
Python tries to use the pointer. This will work *sometimes* and crash—or worse, read
incorrect data—at other times.

### Use Python integer IDs to track any allocated objects on the C++ side

Thus, combining all the above rules, we wind up with a similar rule for Python as we
have for JavaScript. Wherever possible, we avoid interchanging raw pointers between C++
and Python. Instead, we interchange integer IDs. The C++ side of PyMiniRacer can convert
integer IDs to raw pointers using a map, after validating that the IDs are still valid.

### ... except for `BinaryValueHandle` pointers

We break the above rule for `BinaryValueHandle` pointers. PyMiniRacer uses
`BinaryValueHandle` to exchange most data between Python and C++. Python directly reads
the contents of `BinaryValueHandle` pointers, to read primitive values (e.g., booleans,
integers, and strings).

We do this for theoretical performance reasons which have not yet been validated. To be
consistent with the rest of PyMiniRacer's design, we *could* create an API like:

1. C++ generates a numeric `value_id` and stores a BinaryValue in a
    `std::unordered_map<uint64_t, std::shared_ptr<BinaryValue>>`.
1. C++ gives Python that `value_id` to Python.
1. To get any data Python has to call APIs like `mr_value_type(context_id, value_id)`,
    `mr_value_as_bool(context_id, value_id)`,
    `mr_value_as_string_len(context_id, value_id)`,
    `mr_value_as_string(context_id, value_id, buf, buflen)`, ...
1. Eventually Python calls `mr_value_free(context_id, value_id)` which wipes out the map
    entry, thus freeing the `BinaryValue`.

_**Note: We don't do this. The above is _not_ how PyMiniRacer actually handles
values.**_

This is surely slower than direct pointer access, but no performance analysis has been
done to see if it matters. It might be interesting to try the above and benchmark it. It
would be nice to switch to that model if it's sufficiently performant.

For now at least, we instead use raw pointers for this case.

We still don't fully trust Python with the lifecyce of `BinaryValueHandle` pointers;
when Python passes these pointers back to C++, we still check validity by looking up the
pointer as a key into a map (which then lets the C++ side of PyMiniRacer find the *rest*
of the `BinaryValue` object). The C++ `MiniRacer::BinaryValueFactory` can
authoritatively destruct any dangling `BinaryValue` objects when it exists.

This last especially helps with an odd scenario introduced by Python `__del__`: the
order in which Python calls `__del__` on a collection of objects is neither guaranteed
nor very predictable. When a Python program drops references to a Python `MiniRacer`
object, it's common for Python to call `_Context.__del__` before it calls
`ValHandle.__del__`, thus destroying *the container for* the value before it destroys
the value itself. The C++ side of PyMiniRacer can easily detect this scenario: First,
when destroying the `MiniRacer::Context`, it sees straggling `BinaryValue`s and destroys
them. Then, when Python asks C++ to destroy the straggling `BinaryValueHandle`s, the C++
`mr_free_value` API sees the `MiniRacer::Context` is already gone, and ignores the
redundant request.

The above scenario does imply a possibility for dangling pointer access: if Python calls
`_Context.__del__` then tries to read the memory addressed by the raw
`BinaryValueHandle` pointers, it will be committing a use-after-free error. We mitigate
this problem by hiding `BinaryValueHandle` within PyMiniRacer's Python code, and by
giving `ValHandle` (our Python wrapper of `BinaryValueHandle`) a reference to the
`_Context`, preventing the context from being finalized until the `ValHandle` is *also*
in Python's garbage list and on its way out.

### Only touch (most of) the `v8::Isolate` from within the message loop

While a `v8::Isolate` is generally a thread-aware and multi-threaded object, most of its
methods are not thread-safe. The same goes for most `v8` objects. It is, generally, only
safe to touch things belonging to a `v8::Isolate` if you hold the `v8::Locker` lock. (To
make matters more interesting, documentation about what things might be safe to do
*without* the lock is pretty scarce. You find out when your unsafe code crashes. Which,
you know, might not happen until years after you wrote the unsafe code. C++ is fun!)

The "don't touch the `v8::Isolate` without holding the `v8::Locker`" rule is made
particularly hard to follow since we also need to run a message loop thread to service
background work in v8. That message loop, of course, itself needs the `v8::Locker`.
Unfortunately, the message loop can wait indefinitely for new work, and yet doesn't give
up the lock while doing that waiting.

This poses a conundrum: the message loop hogs the isolate lock potentially indefinitely,
and yet other threads (i.e., Python threads) need that lock so they can poke at
`v8::Isolate`-owned objects too.

We resolve the conundrum by leveraging part of the `v8::Isolate` itself, using a trick
similar to what NodeJS does: everything that needs to touch a `v8::Isolate` should
simply run from the `v8::Isolate`'s own message loop. If you want to run JS code,
manipulate an object, *or even delete a V8 object*, you must submit a task to the
message loop. Then nothing but the message loop itself should need to hold the
`v8::Locker` lock, because only the message loop ever touches the `v8::Isolate`.

To make this somewhat easier we have created `MiniRacer::IsolateManager`, which provides
an easy API to submit tasks, whose callbacks accept as their first-and-only argument a
`v8::Isolate*`. Such tasks can freely work on the isolate until they exit. (Obviously,
saving a copy of the pointer and using it later would defeat the point; don't do that.)

One odd tidbit of PyMiniRacer is that *even object destruction* has to use the above
pattern. For example, it is (probably) not safe to free a `v8::Persistent` without
holding the isolate lock, so when a non-message-loop thread needs to destroy a wrapped
V8 value, we enqueue a pretty trivial task for the message loop:
`isolate_manager->Run([persistent]() { delete persistent; })`.

See [here](https://groups.google.com/g/v8-users/c/glG3-3pufCo) for some discussion of
this design on the v8-users mailing list.

### If any C++ code creates an Isolate task, it's responsible for awaiting its completion before teardown

The pattern, described above—of enqueuing all kinds of tasks for the v8 message pump,
including object destruction work—creates an interesting memory management problem for
PyMiniRacer. Such tasks typically create a reference cycle: the creator of the task
(like, say, the `MiniRacer::Context::MakeJSCallback`) bundles into the task references
to various other objects including, often, `this`. Those objects often themselves
contain references to the `MiniRacer::IsolateManager`, which transitively contains a
reference to the `v8::Isolate` and its message queue. Since the message queue contains a
reference to the task, we've just created a reference cycle!

To avoid either use-after-free or memory leak bugs upon teardown of a
`MiniRacer::Context`, we must enforce the following rule:

**If you call `MiniRacer::IsolateManager::Run(xyz)`, you are reponsible for ensuring
that task is done before any objects you bound into the function closure xyz (including
and especially `this`) are destroyed.**

The most common way we ensure this is waiting on the `std::future<void>` returned by
`MiniRacer::IsolateManager::Run(xyz)`. When that future settles, the task is done, and
it's safe to continue tearing down any references the task may hold.
