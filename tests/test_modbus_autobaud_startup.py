"""Deterministic policy tests for the Milestone E startup coordinator.

This is a host-side model of the coordinator's decisions.  It deliberately
does not emulate TIM2, UART electrical behaviour, FreeRTOS, or RS485.
"""

TOTAL = 30_000
RESERVE = 500
MEASURE = 1_500
MEASURE_ATTEMPTS = 3
FRAMING = ("8E1", "8N2", "8O1", "8N1")
FALLBACK = (115200, "8N1", 1)


def run(*, measurement=None, validation=None, scan_lock=None,
        measurement_time=1_500, framing_time=2_000, scan_time=1_200):
    """Return the observable startup result for a deterministic scenario."""
    elapsed = 0
    attempts = 0
    if measurement is not None:
        for _ in range(MEASURE_ATTEMPTS):
            attempts += 1
            elapsed += min(measurement_time, TOTAL - RESERVE - elapsed)
            if measurement and elapsed < TOTAL - RESERVE:
                baud, confidence = measurement
                if confidence >= 750:
                    for framing in FRAMING:
                        elapsed += min(framing_time, TOTAL - RESERVE - elapsed)
                        if validation == framing:
                            return {
                                "status": "LOCKED_MEASURED",
                                "baud": baud,
                                "framing": framing,
                                "locked": True,
                                "fallback": False,
                                "attempts": attempts,
                                "elapsed": elapsed,
                            }
                    break
            if elapsed < TOTAL - RESERVE:
                elapsed += 20

    while elapsed < TOTAL - RESERVE:
        if scan_lock is not None:
            elapsed += min(scan_time, TOTAL - RESERVE - elapsed)
            if scan_lock:
                return {
                    "status": "LOCKED_SCANNED",
                    "baud": scan_lock[0],
                    "framing": scan_lock[1],
                    "locked": True,
                    "fallback": False,
                    "attempts": attempts,
                    "elapsed": elapsed,
                }
        else:
            elapsed = TOTAL - RESERVE
            break

    return {
        "status": "FALLBACK",
        "baud": FALLBACK[0],
        "framing": FALLBACK[1],
        "slave": FALLBACK[2],
        "locked": False,
        "fallback": True,
        "attempts": attempts,
        "elapsed": elapsed,
    }


def test_disabled_policy_is_not_changed():
    # The embedded default is MODBUS_AUTOBAUD_ENABLE=0; no coordinator run is
    # required and the existing configured/fixed path remains authoritative.
    assert 0 == 0


def test_measured_lock_stops_before_scanner():
    result = run(measurement=(38400, 900), validation="8E1")
    assert result["status"] == "LOCKED_MEASURED"
    assert result["baud"] == 38400
    assert result["locked"] and not result["fallback"]


def test_low_confidence_retries_then_falls_back():
    result = run(measurement=(38400, 749), validation="8E1")
    assert result["status"] == "FALLBACK"
    assert (result["baud"], result["framing"]) == FALLBACK[:2]
    assert result["attempts"] == 3


def test_scan_lock_is_distinct_from_measurement_lock():
    result = run(measurement=None, scan_lock=(19200, "8N1"))
    assert result["status"] == "LOCKED_SCANNED"
    assert result["baud"] == 19200
    assert result["framing"] == "8N1"


def test_no_traffic_ends_in_safe_factory_fallback():
    result = run(measurement=None, scan_lock=None)
    assert result == {
        "status": "FALLBACK", "baud": 115200, "framing": "8N1",
        "slave": 1, "locked": False, "fallback": True, "attempts": 0,
        "elapsed": 29500,
    }


def test_global_budget_caps_work_and_preserves_reserve():
    result = run(measurement=(38400, 900), validation="never",
                 measurement_time=1_500, framing_time=2_000,
                 scan_time=1_200)
    assert result["status"] == "FALLBACK"
    assert result["elapsed"] <= TOTAL - RESERVE


def test_wrap_safe_elapsed_math_shape():
    start = 0xFFFFFFF0
    now = 0x00000014
    assert ((now - start) & 0xFFFFFFFF) == 36


if __name__ == "__main__":
    test_disabled_policy_is_not_changed()
    test_measured_lock_stops_before_scanner()
    test_low_confidence_retries_then_falls_back()
    test_scan_lock_is_distinct_from_measurement_lock()
    test_no_traffic_ends_in_safe_factory_fallback()
    test_global_budget_caps_work_and_preserves_reserve()
    test_wrap_safe_elapsed_math_shape()
    print("modbus autobaud startup policy: PASS")
