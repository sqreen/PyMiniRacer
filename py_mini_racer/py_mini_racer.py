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

from pkg_resources import resource_listdir, resource_filename

# In python 3 the extension file name depends on the python version
try:
    EXTENSION_NAME = fnmatch.filter(resource_listdir('py_mini_racer', '.'), '_v8*.so')[0]
    EXTENSION_PATH = resource_filename('py_mini_racer', EXTENSION_NAME)
except NotImplementedError:
    if not hasattr(sys, "_MEIPASS"):
        raise
    __location__ = os.path.join(sys._MEIPASS, "_v8") # pylint: disable=no-member, protected-access
    EXTENSION_NAME = fnmatch.filter(os.listdir(__location__), '_v8*.so')[0]
    EXTENSION_PATH = os.path.join(__location__, EXTENSION_NAME)

class MiniRacerBaseException(Exception):
    """ base MiniRacer exception class """
    pass

class JSParseException(MiniRacerBaseException):
    """ JS could not be parsed """
    pass

class JSEvalException(MiniRacerBaseException):
    """ JS could not be executed """
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
    python_version = sys.version_info[0]

    if python_version == 2:
        return isinstance(value, unicode)
    elif python_version == 3:
        return isinstance(value, str)
    else:
        raise NotImplementedError()


class MiniRacer(object):
    """ Ctypes wrapper arround binary mini racer
        https://docs.python.org/2/library/ctypes.html
    """

    def __init__(self):
        """ Init a JS context """

        self.ext = ctypes.CDLL(EXTENSION_PATH)

        self.ext.mr_init_context.restype = ctypes.c_void_p
        self.ext.mr_eval_context.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int]
        self.ext.mr_eval_context.restype = ctypes.POINTER(PythonValue)

        self.ext.mr_free_value.argtypes = [ctypes.c_void_p]

        self.ext.mr_free_context.argtypes = [ctypes.c_void_p]

        self.ext.mr_heap_stats.argtypes = [ctypes.c_void_p]
        self.ext.mr_heap_stats.restype = ctypes.POINTER(PythonValue)

        self.ctx = self.ext.mr_init_context()

        self.lock = threading.Lock()

    def free(self, res):
        """ Free value returned by mr_eval_context """

        self.ext.mr_free_value(res)

    def execute(self, js_str):
        """ Exec the given JS value """

        wrapped = "(function(){return (%s)})()" % js_str
        return self.eval(wrapped)

    def eval(self, js_str):
        """ Eval the JavaScript string """

        if is_unicode(js_str):
            bytes_val = js_str.encode("utf8")
        else:
            bytes_val = js_str

        self.lock.acquire()
        res = self.ext.mr_eval_context(self.ctx, bytes_val, len(bytes_val))
        self.lock.release()

        if bool(res) is False:
            raise JSConversionException()
        python_value = res.contents.to_python()
        self.free(res)
        return python_value

    def call(self, identifier, *args, **kwargs):
        """ Call the named function with provided arguments
        You can pass a custom JSON encoder by passing it in the encoder
        keyword only argument.
        """

        encoder = kwargs.get('encoder', None)

        json_args = json.dumps(args, separators=(',', ':'), cls=encoder)
        js = "{identifier}.apply(this, {json_args})"
        return self.eval(js.format(identifier=identifier, json_args=json_args))

    def heap_stats(self):
        """ Return heap statistics """

        self.lock.acquire()
        res = self.ext.mr_heap_stats(self.ctx)
        self.lock.release()

        python_value = res.contents.to_python()
        self.free(res)
        return python_value

    def __del__(self):
        """ Free the context """

        self.ext.mr_free_context(self.ctx)


class PythonTypes(object):
    """ Python types identifier - need to be coherent with
    mini_racer_extension.cc """

    null      =   1
    bool      =   2
    integer   =   3
    double    =   4
    str_utf8  =   5
    array     =   6
    hash      =   7
    date      =   8

    function  = 100

    execute_exception = 200
    parse_exception = 201

    invalid   = 300


class PythonValue(ctypes.Structure):
    """ Map to C PythonValue """
    _fields_ = [("value", ctypes.c_void_p),
                ("type", ctypes.c_int),
                ("len", ctypes.c_size_t)]

    def __str__(self):
        return str(self.to_python())

    def _double_value(self):
            ptr = ctypes.c_char_p.from_buffer(self)
            return ctypes.c_double.from_buffer(ptr).value

    def to_python(self):
        """ Return an object as native Python """

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
        elif self.type == PythonTypes.array:
            if self.len == 0:
                return []
            ary = []
            ary_addr = ctypes.c_void_p.from_address(self.value)
            ptr_to_ary = ctypes.pointer(ary_addr)
            for i in range(self.len):
                pval = PythonValue.from_address(ptr_to_ary[i])
                ary.append(pval.to_python())
            result = ary
        elif self.type == PythonTypes.hash:
            if self.len == 0:
                return {}
            res = {}
            hash_ary_addr = ctypes.c_void_p.from_address(self.value)
            ptr_to_hash = ctypes.pointer(hash_ary_addr)
            for i in range(self.len):
                pkey = PythonValue.from_address(ptr_to_hash[i*2])
                pval = PythonValue.from_address(ptr_to_hash[i*2+1])
                res[pkey.to_python()] = pval.to_python()
            result = res
        elif self.type == PythonTypes.function:
            result = JSFunction()
        elif self.type == PythonTypes.parse_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSParseException(msg)
        elif self.type == PythonTypes.execute_exception:
            msg = ctypes.c_char_p(self.value).value
            raise JSEvalException(msg.decode('utf-8', errors='replace'))
        elif self.type == PythonTypes.date:
            timestamp = self._double_value()
            # JS timestamp are milliseconds, in python we are in seconds
            result = datetime.datetime.utcfromtimestamp(timestamp / 1000.)
        else:
            raise WrongReturnTypeException("unknown type %d" % self.type)
        return result
