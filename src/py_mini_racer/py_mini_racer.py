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
from threading import Condition, Lock
from typing import Any, ClassVar, Iterable, Iterator, Union

Numeric = Union[int, float]


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


class JSTerminatedException(JSEvalException):
    """JavaScript execution terminated."""


class JSTimeoutException(JSEvalException):
    """JavaScript execution timed out."""

    def __init__(self):
        super().__init__("JavaScript was terminated by timeout")


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


class _MiniRacerValueUnion(ctypes.Union):
    _fields_: ClassVar[tuple[str, object]] = [
        ("ptr_val", ctypes.c_void_p),
        ("bytes_val", ctypes.POINTER(ctypes.c_char)),
        ("int_val", ctypes.c_int64),
        ("double_val", ctypes.c_double),
    ]


class _MiniRacerValueStruct(ctypes.Structure):
    _fields_: ClassVar[tuple[str, object]] = [
        ("value", _MiniRacerValueUnion),
        ("len", ctypes.c_size_t),
        ("type", ctypes.c_uint8),
    ]
    _pack_ = 1


class ArrayBufferByte(ctypes.Structure):
    # Cannot use c_ubyte directly because it uses <B
    # as an internal type but we need B for memoryview.
    _fields_: ClassVar[tuple[str, object]] = [
        ("b", ctypes.c_ubyte),
    ]
    _pack_ = 1


_MR_CALLBACK = ctypes.CFUNCTYPE(
    None, ctypes.py_object, ctypes.POINTER(_MiniRacerValueStruct)
)


class _MiniRacerValHolder:
    """An object which holds open a Python reference to a _MiniRacerValueStruct owned by
    a C++ MiniRacer context."""

    def __init__(self, ctx, ptr):
        self.ctx = ctx
        self.ptr = ptr

    def __del__(self):
        self.ctx._free(self.ptr)  # noqa: SLF001


def _build_dll_handle(dll_path) -> ctypes.CDLL:
    handle = ctypes.CDLL(dll_path)

    handle.mr_init_v8.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

    handle.mr_init_context.argtypes = []
    handle.mr_init_context.restype = ctypes.c_void_p

    handle.mr_eval.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint64,
        _MR_CALLBACK,
        ctypes.py_object,
    ]
    handle.mr_eval.restype = ctypes.c_void_p

    handle.mr_free_value.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

    handle.mr_free_context.argtypes = [ctypes.c_void_p]

    handle.mr_free_task_handle.argtypes = [ctypes.c_void_p]

    handle.mr_heap_stats.argtypes = [
        ctypes.c_void_p,
        _MR_CALLBACK,
        ctypes.py_object,
    ]
    handle.mr_heap_stats.restype = ctypes.c_void_p

    handle.mr_low_memory_notification.argtypes = [ctypes.c_void_p]

    handle.mr_heap_snapshot.argtypes = [
        ctypes.c_void_p,
        _MR_CALLBACK,
        ctypes.py_object,
    ]
    handle.mr_heap_snapshot.restype = ctypes.c_void_p

    handle.mr_set_hard_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]

    handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    handle.mr_set_soft_memory_limit.restype = None

    handle.mr_hard_memory_limit_reached.argtypes = [ctypes.c_void_p]
    handle.mr_hard_memory_limit_reached.restype = ctypes.c_bool

    handle.mr_soft_memory_limit_reached.argtypes = [ctypes.c_void_p]
    handle.mr_soft_memory_limit_reached.restype = ctypes.c_bool

    handle.mr_v8_version.restype = ctypes.c_char_p

    handle.mr_full_eval_call_count.argtypes = [ctypes.c_void_p]
    handle.mr_full_eval_call_count.restype = ctypes.c_uint64
    handle.mr_function_eval_call_count.argtypes = [ctypes.c_void_p]
    handle.mr_function_eval_call_count.restype = ctypes.c_uint64

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


