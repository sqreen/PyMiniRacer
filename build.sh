#!/bin/bash

set -e
set -x

git submodule init
git submodule update

python py_mini_racer/ffi/v8_build.py

python setup.py bdist_wheel


clang++ -c mini_racer_extension.cc -I py_mini_racer/ffi/v8  -D_XOPEN_SOURCE -D_DARWIN_C_SOURCE -D_DARWIN_UNLIMITED_SELECT -D_REENTRANT -Wall -g -rdynamic -std=c++0x -fpermissive -fno-common -o mini_racer.o
clang++ -dynamic -bundle -o mini_racer_extension.bundle mini_racer.o -stdlib=libstdc++ -fstack-protector -Wl,-undefined,dynamic_lookup -Wl,-multiply_defined,suppress    -lobjc -lpthread  -lpthread -ldl -lobjc ./py_mini_racer/ffi/v8/v8/out/native/libv8_base.a

