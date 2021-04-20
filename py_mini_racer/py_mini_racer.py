# -*- coding: utf-8 -*-
# pylint: disable=bad-whitespace,too-few-public-methods

import ctypes
import datetime
import json
import os
import sys
import sysconfig
import threading

try:
    import pkg_resources
except ImportError:
    pkg_resources = None  # pragma: no cover


def _get_libc_name():
    """Return the libc name of the current system."""
    target = sysconfig.get_config_var("HOST_GNU_TYPE")
    if target is not None and target.endswith("musl"):
        return "muslc"
    return "glibc"


def _get_lib_path(name):
    """Return the path of the library called `name` on the current system."""
    if os.name == "posix" and sys.platform == "darwin":
        prefix, ext = "lib", ".dylib"
    elif sys.platform == "win32":
        prefix, ext = "", ".dll"
    else:
        prefix, ext = "lib", ".{}.so".format(_get_libc_name())
    fn = None
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass is not None:
        fn = os.path.join(meipass, prefix + name + ext)
    if fn is None and pkg_resources is not None:
        fn = pkg_resources.resource_filename("py_mini_racer", prefix + name + ext)
    if fn is None:
        root_dir = os.path.dirname(os.path.abspath(__file__))
        fn = os.path.join(root_dir, prefix + name + ext)
    return fn


# In python 3 the extension file name depends on the python version
EXTENSION_PATH = _get_lib_path("mini_racer")
EXTENSION_NAME = os.path.basename(EXTENSION_PATH) if EXTENSION_PATH is not None else None


if sys.version_info[0] < 3:
    UNICODE_TYPE = unicode  # noqa: F821
else:
    from typing import Any, Optional

    UNICODE_TYPE = str


class MiniRacerBaseException(Exception):
    """Base MiniRacer exception."""


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


class JSObject(object):
    """JavaScript object."""

    def __init__(self, id):
        self.id = id

    def __hash__(self):
        return self.id


class JSFunction(object):
    """JavaScript function."""


class JSSymbol(object):
    """JavaScript symbol."""


def is_unicode(value):
    """Check if a value is a valid unicode string.

    >>> is_unicode(u'foo')
    True
    >>> is_unicode(u'✌')
    True
    >>> is_unicode(b'foo')
    False
    >>> is_unicode(42)
    False
    >>> is_unicode(('abc',))
    False
    """
    return isinstance(value, UNICODE_TYPE)


def _build_ext_handle():

    if EXTENSION_PATH is None or not os.path.exists(EXTENSION_PATH):
        raise RuntimeError("Native library not available at {}".format(EXTENSION_PATH))

    _ext_handle = ctypes.CDLL(EXTENSION_PATH)

    _ext_handle.mr_init_context.argtypes = [ctypes.c_char_p]
    _ext_handle.mr_init_context.restype = ctypes.c_void_p

    _ext_handle.mr_eval_context.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_ulong,
        ctypes.c_size_t]
    _ext_handle.mr_eval_context.restype = ctypes.POINTER(MiniRacerValueStruct)

    _ext_handle.mr_free_value.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

    _ext_handle.mr_free_context.argtypes = [ctypes.c_void_p]

    _ext_handle.mr_heap_stats.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_heap_stats.restype = ctypes.POINTER(MiniRacerValueStruct)

    _ext_handle.mr_low_memory_notification.argtypes = [ctypes.c_void_p]

    _ext_handle.mr_heap_snapshot.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_heap_snapshot.restype = ctypes.POINTER(MiniRacerValueStruct)

    _ext_handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    _ext_handle.mr_set_soft_memory_limit.restype = None

    _ext_handle.mr_soft_memory_limit_reached.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_soft_memory_limit_reached.restype = ctypes.c_bool

    _ext_handle.mr_v8_version.restype = ctypes.c_char_p

    return _ext_handle


