from __future__ import annotations

import ctypes
import json
import sys
from asyncio import Future
from collections.abc import MutableMapping, MutableSequence
from contextlib import ExitStack, contextmanager
from datetime import datetime, timezone
from importlib import resources
from json import JSONEncoder
from operator import index as op_index
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
    """Type could not be converted to or from JavaScript."""


class WrongReturnTypeException(MiniRacerBaseException):
    """Invalid type returned by the JavaScript runtime."""

    def __init__(self, typ):
        super().__init__(f"Unexpected return value type {typ}")


class JSArrayIndexError(IndexError):
    """Invalid index into a JSArray."""

    def __init__(self):
        super().__init__("JSArray deletion out of range")


class JSUndefinedType:
    """The JavaScript undefined type.

    Where JavaScript null is represented as None, undefined is represented as this
    type."""

    def __bool__(self):
        return False

    def __repr__(self):
        return "JSUndefined"


JSUndefined = JSUndefinedType()


class JSObject:
    """A JavaScript object."""

    def __init__(
        self,
        ctx,
        bv_ptr: _MiniRacerBinaryValuePtr,
    ):
        self._bv_holder = _MiniRacerBinaryValueHolder(ctx, bv_ptr)
        self._ctx = ctx


class JSMappedObject(MutableMapping, JSObject):
    """A JavaScript object with Pythonic MutableMapping methods (keys(),
    __getitem__(), etc).

    keys() and __iter__() will return properties from any prototypes as well as this
    object, as if using a for-in statement in JavaScript.
    """

    def __hash__(self):
        return self._ctx.get_identity_hash(self._bv_holder.bv_ptr)

    def keys(self):
        return self._ctx.get_own_property_names(self._bv_holder.bv_ptr)

    def __iter__(self):
        return iter(self.keys())

    def __getitem__(self, key):
        return self._ctx.get_object_item(self._bv_holder.bv_ptr, key)

    def __setitem__(self, key, obj):
        return self._ctx.set_object_item(self._bv_holder.bv_ptr, key, obj)

    def __delitem__(self, key):
        return self._ctx.del_object_item(self._bv_holder.bv_ptr, key)

    def __len__(self):
        return len(self.keys())


class JSArray(MutableSequence, JSObject):
    """JavaScript array.

    Has Pythonic MutableSequence methods (e.g., insert(), __getitem__(), ,,,).
    """

    def __len__(self):
        return self._ctx.get_object_item(self._bv_holder.bv_ptr, "length")

    def __getitem__(self, index: int):
        index = op_index(index)
        if index < 0:
            index += len(self)

        if 0 <= index < len(self):
            return self._ctx.get_object_item(self._bv_holder.bv_ptr, index)

        raise IndexError

    def __setitem__(self, index: int, obj):
        return self._ctx.set_object_item(self._bv_holder.bv_ptr, index, obj)

    def __delitem__(self, index: int):
        if index >= len(self) or index < -len(self):
            # JavaScript Array.prototype.splice() just ignores deletion beyond the
            # end of the array, meaning if you pass a very large value here it would
            # do nothing. Likewise, it just caps negative values at the length of the
            # array, meaning if you pass a very negative value here it would just
            # delete element 0.
            # For consistency with Python lists, let's tell the caller they're out of
            # bounds:
            raise JSArrayIndexError

        return self._ctx.del_from_array(self._bv_holder.bv_ptr, index)

    def insert(self, index: int, new_obj):
        return self._ctx.array_insert(self._bv_holder.bv_ptr, index, new_obj)

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]


class JSFunction(JSMappedObject):
    """JavaScript function."""

    def __call__(self, *args, this=None, timeout_sec: Numeric | None = None):
        return self._ctx.call_function(
            self._bv_holder.bv_ptr, *args, this=this, timeout_sec=timeout_sec
        )


class JSSymbol(JSMappedObject):
    """JavaScript symbol."""


