from py_mini_racer import (
    JSArray,
    JSFunction,
    JSObject,
    JSPromise,
    JSSymbol,
    JSUndefined,
    MiniRacer,
)


def test_object():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var a = {
    5: "key_is_number",
    "key_is_string": 42,
    undefined: "undef_value",
    null: "null_value",
};
a
"""
    )

    assert isinstance(obj, JSObject)
    assert obj.__hash__()
    assert sorted(obj.keys(), key=str) == [5, "key_is_string", "null", "undefined"]
    assert obj["5"] == "key_is_number"
    assert obj[5] == "key_is_number"
    assert obj["key_is_string"] == 42
    assert obj[None] == "null_value"
    assert obj[JSUndefined] == "undef_value"

    # The following are provided by collections.abc mixins:
    assert 5 in obj
    assert None in obj
    assert "elvis" not in obj
    assert sorted(obj.items(), key=lambda x: str(x[0])) == [
        (5, "key_is_number"),
        ("key_is_string", 42),
        ("null", "null_value"),
        ("undefined", "undef_value"),
    ]
    assert set(obj.values()) == {42, "key_is_number", "undef_value", "null_value"}
    obj2 = mr.eval(
        """\
var a = {
    5: "key_is_number",
    "key_is_string": 42,
    undefined: "undef_value",
    null: "null_value",
};
a
"""
    )
    assert obj == obj2


def test_object_prototype():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var proto = { 5: "key_is_number", "key_is_string": 42 };
var a = Object.create(proto);
a.foo = "bar";
a
"""
    )
    assert sorted(obj.items(), key=lambda x: str(x[0])) == [
        (5, "key_is_number"),
        ("foo", "bar"),
        ("key_is_string", 42),
    ]


def test_array():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var a = [ "some_string", 42, undefined, null ];
a
"""
    )

    assert isinstance(obj, JSArray)
    assert obj.__hash__()
    assert obj[0] == "some_string"
    assert obj[1] == 42
    assert obj["1"] == 42  # Because JavaScript, this works too
    assert obj["2"] is JSUndefined
    assert obj["2"] == JSUndefined
    assert obj["3"] is None
    assert len(obj) == 4
    assert list(obj) == ["some_string", 42, JSUndefined, None]
    assert 42 in obj
    assert JSUndefined in obj
    assert None in obj
    assert "elvis" not in obj


def test_function():
    mr = MiniRacer()
    obj = mr.eval(
        """\
function foo() {};
foo
"""
    )

    assert isinstance(obj, JSFunction)
    assert obj.__hash__()
    assert obj.keys() == ()


def test_symbol():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var sym = Symbol("foo");
sym
"""
    )

    assert isinstance(obj, JSSymbol)
    assert obj.__hash__()
    assert obj.keys() == ()


def test_promise():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var p = Promise.resolve(42);
p
"""
    )

    assert isinstance(obj, JSPromise)
    assert obj.__hash__()
    assert obj.keys() == ()


def test_nested_object():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var a = {
    5: "key_is_number",
    "key_is_string": 42,
    "some_func": () => {},
    "some_obj": {"a": 12},
    "some_promise": Promise.resolve(42),
    "some_symbol": Symbol("sym"),
};
a
"""
    )

    assert isinstance(obj, JSObject)
    assert obj.__hash__()
    assert sorted(obj.keys(), key=str) == [
        5,
        "key_is_string",
        "some_func",
        "some_obj",
        "some_promise",
        "some_symbol",
    ]
    assert obj["5"] == "key_is_number"
    assert obj[5] == "key_is_number"
    assert obj["key_is_string"] == 42
    assert isinstance(obj["some_func"], JSFunction)
    assert isinstance(obj["some_obj"], JSObject)
    assert obj["some_obj"]["a"] == 12
    assert isinstance(obj["some_promise"], JSPromise)
    assert isinstance(obj["some_symbol"], JSSymbol)
