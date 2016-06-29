#!/bin/sh

set -e
set -x

# Should only work on OSX so far

clang++ -c src/mini_racer_extension.cc -I py_mini_racer/ffi/v8 \
        -D_XOPEN_SOURCE -D_DARWIN_C_SOURCE                     \
        -D_DARWIN_UNLIMITED_SELECT -D_REENTRANT                \
        -Wall -g -rdynamic -std=c++0x -fpermissive -fno-common \
        -o src/mini_racer.o

clang++ -dynamic -bundle src/mini_racer.o                      \
    -stdlib=libstdc++                                          \
    -fstack-protector                                          \
    -Wl,-undefined,dynamic_lookup                              \
    -Wl,-multiply_defined,suppress                             \
    -lobjc -lpthread  -lpthread -ldl -lobjc                    \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_base.a          \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_libbase.a       \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_libplatform.a   \
    ./py_mini_racer/ffi/v8/v8/out/native/libv8_nosnapshot.a    \
    -o mini_racer_extension.bundle 



