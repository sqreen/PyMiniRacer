#!/bin/bash
set -e
set -x

./build_v8.sh

CONT=$(date +%s)

docker run \
    -e PY_MINI_RACER_V8_PATH=_v8.so \
    -d \
    --name ${CONT} quay.io/pypa/manylinux1_x86_64 bash \
    -c "mkdir /${BASE_PATH}; tail -f /var/log/lastlog"

docker cp . ${CONT}:${BASE_PATH}
docker cp _v8.so ${CONT}:.

for PYVERSION in cp27-cp27m cp27-cp27mu cp34-cp34m cp35-cp35m cp36-cp36m cp37-cp37m cp38-cp38
do
    docker exec ${CONT} /opt/python/${PYVERSION}/bin/python setup.py sdist
    docker exec ${CONT} /opt/python/${PYVERSION}/bin/python setup.py bdist_wheel
done

docker exec ${CONT} bash -c 'for i in $(ls dist/*.whl); do auditwheel repair $i -w tmpdist; done;'
docker exec ${CONT} bash -c "rm dist/*.whl"
docker exec ${CONT} bash -c "cp tmpdist/*.whl  dist"

# Add wheel for Python without PyMalloc. Since we don't rely on it we can
# safely copy the wheel with another name
docker exec ${CONT} /opt/python/cp35-cp35m/bin/pip install auditwheel
docker exec ${CONT} /opt/python/cp35-cp35m/bin/python wheel_pymalloc.py
docker exec ${CONT} bash -c "rm -rf tmpdist/"

docker exec ${CONT} tar cvzf wheels.tar.gz dist/
docker cp ${CONT}:wheels.tar.gz .
tar xvf wheels.tar.gz
rm wheels.tar.gz

docker rm -f ${CONT}
