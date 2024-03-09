from __future__ import annotations

import ctypes
import json
import sys
from contextlib import ExitStack, contextmanager
from datetime import datetime, timezone
from importlib import resources
from json import JSONEncoder
from os.path import exists
from os.path import join as pathjoin
from sys import platform, version_info
from threading import Lock
from typing import ClassVar, Iterable, Iterator


class MiniRacerBaseException(Exception):  # noqa: N818
    """Base MiniRacer exception."""


class LibNotFoundError(MiniRacerBaseException):
    """MiniRacer-wrapped V8 build not found."""

    def __init__(self, path):
        super().__init__(f"Native library or dependency not available at {path}")


class LibAlreadyInitializedError(MiniRacerBaseException):
    """MiniRacer-wrapped V8 build not found."""

    def __init__(self):
        super().__init__(
            "MiniRacer was already initialized before the call to init_mini_racer"
        )


class JSParseException(MiniRacerBaseException):
    """JavaScript could not be parsed."""


class JSEvalException(MiniRacerBaseException):
    """JavaScript could not be executed."""


class JSOOMException(JSEvalException):
    """JavaScript execution ran out of memory."""


class JSTimeoutException(JSEvalException):
    """JavaScript execution timed out."""


class JSConversionException(MiniRacerBaseException):
    """JavaScript type could not be converted."""


class WrongReturnTypeException(MiniRacerBaseException):
    """Invalid type returned by the JavaScript runtime."""

    def __init__(self, typ):
        super().__init__(f"Unexpected return value type {typ}")


class JSObject:
    """JavaScript object."""

    def __init__(self, obj):
        self.id = obj

    def __hash__(self):
        return self.id


class JSFunction:
    """JavaScript function."""


class JSSymbol:
    """JavaScript symbol."""


def _get_lib_filename(name):
    """Return the path of the library called `name` on the current system."""
    if platform == "darwin":
        prefix, ext = "lib", ".dylib"
    elif platform == "win32":
        prefix, ext = "", ".dll"
    else:
        prefix, ext = "lib", ".so"

    return prefix + name + ext


def _build_dll_handle(dll_path) -> ctypes.CDLL:
    handle = ctypes.CDLL(dll_path)

    handle.mr_init_v8.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

    handle.mr_init_context.argtypes = []
    handle.mr_init_context.restype = ctypes.c_void_p

    handle.mr_eval_context.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_ulong,
        ctypes.c_size_t,
    ]
    handle.mr_eval_context.restype = ctypes.POINTER(MiniRacerValueStruct)

    handle.mr_free_value.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

    handle.mr_free_context.argtypes = [ctypes.c_void_p]

    handle.mr_heap_stats.argtypes = [ctypes.c_void_p]
    handle.mr_heap_stats.restype = ctypes.POINTER(MiniRacerValueStruct)

    handle.mr_low_memory_notification.argtypes = [ctypes.c_void_p]

    handle.mr_heap_snapshot.argtypes = [ctypes.c_void_p]
    handle.mr_heap_snapshot.restype = ctypes.POINTER(MiniRacerValueStruct)

    handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    handle.mr_set_soft_memory_limit.restype = None

    handle.mr_soft_memory_limit_reached.argtypes = [ctypes.c_void_p]
    handle.mr_soft_memory_limit_reached.restype = ctypes.c_bool

    handle.mr_v8_version.restype = ctypes.c_char_p

    return handle


# V8 internationalization data:
_ICU_DATA_FILENAME = "icudtl.dat"

# V8 fast-startup snapshot; a dump of the heap after loading built-in JS
# modules:
_SNAPSHOT_FILENAME = "snapshot_blob.bin"

DEFAULT_V8_FLAGS = ("--single-threaded",)


def _open_resource_file(filename, exit_stack):
    if version_info >= (3, 9):
        # resources.as_file was added in Python 3.9
        resource_path = resources.files("py_mini_racer") / filename

        context_manager = resources.as_file(resource_path)
    else:
        # now-deprecated API for Pythons older than 3.9
        context_manager = resources.path("py_mini_racer", filename)

    return str(exit_stack.enter_context(context_manager))


def _check_path(path):
    if path is None or not exists(path):
        raise LibNotFoundError(path)


@contextmanager
def _open_dll(flags: Iterable[str]) -> Iterator[ctypes.CDLL]:
    dll_filename = _get_lib_filename("mini_racer")

    with ExitStack() as exit_stack:
        # Find the dll and its external dependency files:
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass is not None:
            # We are running under PyInstaller
            dll_path = pathjoin(meipass, dll_filename)
            icu_data_path = pathjoin(meipass, _ICU_DATA_FILENAME)
            snapshot_path = pathjoin(meipass, _SNAPSHOT_FILENAME)
        else:
            dll_path = _open_resource_file(dll_filename, exit_stack)
            icu_data_path = _open_resource_file(_ICU_DATA_FILENAME, exit_stack)
            snapshot_path = _open_resource_file(_SNAPSHOT_FILENAME, exit_stack)

        _check_path(dll_path)
        _check_path(icu_data_path)
        _check_path(snapshot_path)

        handle = _build_dll_handle(dll_path)

        handle.mr_init_v8(
            " ".join(flags).encode("utf-8"),
            icu_data_path.encode("utf-8"),
            snapshot_path.encode("utf-8"),
        )

        yield handle


