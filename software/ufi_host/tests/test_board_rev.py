"""INFO board_rev to label: the v0.x boards keep their old numbers, v1.0 reports 10."""
from ufi_host.protocol2 import board_rev_text


def test_board_rev_text():
    assert board_rev_text(0) == "unknown"
    assert board_rev_text(5) == "v0.5"
    assert board_rev_text(7) == "v0.7"
    assert board_rev_text(10) == "v1.0"