class JSPromise(JSMappedObject):
    """JavaScript promise."""

    def __init__(
        self,
        ctx,
        bv_ptr,
        to_sync_future,
        to_async_future,
    ):
        super().__init__(ctx, bv_ptr)
        self._to_sync_future = to_sync_future
        self._to_async_future = to_async_future

    def get(self, *, timeout: Numeric | None = None):
        """Get the value, or raise an exception. This call blocks.

        Args:
            timeout: number of milliseconds after which the execution is interrupted.
                This is deprecated; use timeout_sec instead.
        """

        return self._to_sync_future().get(timeout=timeout)

    def __await__(self):
        return self._to_async_future().__await__()


def _get_lib_filename(name):
    """Return the path of the library called `name` on the current system."""
    if platform == "darwin":
        prefix, ext = "lib", ".dylib"
    elif platform == "win32":
        prefix, ext = "", ".dll"
    else:
        prefix, ext = "lib", ".so"

    return prefix + name + ext


class _MiniRacerBinaryValueUnion(ctypes.Union):
    _fields_: ClassVar[tuple[str, object]] = [
        ("value_ptr", ctypes.c_void_p),
        ("bytes_val", ctypes.POINTER(ctypes.c_char)),
        ("char_p_val", ctypes.c_char_p),
        ("int_val", ctypes.c_int64),
        ("double_val", ctypes.c_double),
    ]


class _MiniRacerBinaryValue(ctypes.Structure):
    _fields_: ClassVar[tuple[str, object]] = [
        ("value", _MiniRacerBinaryValueUnion),
        ("len", ctypes.c_size_t),
        ("type", ctypes.c_uint8),
    ]
    _pack_ = 1


_MiniRacerBinaryValuePtr = ctypes.POINTER(_MiniRacerBinaryValue)


class ArrayBufferByte(ctypes.Structure):
    # Cannot use c_ubyte directly because it uses <B
    # as an internal type but we need B for memoryview.
    _fields_: ClassVar[tuple[str, object]] = [
        ("b", ctypes.c_ubyte),
    ]
    _pack_ = 1


_MR_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.py_object, _MiniRacerBinaryValuePtr)


class _MiniRacerBinaryValueHolder:
    """An object which holds open a Python reference to a _MiniRacerBinaryValue owned by
    a C++ MiniRacer context."""

    def __init__(self, ctx, bv_ptr: _MiniRacerBinaryValuePtr):
        self.ctx = ctx
        self.bv_ptr: _MiniRacerBinaryValuePtr = bv_ptr

    def __del__(self):
        self.ctx._free(self.bv_ptr)  # noqa: SLF001


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

    handle.mr_attach_promise_then.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        _MR_CALLBACK,
        ctypes.py_object,
    ]

    handle.mr_heap_snapshot.argtypes = [
        ctypes.c_void_p,
        _MR_CALLBACK,
        ctypes.py_object,
    ]
    handle.mr_heap_snapshot.restype = ctypes.c_void_p

    handle.mr_get_identity_hash.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
    ]
    handle.mr_get_identity_hash.restype = ctypes.c_int

    handle.mr_get_own_property_names.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
    ]
    handle.mr_get_own_property_names.restype = _MiniRacerBinaryValuePtr

    handle.mr_get_object_item.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
    ]
    handle.mr_get_object_item.restype = _MiniRacerBinaryValuePtr

    handle.mr_set_object_item.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
    ]

    handle.mr_del_object_item.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
    ]
    handle.mr_del_object_item.restype = ctypes.c_bool

    handle.mr_splice_array.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        ctypes.c_int32,
        ctypes.c_int32,
        _MiniRacerBinaryValuePtr,
    ]
    handle.mr_splice_array.restype = _MiniRacerBinaryValuePtr

    handle.mr_call_function.argtypes = [
        ctypes.c_void_p,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
        _MiniRacerBinaryValuePtr,
        _MR_CALLBACK,
        ctypes.py_object,
    ]
    handle.mr_call_function.restype = ctypes.c_void_p

    handle.mr_set_hard_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]

    handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    handle.mr_set_soft_memory_limit.restype = None

    handle.mr_hard_memory_limit_reached.argtypes = [ctypes.c_void_p]
    handle.mr_hard_memory_limit_reached.restype = ctypes.c_bool

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


