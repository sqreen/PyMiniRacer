# -*- coding: utf-8 -*-
""" PyMiniRacer main wrappers """
# pylint: disable=bad-whitespace,too-few-public-methods

import sys
import os
import json
import ctypes
import threading
import datetime
import fnmatch
import sysconfig

try:
    import pkg_resources
except ImportError:
    pkg_resources = None  # pragma: no cover


def _get_libc_name():
    """Return the libc of the system."""
    target = sysconfig.get_config_var("HOST_GNU_TYPE")
    if target is not None and target.endswith("musl"):
        return "muslc"
    return "glibc"


def _get_lib_path(name):
    """Return the path of the library called `name`."""
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
    UNICODE_TYPE = unicode
else:
    UNICODE_TYPE = str


class MiniRacerBaseException(Exception):
    """ base MiniRacer exception class """
    pass

class JSParseException(MiniRacerBaseException):
    """ JS could not be parsed """
    pass

class JSEvalException(MiniRacerBaseException):
    """ JS could not be executed """
    pass

class JSOOMException(JSEvalException):
    """ JS execution out of memory """
    pass

class JSTimeoutException(JSEvalException):
    """ JS execution timed out """
    pass

class JSConversionException(MiniRacerBaseException):
    """ type could not be converted """
    pass

class WrongReturnTypeException(MiniRacerBaseException):
    """ type returned by JS cannot be parsed """
    pass

class JSFunction(object):
    """ type for JS functions """
    pass

class JSSymbol(object):
    """ type for JS symbols """
    pass


