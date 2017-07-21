#!/bin/bash
set -e
set -x

./build_v8.sh

CONT=$(date +%s)

TAG=dockerfile-centos-6

cd docker && docker build -f Dockerfile-centos-6 -t ${TAG} .
cd ..

docker run \
    -e PY_MINI_RACER_V8_PATH=_v8.so \
    -d \
    --name ${CONT} ${TAG} bash \
    -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp . ${CONT}:${BASE_PATH}
docker cp _v8.so ${CONT}:.

for PYVERSION in cp27-cp27m cp27-cp27mu cp34-cp34m cp35-cp35m cp36-cp36m
do
    docker exec ${CONT} /opt/python/${PYVERSION}/bin/python setup.py sdist
    docker exec ${CONT} /opt/python/${PYVERSION}/bin/python setup.py bdist_wheel
done

docker exec ${CONT} tar cvzf wheels.tar.gz dist/
docker cp ${CONT}:wheels.tar.gz .
tar xvf wheels.tar.gz
rm wheels.tar.gz

docker rm -f ${CONT}