class _SyncFuture:
    """A blocking synchronization object for function return values.

    This is like asyncio.Future but blocking, or like
    concurrent.futures.Future but without an executor.
    """

    def __init__(self):
        self._cv = Condition()
        self._settled = False
        self._res = None
        self._exc = None

    def get(self, *, timeout: Numeric | None = None):
        with self._cv:
            while not self._settled:
                if not self._cv.wait(timeout=timeout):
                    raise JSTimeoutException

            if self._exc:
                raise self._exc
            return self._res

    def set_result(self, res):
        with self._cv:
            self._res = res
            self._settled = True
            self._cv.notify()

    def set_exception(self, exc):
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

        # define an all-purpose callback for _SyncFuture:
        @_MR_CALLBACK
        def mr_sync_callback(future, result):
            self._active_callbacks.pop(future)
            try:
                value = self._binary_value_ptr_to_python(result)
                future.set_result(value)
            except Exception as exc:  # noqa: BLE001
                future.set_exception(exc)

        self._mr_sync_callback = mr_sync_callback

        self.eval(_INSTALL_SET_TIMEOUT)

        # define an all-purpose callback for asyncio.Future:
        @_MR_CALLBACK
        def mr_async_callback(future, result):
            self._active_callbacks.pop(future)
            loop = future.get_loop()
            try:
                value = self._binary_value_ptr_to_python(result)
                loop.call_soon_threadsafe(future.set_result, value)
            except Exception as exc:  # noqa: BLE001
                loop.call_soon_threadsafe(future.set_exception, exc)

        self._mr_async_callback = mr_async_callback

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

        with self._run_mr_async_task(
            self._dll.mr_eval, self.ctx, code, len(code)
        ) as future:
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

    def get_identity_hash(self, bv_ptr: _MiniRacerBinaryValuePtr) -> int:
        return self._dll.mr_get_identity_hash(self.ctx, bv_ptr)

    def get_own_property_names(self, bv_ptr: _MiniRacerBinaryValuePtr) -> int:
        names_bv_ptr = self._dll.mr_get_own_property_names(self.ctx, bv_ptr)
        names = self._binary_value_ptr_to_python(names_bv_ptr)
        if not isinstance(names, JSArray):
            raise TypeError
        return tuple(names)

    def get_object_item(self, bv_ptr: ctypes.c_void_p, key: Any):
        key_bv_ptr = self._python_to_binary_value_ptr(key)
        val_bv_ptr = self._dll.mr_get_object_item(self.ctx, bv_ptr, key_bv_ptr)

        if not val_bv_ptr:
            raise KeyError(key)

        return self._binary_value_ptr_to_python(val_bv_ptr)

    def set_object_item(self, bv_ptr: _MiniRacerBinaryValuePtr, key: Any, val: Any):
        key_bv_ptr = self._python_to_binary_value_ptr(key)
        val_bv_ptr = self._python_to_binary_value_ptr(val)
        self._dll.mr_set_object_item(self.ctx, bv_ptr, key_bv_ptr, val_bv_ptr)

    def del_object_item(self, bv_ptr: _MiniRacerBinaryValuePtr, key: Any):
        key_bv_ptr = self._python_to_binary_value_ptr(key)
        success = self._dll.mr_del_object_item(self.ctx, bv_ptr, key_bv_ptr)
        if not success:
            raise KeyError(key)

    def del_from_array(self, bv_ptr: _MiniRacerBinaryValuePtr, index: int):
        res = self._dll.mr_splice_array(self.ctx, bv_ptr, index, 1, None)
        # Convert the value just to convert any exceptions (and GC the result)
        _ = self._binary_value_ptr_to_python(res)

    def array_insert(self, bv_ptr: _MiniRacerBinaryValuePtr, index: int, new_val: Any):
        new_val_bv_ptr = self._python_to_binary_value_ptr(new_val)
        res = self._dll.mr_splice_array(self.ctx, bv_ptr, index, 0, new_val_bv_ptr)
        # Convert the value just to convert any exceptions (and GC the result)
        _ = self._binary_value_ptr_to_python(res)

    def call_function(
        self,
        func_ptr: _MiniRacerBinaryValuePtr,
        *args,
        this=None,
        timeout_sec: Numeric | None = None,
    ):
        argv = self.eval("[]")
        for arg in args:
            argv.append(arg)

        argv_ptr = self._python_to_binary_value_ptr(argv)
        this_ptr = self._python_to_binary_value_ptr(this)

        with self._run_mr_async_task(
            self._dll.mr_call_function, self.ctx, func_ptr, this_ptr, argv_ptr
        ) as future:
            return future.get(timeout=timeout_sec)

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

        with self._run_mr_async_task(self._dll.mr_heap_stats, self.ctx) as future:
            res = future.get()

        return self.json_impl.loads(res)

    def heap_snapshot(self) -> dict:
        """Return a snapshot of the V8 isolate heap."""

        with self._run_mr_async_task(self._dll.mr_heap_snapshot, self.ctx) as future:
            return future.get()

    def _make_sync_future(self, hold=None):
        future = _SyncFuture()

        # Keep track of this future until we are called back.
        # This helps ensure Python doesn't garbage collect the future before the C++
        # side of things is done with it.
        # The items in "hold" are the things we passed into the C++ call, likewise
        # here to avoid garbage collection until the callback completes.
        self._active_callbacks[future] = hold

        return future

    def _make_async_future(self, hold=None):
        future = Future()

        # Keep track of this future until we are called back.
        # This helps ensure Python doesn't garbage collect the future before the C++
        # side of things is done with it.
        self._active_callbacks[future] = hold

        return future

    def _binary_value_ptr_to_python(self, bv_ptr: _MiniRacerBinaryValuePtr) -> Any:
        free_value = True
        try:
            typ = bv_ptr.contents.type
            val = bv_ptr.contents.value
            length = bv_ptr.contents.len

            error_info = _ERRORS.get(bv_ptr.contents.type)
            if error_info:
                klass, generic_msg = error_info

                msg = val.bytes_val[0:length].decode("utf-8")
                msg = msg or generic_msg

                raise klass(msg)

            if typ == MiniRacerTypes.null:
                return None
            if typ == MiniRacerTypes.undefined:
                return JSUndefined
            if typ == MiniRacerTypes.bool:
                return val.int_val == 1
            if typ == MiniRacerTypes.integer:
                return val.int_val
            if typ == MiniRacerTypes.double:
                return val.double_val
            if typ == MiniRacerTypes.str_utf8:
                return val.bytes_val[0:length].decode("utf-8")
            if typ == MiniRacerTypes.function:
                free_value = False
                return JSFunction(self, bv_ptr)
            if typ == MiniRacerTypes.date:
                timestamp = val.double_val
                # JS timestamps are milliseconds. In Python we are in seconds:
                return datetime.fromtimestamp(timestamp / 1000.0, timezone.utc)
            if typ == MiniRacerTypes.symbol:
                free_value = False
                return JSSymbol(self, bv_ptr)
            if typ in (MiniRacerTypes.shared_array_buffer, MiniRacerTypes.array_buffer):
                buf = ArrayBufferByte * length
                cdata = buf.from_address(val.value_ptr)
                # Keep a reference to prevent garbage collection of the backing store:
                free_value = False
                cdata._origin = _MiniRacerBinaryValueHolder(self, bv_ptr)  # noqa: SLF001
                result = memoryview(cdata)
                # Avoids "NotImplementedError: memoryview: unsupported format T{<B:b:}"
                # in Python 3.12:
                return result.cast("B")

            if typ == MiniRacerTypes.promise:
                free_value = False

                def attach_future(future, callback):
                    self._dll.mr_attach_promise_then(
                        self.ctx,
                        bv_ptr,
                        callback,
                        future,
                    )
                    return future

                def to_sync_future():
                    return attach_future(
                        self._make_sync_future(), self._mr_sync_callback
                    )

                def to_async_future():
                    return attach_future(
                        self._make_async_future(), self._mr_async_callback
                    )

                return JSPromise(self, bv_ptr, to_sync_future, to_async_future)

            if typ == MiniRacerTypes.array:
                free_value = False
                return JSArray(self, bv_ptr)

            if typ == MiniRacerTypes.object:
                free_value = False
                return JSMappedObject(self, bv_ptr)

            raise JSConversionException
        finally:
            if free_value:
                self._free(bv_ptr)

    def _python_to_binary_value_ptr(self, obj) -> _MiniRacerBinaryValue:
        if isinstance(obj, JSObject):
            # JSObjects originate from the V8 side. We can just send back pointers to
            # them. (This also covers derived types JSFunction, JSSymbol, JSPromise,
            # and JSArray.)
            return obj._bv_holder.bv_ptr  # noqa: SLF001

        val = _MiniRacerBinaryValue()

        if obj is None:
            val.type = MiniRacerTypes.null
            val.value.int_val = 0
            val.len = 0
            return val
        if obj is JSUndefined:
            val.type = MiniRacerTypes.undefined
            val.value.int_val = 0
            val.len = 0
            return val
        if isinstance(obj, bool):
            val.type = MiniRacerTypes.bool
            val.value.int_val = 1 if obj else 0
            val.len = 0
            return val
        if isinstance(obj, int):
            if obj - 2**31 <= obj < 2**31:
                val.type = MiniRacerTypes.integer
                val.value.int_val = obj
                val.len = 0
                return val

            # We transmit ints as int32, so "upgrade" to double upon overflow.
            # (ECMAScript numeric is double anyway, but V8 does internally distinguish
            # int types, so we try and preserve integer-ness for round-tripping
            # purposes.)
            # JS BigInt would be a closer representation of Python int, but upgrading
            # to BigInt would probably be surprising for most applications, so for now,
            # we approximate with double:
            val.type = MiniRacerTypes.double
            val.value.double_val = obj
            val.len = 0
            return val
        if isinstance(obj, float):
            val.type = MiniRacerTypes.double
            val.value.double_val = obj
            val.len = 0
            return val
        if isinstance(obj, str):
            val.type = MiniRacerTypes.str_utf8
            b = obj.encode("utf-8")
            val.value.char_p_val = b
            val.len = len(b)
            return val
        if isinstance(obj, datetime):
            val.type = MiniRacerTypes.date
            # JS timestamps are milliseconds. In Python we are in seconds:
            val.value.double_val = obj.timestamp() * 1000.0
            val.len = 0
            return val

        # Note: we skip shared array buffers, so for now at least, handles to shared
        # array buffers can only be transmitted from JS to Python.

        raise JSConversionException

    def _free(self, res: _MiniRacerBinaryValue) -> None:
        if self._dll and self.ctx:
            self._dll.mr_free_value(self.ctx, res)

    @contextmanager
    def _run_mr_async_task(self, dll_method, *args):
        """Manages those tasks which generate callbacks from the MiniRacer DLL.

        Several MiniRacer functions (JS evaluation and 2 heap stats calls) are
        asynchronous. They take a function callback and callback data parameter, and
        they return a task handle.

        In this method, we create a future for each callback to get the right data to
        the right caller, and we manage the lifecycle of the task and task handle.
        """

        # Stuff the args into a map along with the future, so they aren't GC'd until
        # the task completes:
        future = self._make_sync_future(hold=args)

        # Start the task:
        task_handle = dll_method(*args, self._mr_sync_callback, future)
        try:
            # Let the caller handle waiting on the result:
            yield future
        finally:
            # Free the task handle.
            # Note this also cancels the task if it hasn't completed yet.
            self._dll.mr_free_task_handle(task_handle)

    def __del__(self):
        if self._dll and self.ctx:
            # Because finalizers aren't necessarily called in any consistent order,
            # it's possible for this MiniRacer.__del__() method to be called before
            # _MiniRacerBinaryValueHolder.__del__() which calls MiniRacer._free() on
            # this same object.
            # To prevent further attempted use of the context, remove the variable.
            # (Note that the C++ MiniRacer object tracks all _MiniRacerBinaryValue
            # instances and frees them upon its own destruction, so there is no memory
            # leak in practice when belated calls to MiniRacer._free() are ignored.)
            ctx, self.ctx = self.ctx, None
            self._dll.mr_free_context(ctx)


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
