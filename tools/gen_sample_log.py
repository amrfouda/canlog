#!/usr/bin/env python3
"""Generate data/sample.log (candump -L format) that matches data/sample.dbc.

The encoders below use a *different* formulation of the DBC bit layout than the
C++ decoder (whole-payload integers instead of a bit-by-bit walk), so decoding
this log with canlog is an independent cross-check of the decoder.

Usage: python3 tools/gen_sample_log.py
"""
import math
from pathlib import Path

T0 = 1_700_000_000.0


def intel(fields, size=8):
    """fields: (start_bit, length, raw). start_bit is the LSB; little-endian payload."""
    value = 0
    for start, length, raw in fields:
        value |= (raw & ((1 << length) - 1)) << start
    return value.to_bytes(8, "little")[:size]


def motorola(fields, size=8):
    """fields: (start_bit, length, raw). start_bit is the DBC MSB position.

    Trick: in a *big-endian* 64-bit integer a Motorola signal is one contiguous
    run of bits. Map the DBC sawtooth bit number to a position in that integer,
    then it is a plain shift.
    """
    value = 0
    for start, length, raw in fields:
        msb = (7 - start // 8) * 8 + start % 8
        lsb = msb - length + 1
        value |= (raw & ((1 << length) - 1)) << lsb
    return value.to_bytes(8, "big")[:size]


def line(t, can_id, extended, data, iface="can0"):
    id_str = f"{can_id:08X}" if extended else f"{can_id:03X}"
    return f"({T0 + t:.6f}) {iface} {id_str}#{data.hex().upper()}"


def build():
    events = []

    # EngineData @ 20 ms: rpm ramps 800 -> 3000, coolant 50 -> 52 degC, throttle 0 -> 60 %
    for k in range(51):
        t = k * 0.02
        rpm = 800 + 2200 * t
        coolant = 50 + 2 * t
        throttle = 60 * t
        state = 1 if t < 0.1 else 2   # Cranking, then Running
        data = intel([
            (0, 16, round(rpm / 0.25)),
            (16, 8, round(coolant + 40)),
            (24, 8, round(throttle / 0.4)),
            (32, 2, state),
        ])
        events.append((t, line(t, 0x100, False, data)))

    # VehicleDynamics @ 50 ms (Motorola): speed 0 -> 60 km/h, steering sweeps +-30 deg
    for k in range(21):
        t = k * 0.05
        speed = 60 * t
        steering = 30 * math.sin(2 * math.pi * t)
        data = motorola([
            (7, 16, round(speed / 0.01)),
            (23, 16, round(steering / 0.1) & 0xFFFF),
        ])
        events.append((t, line(t, 0x200, False, data)))

    # BrakeStatus @ 100 ms, DLC 4: pressure builds after t=0.4, ABS kicks in at 0.9
    for k in range(11):
        t = k * 0.1
        pressure = max(0.0, (t - 0.4) * 100)
        abs_active = 1 if t >= 0.9 else 0
        data = intel([(0, 12, round(pressure / 0.1)), (12, 1, abs_active)], size=4)
        events.append((t, line(t, 0x300, False, data)))

    # DiagFrame @ 200 ms (multiplexed): selector 0 -> OilTemp, selector 1 -> OilPressure
    for k in range(6):
        t = k * 0.2
        if k % 2 == 0:
            oil_temp = 70 + 10 * t
            data = intel([(0, 8, 0), (8, 16, round((oil_temp + 40) / 0.1))])
        else:
            oil_pressure = 300 + 50 * t
            data = intel([(0, 8, 1), (8, 16, round(oil_pressure / 0.1))])
        events.append((t, line(t, 0x400, False, data)))

    # J1939 EEC1 (29-bit id 0x0CF00400) @ 100 ms
    for k in range(11):
        t = k * 0.1
        rpm = 800 + 2200 * t
        torque = 20 + 40 * t
        data = intel([(16, 8, round(torque + 125)), (24, 16, round(rpm / 0.125))])
        events.append((t, line(t, 0x0CF00400, True, data)))

    # An id that is not in the DBC (a UDS request), to exercise "unknown id" handling
    for t in (0.3, 0.6, 0.9):
        events.append((t, line(t, 0x7E0, False, bytes.fromhex("0322F19000000000"))))

    events.sort(key=lambda e: e[0])   # stable: same-timestamp frames keep insertion order
    return [text for _, text in events]


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "data" / "sample.log"
    lines = build()
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} frames to {out}")