_init_lock = Lock()
_dll_handle_context_manager = None
_dll_handle = None


def init_mini_racer(
    *, flags: Iterable[str] = DEFAULT_V8_FLAGS, ignore_duplicate_init=False
) -> ctypes.CDLL:
    """Initialize py_mini_racer (and V8).

    This function can optionally be used to set V8 flags. This function can be called
    at most once, before any instances of MiniRacer are initialized. Instances of
    MiniRacer will automatically call this function to initialize MiniRacer and V8.
    """

    global _dll_handle_context_manager  # noqa: PLW0603
    global _dll_handle  # noqa: PLW0603

    with _init_lock:
        if _dll_handle is None:
            _dll_handle_context_manager = _open_dll(flags)
            _dll_handle = _dll_handle_context_manager.__enter__()
            # Note: we never call _dll_handle_context_manager.__exit__() because it's
            # designed as a singleton. But we could if we wanted to!
        elif not ignore_duplicate_init:
            raise LibAlreadyInitializedError

        return _dll_handle


class MiniRacer:
    """
    MiniRacer evaluates JavaScript code using a V8 isolate.

    Attributes:
        json_impl: JSON module used by helper methods default is
            [json](https://docs.python.org/3/library/json.html)
    """

    json_impl: ClassVar[object] = json

    def __init__(self):
        self._dll: ctypes.CDLL = init_mini_racer(ignore_duplicate_init=True)
        self.ctx = self._dll.mr_init_context()
        self.lock = Lock()

    @property
    def v8_version(self):
        """Return the V8 version string."""
        return str(self._dll.mr_v8_version())

    def eval(  # noqa: A003
        self, code: str, timeout: int | None = None, max_memory: int | None = None
    ):
        """Evaluate JavaScript code in the V8 isolate.

        Side effects from the JavaScript evaluation is persisted inside a context
        (meaning variables set are kept for the next evaluations).

        The JavaScript value returned by the last expression in `code` is converted to
        a Python value and returned by this method. Only primitive types are supported
        (numbers, strings, buffers...). Use the
        [py_mini_racer.py_mini_racer.MiniRacer.execute][] method to return more complex
        types such as arrays or objects.

        The evaluation can be interrupted by an exception for several reasons: a limit
        was reached, the code could not be parsed, a returned value could not be
        converted to a Python value.

        Args:
            code: JavaScript code
            timeout: number of milliseconds after which the execution is interrupted
            max_memory: hard memory limit after which the execution is interrupted
        """

        if isinstance(code, str):
            code = code.encode("utf8")

        with self.lock:
            res = self._dll.mr_eval_context(
                self.ctx,
                code,
                len(code),
                ctypes.c_ulong(timeout or 0),
                ctypes.c_size_t(max_memory or 0),
            )
        if not res:
            raise JSConversionException

        return MiniRacerValue(self, res).to_python()

    def execute(
        self, expr: str, timeout: int | None = None, max_memory: int | None = None
    ):
        """Helper to evaluate a JavaScript expression and return composite types.

        Returned value is serialized to JSON inside the V8 isolate and deserialized
        using `json_impl`.

        Args:
            expr: JavaScript expression
            timeout: number of milliseconds after which the execution is interrupted
            max_memory: hard memory limit after which the execution is interrupted
        """

        wrapped_expr = "JSON.stringify((function(){return (%s)})())" % expr
        ret = self.eval(wrapped_expr, timeout=timeout, max_memory=max_memory)
        if not isinstance(ret, str):
            raise WrongReturnTypeException(type(ret))
        return self.json_impl.loads(ret)

    def call(
        self,
        expr: str,
        *args,
        encoder: JSONEncoder | None = None,
        timeout: int | None = None,
        max_memory: int | None = None,
    ):
        """Helper to call a JavaScript function and return compositve types.

        The `expr` argument refers to a JavaScript function in the current V8
        isolate context. Further positional arguments are serialized using the JSON
        implementation `json_impl` and passed to the JavaScript function as arguments.

        Returned value is serialized to JSON inside the V8 isolate and deserialized
        using `json_impl`.

        Args:
            expr: JavaScript expression referring to a function
            encoder: Custom JSON encoder
            timeout: number of milliseconds after which the execution is
                interrupted
            max_memory: hard memory limit after which the execution is
                interrupted
        """

        json_args = self.json_impl.dumps(args, separators=(",", ":"), cls=encoder)
        js = f"{expr}.apply(this, {json_args})"
        return self.execute(js, timeout=timeout, max_memory=max_memory)

    def set_soft_memory_limit(self, limit):
        # type: (int) -> None
        """Set a soft memory limit on this V8 isolate.

        The Garbage Collection will use a more aggressive strategy when
        the soft limit is reached but the execution will not be stopped.

        :param int limit: memory limit in bytes or 0 to reset the limit
        """
        self._dll.mr_set_soft_memory_limit(self.ctx, limit)

    def was_soft_memory_limit_reached(self):
        # type: () -> bool
        """Return true if the soft memory limit was reached on the V8 isolate."""
        return self._dll.mr_soft_memory_limit_reached(self.ctx)

    def low_memory_notification(self):
        """Ask the V8 isolate to collect memory more aggressively."""
        self._dll.mr_low_memory_notification(self.ctx)

    def heap_stats(self):
        """Return the V8 isolate heap statistics."""

        with self.lock:
            res = self._dll.mr_heap_stats(self.ctx)

        if not res:
            return {
                "total_physical_size": 0,
                "used_heap_size": 0,
                "total_heap_size": 0,
                "total_heap_size_executable": 0,
                "heap_size_limit": 0,
            }

        return self.json_impl.loads(MiniRacerValue(self, res).to_python())

    def heap_snapshot(self):
        """Return a snapshot of the V8 isolate heap."""

        with self.lock:
            res = self._dll.mr_heap_snapshot(self.ctx)

        return MiniRacerValue(self, res).to_python()

    def _free(self, res):
        self._dll.mr_free_value(self.ctx, res)

    def __del__(self):
        dll = getattr(self, "_dll", None)
        if dll:
            self._dll.mr_free_context(getattr(self, "ctx", None))