# Emulating asyncio.Future without asyncio.
# (Or, concurrent.futures.Future without an executor)
class _SyncFuture:
    def __init__(self):
        self._cv = Condition()
        self._complete = False
        self._res = None
        self._exc = None

    def get(self, *, timeout: Numeric | None = None):
        with self._cv:
            while not self._complete:
                if not self._cv.wait(timeout=timeout):
                    raise JSTimeoutException

            if self._exc:
                raise self._exc
            return self._res

    def set_result(self, res):
        with self._cv:
            self._res = res
            self._complete = True
            self._cv.notify()

    def set_exception(self, exc):
        with self._cv:
            self._exc = exc
            self._complete = True
            self._cv.notify()


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
        self._active_callbacks = {}

        # define an all-purpose callback:
        @_MR_CALLBACK
        def mr_callback(future, result):
            future: _SyncFuture = self._active_callbacks.pop(future)
            try:
                value = self._mr_val_to_python(result)
                future.set_result(value)
            except Exception as exc:  # noqa: BLE001
                future.set_exception(exc)

        self._mr_callback = mr_callback

    @property
    def v8_version(self) -> str:
        """Return the V8 version string."""
        return str(self._dll.mr_v8_version())

    def eval(  # noqa: A003
        self,
        code: str,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> Any:
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
            timeout: number of milliseconds after which the execution is interrupted.
                This is deprecated; use timeout_sec instead.
            timeout_sec: number of seconds after which the execution is interrupted
            max_memory: hard memory limit, in bytes, after which the execution is
                interrupted.
        """

        if max_memory is not None:
            self.set_hard_memory_limit(max_memory)

        if timeout:
            # PyMiniRacer unfortunately uses milliseconds while Python and
            # Système international d'unités use seconds.
            timeout_sec = timeout / 1000

        if isinstance(code, str):
            code = code.encode("utf-8")

        def task(callback, future):
            return self._dll.mr_eval(
                self.ctx,
                code,
                len(code),
                callback,
                future,
            )

        with self._run_task(task) as future:
            return future.get(timeout=timeout_sec)

    def execute(
        self,
        expr: str,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> dict:
        """Helper to evaluate a JavaScript expression and return composite types.

        Returned value is serialized to JSON inside the V8 isolate and deserialized
        using `json_impl`.

        Args:
            expr: JavaScript expression
            timeout: number of milliseconds after which the execution is interrupted.
                This is deprecated; use timeout_sec instead.
            timeout_sec: number of seconds after which the execution is interrupted
            max_memory: hard memory limit, in bytes, after which the execution is
                interrupted.
        """

        if timeout:
            # PyMiniRacer unfortunately uses milliseconds while Python and
            # Système international d'unités use seconds.
            timeout_sec = timeout / 1000

        wrapped_expr = "JSON.stringify((function(){return (%s)})())" % expr
        ret = self.eval(wrapped_expr, timeout_sec=timeout_sec, max_memory=max_memory)
        if not isinstance(ret, str):
            raise WrongReturnTypeException(type(ret))
        return self.json_impl.loads(ret)

    def call(
        self,
        expr: str,
        *args,
        encoder: JSONEncoder | None = None,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> dict:
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
                interrupted.
            timeout_sec: number of seconds after which the execution is interrupted
            max_memory: hard memory limit, in bytes, after which the execution is
                interrupted
        """

        if timeout:
            # PyMiniRacer unfortunately uses milliseconds while Python and
            # Système international d'unités use seconds.
            timeout_sec = timeout / 1000

        json_args = self.json_impl.dumps(args, separators=(",", ":"), cls=encoder)
        js = f"{expr}.apply(this, {json_args})"
        return self.execute(js, timeout_sec=timeout_sec, max_memory=max_memory)

    def set_hard_memory_limit(self, limit: int) -> None:
        """Set a hard memory limit on this V8 isolate.

        JavaScript execution will be terminated when this limit is reached.

        :param int limit: memory limit in bytes or 0 to reset the limit
        """
        self._dll.mr_set_hard_memory_limit(self.ctx, limit)

    def set_soft_memory_limit(self, limit: int) -> None:
        """Set a soft memory limit on this V8 isolate.

        The Garbage Collection will use a more aggressive strategy when
        the soft limit is reached but the execution will not be stopped.

        :param int limit: memory limit in bytes or 0 to reset the limit
        """
        self._dll.mr_set_soft_memory_limit(self.ctx, limit)

    def was_hard_memory_limit_reached(self) -> bool:
        """Return true if the hard memory limit was reached on the V8 isolate."""
        return self._dll.mr_hard_memory_limit_reached(self.ctx)

    def was_soft_memory_limit_reached(self) -> bool:
        """Return true if the soft memory limit was reached on the V8 isolate."""
        return self._dll.mr_soft_memory_limit_reached(self.ctx)

    def low_memory_notification(self) -> None:
        """Ask the V8 isolate to collect memory more aggressively."""
        self._dll.mr_low_memory_notification(self.ctx)

    def heap_stats(self) -> dict:
        """Return the V8 isolate heap statistics."""

        def task(callback, future):
            return self._dll.mr_heap_stats(
                self.ctx,
                callback,
                future,
            )

        with self._run_task(task) as future:
            res = future.get()

        return self.json_impl.loads(res)

    def heap_snapshot(self) -> dict:
        """Return a snapshot of the V8 isolate heap."""

        def task(callback, future):
            return self._dll.mr_heap_snapshot(
                self.ctx,
                callback,
                future,
            )

        with self._run_task(task) as future:
            return future.get()

    def _function_eval_call_count(self) -> int:
        """Return the number of shortcut function-like evaluations done in this context.

        This is exposed for testing only."""
        return self._dll.mr_function_eval_call_count(self.ctx)

    def _full_eval_call_count(self) -> int:
        """Return the number of full script evaluations done in this context.

        This is exposed for testing only."""
        return self._dll.mr_full_eval_call_count(self.ctx)

    def _mr_val_to_python(self, ptr) -> Any:
        free_value = True
        try:
            typ = ptr.contents.type
            val = ptr.contents.value
            length = ptr.contents.len

            error_info = _ERRORS.get(ptr.contents.type)
            if error_info:
                klass, generic_msg = error_info

                msg = val.bytes_val[0:length].decode("utf-8")
                msg = msg or generic_msg

                raise klass(msg)

            if typ == MiniRacerTypes.null:
                return None
            if typ == MiniRacerTypes.bool:
                return val.int_val == 1
            if typ == MiniRacerTypes.integer:
                return val.int_val
            if typ == MiniRacerTypes.double:
                return val.double_val
            if typ == MiniRacerTypes.str_utf8:
                return val.bytes_val[0:length].decode("utf-8")
            if typ == MiniRacerTypes.function:
                return JSFunction()
            if typ == MiniRacerTypes.date:
                timestamp = val.double_val
                # JS timestamp are milliseconds, in python we are in seconds
                return datetime.fromtimestamp(timestamp / 1000.0, timezone.utc)
            if typ == MiniRacerTypes.symbol:
                return JSSymbol()
            if typ in (MiniRacerTypes.shared_array_buffer, MiniRacerTypes.array_buffer):
                cdata = (ArrayBufferByte * length).from_address(val.ptr_val)
                free_value = False
                # Keep a reference to prevent garbage collection of the backing store:
                cdata._origin = _MiniRacerValHolder(self, ptr)  # noqa: SLF001
                result = memoryview(cdata)
                # Avoids "NotImplementedError: memoryview: unsupported format T{<B:b:}"
                # in Python 3.12:
                return result.cast("B")
            if typ == MiniRacerTypes.object:
                # The C++ side puts the hash of the JS object into the value:
                return JSObject(val.int_val)

            raise JSConversionException
        finally:
            if free_value:
                self._free(ptr)

    def _free(self, res: _MiniRacerValueStruct) -> None:
        self._dll.mr_free_value(self.ctx, res)

    @contextmanager
    def _run_task(self, task):
        """Manages those tasks which generate callbacks from the MiniRacer DLL.

        Several MiniRacer functions (JS evaluation and 2 heap stats calls) are
        asynchronous. They take a function callback and callback data parameter, and
        they return a task handle.

        In this method, we create a future for each callback to get the right data to
        the right caller, and we manage the lifecycle of the task and task handle.
        """

        future = _SyncFuture()

        # Keep track of this future until we are called back.
        # This helps ensure Python doesn't garbage collect the future before the C++
        # side of things is done with it.
        self._active_callbacks[future] = future

        # Start the task:
        task_handle = task(self._mr_callback, future)
        try:
            # Let the caller handle waiting on the result:
            yield future
        finally:
            # Free the task handle.
            # Note this also cancels the task if it hasn't completed yet.
            self._dll.mr_free_task_handle(task_handle)

    def __del__(self):
        dll = getattr(self, "_dll", None)
        if dll:
            dll.mr_free_context(getattr(self, "ctx", None))


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
    terminated_exception = 204


_ERRORS = {
    MiniRacerTypes.parse_exception: (
        JSParseException,
        "Unknown JavaScript error during parse",
    ),
    MiniRacerTypes.execute_exception: (
        JSEvalException,
        "Uknown JavaScript error during execution",
    ),
    MiniRacerTypes.oom_exception: (JSOOMException, "JavaScript memory limit reached"),
    MiniRacerTypes.terminated_exception: (
        JSTerminatedException,
        "JavaScript was terminated",
    ),
}
