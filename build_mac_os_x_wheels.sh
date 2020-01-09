set -e
set -x

VERSION=6.7.288.46.1
GEM=libv8-$VERSION-x86_64-darwin-16.gem

if ! [ -f ${GEM} ]; then

    # Get the .a file from libv8 gem
    wget https://rubygems.org/downloads/${GEM}
    tar xvf ${GEM}
    tar xvf data.tar.gz

fi

for PYVERSION in 2.7 3.4 3.5 3.6 3.7 3.8; do
    curl https://bootstrap.pypa.io/get-pip.py | python
    pip install virtualenv
    ORIG_PATH=$PATH
    PATH=/Library/Frameworks/Python.framework/Versions/2.7/bin:$PATH
    virtualenv -p /Library/Frameworks/Python.framework/Versions/${PYVERSION}/bin/python${PYVERSION} venv_${PYVERSION}
    PATH=$ORIG_PATH
    . ./venv_${PYVERSION}/bin/activate
    pip install certifi
    pip install -r requirements/setup.txt

    export LDSHARED="clang++ -bundle -undefined dynamic_lookup -arch i386 -arch x86_64"
    python setup.py bdist_wheel
    deactivate
done
