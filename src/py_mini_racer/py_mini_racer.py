from __future__ import annotations

import ctypes
import json
import sys
from asyncio import Future
from contextlib import ExitStack, contextmanager, suppress
from datetime import datetime, timezone
from importlib import resources
from itertools import count
from json import JSONEncoder
from operator import index as op_index
from os.path import exists
from os.path import join as pathjoin
from sys import platform, version_info
from threading import Condition, Lock
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    ClassVar,
    Generator,
    Iterable,
    Iterator,
    MutableMapping,
    MutableSequence,
    Union,
    cast,
)

Numeric = Union[int, float]


class MiniRacerBaseException(Exception):  # noqa: N818
    """Base MiniRacer exception."""


class LibNotFoundError(MiniRacerBaseException):
    """MiniRacer-wrapped V8 build not found."""

    def __init__(self, path: str):
        super().__init__(f"Native library or dependency not available at {path}")


class LibAlreadyInitializedError(MiniRacerBaseException):
    """MiniRacer-wrapped V8 build not found."""

    def __init__(self) -> None:
        super().__init__(
            "MiniRacer was already initialized before the call to init_mini_racer"
        )


class JSEvalException(MiniRacerBaseException):
    """JavaScript could not be executed."""


class JSParseException(JSEvalException):
    """JavaScript could not be parsed."""


def _get_exception_msg(reason: PythonJSConvertedTypes) -> str:
    if not isinstance(reason, JSMappedObject):
        return str(reason)

    if "stack" in reason:
        return cast(str, reason["stack"])

    return str(reason)


class JSPromiseError(MiniRacerBaseException):
    """JavaScript rejected a promise."""

    def __init__(self, reason: PythonJSConvertedTypes) -> None:
        super().__init__(
            f"JavaScript rejected promise with reason: {_get_exception_msg(reason)}\n"
        )
        self.reason = reason


class JSKeyError(JSEvalException, KeyError):
    """No such key found."""


class JSOOMException(JSEvalException):
    """JavaScript execution ran out of memory."""


class JSTerminatedException(JSEvalException):
    """JavaScript execution terminated."""


class JSValueError(JSEvalException, ValueError):
    """Bad value passed to JavaScript engine."""


class JSTimeoutException(JSEvalException):
    """JavaScript execution timed out."""

    def __init__(self) -> None:
        super().__init__("JavaScript was terminated by timeout")


class JSConversionException(MiniRacerBaseException):
    """Type could not be converted to or from JavaScript."""


class WrongReturnTypeException(MiniRacerBaseException):
    """Invalid type returned by the JavaScript runtime."""

    def __init__(self, typ: type):
        super().__init__(f"Unexpected return value type {typ}")


class JSArrayIndexError(IndexError, MiniRacerBaseException):
    """Invalid index into a JSArray."""

    def __init__(self) -> None:
        super().__init__("JSArray deletion out of range")


class JSUndefinedType:
    """The JavaScript undefined type.

    Where JavaScript null is represented as None, undefined is represented as this
    type."""

    def __bool__(self) -> bool:
        return False

    def __repr__(self) -> str:
        return "JSUndefined"


JSUndefined = JSUndefinedType()


class JSObject:
    """A JavaScript object."""

    def __init__(
        self,
        ctx: _Context,
        handle: _ValueHandle,
    ):
        self._ctx = ctx
        self._handle = handle

    def __hash__(self) -> int:
        return self._ctx.get_identity_hash(self)


PythonJSConvertedTypes = Union[
    None,
    JSUndefinedType,
    bool,
    int,
    float,
    str,
    JSObject,
    datetime,
    memoryview,
]


class JSMappedObject(
    JSObject,
    MutableMapping[PythonJSConvertedTypes, PythonJSConvertedTypes],
):
    """A JavaScript object with Pythonic MutableMapping methods (`keys()`,
    `__getitem__()`, etc).

    `keys()` and `__iter__()` will return properties from any prototypes as well as this
    object, as if using a for-in statement in JavaScript.
    """

    def __iter__(self) -> Iterator[PythonJSConvertedTypes]:
        return iter(self._get_own_property_names())

    def __getitem__(self, key: PythonJSConvertedTypes) -> PythonJSConvertedTypes:
        return self._ctx.get_object_item(self, key)

    def __setitem__(
        self, key: PythonJSConvertedTypes, val: PythonJSConvertedTypes
    ) -> None:
        self._ctx.set_object_item(self, key, val)

    def __delitem__(self, key: PythonJSConvertedTypes) -> None:
        self._ctx.del_object_item(self, key)

    def __len__(self) -> int:
        return len(self._get_own_property_names())

    def _get_own_property_names(self) -> tuple[PythonJSConvertedTypes, ...]:
        return self._ctx.get_own_property_names(self)


