#!/bin/bash
set -e
set -x

# Get the .a file from libv8 gem
wget --no-check-certificate https://rubygems.org/downloads/libv8-5.7.492.65.1-x86_64-linux.gem
tar xvf libv8-5.7.492.65.1-x86_64-linux.gem
tar xvf data.tar.gz

# Compile py_mini_racer extension
g++ -Ivendor/v8/include \
    -Ivendor/v8 py_mini_racer/extension/mini_racer_extension.cc \
    -o _v8.so \
    -Wl,--start-group vendor/v8/out/x64.release/obj.target/src/libv8_{base,libbase,snapshot,libplatform,libsampler}.a \
    -Wl,--end-group \
    -lrt \
    -ldl \
    -pthread \
    -std=c++0x \
    -shared \
    -fPIC
