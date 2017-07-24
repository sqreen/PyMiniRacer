#!/bin/bash
set -e
set -x

./build_linux_wheels.sh

CONT=$(date +%s)

TAG=dockerfile-centos-6


cd docker
docker build -f Dockerfile-centos-6 -t ${TAG} .
cd ..

docker run \
    -e PY_MINI_RACER_V8_PATH=_v8.so \
    -d \
    --name ${CONT} ${TAG} bash \
    -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp dist/ ${CONT}:${BASE_PATH}/dist
docker cp tests/ ${CONT}:${BASE_PATH}/tests
docker cp requirements/ ${CONT}:${BASE_PATH}/requirements

for PYVERSION in cp27-cp27m cp27-cp27mu cp34-cp34m cp35-cp35m cp36-cp36m
do
    WHEEL=`find dist/ -name "*${PYVERSION}-manylinux1*.whl" -print`
    docker exec ${CONT} bash -c 'find tests -name "*.pyc" -delete'
    docker exec ${CONT} bash -c "/opt/python/${PYVERSION}/bin/pip install ${WHEEL}"
    docker exec ${CONT} bash -c "/opt/python/${PYVERSION}/bin/pip install -r requirements/test.txt"
    docker exec ${CONT} bash -c "/opt/python/${PYVERSION}/bin/pytest tests"
done