class JSArray(MutableSequence[PythonJSConvertedTypes], JSObject):
    """JavaScript array.

    Has Pythonic MutableSequence methods (e.g., `insert()`, `__getitem__()`, ...).
    """

    def __len__(self) -> int:
        ret = self._ctx.get_object_item(self, "length")
        return cast(int, ret)

    def __getitem__(self, index: int | slice) -> Any:
        if not isinstance(index, int):
            raise TypeError

        index = op_index(index)
        if index < 0:
            index += len(self)

        if 0 <= index < len(self):
            return self._ctx.get_object_item(self, index)

        raise IndexError

    def __setitem__(self, index: int | slice, val: Any) -> None:
        if not isinstance(index, int):
            raise TypeError

        self._ctx.set_object_item(self, index, val)

    def __delitem__(self, index: int | slice) -> None:
        if not isinstance(index, int):
            raise TypeError

        if index >= len(self) or index < -len(self):
            # JavaScript Array.prototype.splice() just ignores deletion beyond the
            # end of the array, meaning if you pass a very large value here it would
            # do nothing. Likewise, it just caps negative values at the length of the
            # array, meaning if you pass a very negative value here it would just
            # delete element 0.
            # For consistency with Python lists, let's tell the caller they're out of
            # bounds:
            raise JSArrayIndexError

        return self._ctx.del_from_array(self, index)

    def insert(self, index: int, new_obj: PythonJSConvertedTypes) -> None:
        return self._ctx.array_insert(self, index, new_obj)

    def __iter__(self) -> Iterator[PythonJSConvertedTypes]:
        for i in range(len(self)):
            yield self[i]


class JSFunction(JSMappedObject):
    """JavaScript function.

    You can call this object from Python, passing in positional args to match what the
    JavaScript function expects, along with a keyword argument, `timeout_sec`.
    """

    def __call__(
        self,
        *args: PythonJSConvertedTypes,
        this: JSObject | JSUndefinedType = JSUndefined,
        timeout_sec: Numeric | None = None,
    ) -> PythonJSConvertedTypes:
        return self._ctx.call_function(self, *args, this=this, timeout_sec=timeout_sec)


class JSSymbol(JSMappedObject):
    """JavaScript symbol."""


class JSPromise(JSObject):
    """JavaScript Promise.

    To get a value, call `promise.get()` to block, or `await promise` from within an
    `async` coroutine. Either will raise a Python exception if the JavaScript Promise
    is rejected.
    """

    def __init__(
        self,
        ctx: _Context,
        handle: _ValueHandle,
    ):
        super().__init__(ctx, handle)

    def get(self, *, timeout: Numeric | None = None) -> PythonJSConvertedTypes:
        """Get the value, or raise an exception. This call blocks.

        Args:
            timeout: number of milliseconds after which the execution is interrupted.
                This is deprecated; use timeout_sec instead.
        """

        return self._ctx.get_from_promise(self, timeout=timeout)

    def __await__(self) -> Generator[Any, None, Any]:
        return self._ctx.await_promise(self).__await__()


def _get_lib_filename(name: str) -> str:
    """Return the path of the library called `name` on the current system."""
    if platform == "darwin":
        prefix, ext = "lib", ".dylib"
    elif platform == "win32":
        prefix, ext = "", ".dll"
    else:
        prefix, ext = "lib", ".so"

    return prefix + name + ext


class _RawValueUnion(ctypes.Union):
    _fields_: ClassVar[list[tuple[str, object]]] = [
        ("value_ptr", ctypes.c_void_p),
        ("bytes_val", ctypes.POINTER(ctypes.c_char)),
        ("char_p_val", ctypes.c_char_p),
        ("int_val", ctypes.c_int64),
        ("double_val", ctypes.c_double),
    ]


class _RawValue(ctypes.Structure):
    _fields_: ClassVar[list[tuple[str, object]]] = [
        ("value", _RawValueUnion),
        ("len", ctypes.c_size_t),
        ("type", ctypes.c_uint8),
    ]
    _pack_ = 1


_RawValueHandle = ctypes.POINTER(_RawValue)

if TYPE_CHECKING:
    _RawValueHandleType = ctypes._Pointer[_RawValue]  # noqa: SLF001


class _ArrayBufferByte(ctypes.Structure):
    # Cannot use c_ubyte directly because it uses <B
    # as an internal type but we need B for memoryview.
    _fields_: ClassVar[list[tuple[str, object]]] = [
        ("b", ctypes.c_ubyte),
    ]
    _pack_ = 1


_MR_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_uint64, _RawValueHandle)


