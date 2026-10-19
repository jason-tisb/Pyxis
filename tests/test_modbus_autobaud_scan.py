"""Host-side deterministic tests for the Milestone D scanner policy."""

PROFILES = [
    (19200, "8E1"), (9600, "8E1"), (38400, "8E1"),
    (57600, "8E1"), (115200, "8E1"),
    (19200, "8N2"), (9600, "8N2"), (38400, "8N2"),
    (57600, "8N2"), (115200, "8N2"),
    (19200, "8O1"), (9600, "8O1"), (38400, "8O1"),
    (57600, "8O1"), (115200, "8O1"),
    (19200, "8N1"), (9600, "8N1"), (38400, "8N1"),
    (57600, "8N1"), (115200, "8N1"),
]


def scan(budget_ms, valid_index=None, dwell=1200):
    assert budget_ms > 0
    elapsed = 0
    attempted = []
    index = 0
    while elapsed < budget_ms:
        attempted.append(index)
        if index == valid_index:
            return "LOCKED", attempted, elapsed
        elapsed += min(dwell, budget_ms - elapsed)
        if elapsed >= budget_ms:
            break
        index = (index + 1) % len(PROFILES)
    return "TIMEOUT", attempted, elapsed


def test_order_and_grouping():
    assert PROFILES[:5] == [
        (19200, "8E1"), (9600, "8E1"), (38400, "8E1"),
        (57600, "8E1"), (115200, "8E1")
    ]
    assert PROFILES[15:] == [
        (19200, "8N1"), (9600, "8N1"), (38400, "8N1"),
        (57600, "8N1"), (115200, "8N1")
    ]


def test_each_profile_can_lock():
    for index in (0, 4, 9, 14, 19):
        status, attempted, _ = scan(30000, index)
        assert status == "LOCKED"
        assert attempted[-1] == index


def test_budget_stops_mid_scan_and_wraps():
    status, attempted, elapsed = scan(21000, None)
    assert status == "TIMEOUT"
    assert elapsed == 21000
    assert attempted == list(range(18))
    status, attempted, _ = scan(25000, None)
    assert attempted[-1] == 0  # one complete cycle plus profile 1 again


def test_invalid_and_repeated_starts():
    status, attempted, _ = scan(1200, None)
    assert status == "TIMEOUT" and attempted == [0]
    assert PROFILES[0] != (0, "bad")


if __name__ == "__main__":
    test_order_and_grouping()
    test_each_profile_can_lock()
    test_budget_stops_mid_scan_and_wraps()
    test_invalid_and_repeated_starts()
    print("modbus autobaud scan policy: PASS")
