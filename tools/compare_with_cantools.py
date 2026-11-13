#!/usr/bin/env python3
"""Compare canlog's decoding with cantools (a widely used reference) on any DBC file.

For every message in the DBC, random payloads are generated, written as a
`candump -L` log, decoded by `canlog_cli`, and also decoded by cantools. Every signal
value must agree. Random bytes exercise all bit layouts: Intel and Motorola,
signed and unsigned, odd lengths, factors and offsets.

Setup (once):   pip install cantools
Usage:          python tools/compare_with_cantools.py --dbc some.dbc --cli build/Release/canlog_cli.exe
Options:        --frames 200   frames per message (default 200)
                --seed 1       random seed, so runs are reproducible
                --keep         keep the generated log and CSV for inspection

Exit code 0 = all values agree, 1 = mismatches or canlog failed, 2 = usage/setup problem.

Known gaps, reported as "skipped" rather than failures:
  * messages longer than 8 bytes (CAN FD) and messages with IEEE float signals
    (canlog does not implement those yet);
  * frames cantools itself refuses to decode (for example a multiplexer value that
    selects no signal group).
"""
import argparse
import math
import random
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

BASE_TIME = 1000.0   # seconds; small enough that %.6f timestamps round-trip exactly
STEP = 0.001


def fail(msg, code=2):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def import_cantools():
    try:
        import cantools  # noqa: PLC0415
    except ImportError:
        fail("cantools is not installed. Run:  pip install cantools")
    return cantools


def payloads(rng, length, count):
    """Edge-case payloads first, then random ones."""
    yield bytes(length)
    yield bytes([0xFF] * length)
    yield bytes([0xAA] * length)
    yield bytes([0x55] * length)
    yield bytes(range(1, length + 1))
    for _ in range(max(0, count - 5)):
        yield bytes(rng.getrandbits(8) for _ in range(length))


def log_line(ts, msg, data):
    ident = f"{msg.frame_id:08X}" if msg.is_extended_frame else f"{msg.frame_id:03X}"
    return f"({ts:.6f}) can0 {ident}#{data.hex().upper()}"


def values_equal(expected, got):
    if math.isnan(expected) and math.isnan(got):
        return True
    return math.isclose(expected, got, rel_tol=1e-7, abs_tol=1e-9)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dbc", required=True, help="DBC file to test")
    ap.add_argument("--cli", required=True, help="path to canlog_cli (canlog_cli.exe on Windows)")
    ap.add_argument("--frames", type=int, default=200, help="frames per message")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep", action="store_true", help="keep generated files")
    ap.add_argument("--max-report", type=int, default=20, help="mismatches to print in detail")
    args = ap.parse_args()

    if not Path(args.dbc).is_file():
        fail(f"DBC file not found: {args.dbc}")
    if not Path(args.cli).is_file():
        fail(f"canlog_cli not found: {args.cli}")

    cantools = import_cantools()
    try:
        db = cantools.database.load_file(args.dbc, strict=False)
    except Exception as e:  # noqa: BLE001 - cantools raises several error types
        fail(f"cantools could not load {args.dbc}: {e}")

    rng = random.Random(args.seed)
    skipped_long, skipped_float = [], []
    frames = []   # (timestamp string, message, data)
    ts_index = 0
    for msg in db.messages:
        if msg.length > 8:
            skipped_long.append(msg.name)
            continue
        if any(getattr(sig, "is_float", False) for sig in msg.signals):
            skipped_float.append(msg.name)
            continue
        for data in payloads(rng, msg.length, args.frames):
            ts = BASE_TIME + ts_index * STEP
            ts_index += 1
            frames.append((f"{ts:.6f}", ts, msg, data))

    if not frames:
        fail("no comparable messages in the DBC", 1)

    workdir = Path(tempfile.mkdtemp(prefix="canlog_cmp_"))
    log_path, csv_path = workdir / "frames.log", workdir / "canlog.csv"
    log_path.write_text("\n".join(log_line(ts, msg, data) for _, ts, msg, data in frames) + "\n")

    run = subprocess.run(
        [args.cli, "--dbc", args.dbc, "--log", str(log_path), "--csv", str(csv_path)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if run.returncode != 0:
        print("FAIL: canlog_cli could not process this DBC:")
        print("  " + run.stderr.strip().replace("\n", "\n  "))
        print(f"(files kept in {workdir})")
        return 1

    # timestamp string -> {signal name: value}
    got = defaultdict(dict)
    for line in csv_path.read_text().splitlines()[1:]:
        ts, _message, signal, value, _unit = line.split(",", 4)
        got[ts][signal] = float(value)

    compared_frames = skipped_decode = compared_values = 0
    mismatches = []
    per_message = defaultdict(int)

    for ts_text, _ts, msg, data in frames:
        try:
            expected = msg.decode(data, decode_choices=False, scaling=True)
        except Exception:  # noqa: BLE001 - cantools refuses some frames; not canlog's fault
            skipped_decode += 1
            continue
        compared_frames += 1
        ours = got.get(ts_text, {})

        for name, value in expected.items():
            compared_values += 1
            try:
                expected_value = float(value)
            except (TypeError, ValueError):
                continue   # non-numeric result (for example a choice string): nothing to compare
            if name not in ours:
                mismatches.append((msg.name, name, data, expected_value, "(missing in canlog)"))
                per_message[msg.name] += 1
            elif not values_equal(expected_value, ours[name]):
                mismatches.append((msg.name, name, data, expected_value, ours[name]))
                per_message[msg.name] += 1
        for name in ours:
            if name not in expected:
                mismatches.append((msg.name, name, data, "(missing in cantools)", ours[name]))
                per_message[msg.name] += 1

    print(f"canlog vs cantools on {args.dbc}")
    print(f"  messages in DBC         : {len(db.messages)}")
    print(f"  skipped (CAN FD, >8 B)  : {len(skipped_long)}")
    print(f"  skipped (float signals) : {len(skipped_float)}")
    print(f"  frames generated        : {len(frames)}")
    print(f"  frames compared         : {compared_frames}  ({skipped_decode} skipped: cantools could not decode them)")
    print(f"  signal values compared  : {compared_values}")
    print(f"  mismatches              : {len(mismatches)}")

    if mismatches:
        print("\nMessages with mismatches:")
        for name, count in sorted(per_message.items(), key=lambda kv: -kv[1])[:15]:
            print(f"  {count:6d}  {name}")
        print(f"\nFirst {min(args.max_report, len(mismatches))} mismatches:")
        for name, signal, data, expected, actual in mismatches[: args.max_report]:
            print(f"  {name}.{signal}  data={data.hex().upper()}  cantools={expected}  canlog={actual}")
        print(f"\nFAIL (generated files kept in {workdir})")
        return 1

    if not args.keep:
        for path in (log_path, csv_path):
            path.unlink(missing_ok=True)
        workdir.rmdir()
    else:
        print(f"(files kept in {workdir})")
    print("\nRESULT: OK, canlog and cantools agree on every compared value")
    return 0


if __name__ == "__main__":
    sys.exit(main())