class _ValueHandle:
    """An object which holds open a Python reference to a _RawValue owned by
    a C++ MiniRacer context."""

    def __init__(self, ctx: _Context, raw: _RawValueHandleType):
        self.ctx = ctx
        self.raw: _RawValueHandleType = raw

    def __del__(self) -> None:
        self.ctx.free(self.raw)

    def to_python_or_raise(self) -> PythonJSConvertedTypes:
        val = self.to_python()
        if isinstance(val, JSEvalException):
            raise val
        return val

    def to_python(self) -> PythonJSConvertedTypes | JSEvalException:
        """Convert a binary value handle from the C++ side into a Python object."""

        # A MiniRacer binary value handle is a pointer to a structure which, for some
        # simple types like ints, floats, and strings, is sufficient to describe the
        # data, enabling us to convert the value immediately and free the handle.

        # For more complex types, like Objects and Arrays, the handle is just an opaque
        # pointer to a V8 object. In these cases, we retain the binary value handle,
        # wrapping it in a Python object. We can then use the handle in follow-on API
        # calls to work with the underlying V8 object.

        # In either case the handle is owned by the C++ side. It's the responsibility
        # of the Python side to call mr_free_value() when done with with the handle
        # to free up memory, but the C++ side will eventually free it on context
        # teardown either way.

        typ = self.raw.contents.type
        val = self.raw.contents.value
        length = self.raw.contents.len

        error_info = _ERRORS.get(self.raw.contents.type)
        if error_info:
            klass, generic_msg = error_info

            msg = val.bytes_val[0:length].decode("utf-8")
            msg = msg or generic_msg

            return klass(msg)

        if typ == _MiniRacerTypes.null:
            return None
        if typ == _MiniRacerTypes.undefined:
            return JSUndefined
        if typ == _MiniRacerTypes.bool:
            return bool(val.int_val == 1)
        if typ == _MiniRacerTypes.integer:
            return int(val.int_val)
        if typ == _MiniRacerTypes.double:
            return float(val.double_val)
        if typ == _MiniRacerTypes.str_utf8:
            return str(val.bytes_val[0:length].decode("utf-8"))
        if typ == _MiniRacerTypes.function:
            return JSFunction(self.ctx, self)
        if typ == _MiniRacerTypes.date:
            timestamp = val.double_val
            # JS timestamps are milliseconds. In Python we are in seconds:
            return datetime.fromtimestamp(timestamp / 1000.0, timezone.utc)
        if typ == _MiniRacerTypes.symbol:
            return JSSymbol(self.ctx, self)
        if typ in (_MiniRacerTypes.shared_array_buffer, _MiniRacerTypes.array_buffer):
            buf = _ArrayBufferByte * length
            cdata = buf.from_address(val.value_ptr)
            # Save a reference to ourselves to prevent garbage collection of the
            # backing store:
            cdata._origin = self  # noqa: SLF001
            result = memoryview(cdata)
            # Avoids "NotImplementedError: memoryview: unsupported format T{<B:b:}"
            # in Python 3.12:
            return result.cast("B")

        if typ == _MiniRacerTypes.promise:
            return JSPromise(self.ctx, self)

        if typ == _MiniRacerTypes.array:
            return JSArray(self.ctx, self)

        if typ == _MiniRacerTypes.object:
            return JSMappedObject(self.ctx, self)

        raise JSConversionException


