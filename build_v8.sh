#!/bin/bash
set -e
set -x

CONT=$(date +%s)

TAG=dockerfile-centos-6

cd docker && docker build -f Dockerfile-centos-6 -t ${TAG} .
cd ..

docker run -d --name ${CONT} ${TAG} bash -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp . ${CONT}:${BASE_PATH}

docker exec ${CONT} bash build_so.sh

docker cp ${CONT}:_v8.so .

docker rm -f ${CONT}
