"""Deterministic host fixtures for the Milestone B scoring contract.

The arithmetic intentionally mirrors modbus_autobaud_score.c so the same
fixtures can be reviewed/run on a development host without STM32 headers.
"""
from dataclasses import dataclass

BAUDS = (9600, 19200, 38400, 57600, 115200)
HZ = 10_666_667


@dataclass
class Candidate:
    baud: int
    score: int
    accepted: int
    rejected: int


def evaluate(intervals, timer_hz=HZ):
    candidates = []
    for baud in BAUDS:
        bit_ticks = (timer_hz + baud // 2) // baud
        accepted = rejected = total_error = 0
        for delta in intervals:
            if delta < 65:
                rejected += 1
                continue
            multiple = (delta + bit_ticks // 2) // bit_ticks
            if multiple == 0 or multiple > 12:
                rejected += 1
                continue
            residual = abs(delta - multiple * bit_ticks) * 1000 // bit_ticks
            if residual > 250:
                rejected += 1
                continue
            accepted += 1
            total_error += residual
        score = (2**32 - 1 if accepted == 0 else
                 (total_error + accepted // 2) // accepted +
                 (rejected * 1000 + len(intervals) // 2) // len(intervals))
        candidates.append(Candidate(baud, score, accepted, rejected))
    candidates.sort(key=lambda c: (c.score, -c.accepted))
    best, runner = candidates[:2]
    acceptance = (best.accepted * 1000 + len(intervals) // 2) // len(intervals)
    margin = (0 if runner.score <= best.score else
              (runner.score - best.score) * 1000 // runner.score)
    confidence = (acceptance + margin) // 2
    status = ("VALID" if best.accepted >= 8 and acceptance >= 650 and
              margin >= 150 and confidence >= 750 else "AMBIGUOUS")
    return status, best, runner, confidence


fixtures = {
    9600: [1111, 2222, 3333] * 4,
    19200: [556, 1112, 3892] * 4,
    38400: [278, 556, 1390] * 4,
    57600: [185, 370, 1295] * 4,
    115200: [93, 185, 278, 370, 463, 93, 185, 278, 370, 463, 93, 185],
}

for expected, intervals in fixtures.items():
    status, best, runner, confidence = evaluate(intervals)
    assert status == "VALID", (expected, status, best, runner, confidence)
    assert best.baud == expected, (expected, best)
    assert best.accepted >= 8

status, *_ = evaluate([3065, 2194, 3320, 1110, 1901, 1672,
                       3719, 3573, 1797, 3261, 3228, 1039])
assert status == "AMBIGUOUS"
assert evaluate([10, 20, 30, 40, 50, 60, 64, 10])[0] == "AMBIGUOUS"
print("modbus autobaud score fixtures: PASS")