def _build_dll_handle(dll_path: str) -> ctypes.CDLL:
    handle = ctypes.CDLL(dll_path)

    handle.mr_init_v8.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

    handle.mr_init_context.argtypes = [_MR_CALLBACK]
    handle.mr_init_context.restype = ctypes.c_uint64

    handle.mr_eval.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        ctypes.c_uint64,
    ]
    handle.mr_eval.restype = ctypes.c_uint64

    handle.mr_free_value.argtypes = [ctypes.c_uint64, _RawValueHandle]

    handle.mr_alloc_int_val.argtypes = [ctypes.c_uint64, ctypes.c_int64, ctypes.c_uint8]
    handle.mr_alloc_int_val.restype = _RawValueHandle

    handle.mr_alloc_double_val.argtypes = [
        ctypes.c_uint64,
        ctypes.c_double,
        ctypes.c_uint8,
    ]
    handle.mr_alloc_double_val.restype = _RawValueHandle

    handle.mr_alloc_string_val.argtypes = [
        ctypes.c_uint64,
        ctypes.c_char_p,
        ctypes.c_uint64,
        ctypes.c_uint8,
    ]
    handle.mr_alloc_string_val.restype = _RawValueHandle

    handle.mr_free_context.argtypes = [ctypes.c_uint64]

    handle.mr_context_count.argtypes = []
    handle.mr_context_count.restype = ctypes.c_size_t

    handle.mr_cancel_task.argtypes = [ctypes.c_uint64, ctypes.c_uint64]

    handle.mr_heap_stats.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    handle.mr_heap_stats.restype = ctypes.c_uint64

    handle.mr_low_memory_notification.argtypes = [ctypes.c_uint64]

    handle.mr_make_js_callback.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    handle.mr_make_js_callback.restype = _RawValueHandle

    handle.mr_heap_snapshot.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    handle.mr_heap_snapshot.restype = ctypes.c_uint64

    handle.mr_get_identity_hash.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
    ]
    handle.mr_get_identity_hash.restype = _RawValueHandle

    handle.mr_get_own_property_names.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
    ]
    handle.mr_get_own_property_names.restype = _RawValueHandle

    handle.mr_get_object_item.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        _RawValueHandle,
    ]
    handle.mr_get_object_item.restype = _RawValueHandle

    handle.mr_set_object_item.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        _RawValueHandle,
        _RawValueHandle,
    ]
    handle.mr_set_object_item.restype = _RawValueHandle

    handle.mr_del_object_item.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        _RawValueHandle,
    ]
    handle.mr_del_object_item.restype = _RawValueHandle

    handle.mr_splice_array.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        ctypes.c_int32,
        ctypes.c_int32,
        _RawValueHandle,
    ]
    handle.mr_splice_array.restype = _RawValueHandle

    handle.mr_call_function.argtypes = [
        ctypes.c_uint64,
        _RawValueHandle,
        _RawValueHandle,
        _RawValueHandle,
        ctypes.c_uint64,
    ]
    handle.mr_call_function.restype = ctypes.c_uint64

    handle.mr_set_hard_memory_limit.argtypes = [ctypes.c_uint64, ctypes.c_size_t]

    handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_uint64, ctypes.c_size_t]
    handle.mr_set_soft_memory_limit.restype = None

    handle.mr_hard_memory_limit_reached.argtypes = [ctypes.c_uint64]
    handle.mr_hard_memory_limit_reached.restype = ctypes.c_bool

    handle.mr_soft_memory_limit_reached.argtypes = [ctypes.c_uint64]
    handle.mr_soft_memory_limit_reached.restype = ctypes.c_bool

    handle.mr_v8_version.argtypes = []
    handle.mr_v8_version.restype = ctypes.c_char_p

    handle.mr_v8_is_using_sandbox.argtypes = []
    handle.mr_v8_is_using_sandbox.restype = ctypes.c_bool

    handle.mr_value_count.argtypes = [ctypes.c_uint64]
    handle.mr_value_count.restype = ctypes.c_size_t

    return handle


# V8 internationalization data:
_ICU_DATA_FILENAME = "icudtl.dat"

# V8 fast-startup snapshot; a dump of the heap after loading built-in JS
# modules:
_SNAPSHOT_FILENAME = "snapshot_blob.bin"

DEFAULT_V8_FLAGS = ("--single-threaded",)


def _open_resource_file(filename: str, exit_stack: ExitStack) -> str:
    if version_info >= (3, 9):
        # resources.as_file was added in Python 3.9
        resource_path = resources.files("py_mini_racer") / filename

        context_manager = resources.as_file(resource_path)
    else:
        # now-deprecated API for Pythons older than 3.9
        context_manager = resources.path("py_mini_racer", filename)

    return str(exit_stack.enter_context(context_manager))


def _check_path(path: str) -> None:
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
    *, flags: Iterable[str] = DEFAULT_V8_FLAGS, ignore_duplicate_init: bool = False
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


class _SyncFuture:
    """A blocking synchronization object for function return values.

    This is like asyncio.Future but blocking, or like
    concurrent.futures.Future but without an executor.
    """

    def __init__(self) -> None:
        self._cv = Condition()
        self._settled: bool = False
        self._res: PythonJSConvertedTypes = None
        self._exc: Exception | None = None

    def get(self, *, timeout: Numeric | None = None) -> PythonJSConvertedTypes:
        with self._cv:
            while not self._settled:
                if not self._cv.wait(timeout=timeout):
                    raise JSTimeoutException

            if self._exc:
                raise self._exc
            return self._res

    def set_result(self, res: PythonJSConvertedTypes) -> None:
        with self._cv:
            self._res = res
            self._settled = True
            self._cv.notify()

    def set_exception(self, exc: Exception) -> None:
        with self._cv:
            self._exc = exc
            self._settled = True
            self._cv.notify()


