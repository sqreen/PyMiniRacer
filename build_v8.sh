#!/bin/bash
set -e
set -x

CONT=$(date +%s)

docker run -d --name ${CONT} quay.io/pypa/manylinux1_x86_64 bash -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp . ${CONT}:${BASE_PATH}

docker exec ${CONT} bash build_so.sh

docker cp ${CONT}:_v8.so .

docker rm -f ${CONT}
