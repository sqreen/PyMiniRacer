set -x

sleep 5

for i in $(ls dist/*.whl); do
    auditwheel repair $i;
done;

ls wheelhouse