# V8 does not include an implementation of the setTimeout or clearTimeout APIs, because
# they are web APIs (defined in the DOM and Web Worker spec), not ECMAScript APIs.
# Nonetheless they are hugely useful and generally taken for granted, so let's go ahead
# and install a basic implementation.
# We base this on the Atomics.waitAsync API, which *is* part of the ECMAScript spec.
# (It would be possible for MiniRacer to implement this on the C++ side instead, but
# this way seems simpler.)
_INSTALL_SET_TIMEOUT = """
class __TimerManager {
  constructor() {
    this.next_idx = 0;
    this.pending = new Set();
  }

  setTimeout(func, delay) {
    const id = this.next_idx++;

    const shared = new SharedArrayBuffer(8);
    const view = new Int32Array(shared);

    const args = Array.prototype.slice.call(arguments, 2);

    let callback = () => {
        if (this.pending.has(id)) {
            this.pending.delete(id);
            func(...args);
        }
    };

    this.pending.add(id);
    Atomics.waitAsync(view, 0, 0, delay).value.then(() => callback());

    return id;
  }

  clearTimeout(timeout_id) {
    this.pending.delete(timeout_id);
  }
}

var __timer_manager = new __TimerManager();
var setTimeout = (...arguments) => __timer_manager.setTimeout(...arguments);
var clearTimeout = (...arguments) => __timer_manager.clearTimeout(...arguments);
"""


def _context_count() -> int:
    """For tests only: how many context handles are still allocated?"""

    dll = init_mini_racer(ignore_duplicate_init=True)
    return int(dll.mr_context_count())


