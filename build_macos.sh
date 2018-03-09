#!/bin/bash
set -e
set -x

GEM=libv8-5.7.492.65.1-x86_64-darwin-16.gem

if ! [ -f ${GEM} ]; then

    # Get the .a file from libv8 gem
    wget https://rubygems.org/downloads/${GEM}
    tar xvf ${GEM}
    tar xvf data.tar.gz

fi;

# Compile py_mini_racer extension
clang++ -Ivendor/v8/include \
    -Ivendor/v8 py_mini_racer/extension/mini_racer_extension.cc \
    -o _v8.so \
    vendor/v8/out/x64.release/libv8_{base,libbase,snapshot,libplatform,libsampler}.a \
    -ldl \
    -pthread \
    -std=c++11 \
    -stdlib=libc++ \
    -shared \
    -fPIC
