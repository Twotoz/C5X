#!/usr/bin/env python3
"""Analyze C5X R0 bounded Q4/I4 captures.

Usage:
    python tools/analyze_capture.py c5xcap.bin [more-captures.bin ...]

The binary is the raw contents of the c5xcap partition, normally read with:
    parttool.py --port <PORT> read_partition \
        --partition-name c5xcap --output c5xcap.bin
"""

from __future__ import annotations

import math
import struct
import sys
from collections import Counter
from pathlib import Path

MAGIC = 0x30583543
VERSION = 1
SLOT_SIZE = 0x11000
DATA_OFFSET = 0x1000

HEADER = struct.Struct("<IHH11IiiQ15I")
assert HEADER.size == 128


def sign4(v: int) -> int:
    v &= 0xF
    return v - 16 if v & 8 else v


def parse_header(blob: bytes, slot: int) -> dict:
    raw = blob[slot: slot + HEADER.size]
    if len(raw) != HEADER.size:
        raise ValueError("capture file is too small")

    values = list(HEADER.unpack(raw))
    magic, version, header_bytes = values[:3]
    if magic != MAGIC:
        raise ValueError(
            f"slot 0x{slot:x}: bad magic 0x{magic:08x}; "
            "record was not completed"
        )
    if version != VERSION or header_bytes != HEADER.size:
        raise ValueError(
            f"slot 0x{slot:x}: unsupported header "
            f"version={version} bytes={header_bytes}"
        )

    names = [
        "kind", "capture_status", "sample_rate_hz", "sample_bytes",
        "wifi_channel", "flags", "dump_ctrl_before", "dump_ptr_before",
        "dump_ctrl_after", "dump_ptr_after", "fnv1a",
    ]
    out = {
        "magic": magic,
        "version": version,
        "header_bytes": header_bytes,
    }
    pos = 3
    for name in names:
        out[name] = values[pos]
        pos += 1

    out["i_sum"] = values[pos]
    out["q_sum"] = values[pos + 1]
    pos += 2
    out["power_sum"] = values[pos]
    pos += 1

    tail_names = [
        "nonzero_bytes", "changed_bytes", "sentinel_bytes",
        "tx_attempted", "tx_ok", "hook_status", "capture_cycles",
        "parlio_int_raw", "parlio_rx_st0", "parlio_rx_st1",
        "reserved0", "reserved1", "reserved2", "reserved3", "reserved4",
    ]
    for name in tail_names:
        out[name] = values[pos]
        pos += 1

    return out


def metrics(samples: bytes) -> dict:
    hist = Counter(samples)
    isum = qsum = 0
    pwr = 0
    for b in samples:
        q = sign4(b)
        i = sign4(b >> 4)
        isum += i
        qsum += q
        pwr += i * i + q * q
    n = max(1, len(samples))
    return {
        "mean_i": isum / n,
        "mean_q": qsum / n,
        "rms": math.sqrt(pwr / n),
        "unique": len(hist),
        "hist": hist,
    }


def hist_distance(a: Counter, b: Counter, n_a: int, n_b: int) -> float:
    keys = set(a) | set(b)
    return 0.5 * sum(
        abs(a.get(k, 0) / n_a - b.get(k, 0) / n_b)
        for k in keys
    )


def load_record(blob: bytes, index: int) -> tuple[dict, bytes, dict]:
    slot = index * SLOT_SIZE
    h = parse_header(blob, slot)
    start = slot + DATA_OFFSET
    end = start + h["sample_bytes"]
    samples = blob[start:end]
    if len(samples) != h["sample_bytes"]:
        raise ValueError(f"slot {index}: truncated sample payload")
    return h, samples, metrics(samples)


def describe(label: str, h: dict, m: dict) -> None:
    duration_us = h["sample_bytes"] / h["sample_rate_hz"] * 1e6
    print(f"{label}:")
    print(
        f"  bytes/rate: {h['sample_bytes']} @ "
        f"{h['sample_rate_hz']/1e6:.3f} MS/s "
        f"({duration_us:.1f} us nominal)"
    )
    print(
        f"  capture_status=0x{h['capture_status']:08x} "
        f"hook_status=0x{h['hook_status']:08x}"
    )
    print(
        f"  hash={h['fnv1a']:08x} unique={m['unique']} "
        f"sentinel={h['sentinel_bytes']}"
    )
    print(
        f"  mean I/Q={m['mean_i']:.3f}/{m['mean_q']:.3f} "
        f"RMS={m['rms']:.3f}"
    )
    print(
        f"  dump ctrl {h['dump_ctrl_before']:08x} -> "
        f"{h['dump_ctrl_after']:08x}"
    )
    print(
        f"  dump ptr  {h['dump_ptr_before']:08x} -> "
        f"{h['dump_ptr_after']:08x}"
    )
    if h["kind"] == 1:
        print(
            f"  TX frames: {h['tx_ok']}/{h['tx_attempted']} queued"
        )


def analyze(path: Path) -> None:
    blob = path.read_bytes()
    base_h, base_s, base_m = load_record(blob, 0)
    tx_h, tx_s, tx_m = load_record(blob, 1)

    print(f"\n=== {path} ===")
    describe("BASE", base_h, base_m)
    describe("TX", tx_h, tx_m)

    d = hist_distance(
        base_m["hist"], tx_m["hist"], len(base_s), len(tx_s)
    )
    rms_ratio = tx_m["rms"] / base_m["rms"] if base_m["rms"] else float("inf")
    same_bytes = sum(a == b for a, b in zip(base_s, tx_s))

    print("COMPARISON:")
    print(f"  histogram TV distance: {d:.4f}")
    print(f"  TX/base RMS ratio:     {rms_ratio:.4f}")
    print(f"  same byte positions:   {same_bytes}/{min(len(base_s), len(tx_s))}")

    if tx_h["tx_ok"] == 0:
        print("  verdict: TX did not queue; overlap test is inconclusive")
    elif tx_h["sentinel_bytes"] > len(tx_s) // 2:
        print("  verdict: TX capture is mostly sentinel/unfilled; inspect RF clocking")
    elif tx_h["capture_status"] != 0:
        print("  verdict: capture timed out/failed; register transitions are still useful")
    else:
        print(
            "  verdict: both records were captured. "
            "Repeat at several reflector positions before claiming an RF echo."
        )


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    for arg in sys.argv[1:]:
        analyze(Path(arg))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