class _Context:
    """Wrapper for all operations involving the DLL and C++ MiniRacer::Context."""

    def __init__(self) -> None:
        self._dll = init_mini_racer(ignore_duplicate_init=True)

        self._active_callbacks: dict[
            int, Callable[[PythonJSConvertedTypes | JSEvalException], None]
        ] = {}
        self._next_callback_id = count()

        # define an all-purpose callback:
        @_MR_CALLBACK  # type: ignore[misc]
        def mr_callback(callback_id: int, raw_val_handle: _RawValueHandleType) -> None:
            val_handle = self._wrap_raw_handle(raw_val_handle)
            callback = self._active_callbacks[callback_id]
            callback(val_handle.to_python())

        # We need to save a reference to the callback or else Python will GC it:
        self._mr_callback = mr_callback

        self._ctx = self._dll.mr_init_context(mr_callback)

    def v8_version(self) -> str:
        return str(self._dll.mr_v8_version().decode("utf-8"))

    def v8_is_using_sandbox(self) -> bool:
        """Checks for enablement of the V8 Sandbox. See https://v8.dev/blog/sandbox."""

        return bool(self._dll.mr_v8_is_using_sandbox())

    def evaluate(
        self,
        code: str,
        timeout_sec: Numeric | None = None,
    ) -> PythonJSConvertedTypes:
        code_handle = self._python_to_value_handle(code)

        with self._run_mr_task(self._dll.mr_eval, self._ctx, code_handle.raw) as future:
            return future.get(timeout=timeout_sec)

    def _attach_callbacks_to_promise(
        self,
        promise: JSPromise,
        future_caller: Callable[[Any], None],
    ) -> None:
        """Attach the given Python callbacks to a JS Promise."""

        cleanups: list[Callable[[], None]] = []

        def run_cleanups() -> None:
            for cleanup in cleanups:
                cleanup()

        def on_resolved_and_cleanup(
            value: PythonJSConvertedTypes | JSEvalException,
        ) -> None:
            run_cleanups()
            future_caller([False, cast(JSArray, value)])

        def on_rejected_and_cleanup(
            value: PythonJSConvertedTypes | JSEvalException,
        ) -> None:
            run_cleanups()
            future_caller([True, cast(JSArray, value)])

        (on_resolved_callback_cleanup, on_resolved_js_func) = self._make_js_callback(
            on_resolved_and_cleanup
        )
        cleanups.append(on_resolved_callback_cleanup)

        (on_rejected_callback_cleanup, on_rejected_js_func) = self._make_js_callback(
            on_rejected_and_cleanup
        )
        cleanups.append(on_rejected_callback_cleanup)

        promise_handle = self._python_to_value_handle(promise)
        then_name_handle = self._python_to_value_handle("then")
        then_func = self._wrap_raw_handle(
            self._dll.mr_get_object_item(
                self._ctx,
                promise_handle.raw,
                then_name_handle.raw,
            )
        ).to_python_or_raise()

        then_func = cast(JSFunction, then_func)
        then_func(on_resolved_js_func, on_rejected_js_func, this=promise)

    def _unpack_promise_results(self, results: Any) -> PythonJSConvertedTypes:
        is_rejected, argv = results
        result = cast(PythonJSConvertedTypes, cast(JSArray, argv)[0])
        if is_rejected:
            raise JSPromiseError(result)
        return result

    def get_from_promise(
        self, promise: JSPromise, timeout: Numeric | None = None
    ) -> PythonJSConvertedTypes:
        """Create and attach a Python _SyncFuture to a JS Promise."""

        future = _SyncFuture()

        def future_caller(value: Any) -> None:
            future.set_result(value)

        self._attach_callbacks_to_promise(promise, future_caller)

        results = future.get(timeout=timeout)
        return self._unpack_promise_results(results)

    async def await_promise(self, promise: JSPromise) -> PythonJSConvertedTypes:
        """Create and attach a Python asyncio.Future to a JS Promise."""

        future: Future[PythonJSConvertedTypes] = Future()
        loop = future.get_loop()

        def future_caller(value: Any) -> None:
            loop.call_soon_threadsafe(future.set_result, value)

        self._attach_callbacks_to_promise(promise, future_caller)

        results = await future
        return self._unpack_promise_results(results)

    def get_identity_hash(self, obj: JSObject) -> int:
        obj_handle = self._python_to_value_handle(obj)

        ret = self._wrap_raw_handle(
            self._dll.mr_get_identity_hash(self._ctx, obj_handle.raw)
        ).to_python_or_raise()
        return cast(int, ret)

    def get_own_property_names(
        self, obj: JSObject
    ) -> tuple[PythonJSConvertedTypes, ...]:
        obj_handle = self._python_to_value_handle(obj)

        names = self._wrap_raw_handle(
            self._dll.mr_get_own_property_names(self._ctx, obj_handle.raw)
        ).to_python_or_raise()
        if not isinstance(names, JSArray):
            raise TypeError
        return tuple(names)

    def get_object_item(
        self, obj: JSObject, key: PythonJSConvertedTypes
    ) -> PythonJSConvertedTypes:
        obj_handle = self._python_to_value_handle(obj)
        key_handle = self._python_to_value_handle(key)

        return self._wrap_raw_handle(
            self._dll.mr_get_object_item(
                self._ctx,
                obj_handle.raw,
                key_handle.raw,
            )
        ).to_python_or_raise()

    def set_object_item(
        self, obj: JSObject, key: PythonJSConvertedTypes, val: PythonJSConvertedTypes
    ) -> None:
        obj_handle = self._python_to_value_handle(obj)
        key_handle = self._python_to_value_handle(key)
        val_handle = self._python_to_value_handle(val)

        # Convert the value just to convert any exceptions (and GC the result)
        self._wrap_raw_handle(
            self._dll.mr_set_object_item(
                self._ctx,
                obj_handle.raw,
                key_handle.raw,
                val_handle.raw,
            )
        ).to_python_or_raise()

    def del_object_item(self, obj: JSObject, key: PythonJSConvertedTypes) -> None:
        obj_handle = self._python_to_value_handle(obj)
        key_handle = self._python_to_value_handle(key)

        # Convert the value just to convert any exceptions (and GC the result)
        self._wrap_raw_handle(
            self._dll.mr_del_object_item(
                self._ctx,
                obj_handle.raw,
                key_handle.raw,
            )
        ).to_python_or_raise()

    def del_from_array(self, arr: JSArray, index: int) -> None:
        arr_handle = self._python_to_value_handle(arr)

        # Convert the value just to convert any exceptions (and GC the result)
        self._wrap_raw_handle(
            self._dll.mr_splice_array(self._ctx, arr_handle.raw, index, 1, None)
        ).to_python_or_raise()

    def array_insert(
        self, arr: JSArray, index: int, new_val: PythonJSConvertedTypes
    ) -> None:
        arr_handle = self._python_to_value_handle(arr)
        new_val_handle = self._python_to_value_handle(new_val)

        # Convert the value just to convert any exceptions (and GC the result)
        self._wrap_raw_handle(
            self._dll.mr_splice_array(
                self._ctx,
                arr_handle.raw,
                index,
                0,
                new_val_handle.raw,
            )
        ).to_python_or_raise()

    def call_function(
        self,
        func: JSFunction,
        *args: PythonJSConvertedTypes,
        this: JSObject | JSUndefinedType = JSUndefined,
        timeout_sec: Numeric | None = None,
    ) -> PythonJSConvertedTypes:
        argv = cast(JSArray, self.evaluate("[]"))
        for arg in args:
            argv.append(arg)

        func_handle = self._python_to_value_handle(func)
        this_handle = self._python_to_value_handle(this)
        argv_handle = self._python_to_value_handle(argv)

        with self._run_mr_task(
            self._dll.mr_call_function,
            self._ctx,
            func_handle.raw,
            this_handle.raw,
            argv_handle.raw,
        ) as future:
            return future.get(timeout=timeout_sec)

    def set_hard_memory_limit(self, limit: int) -> None:
        self._dll.mr_set_hard_memory_limit(self._ctx, limit)

    def set_soft_memory_limit(self, limit: int) -> None:
        self._dll.mr_set_soft_memory_limit(self._ctx, limit)

    def was_hard_memory_limit_reached(self) -> bool:
        return bool(self._dll.mr_hard_memory_limit_reached(self._ctx))

    def was_soft_memory_limit_reached(self) -> bool:
        return bool(self._dll.mr_soft_memory_limit_reached(self._ctx))

    def low_memory_notification(self) -> None:
        self._dll.mr_low_memory_notification(self._ctx)

    def heap_stats(self) -> str:
        with self._run_mr_task(self._dll.mr_heap_stats, self._ctx) as future:
            return cast(str, future.get())

    def heap_snapshot(self) -> str:
        """Return a snapshot of the V8 isolate heap."""

        with self._run_mr_task(self._dll.mr_heap_snapshot, self._ctx) as future:
            return cast(str, future.get())

    def value_count(self) -> int:
        """For tests only: how many value handles are still allocated?"""

        return int(self._dll.mr_value_count(self._ctx))

    def _make_js_callback(
        self, func: Callable[[PythonJSConvertedTypes | JSEvalException], None]
    ) -> tuple[Callable[[], None], JSFunction]:
        """Make a JS callback which forwards to the given Python function.

        Note that it's crucial that the given Python function *not* call back
        into the C++ MiniRacer context, or it will deadlock. Instead it should
        signal another thread; e.g., by putting received data onto a queue or
        future.
        """

        callback_id = next(self._next_callback_id)

        def cleanup() -> None:
            self._active_callbacks.pop(callback_id)

        self._active_callbacks[callback_id] = func

        js_callback = self._wrap_raw_handle(
            self._dll.mr_make_js_callback(self._ctx, callback_id)
        )
        js_callback_py = js_callback.to_python_or_raise()
        return cleanup, cast(JSFunction, js_callback_py)

    def _wrap_raw_handle(self, raw: _RawValueHandleType) -> _ValueHandle:
        return _ValueHandle(self, raw)

    def _python_to_value_handle(self, obj: PythonJSConvertedTypes) -> _ValueHandle:
        if isinstance(obj, JSObject):
            # JSObjects originate from the V8 side. We can just send back the handle
            # we originally got. (This also covers derived types JSFunction, JSSymbol,
            # JSPromise, and JSArray.)
            return obj._handle  # noqa: SLF001

        if obj is None:
            return self._wrap_raw_handle(
                self._dll.mr_alloc_int_val(
                    self._ctx,
                    0,
                    _MiniRacerTypes.null,
                )
            )
        if obj is JSUndefined:
            return self._wrap_raw_handle(
                self._dll.mr_alloc_int_val(
                    self._ctx,
                    0,
                    _MiniRacerTypes.undefined,
                )
            )
        if isinstance(obj, bool):
            return self._wrap_raw_handle(
                self._dll.mr_alloc_int_val(
                    self._ctx,
                    1 if obj else 0,
                    _MiniRacerTypes.bool,
                )
            )
        if isinstance(obj, int):
            if obj - 2**31 <= obj < 2**31:
                return self._wrap_raw_handle(
                    self._dll.mr_alloc_int_val(
                        self._ctx,
                        obj,
                        _MiniRacerTypes.integer,
                    )
                )

            # We transmit ints as int32, so "upgrade" to double upon overflow.
            # (ECMAScript numeric is double anyway, but V8 does internally distinguish
            # int types, so we try and preserve integer-ness for round-tripping
            # purposes.)
            # JS BigInt would be a closer representation of Python int, but upgrading
            # to BigInt would probably be surprising for most applications, so for now,
            # we approximate with double:
            return self._wrap_raw_handle(
                self._dll.mr_alloc_double_val(
                    self._ctx,
                    obj,
                    _MiniRacerTypes.double,
                )
            )
        if isinstance(obj, float):
            return self._wrap_raw_handle(
                self._dll.mr_alloc_double_val(
                    self._ctx,
                    obj,
                    _MiniRacerTypes.double,
                )
            )
        if isinstance(obj, str):
            b = obj.encode("utf-8")
            return self._wrap_raw_handle(
                self._dll.mr_alloc_string_val(
                    self._ctx,
                    b,
                    len(b),
                    _MiniRacerTypes.str_utf8,
                )
            )
        if isinstance(obj, datetime):
            # JS timestamps are milliseconds. In Python we are in seconds:
            return self._wrap_raw_handle(
                self._dll.mr_alloc_double_val(
                    self._ctx,
                    obj.timestamp() * 1000.0,
                    _MiniRacerTypes.date,
                )
            )

        # Note: we skip shared array buffers, so for now at least, handles to shared
        # array buffers can only be transmitted from JS to Python.

        raise JSConversionException

    def free(self, res: _RawValueHandleType) -> None:
        if self._dll:
            self._dll.mr_free_value(self._ctx, res)

    @contextmanager
    def _run_mr_task(self, dll_method: Any, *args: Any) -> Iterator[_SyncFuture]:
        """Manages those tasks which generate callbacks from the MiniRacer DLL.

        Several MiniRacer functions (JS evaluation and 2 heap stats calls) are
        asynchronous. They take a function callback and callback data parameter, and
        they return a task handle.

        In this method, we create a future for each callback to get the right data to
        the right caller, and we manage the lifecycle of the task and task handle.
        """

        future = _SyncFuture()

        def callback(value: PythonJSConvertedTypes | JSEvalException) -> None:
            if isinstance(value, JSEvalException):
                future.set_exception(value)
            else:
                future.set_result(value)

        callback_id = next(self._next_callback_id)
        self._active_callbacks[callback_id] = callback

        # Start the task:
        task_id = dll_method(*args, callback_id)
        try:
            # Let the caller handle waiting on the result:
            yield future
        finally:
            # Cancel the task if it's not already done (this call is ignored if it's
            # already done)
            self._dll.mr_cancel_task(self._ctx, task_id)

            # If the caller gives up on waiting, let's at least await the
            # cancelation error for GC purposes:
            with suppress(Exception):
                future.get()

            self._active_callbacks.pop(callback_id)

    def __del__(self) -> None:
        if self._dll:
            self._dll.mr_free_context(self._ctx)


