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

COPY . /code/

RUN git submodule update

RUN python setup.py build_v8

RUN pip wheel . -w dist

RUN python3 -m pip install auditwheel

RUN auditwheel repair dist/py_mini_racer-*.whl

CMD bash
