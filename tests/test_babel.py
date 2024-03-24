""" Test loading and executing babel.js """

from os.path import dirname
from os.path import join as pathjoin

from py_mini_racer import MiniRacer


def test_babel():
    context = MiniRacer()

    path = pathjoin(dirname(__file__), "fixtures/babel.js")
    with open(path, encoding="utf-8") as f:
        babel_source = f.read()
    source = (
        """
      var self = this;
      %s
      babel.eval = function(code) {
        return eval(babel.transform(code)["code"]);
      }
    """
        % babel_source
    )
    context.eval(source)
    assert context.eval("babel.eval(((x) => x * x)(8))") == 64
