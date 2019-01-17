#!/bin/bash
set -e
set -x

# ./build_v8.sh alpine

CONT=$(date +%s)

mkdir -p dist

for PYVERSION in 2.7 3.4 3.5 3.6 3.7; do

	docker run \
	    -e PY_MINI_RACER_V8_PATH=_v8.so \
	    -d \
	    --name ${CONT} python:${PYVERSION}-alpine sh \
	    -c "tail -f /dev/null"

	docker cp . ${CONT}:${BASE_PATH}
	docker cp _v8.so ${CONT}:.

    docker exec ${CONT} python setup.py sdist
    docker exec ${CONT} python setup.py bdist_wheel

	docker exec ${CONT} tar cvzf wheels.tar.gz dist/
	docker cp ${CONT}:wheels.tar.gz .
	tar xvf wheels.tar.gz
	rm wheels.tar.gz

	docker rm -f ${CONT}
done

for i in $(ls dist/*.whl); do 
	auditwheel repair $i -w tmpdist
done

rm dist/*.whl
mv tmpdist/* dist/
rm -r tmpdist

# Add wheel for Python without PyMalloc. Since we don't rely on it we can
# safely copy the wheel with another name
python wheel_pymalloc.py

docker rm -f ${CONT}
