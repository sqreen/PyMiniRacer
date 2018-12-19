set -e
set -x

for PYVERSION in 2.7 3.4 3.5 3.6 3.7; do
    curl https://bootstrap.pypa.io/get-pip.py | python
    pip install virtualenv
    ORIG_PATH=$PATH
    PATH=/Library/Frameworks/Python.framework/Versions/2.7/bin:$PATH
    virtualenv -p /Library/Frameworks/Python.framework/Versions/${PYVERSION}/bin/python${PYVERSION} venv_${PYVERSION}
    PATH=$ORIG_PATH
    . ./venv_${PYVERSION}/bin/activate
    pip install certifi
    pip install -r requirements/setup.txt
    python setup.py build_v8
    export LDSHARED="clang++ -bundle -undefined dynamic_lookup -arch i386 -arch x86_64"
    python setup.py bdist_wheel
    deactivate
done
