# -*- coding: utf-8 -*-

import os

from cffi import FFI


SRC_PATH = os.path.join('v8cffi', 'src')
STATIC_LIBS_PATH = os.path.join(SRC_PATH, 'v8', 'release')

ffi = FFI()
ffi.cdef(
    """
    int printf(const char *format, ...);
    """
)

ffi.set_source(
    "_miniracer",  # This is the name of the import that this will build.
    """
    #include <stdio.h>
    """
)

if __name__ == "__main__":
    ffi.compile()
