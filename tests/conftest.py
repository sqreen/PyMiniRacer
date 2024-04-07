from gc import collect
from time import sleep

import pytest


class GcCheck:
    @staticmethod
    def check(mr):
        collect()
        sleep(0.1)
        collect()
        assert mr._ctx.value_count() == 0  # noqa: SLF001


@pytest.fixture
def gc_check():
    return GcCheck