class MiniRacer:
    """
    MiniRacer evaluates JavaScript code using a V8 isolate.

    Attributes:
        json_impl: JSON module used by helper methods default is
            [json](https://docs.python.org/3/library/json.html)
    """

    json_impl: ClassVar[Any] = json

    def __init__(self) -> None:
        self._ctx = _Context()

        self.eval(_INSTALL_SET_TIMEOUT)

    @property
    def v8_version(self) -> str:
        """Return the V8 version string."""
        return self._ctx.v8_version()

    def eval(  # noqa: A003
        self,
        code: str,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> PythonJSConvertedTypes:
        """Evaluate JavaScript code in the V8 isolate.

        Side effects from the JavaScript evaluation is persisted inside a context
        (meaning variables set are kept for the next evaluation).

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

        return self._ctx.evaluate(code=code, timeout_sec=timeout_sec)

    def execute(
        self,
        expr: str,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> Any:
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
        *args: Any,
        encoder: JSONEncoder | None = None,
        timeout: Numeric | None = None,
        timeout_sec: Numeric | None = None,
        max_memory: int | None = None,
    ) -> Any:
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
        self._ctx.set_hard_memory_limit(limit)

    def set_soft_memory_limit(self, limit: int) -> None:
        """Set a soft memory limit on this V8 isolate.

        The Garbage Collection will use a more aggressive strategy when
        the soft limit is reached but the execution will not be stopped.

        :param int limit: memory limit in bytes or 0 to reset the limit
        """
        self._ctx.set_soft_memory_limit(limit)

    def was_hard_memory_limit_reached(self) -> bool:
        """Return true if the hard memory limit was reached on the V8 isolate."""
        return self._ctx.was_hard_memory_limit_reached()

    def was_soft_memory_limit_reached(self) -> bool:
        """Return true if the soft memory limit was reached on the V8 isolate."""
        return self._ctx.was_soft_memory_limit_reached()

    def low_memory_notification(self) -> None:
        """Ask the V8 isolate to collect memory more aggressively."""
        self._ctx.low_memory_notification()

    def heap_stats(self) -> Any:
        """Return the V8 isolate heap statistics."""

        return self.json_impl.loads(self._ctx.heap_stats())


# Compatibility with versions 0.4 & 0.5
StrictMiniRacer = MiniRacer


class _MiniRacerTypes:
    """MiniRacer types identifier

    Note: it needs to be coherent with mini_racer.cc.
    """

    invalid = 0
    null = 1
    bool = 2  # noqa: A003
    integer = 3
    double = 4
    str_utf8 = 5
    array = 6
    # deprecated:
    hash = 7  # noqa: A003
    date = 8
    symbol = 9
    object = 10  # noqa: A003
    undefined = 11

    function = 100
    shared_array_buffer = 101
    array_buffer = 102
    promise = 103

    execute_exception = 200
    parse_exception = 201
    oom_exception = 202
    timeout_exception = 203
    terminated_exception = 204
    value_exception = 205
    key_exception = 206


_ERRORS: dict[int, tuple[type[JSEvalException], str]] = {
    _MiniRacerTypes.parse_exception: (
        JSParseException,
        "Unknown JavaScript error during parse",
    ),
    _MiniRacerTypes.execute_exception: (
        JSEvalException,
        "Uknown JavaScript error during execution",
    ),
    _MiniRacerTypes.oom_exception: (JSOOMException, "JavaScript memory limit reached"),
    _MiniRacerTypes.terminated_exception: (
        JSTerminatedException,
        "JavaScript was terminated",
    ),
    _MiniRacerTypes.key_exception: (
        JSKeyError,
        "No such key found in object",
    ),
    _MiniRacerTypes.value_exception: (
        JSValueError,
        "Bad value passed to JavaScript engine",
    ),
}
