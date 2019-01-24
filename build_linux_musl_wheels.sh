#!/bin/bash
set -e
set -x

./build_v8.sh alpine

mkdir -p dist

for PYVERSION in 2.7 3.4 3.5 3.6 3.7; do

	CONT=$(date +%s)

	docker run \
		-e PY_MINI_RACER_V8_PATH=_v8.so \
		-d \
		--name ${CONT} python:${PYVERSION}-alpine sh \
		-c "tail -f /dev/null"

	docker cp . ${CONT}:${BASE_PATH}
	docker cp _v8.so ${CONT}:.

	docker exec ${CONT} python setup.py sdist
	docker exec ${CONT} python setup.py bdist_wheel

	if [ PYVERSION == 3.7 ]; then
		docker exec ${CONT} sh -c "pip install auditwheel"
		docker exec ${CONT} sh -c "apk add patchelf"
		docker exec ${CONT} sh -c "for i in $(ls dist/*.whl); do; auditwheel repair $i -w tmpdist; done"
		docker exec ${CONT} sh -c "rm dist/*.whl"
		docker exec ${CONT} sh -c "mv tmpdist/* dist/"
		docker exec ${CONT} sh -c "rm -r tmpdist"

		# Add wheel for Python without PyMalloc. Since we don't rely on it we can
		# safely copy the wheel with another name
		docker exec ${CONT} sh -c "python3 wheel_pymalloc.py"
	fi

	docker exec ${CONT} tar cvzf wheels.tar.gz dist/
	docker cp ${CONT}:wheels.tar.gz .
	tar xvf wheels.tar.gz
	rm wheels.tar.gz

	docker rm -f ${CONT}
	unset CONT

	trap "if [ '$CONT' -ne '' ]; then docker rm -f ${CONT}; fi" EXIT

done
