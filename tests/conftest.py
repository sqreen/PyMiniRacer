from gc import collect
from time import sleep, time

import pytest


class GcCheck:
    @staticmethod
    def check(mr):
        """Test helper for garbage collection.

        PyMiniRacer does somewhat tricky things with object lifecycle management
        (basically, various __del__ methods, backed by C++ keeping its own track of
        all allocated objects). This is a somewhat kludgey test helper to verify
        those tricks are working.

        The Python gc doesn't seem particularly deterministic, so we do 2 collects
        and a sleep here to reduce the flake rate.
        """
        start = time()
        while time() - start < 5 and mr._ctx.value_count() != 0:  # noqa: SLF001
            collect()
            sleep(0.05)

        assert mr._ctx.value_count() == 0  # noqa: SLF001


@pytest.fixture
def gc_check():
    return GcCheck
