#!/bin/bash
set -euo pipefail
set -x

URLS=(
    'https://www.python.org/ftp/python/2.7.13/python-2.7.13-macosx10.6.pkg'
    'https://www.python.org/ftp/python/3.4.4/python-3.4.4-macosx10.6.pkg'
    'https://www.python.org/ftp/python/3.5.3/python-3.5.3-macosx10.6.pkg'
    'https://www.python.org/ftp/python/3.6.0/python-3.6.0-macosx10.6.pkg'
    'https://www.python.org/ftp/python/3.7.0/python-3.7.0-macosx10.6.pkg '
)

for url in "${URLS[@]}"; do
    curl -Lo /tmp/python.pkg "$url"
    sudo installer -pkg /tmp/python.pkg -target /
done
