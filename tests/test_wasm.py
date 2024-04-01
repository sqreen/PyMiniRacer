"""Test executing a WASM module."""

from os.path import abspath, dirname, getsize
from os.path import join as pathjoin

from py_mini_racer import MiniRacer

test_dir = dirname(abspath(__file__))


def test_add():
    fn = pathjoin(test_dir, "add.wasm")
    mr = MiniRacer()

    # 1. Allocate a buffer to hold the WASM module code
    size = getsize(fn)
    module_raw = mr.eval(
        f"""
    const moduleRaw = new SharedArrayBuffer({size});
    moduleRaw
    """
    )

    # 2. Read the WASM module code
    with open(fn, "rb") as f:
        assert f.readinto(module_raw) == size

    # 3. Instantiate the WASM module
    mr.eval(
        """
    var res = null;
    WebAssembly.instantiate(new Uint8Array(moduleRaw)).then(result => {
        res = result.instance;
    }).catch(result => { res = result.message; });
    """
    )

    # 4. Wait for WASM module instantiation
    while mr.eval("res") is None:
        pass

    assert mr.eval("typeof res !== 'string'")

    # 5. Execute a WASM function
    assert mr.eval("res.exports.addTwo(1, 2)") == 3
