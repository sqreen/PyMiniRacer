FROM ubuntu

RUN apt-get update &&       \
    apt-get install -qy     \
            git             \
            build-essential \
            python-pip      \
            python3-pip     \
            git             \
            curl            \
            vim             \
            man             \
            unzip           \
            patchelf

WORKDIR /code

RUN python3 -m pip install auditwheel

RUN git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git vendor/depot_tools

COPY . /code/

RUN python setup.py build_v8

RUN python -u setup.py bdist_wheel

RUN python3 -u setup.py bdist_wheel

RUN for i in $(ls dist/*.whl); do auditwheel repair $i; done;

RUN ls wheelhouse

CMD bash
