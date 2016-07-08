FROM ubuntu

RUN apt-get update
RUN apt-get install -y git build-essential python-pip git curl

WORKDIR /code

COPY . /code/

RUN git submodule update

RUN python setup.py build_v8

CMD bash