class MiniRacer(object):
    """
    MiniRacer evaluates JavaScript code using a V8 isolate.

    :cvar json_impl: JSON module used by helper methods default is :py:mod:`json`
    :cvar v8_flags: Flags used for V8 initialization
    :vartype v8_flags: class attribute list of str
    """

    json_impl = json
    v8_flags = ["--single-threaded"]
    ext = None

    def __init__(self):
        if self.__class__.ext is None:
            self.__class__.ext = _build_ext_handle()

        self.ctx = self.ext.mr_init_context(" ".join(self.v8_flags).encode("utf-8"))
        self.lock = threading.Lock()

    @property
    def v8_version(self):
        """Return the V8 version string."""
        return UNICODE_TYPE(self.ext.mr_v8_version())

    def eval(self, code, timeout=None, max_memory=None):
        # type: (str, Optional[int], Optional[int]) -> Any
        """Evaluate JavaScript code in the V8 isolate.

        Side effects from the JavaScript evaluation is persisted inside a context
        (meaning variables set are kept for the next evaluations).

        The JavaScript value returned by the last expression in `code` is converted
        to a Python value and returned by this method. Only primitive types are
        supported (numbers, strings, buffers...). Use the :py:meth:`.execute` method to return
        more complex types such as arrays or objects.

        The evaluation can be interrupted by an exception for several reasons: a limit
        was reached, the code could not be parsed, a returned value could not be
        converted to a Python value.

        :param code: JavaScript code
        :param timeout: number of milliseconds after which the execution is interrupted
        :param max_memory: hard memory limit after which the execution is interrupted
        """

        if is_unicode(code):
            code = code.encode("utf8")

        with self.lock:
            res = self.ext.mr_eval_context(self.ctx,
                                           code,
                                           len(code),
                                           ctypes.c_ulong(timeout or 0),
                                           ctypes.c_size_t(max_memory or 0))
        if not res:
            raise JSConversionException()

        return MiniRacerValue(self, res).to_python()

    def execute(self, expr, timeout=None, max_memory=None):
        # type: (str, Optional[int], Optional[int]) -> Any
        """Helper to evaluate a JavaScript expression and return composite types.

        Returned value is serialized to JSON inside the V8 isolate and deserialized
        using :py:attr:`.json_impl`.

        :param expr: JavaScript expression
        :param timeout: number of milliseconds after which the execution is interrupted
        :param max_memory: hard memory limit after which the execution is interrupted
        """
        wrapped_expr = u"JSON.stringify((function(){return (%s)})())" % expr
        ret = self.eval(wrapped_expr, timeout=timeout, max_memory=max_memory)
        if not is_unicode(ret):
            raise ValueError(u"Unexpected return value type {}".format(type(ret)))
        return self.json_impl.loads(ret)

    def call(self, expr, *args, **kwargs):
        """Helper to call a JavaScript function and return compositve types.

        The `expr` argument refers to a JavaScript function in the current V8
        isolate context. Further positional arguments are serialized using the JSON
        implementation :py:attr:`.json_impl` and passed to the JavaScript function
        as arguments.

        Returned value is serialized to JSON inside the V8 isolate and deserialized
        using :py:attr:`.json_impl`.

        :param str expr: JavaScript expression referring to a function
        :param encoder: Custom JSON encoder
        :type encoder: JSONEncoder or None
        :param int timeout: number of milliseconds after which the execution is interrupted
        :param int max_memory: hard memory limit after which the execution is interrupted
        """

        encoder = kwargs.get('encoder', None)
        timeout = kwargs.get('timeout', None)
        max_memory = kwargs.get('max_memory', None)

        json_args = self.json_impl.dumps(args, separators=(',', ':'), cls=encoder)
        js = u"{expr}.apply(this, {json_args})".format(expr=expr, json_args=json_args)
        return self.execute(js, timeout=timeout, max_memory=max_memory)

    def set_soft_memory_limit(self, limit):
        # type: (int) -> None
        """Set a soft memory limit on this V8 isolate.

        The Garbage Collection will use a more aggressive strategy when
        the soft limit is reached but the execution will not be stopped.

        :param int limit: memory limit in bytes or 0 to reset the limit
        """
        self.ext.mr_set_soft_memory_limit(self.ctx, limit)

    def was_soft_memory_limit_reached(self):
        # type: () -> bool
        """Return true if the soft memory limit was reached on the V8 isolate."""
        return self.ext.mr_soft_memory_limit_reached(self.ctx)

    def low_memory_notification(self):
        """Ask the V8 isolate to collect memory more aggressively."""
        self.ext.mr_low_memory_notification(self.ctx)

    def heap_stats(self):
        """Return the V8 isolate heap statistics."""

        with self.lock:
            res = self.ext.mr_heap_stats(self.ctx)

        if not res:
            return {
                u"total_physical_size": 0,
                u"used_heap_size": 0,
                u"total_heap_size": 0,
                u"total_heap_size_executable": 0,
                u"heap_size_limit": 0
            }

        return self.json_impl.loads(MiniRacerValue(self, res).to_python())

    def heap_snapshot(self):
        """Return a snapshot of the V8 isolate heap."""

        with self.lock:
            res = self.ext.mr_heap_snapshot(self.ctx)

        return MiniRacerValue(self, res).to_python()

    def _free(self, res):
        self.ext.mr_free_value(self.ctx, res)

    def __del__(self):
        self.ext.mr_free_context(getattr(self, "ctx", None))


