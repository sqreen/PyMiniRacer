import pytest
from py_mini_racer import (
    JSArray,
    JSFunction,
    JSObject,
    JSPromise,
    JSSymbol,
    JSUndefined,
    MiniRacer,
)


def test_object_read():
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
    assert obj
    assert sorted(obj.keys(), key=str) == [5, "key_is_string", "null", "undefined"]
    assert obj["5"] == "key_is_number"
    assert obj[5] == "key_is_number"
    assert obj["key_is_string"] == 42
    assert obj[None] == "null_value"
    assert obj[JSUndefined] == "undef_value"
    assert len(obj) == 4

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

    obj3 = mr.eval(
        """\
var a = {};
a
"""
    )
    assert not obj3


def test_object_mutation():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var a = {};
a
"""
    )

    assert isinstance(obj, JSObject)
    obj["some_string"] = "some_string_val"
    obj[JSUndefined] = "undefined_val"
    obj[None] = "none_val"
    obj[5] = "int_val"
    # Note that this should overwrite 5=int_val above:
    obj[5.0] = "double_val"
    assert sorted(obj.items(), key=lambda x: str(x[0])) == [
        (5, "double_val"),
        ("null", "none_val"),
        ("some_string", "some_string_val"),
        ("undefined", "undefined_val"),
    ]
    assert obj.pop(None) == "none_val"
    with pytest.raises(KeyError):
        obj.pop(None)
    with pytest.raises(KeyError):
        del obj["elvis"]
    obj.clear()
    assert not obj
    obj["foo"] = "bar"
    assert obj.setdefault("foo", "baz") == "bar"
    obj.update({"froz": "blargh"})
    assert len(obj) == 2

    inner_obj = mr.eval(
        """\
var b = {"k": "v"};
b
"""
    )
    obj["inner"] = inner_obj
    assert len(obj) == 3
    assert obj["inner"]["k"] == "v"


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
    assert obj
    assert obj[0] == "some_string"
    assert obj[1] == 42
    assert obj[2] is JSUndefined
    assert obj[2] == JSUndefined
    assert obj[3] is None
    assert obj[-3] == 42
    with pytest.raises(IndexError):
        obj[4]
    with pytest.raises(IndexError):
        obj[-5]

    assert list(obj) == ["some_string", 42, JSUndefined, None]
    assert len(obj) == 4
    assert list(obj) == ["some_string", 42, JSUndefined, None]
    assert 42 in obj
    assert JSUndefined in obj
    assert None in obj
    assert "elvis" not in obj

    obj2 = mr.eval(
        """\
var a = [];
a
"""
    )
    assert not obj2


def test_array_mutation():
    mr = MiniRacer()
    obj = mr.eval(
        """\
var a = [];
a
"""
    )

    obj.append("some_string")
    obj.append(JSUndefined)
    obj.insert(1, 42)
    obj.insert(-1, None)
    assert list(obj) == ["some_string", 42, None, JSUndefined]

    del obj[-1]
    assert list(obj) == ["some_string", 42, None]

    del obj[0]
    assert list(obj) == [42, None]

    with pytest.raises(IndexError):
        del obj[-3]

    with pytest.raises(IndexError):
        del obj[2]

    inner_obj = mr.eval(
        """\
var b = {"k": "v"};
b
"""
    )
    obj.append(inner_obj)
    assert len(obj) == 3
    assert obj[-1]["k"] == "v"


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
