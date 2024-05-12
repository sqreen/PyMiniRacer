"""Test loading and executing babel.js"""

from os.path import dirname
from os.path import join as pathjoin

from py_mini_racer import MiniRacer


def test_babel(gc_check):
    mr = MiniRacer()

    path = pathjoin(dirname(__file__), "fixtures/babel.js")
    with open(path, encoding="utf-8") as f:
        babel_source = f.read()
    source = f"""
      var self = this;
      {babel_source}
      babel.eval = function(code) {{
        return eval(babel.transform(code)["code"]);
      }}
    """
    mr.eval(source)
    assert mr.eval("babel.eval(((x) => x * x)(8))") == 64

    gc_check.check(mr)
