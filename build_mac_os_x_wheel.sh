set -e
set +x

virtualenv -p /Library/Frameworks/Python.framework/Versions/$1/bin/python$1 venv_$1
. ./venv_$1/bin/activate
pip install awscli certifi
python setup.py bdist_wheel
aws s3 cp dist/ "s3://sqreen-pyminiracer-travis-artefact/$(git rev-parse HEAD)/dist/" --recursive --exclude "*" --include "*.whl"
