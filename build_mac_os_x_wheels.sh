set -e
set +x

for PYVERSION in 2.7 3.4 3.5 3.6; do
    virtualenv -p /Library/Frameworks/Python.framework/Versions/${PYVERSION}/bin/python${PYVERSION} venv_${PYVERSION}
    . ./venv_${PYVERSION}/bin/activate
    pip install certifi
    python setup.py bdist_wheel
    deactivate
done

. ./venv_3.6/bin/activate
pip install awscli
aws s3 cp dist/ "s3://sqreen-pyminiracer-travis-artefact/$(git rev-parse HEAD)/dist/" --recursive --exclude "*" --include "*.whl"
