#!/bin/bash
set -e
set -x

VERSION=6.7.288.46.1

if [ $# == 1 ] && [ $1 == "alpine" ]; then
    URL=https://rubygems.org/downloads/libv8-alpine-$VERSION-x86_64-linux.gem
else
    URL=https://rubygems.org/downloads/libv8-$VERSION-x86_64-linux.gem
fi

[[ -f libv8.gem ]] || curl "$URL" --output libv8.gem
tar xvf libv8.gem
tar xvf data.tar.gz

# Compile py_mini_racer extension
"${CXX:=g++}" \
    -g -O2 \
    -Ivendor/v8/include \
    py_mini_racer/extension/mini_racer_extension.cc \
    -o _v8.so \
    -Wl,--start-group vendor/v8/out.gn/libv8/obj/libv8_monolith.a \
    -Wl,--end-group \
    -lrt \
    -ldl \
    -pthread \
    -std=c++0x \
    -shared \
    -fPIC
