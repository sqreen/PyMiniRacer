""" Test loading and executing babel.js """

from gc import collect
from os.path import dirname
from os.path import join as pathjoin

from py_mini_racer import MiniRacer


def test_babel():
    mr = MiniRacer()

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
    mr.eval(source)
    assert mr.eval("babel.eval(((x) => x * x)(8))") == 64

    collect()
    assert mr._ctx.value_count() == 0  # noqa: SLF001
