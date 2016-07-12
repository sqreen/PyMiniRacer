# -*- coding: utf-8 -*-
""" PyMiniRacer main wrappers """
# pylint: disable=bad-whitespace,too-few-public-methods

import ctypes
import threading
import pkg_resources

import enum

EXTENSION_PATH = pkg_resources.resource_filename('py_mini_racer', '_v8.so')


class MiniRacerBaseException(Exception):
    """ base MiniRacer exception class """
    pass

class JSEvalException(MiniRacerBaseException):
    """ JS could not be executed """
    pass

class WrongReturnTypeException(MiniRacerBaseException):
    """ type returned by JS cannot be parsed """
    pass

class JSFunction(object):
    """ type for JS functions """
    pass

class MiniRacer(object):
    """ Ctypes wrapper arround binary mini racer """

    def __init__(self):
        """ Init a JS context """

        self.ext = ctypes.CDLL(EXTENSION_PATH)

        self.ext.pmr_init_context.restype = ctypes.c_void_p
        self.ext.pmr_eval_context.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p]
        self.ext.pmr_eval_context.restype = ctypes.POINTER(PythonValue)

        self.ext.pmr_free_value.argtypes = [ctypes.c_void_p]

        self.ext.pmr_free_context.argtypes = [ctypes.c_void_p]

        self.ctx = self.ext.pmr_init_context()

        self.lock = threading.Lock()

    def free(self, res):
        """ Free value returned by pmr_eval_context """

        self.ext.pmr_free_value(res)

    def execute(self, js_str):
        """ Exec the given JS value """

        return self.eval("(function(){return (%s)})()" % js_str)

    def eval(self, js_str):
        """ Eval the JavaScript string """

        self.lock.acquire()
        res = self.ext.pmr_eval_context(self.ctx, js_str)
        self.lock.release()
        python_value = res.contents.to_python()
        self.free(res)
        return python_value

    def __del__(self):
        """ Free the context """
        self.ext.pmr_free_context(self.ctx)


class PythonTypes(enum.Enum):
    """ Python types identifier - need to be coherent with
    mini_racer_extension.cc """

    null      = 1
    bool      = 2
    integer   = 3
    float     = 4
    double    = 5
    str       = 6
    str_utf8  = 7
    array     = 8
    hash      = 9
    function  = 10
    exception = 11
    invalid   = 12


class PythonValue(ctypes.Structure):
    """ Map to C PythonValue """
    _fields_ = [("value", ctypes.c_void_p),
                ("type", ctypes.c_int),
                ("len", ctypes.c_size_t)]

    def __str__(self):
        return str(self.to_python())

    def to_python(self):
        """ Return an object as native Python """

        result = None
        if self.type == PythonTypes.null.value:
            result = None
        elif self.type == PythonTypes.bool.value:
            result = self.value == 1
        elif self.type == PythonTypes.integer.value:
            if self.value is None:
                result = 0
            else:
                result = ctypes.c_int32(self.value).value
        elif self.type == PythonTypes.double.value:
            result = ctypes.c_double(self.value).value
        elif self.type == PythonTypes.float.value:
            result = ctypes.c_float(self.value).value
        elif self.type == PythonTypes.str_utf8.value:
            result = ctypes.c_char_p(self.value).value
        elif self.type == PythonTypes.array.value:
            if self.len == 0:
                return []
            ary = []
            ary_addr = ctypes.c_void_p.from_address(self.value)
            ptr_to_ary = ctypes.pointer(ary_addr)
            for i in range(self.len):
                pval = PythonValue.from_address(ptr_to_ary[i])
                ary.append(pval.to_python())
            result = ary
        elif self.type == PythonTypes.hash.value:
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
        elif self.type == PythonTypes.function.value:
            result = JSFunction()
        elif self.type == PythonTypes.exception.value:
            msg = ctypes.c_char_p(self.value).value
            raise JSEvalException(msg)
        else:
            raise WrongReturnTypeException("unknown type %d" % self.type)
        return result