# Compatibility with versions 0.4 & 0.5
StrictMiniRacer = MiniRacer


class MiniRacerTypes:
    """MiniRacer types identifier

    Note: it needs to be coherent with mini_racer.cc.
    """

    invalid = 0
    null = 1
    bool = 2  # noqa: A003
    integer = 3
    double = 4
    str_utf8 = 5
    # deprecated:
    array = 6
    # deprecated:
    hash = 7  # noqa: A003
    date = 8
    symbol = 9
    object = 10  # noqa: A003

    function = 100
    shared_array_buffer = 101
    array_buffer = 102

    execute_exception = 200
    parse_exception = 201
    oom_exception = 202
    timeout_exception = 203


class MiniRacerValueStruct(ctypes.Structure):
    _fields_: ClassVar[tuple[str, object]] = [
        ("value", ctypes.c_void_p),  # value is 8 bytes, works only for 64bit systems
        ("type", ctypes.c_int),
        ("len", ctypes.c_size_t),
    ]


class ArrayBufferByte(ctypes.Structure):
    # Cannot use c_ubyte directly because it uses <B
    # as an internal type but we need B for memoryview.
    _fields_: ClassVar[tuple[str, object]] = [
        ("b", ctypes.c_ubyte),
    ]
    _pack_ = 1


class MiniRacerValue:
    def __init__(self, ctx, ptr):
        self.ctx = ctx
        self.ptr = ptr

    def __str__(self):
        return str(self.to_python())

    @property
    def type(self):  # noqa: A003
        return self.ptr.contents.type

    @property
    def value(self):
        return self.ptr.contents.value

    @property
    def len(self):  # noqa: A003
        return self.ptr.contents.len

    def _double_value(self):
        ptr = ctypes.c_char_p.from_buffer(self.ptr.contents)
        return ctypes.c_double.from_buffer(ptr).value

    def _raise_from_error(self):
        if self.type == MiniRacerTypes.parse_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSParseException(msg)
        if self.type == MiniRacerTypes.execute_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSEvalException(msg.decode("utf-8", errors="replace"))
        if self.type == MiniRacerTypes.oom_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSOOMException(msg)
        if self.type == MiniRacerTypes.timeout_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSTimeoutException(msg)

    def to_python(self):
        self._raise_from_error()
        result = None
        typ = self.type
        if typ == MiniRacerTypes.null:
            result = None
        elif typ == MiniRacerTypes.bool:
            result = self.value == 1
        elif typ == MiniRacerTypes.integer:
            val = self.value
            result = 0 if val is None else ctypes.c_int32(val).value
        elif typ == MiniRacerTypes.double:
            result = self._double_value()
        elif typ == MiniRacerTypes.str_utf8:
            buf = ctypes.c_char_p(self.value)
            ptr = ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))
            result = ptr[0 : self.len].decode("utf8")
        elif typ == MiniRacerTypes.function:
            result = JSFunction()
        elif typ == MiniRacerTypes.date:
            timestamp = self._double_value()
            # JS timestamp are milliseconds, in python we are in seconds
            result = datetime.fromtimestamp(timestamp / 1000.0, timezone.utc)
        elif typ == MiniRacerTypes.symbol:
            result = JSSymbol()
        elif typ in (MiniRacerTypes.shared_array_buffer, MiniRacerTypes.array_buffer):
            cdata = (ArrayBufferByte * self.len).from_address(self.value)
            # Keep a reference to prevent the GC to free the backing store
            cdata._origin = self  # noqa: SLF001
            result = memoryview(cdata)
            # Avoids "NotImplementedError: memoryview: unsupported format T{<B:b:}"
            # in Python 3.12:
            result = result.cast("B")
        elif typ == MiniRacerTypes.object:
            return JSObject(self.value)
        else:
            raise JSConversionException
        return result

    def __del__(self):
        self.ctx._free(self.ptr)  # noqa: SLF001
