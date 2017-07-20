#!/bin/bash
set -e
set -x

CONT=$(date +%s)

TAG=dockerfile-centos-5

docker build -f docker/Dockerfile-centos-5 -t ${TAG} .

docker run -d --name ${CONT} ${TAG} bash -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp . ${CONT}:${BASE_PATH}

docker exec ${CONT} bash build_so.sh

docker cp ${CONT}:_v8.so .

docker rm -f ${CONT}
