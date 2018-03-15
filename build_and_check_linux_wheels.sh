#!/bin/bash
set -e
set -x

./build_linux_wheels.sh


TAG=dockerfile-centos-6


for PYVERSION in cp27-cp27mu cp34-cp34m cp35-cp35m cp36-cp36m
do
    CONT=$(date +%s)A

    PYTHON=${PYVERSION:2:1}.${PYVERSION:3:1}
    docker run \
        -d \
        --name ${CONT} \
        python:$PYTHON bash \
        -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

    docker cp dist/ ${CONT}:${BASE_PATH}/dist
    docker cp tests/ ${CONT}:${BASE_PATH}/tests
    docker cp requirements/ ${CONT}:${BASE_PATH}/requirements

    WHEEL=`find dist/ -name "*${PYVERSION}-manylinux1*.whl" -print`
    docker exec ${CONT} bash -c 'echo python --version'

    docker exec ${CONT} bash -c 'find tests -name "*.pyc" -delete'
    docker exec ${CONT} bash -c "pip install ${WHEEL}"
    docker exec ${CONT} bash -c "pip install -r requirements/test.txt"
    docker exec ${CONT} bash -c "pytest tests"

    docker rm -f ${CONT}
done
