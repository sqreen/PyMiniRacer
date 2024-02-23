""" Basic JS call functions """

from datetime import datetime, timezone
from json import JSONEncoder

from py_mini_racer import MiniRacer


def test_call_js():
    js_func = """var f = function() {
        return arguments.length;
    }"""

    mr = MiniRacer()
    mr.eval(js_func)

    assert mr.call("f") == 0
    assert mr.call("f", *list(range(5))) == 5
    assert mr.call("f", *list(range(10))) == 10
    assert mr.call("f", *list(range(20))) == 20


def test_call_custom_encoder():
    # Custom encoder for dates
    class CustomEncoder(JSONEncoder):
        def default(self, obj):
            if isinstance(obj, datetime):
                return obj.isoformat()

            return JSONEncoder.default(self, obj)

    now = datetime.now(tz=timezone.utc)
    js_func = """var f = function(args) {
        return args;
    }"""
    mr = MiniRacer()
    mr.eval(js_func)

    assert mr.call("f", now, encoder=CustomEncoder) == now.isoformat()
