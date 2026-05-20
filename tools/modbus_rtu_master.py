#!/usr/bin/env python3
"""
Simple Modbus RTU master helper for quick write/read tests.

Examples:
  python tools/modbus_rtu_master.py --port COM3 --slave 3 --baud 19200 --parity N --stopbits 2 read-holding 11520 10
  python tools/modbus_rtu_master.py --port COM3 --slave 3 --baud 19200 --parity N --stopbits 2 write-holding 11521 5000
"""

from __future__ import annotations

import argparse
import sys
from typing import List

import minimalmodbus


def _parity(value: str) -> str:
    v = value.strip().upper()
    if v in ("N", "NONE"):
        return "N"
    if v in ("E", "EVEN"):
        return "E"
    if v in ("O", "ODD"):
        return "O"
    raise argparse.ArgumentTypeError("parity must be N/E/O")


def _u16(value: str) -> int:
    base = 16 if value.lower().startswith("0x") else 10
    n = int(value, base)
    if not (0 <= n <= 0xFFFF):
        raise argparse.ArgumentTypeError("value must be 0..65535")
    return n


def _make_instrument(args: argparse.Namespace) -> minimalmodbus.Instrument:
    inst = minimalmodbus.Instrument(args.port, args.slave, mode=minimalmodbus.MODE_RTU)
    inst.serial.baudrate = args.baud
    inst.serial.bytesize = 8
    inst.serial.parity = args.parity
    inst.serial.stopbits = args.stopbits
    inst.serial.timeout = args.timeout
    inst.clear_buffers_before_each_transaction = True
    return inst


def _cmd_read_holding(inst: minimalmodbus.Instrument, addr: int, count: int) -> int:
    regs: List[int] = inst.read_registers(addr, count, functioncode=3)
    for i, v in enumerate(regs):
        print(f"{addr + i}: {v}")
    return 0


def _cmd_write_holding(inst: minimalmodbus.Instrument, addr: int, value: int) -> int:
    inst.write_register(addr, value, functioncode=6)
    readback = inst.read_register(addr, functioncode=3)
    print(f"{addr}: wrote {value}, readback {readback}")
    return 0


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--slave", type=int, required=True)
    p.add_argument("--baud", type=int, default=19200)
    p.add_argument("--parity", type=_parity, default="N")
    p.add_argument("--stopbits", type=int, choices=(1, 2), default=2)
    p.add_argument("--timeout", type=float, default=0.4)

    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("read-holding")
    r.add_argument("addr", type=int)
    r.add_argument("count", type=int)

    w = sub.add_parser("write-holding")
    w.add_argument("addr", type=int)
    w.add_argument("value", type=_u16)

    args = p.parse_args(argv)
    inst = _make_instrument(args)

    if args.cmd == "read-holding":
        return _cmd_read_holding(inst, args.addr, args.count)
    if args.cmd == "write-holding":
        return _cmd_write_holding(inst, args.addr, args.value)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

