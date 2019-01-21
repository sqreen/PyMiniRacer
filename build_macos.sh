#!/bin/bash
set -e
set -x

VERSION=6.7.288.46.1
GEM=libv8-$VERSION-x86_64-darwin-16.gem

if ! [ -f ${GEM} ]; then

    # Get the .a file from libv8 gem
    wget https://rubygems.org/downloads/${GEM}
    tar xvf ${GEM}
    tar xvf data.tar.gz

fi;

python setup.py build_ext
