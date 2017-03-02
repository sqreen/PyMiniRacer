set -e
set +x

virtualenv -p /Library/Frameworks/Python.framework/Versions/$1/bin/python$1 venv_$1
. ./venv_$1/bin/activate
pip install certifi
python setup.py bdist_wheel