# Compatibility with versions 0.4 & 0.5
StrictMiniRacer = MiniRacer


class MiniRacerTypes(object):
    """MiniRacer types identifier

    Note: it needs to be coherent with mini_racer_extension.cc.
    """

    invalid = 0
    null = 1
    bool = 2
    integer = 3
    double = 4
    str_utf8 = 5
    array = 6  # deprecated
    hash = 7  # deprecated
    date = 8
    symbol = 9
    object = 10

    function = 100
    shared_array_buffer = 101
    array_buffer = 102

    execute_exception = 200
    parse_exception = 201
    oom_exception = 202
    timeout_exception = 203


class MiniRacerValueStruct(ctypes.Structure):
    _fields_ = [("value", ctypes.c_void_p),  # value is 8 bytes, works only for 64bit systems
                ("type", ctypes.c_int),
                ("len", ctypes.c_size_t)]


class ArrayBufferByte(ctypes.Structure):
    # Cannot use c_ubyte directly because it uses <B
    # as an internal type but we need B for memoryview.
    _fields_ = [("b", ctypes.c_ubyte)]
    _pack_ = 1


class MiniRacerValue(object):

    def __init__(self, ctx, ptr):
        self.ctx = ctx
        self.ptr = ptr

    def __str__(self):
        return str(self.to_python())

    @property
    def type(self):
        return self.ptr.contents.type

    @property
    def value(self):
        return self.ptr.contents.value

    @property
    def len(self):
        return self.ptr.contents.len

    def _double_value(self):
        ptr = ctypes.c_char_p.from_buffer(self.ptr.contents)
        return ctypes.c_double.from_buffer(ptr).value

    def _raise_from_error(self):
        if self.type == MiniRacerTypes.parse_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSParseException(msg)
        elif self.type == MiniRacerTypes.execute_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSEvalException(msg.decode('utf-8', errors='replace'))
        elif self.type == MiniRacerTypes.oom_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSOOMException(msg)
        elif self.type == MiniRacerTypes.timeout_exception:
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
            if val is None:
                result = 0
            else:
                result = ctypes.c_int32(val).value
        elif typ == MiniRacerTypes.double:
            result = self._double_value()
        elif typ == MiniRacerTypes.str_utf8:
            buf = ctypes.c_char_p(self.value)
            ptr = ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))
            result = ptr[0:self.len].decode("utf8")
        elif typ == MiniRacerTypes.function:
            result = JSFunction()
        elif typ == MiniRacerTypes.date:
            timestamp = self._double_value()
            # JS timestamp are milliseconds, in python we are in seconds
            result = datetime.datetime.utcfromtimestamp(timestamp / 1000.)
        elif typ == MiniRacerTypes.symbol:
            result = JSSymbol()
        elif typ == MiniRacerTypes.shared_array_buffer or typ == MiniRacerTypes.array_buffer:
            cdata = (ArrayBufferByte * self.len).from_address(self.value)
            # Keep a reference to prevent the GC to free the backing store
            cdata._origin = self
            result = memoryview(cdata)
        elif typ == MiniRacerTypes.object:
            return JSObject(self.value)
        else:
            raise JSConversionException()
        return result

    def __del__(self):
        self.ctx._free(self.ptr)