def is_unicode(value):
    """ Check if a value is a valid unicode string, compatible with python 2 and python 3

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


_ext_handle = None


def _fetch_ext_handle():
    global _ext_handle

    if _ext_handle:
        return _ext_handle

    if EXTENSION_PATH is None or not os.path.exists(EXTENSION_PATH):
        raise RuntimeError("Native library not available at {}".format(EXTENSION_PATH))
    _ext_handle = ctypes.CDLL(EXTENSION_PATH)

    _ext_handle.mr_init_context.restype = ctypes.c_void_p

    _ext_handle.mr_eval_context.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_ulong,
        ctypes.c_size_t,
        ctypes.c_bool]
    _ext_handle.mr_eval_context.restype = ctypes.POINTER(MiniRacerValue)

    _ext_handle.mr_free_value.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

    _ext_handle.mr_free_context.argtypes = [ctypes.c_void_p]

    _ext_handle.mr_heap_stats.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_heap_stats.restype = ctypes.POINTER(MiniRacerValue)

    _ext_handle.mr_low_memory_notification.argtypes = [ctypes.c_void_p]

    _ext_handle.mr_heap_snapshot.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_heap_snapshot.restype = ctypes.POINTER(MiniRacerValue)

    _ext_handle.mr_set_soft_memory_limit.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    _ext_handle.mr_set_soft_memory_limit.restype = None

    _ext_handle.mr_soft_memory_limit_reached.argtypes = [ctypes.c_void_p]
    _ext_handle.mr_soft_memory_limit_reached.restype = ctypes.c_bool

    _ext_handle.mr_v8_version.restype = ctypes.c_char_p

    return _ext_handle


class MiniRacer(object):
    """ Ctypes wrapper arround binary mini racer
        https://docs.python.org/2/library/ctypes.html
    """

    json_impl = json

    def __init__(self):
        """ Init a JS context """

        self.ext = _fetch_ext_handle()
        self.ctx = self.ext.mr_init_context()
        self.lock = threading.Lock()

    def set_soft_memory_limit(self, limit):
        """ Set instance soft memory limit """
        self.ext.mr_set_soft_memory_limit(self.ctx, limit)

    def was_soft_memory_limit_reached(self):
        """ Tell if the instance soft memory limit was reached """
        return self.ext.mr_soft_memory_limit_reached(self.ctx)

    def execute(self, expr, timeout=0, max_memory=0):
        """ Helper method to execute an expression with JSON serialization of returned value.
        """
        wrapped_expr = u"JSON.stringify((function(){return (%s)})())" % expr
        ret = self.eval(wrapped_expr, timeout=timeout, max_memory=max_memory)
        if not is_unicode(ret):
            raise ValueError(u"Unexpected return value type {}".format(type(ret)))
        return self.json_impl.loads(ret)

    def call(self, expr, *args, **kwargs):
        """ Helper method to call a function returned by expr with the given arguments.

        You can pass a custom JSON encoder in the encoder keyword argument to encode arguments
        and the function return value.
        """

        encoder = kwargs.get('encoder', None)
        timeout = kwargs.get('timeout', 0)
        max_memory = kwargs.get('max_memory', 0)

        json_args = self.json_impl.dumps(args, separators=(',', ':'), cls=encoder)
        js = u"{expr}.apply(this, {json_args})".format(expr=expr, json_args=json_args)
        return self.execute(js, timeout=timeout, max_memory=max_memory)

    def fast_call(self, js_identifier, timeout=0, max_memory=0):
        """ Call the function referenced by a global identifier with provided arguments.
        """
        return self.eval(js_identifier, timeout=timeout, max_memory=max_memory, fast_call=True)

    def eval(self, js_str, timeout=0, max_memory=0, fast_call=False):
        """ Eval the JavaScript string """

        if is_unicode(js_str):
            js_str = js_str.encode("utf8")

        with self.lock:
            res = self.ext.mr_eval_context(self.ctx,
                                           js_str,
                                           len(js_str),
                                           ctypes.c_ulong(timeout),
                                           ctypes.c_size_t(max_memory),
                                           ctypes.c_bool(fast_call))
        if not res:
            raise JSConversionException()

        return PythonValue(self, res).to_python()

    def low_memory_notification(self):
        """ Ask the V8 VM to collect memory more aggressively.
        """
        self.ext.mr_low_memory_notification(self.ctx)

    def heap_stats(self):
        """ Return heap statistics """

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

        return self.json_impl.loads(PythonValue(self, res).to_python())

    def heap_snapshot(self):
        """ Return heap snapshot """

        with self.lock:
            res = self.ext.mr_heap_snapshot(self.ctx)

        return PythonValue(self, res).to_python()

    def _free(self, res):
        """ Free value returned by mr_eval_context """

        self.ext.mr_free_value(self.ctx, res)

    def __del__(self):
        """ Free the context """

        self.ext.mr_free_context(self.ctx)

    def v8_version(self):
        return UNICODE_TYPE(self.ext.mr_v8_version())


# Compatibility with versions 0.4 & 0.5
StrictMiniRacer = MiniRacer


class PythonTypes(object):
    """ Python types identifier - need to be coherent with
    mini_racer_extension.cc """

    invalid   =   0
    null      =   1
    bool      =   2
    integer   =   3
    double    =   4
    str_utf8  =   5
    array     =   6  # deprecated
    hash      =   7  # deprecated
    date      =   8
    symbol    =   9

    function  = 100
    shared_array_buffer = 101

    execute_exception = 200
    parse_exception = 201
    oom_exception = 202
    timeout_exception = 203


class MiniRacerValue(ctypes.Structure):
    """ Map to C """
    _fields_ = [("value", ctypes.c_void_p),
                ("type", ctypes.c_int),
                ("len", ctypes.c_size_t)]


class PythonValue:

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
        if self.type == PythonTypes.parse_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSParseException(msg)
        elif self.type == PythonTypes.execute_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSEvalException(msg.decode('utf-8', errors='replace'))
        elif self.type == PythonTypes.oom_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSOOMException(msg)
        elif self.type == PythonTypes.timeout_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSTimeoutException(msg)

    def to_python(self):
        self._raise_from_error()
        result = None
        if self.type == PythonTypes.null:
            result = None
        elif self.type == PythonTypes.bool:
            result = self.value == 1
        elif self.type == PythonTypes.integer:
            if self.value is None:
                result = 0
            else:
                result = ctypes.c_int32(self.value).value
        elif self.type == PythonTypes.double:
            result = self._double_value()
        elif self.type == PythonTypes.str_utf8:
            buf = ctypes.c_char_p(self.value)
            ptr = ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))
            result = ptr[0:self.len].decode("utf8")
        elif self.type == PythonTypes.function:
            result = JSFunction()
        elif self.type == PythonTypes.date:
            timestamp = self._double_value()
            # JS timestamp are milliseconds, in python we are in seconds
            result = datetime.datetime.utcfromtimestamp(timestamp / 1000.)
        elif self.type == PythonTypes.symbol:
            result = JSSymbol()
        elif self.type == PythonTypes.shared_array_buffer:
            cdata = (ctypes.c_ubyte * self.len).from_address(self.value)
            # Keep a reference to prevent the GC to free the backing store
            cdata._origin = self
            result = memoryview(cdata)
        else:
            raise JSConversionException()
        return result

    def __del__(self):
        self.ctx._free(self.ptr)
