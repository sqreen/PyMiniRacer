#!/bin/bash
set -e
set -x

if [ $# == 1 ] && [ $1 == "alpine" ]; then
    ./build_linux_musl_wheels.sh
    docker_tag="-alpine"
    wheel_ext="linux_"
else
    ./build_linux_wheels.sh
    wheel_ext="manylinux1"
fi



TAG=dockerfile-centos-6


for PYVERSION in cp27-cp27mu cp34-cp34m cp35-cp35m cp36-cp36m cp37-cp37m
do
    CONT=$(date +%s)A

    PYTHON=${PYVERSION:2:1}.${PYVERSION:3:1}

    docker run \
        -d \
        --name ${CONT} \
        python:$PYTHON$docker_tag sh \
        -c "tail -f /dev/null"

    docker cp dist/ ${CONT}:${BASE_PATH}/dist
    docker cp tests/ ${CONT}:${BASE_PATH}/tests
    docker cp requirements/ ${CONT}:${BASE_PATH}/requirements

    WHEEL=`find dist/ -name "*${PYVERSION}-${wheel_ext}*.whl" -print`
    docker exec ${CONT} sh -c 'echo python --version'

    docker exec ${CONT} sh -c 'find tests -name "*.pyc" -delete'
    docker exec ${CONT} sh -c "pip install ${WHEEL}"
    docker exec ${CONT} sh -c "pip install -r requirements/test.txt"
    docker exec ${CONT} sh -c "pytest tests"

    docker rm -f ${CONT}
done
