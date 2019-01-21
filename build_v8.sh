#!/bin/bash
set -e
set -x

if [ $# == 1 ] && [ $1 == "alpine" ]; then
	distribution="alpine"
else
	distribution="quay.io/pypa/manylinux1_x86_64"
fi

CONT=$(date +%s)

docker run -d --name ${CONT} $distribution sh -c "tail -f /dev/null"

docker cp . ${CONT}:${BASE_PATH}

if [ "$distribution" == "alpine" ]; then
	docker exec ${CONT} sh -c "apk update; apk add bash curl g++"
	docker exec ${CONT} bash build_so.sh alpine
else
	docker exec ${CONT} bash build_so.sh
fi

docker cp ${CONT}:_v8.so .

docker rm -f ${CONT}
