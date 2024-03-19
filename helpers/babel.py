""" Transform the input stream using babel.transform """

import os
import sys

from py_mini_racer import py_mini_racer


def babel_transform(es_string):
    """Transform the provided string using babel.transform"""

    path_to_babel = os.path.join(
        os.path.dirname(__file__), "..", "tests", "fixtures", "babel.js"
    )

    with open(path_to_babel) as f:
        babel_source = f.read()

    # Initializes PyMiniRacer
    ctx = py_mini_racer.MiniRacer()

    # Parse babel
    ctx.eval("""var self = this; %s """ % babel_source)

    # Transform stuff :)
    val = "babel.transform(`%s`)['code']" % es_string
    return ctx.eval(val)


if __name__ == "__main__":
    if len(sys.argv) != 1:
        name = sys.argv[0]
        sys.stderr.write("Usage: cat es6file.js | %s\n" % name)
        sys.stderr.write("Example: echo [1,2,3].map(n => n + 1); | %s\n" % name)
        sys.exit(-1)

    es6_data = sys.stdin.read()

    res = babel_transform(es6_data)

    sys.stdout.write(res)
