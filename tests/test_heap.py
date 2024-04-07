from py_mini_racer import MiniRacer


def test_heap_stats(gc_check):
    mr = MiniRacer()

    assert mr.heap_stats()["used_heap_size"] > 0
    assert mr.heap_stats()["total_heap_size"] > 0

    gc_check.check(mr)
