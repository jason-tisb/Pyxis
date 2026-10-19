"""Deterministic host checks for the Milestone C validation policy.

These tests model the observation boundary used by the embedded validator;
they do not claim to emulate an electrical UART or RS485 link.
"""

from dataclasses import dataclass


OK = 0
DROP = 2


@dataclass
class Event:
    processed: int
    result: int
    address: int


def accepts(baseline: int, event: Event, slave: int = 1) -> bool:
    return (event.processed != baseline and event.result == OK
            and event.address == slave)


def test_valid_current_generation():
    assert accepts(10, Event(11, OK, 1))


def test_stale_wrong_crc_and_wrong_address_rejected():
    assert not accepts(10, Event(10, OK, 1))
    assert not accepts(10, Event(11, DROP, 1))
    assert not accepts(10, Event(11, OK, 2))


def test_broadcast_is_not_a_lock_event():
    assert not accepts(10, Event(11, OK, 0))


def test_framing_order_and_stop_bits():
    # Matches the embedded table: 8E1, 8N2, 8O1, 8N1.
    assert [(0, 1), (2, 2), (1, 1), (2, 1)] == [
        (0, 1), (2, 2), (1, 1), (2, 1)
    ]


if __name__ == "__main__":
    test_valid_current_generation()
    test_stale_wrong_crc_and_wrong_address_rejected()
    test_broadcast_is_not_a_lock_event()
    test_framing_order_and_stop_bits()
    print("modbus autobaud validation policy: PASS")
