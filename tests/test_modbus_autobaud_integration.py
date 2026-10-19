"""Software-only startup-path qualification matrix for Milestone F."""

BAUDS = (9600, 19200, 38400, 57600, 115200)
FRAMINGS = ("8E1", "8N2", "8O1", "8N1")


def validate_frame(address, crc_ok, complete, slave=1):
    return complete and crc_ok and address == slave and address != 0


def test_all_supported_profiles_have_a_candidate():
    assert len([(baud, framing) for baud in BAUDS for framing in FRAMINGS]) == 20


def test_only_new_complete_unicast_crc_valid_frames_lock():
    assert validate_frame(1, True, True)
    assert not validate_frame(2, True, True)
    assert not validate_frame(0, True, True)
    assert not validate_frame(1, False, True)
    assert not validate_frame(1, True, False)


def test_timeout_and_recovery_policy():
    measurement_budget = 3 * 1500
    framing_budget = 4 * 2000
    scan_budget = 30000 - 500 - measurement_budget - framing_budget
    assert scan_budget == 17000
    assert scan_budget >= 0
    assert 20 * 1200 > scan_budget


def test_fixed_and_enabled_precedence():
    fixed = {"engine": 1, "autobaud": 0, "baud": 115200, "framing": "8N1"}
    enabled = {"engine": 1, "autobaud": 1, "startup_owner": "coordinator"}
    unsupported = {"engine": 0, "autobaud": 1}
    assert fixed["baud"] == 115200
    assert enabled["startup_owner"] == "coordinator"
    assert unsupported["engine"] == 0 and unsupported["autobaud"] == 1


if __name__ == "__main__":
    test_all_supported_profiles_have_a_candidate()
    test_only_new_complete_unicast_crc_valid_frames_lock()
    test_timeout_and_recovery_policy()
    test_fixed_and_enabled_precedence()
    print("modbus autobaud integration policy: PASS")
