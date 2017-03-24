FROM ubuntu:14.04

RUN apt-get update &&       \
    apt-get install -qy     \
            git             \
            build-essential \
            git             \
            curl            \
            python-pip

WORKDIR /code

COPY patches /code/patches/

# Initialize vendor
RUN git init .
RUN git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git vendor/depot_tools

COPY py_mini_racer/__init__.py /code/py_mini_racer/

COPY py_mini_racer/extension/v8_build.py /code/py_mini_racer/extension/v8_build.py

###
# Build V8
###

RUN pip install wheel

RUN python py_mini_racer/extension/v8_build.py

COPY . /code/

CMD cp -Raf * /artifact && echo "/code copied"
