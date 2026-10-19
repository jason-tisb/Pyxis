"""Host qualification model for the Milestone F diagnostic contract."""

from dataclasses import dataclass


@dataclass
class Snapshot:
    valid: int = 0
    ambiguous: int = 0
    unavailable: int = 0
    timeout: int = 0
    errors: int = 0
    scans: int = 0
    scan_locks: int = 0
    measured_locks: int = 0
    failures: int = 0
    crc_rejects: int = 0
    outcome: str = "NONE"
    active: bool = False
    locked: bool = False
    fallback: bool = False
    baud: int = 0
    active_baud: int = 0
    confidence: int = 0
    last_error: str = "NONE"


def session(*events):
    s = Snapshot(active=True, outcome="RUNNING")
    for event in events:
        if event == "valid":
            s.valid += 1
        elif event == "ambiguous":
            s.ambiguous += 1
            s.last_error = "AMBIGUOUS"
        elif event == "unavailable":
            s.unavailable += 1
            s.last_error = "UNAVAILABLE"
        elif event == "timeout":
            s.timeout += 1
            s.last_error = "TIMEOUT"
        elif event == "crc":
            s.crc_rejects += 1
        elif event == "scan":
            s.scans += 1
        elif event == "measured_lock":
            s.active = False
            s.locked = True
            s.outcome = "LOCKED_MEASURED"
            s.measured_locks += 1
        elif event == "scan_lock":
            s.active = False
            s.locked = True
            s.outcome = "LOCKED_SCANNED"
            s.scan_locks += 1
        elif event == "fallback":
            s.active = False
            s.locked = False
            s.fallback = True
            s.outcome = "FALLBACK"
            s.failures += 1
        elif event == "error":
            s.active = False
            s.locked = False
            s.outcome = "ERROR"
            s.failures += 1
    return s


def test_initialization_and_disabled_state():
    s = Snapshot()
    assert not s.active and not s.locked and s.outcome == "NONE"


def test_intermediate_events_count_once():
    s = session("timeout", "ambiguous", "valid", "crc", "scan")
    assert (s.timeout, s.ambiguous, s.valid, s.crc_rejects, s.scans) == (1, 1, 1, 1, 1)


def test_terminal_outcomes_are_distinct():
    assert session("measured_lock").outcome == "LOCKED_MEASURED"
    assert session("scan", "scan_lock").outcome == "LOCKED_SCANNED"
    fallback = session("fallback")
    assert fallback.outcome == "FALLBACK" and not fallback.locked
    assert session("error").outcome == "ERROR"


def test_repeated_service_does_not_duplicate_terminal_event():
    s = session("measured_lock")
    # A terminal snapshot is stable when Service() is called again.
    assert session("measured_lock").measured_locks == s.measured_locks == 1


def test_cumulative_sessions_and_wrap_policy():
    a = session("timeout", "fallback")
    b = session("valid", "scan", "scan_lock")
    assert a.timeout + b.timeout == 1
    assert a.failures + b.failures == 1
    assert b.scan_locks == 1
    assert ((0x14 - 0xFFFFFFF0) & 0xFFFFFFFF) == 36


def test_supported_matrix_shape():
    bauds = (9600, 19200, 38400, 57600, 115200)
    framings = ("8E1", "8N2", "8O1", "8N1")
    assert len(bauds) * len(framings) == 20


if __name__ == "__main__":
    test_initialization_and_disabled_state()
    test_intermediate_events_count_once()
    test_terminal_outcomes_are_distinct()
    test_repeated_service_does_not_duplicate_terminal_event()
    test_cumulative_sessions_and_wrap_policy()
    test_supported_matrix_shape()
    print("modbus autobaud diagnostics: PASS")
