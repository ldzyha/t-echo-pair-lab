#!/usr/bin/env python3
"""Capture one fresh TH4/TH5/TH6 sensor log with a manual reference; no config changes."""

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
NUMBER = r"(?:[-+]?\d+(?:\.\d+)?|nan)"
SAMPLE = re.compile(
    rf"BME280 (TH[456]-[\w-]+): rawT=({NUMBER}) rawRH=({NUMBER}) "
    rf"die=({NUMBER}) heat=({NUMBER}) T=({NUMBER}) RH=({NUMBER}) "
    rf"P=({NUMBER}) USB=(\d) known=(\d)",
    re.IGNORECASE,
)
CURVE = re.compile(
    rf"BME280 curve: dieLP=({NUMBER}) range=(-?\d) RH_gain=({NUMBER}) "
    rf"T_offset=({NUMBER}) RH_offset=({NUMBER})",
    re.IGNORECASE,
)
BOARD_STATE = re.compile(r"BME280 board state: frontlight=([01])")


def parse_sample(line):
    match = SAMPLE.search(line)
    if not match:
        return None
    names = (
        "raw_temperature_c",
        "raw_rh_percent",
        "mcu_temperature_c",
        "heat_c",
        "reported_temperature_c",
        "reported_rh_percent",
        "pressure_hpa",
        "usb_reported",
        "usb_state_known",
    )
    result = dict(zip(names, map(float, match.groups()[1:])))
    if not all(math.isfinite(v) for k, v in result.items() if k != "mcu_temperature_c"):
        return None
    if not math.isfinite(result["mcu_temperature_c"]):
        result["mcu_temperature_c"] = None
    result["profile"] = match.group(1)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument(
        "--expected-node", required=True, help="Physical radio ID, e.g. !11223344"
    )
    parser.add_argument("--reference-temperature", required=True, type=float)
    parser.add_argument("--reference-humidity", required=True, type=float)
    parser.add_argument("--timeout", type=float, default=40)
    args = parser.parse_args()
    try:
        expected = int(args.expected_node.removeprefix("!"), 16)
    except ValueError:
        parser.error("Invalid hexadecimal node ID")
    if not 0 < expected < 0xFFFFFFFF:
        parser.error("Invalid node ID")
    if not (
        -40 <= args.reference_temperature <= 85 and 0 <= args.reference_humidity <= 100
    ):
        parser.error("Reference values outside supported ranges")
    if not 0 < args.timeout <= 300:
        parser.error("Timeout must be between 0 and 300 seconds")

    from meshtastic.serial_interface import SerialInterface
    from pubsub import pub
    import serial

    def bounded_flush(stream):
        deadline = time.monotonic() + 2
        while stream.out_waiting:
            if time.monotonic() >= deadline:
                raise serial.SerialTimeoutException("USB drain timeout")
            time.sleep(0.02)

    serial.Serial.flush = bounded_flush
    os.umask(0o077)
    directory = ROOT / ".private" / "calibration"
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    began = time.monotonic()
    record = {
        "reference_recorded_utc": datetime.now(timezone.utc).isoformat(),
        "reference_temperature_c": args.reference_temperature,
        "reference_rh_percent": args.reference_humidity,
        "reference_basis": "manual_before_capture; check latency; not simultaneous",
        "status": "no_fresh_sample",
    }
    ready, identified = threading.Event(), threading.Event()
    sample = {}

    def on_log(line, interface):
        if (
            not identified.is_set()
            or getattr(interface.localNode, "nodeNum", None) != expected
        ):
            return
        if ready.is_set():
            return
        parsed = parse_sample(line)
        if parsed:
            sample.clear()
            sample.update(parsed)
            sample["capture_latency_seconds"] = round(time.monotonic() - began, 3)
            if parsed["profile"].upper().endswith("-RAW"):
                ready.set()
            return
        board = BOARD_STATE.search(line)
        if board and sample:
            sample["frontlight_gpio"] = int(board.group(1))
        match = CURVE.search(line)
        if match and sample:
            values = list(map(float, match.groups()))
            if all(math.isfinite(value) for value in values):
                sample["curve"] = dict(
                    zip(
                        (
                            "filtered_mcu_temperature_c",
                            "range",
                            "rh_gain",
                            "temperature_offset_c",
                            "rh_offset_pp",
                        ),
                        values,
                    )
                )
                ready.set()

    pub.subscribe(on_log, "meshtastic.log.line")
    radio = None
    try:
        radio = SerialInterface(args.port, timeout=35, connectNow=False)
        radio.connect()
        if radio.localNode.nodeNum != expected:
            raise RuntimeError("Connected radio ID differs from --expected-node")
        identified.set()
        if not ready.wait(args.timeout):
            raise RuntimeError(
                "No complete fresh TH4/TH5/TH6 log. Check firmware/log stream and port ownership."
            )
        record["sample"] = sample
        record["status"] = "captured"
    except Exception as error:
        record["error"] = str(error)
        raise
    finally:
        identified.clear()
        pub.unsubscribe(on_log, "meshtastic.log.line")
        try:
            if radio:
                radio.close()
        finally:
            filename = directory / f"sample-{time.time_ns()}.json"
            filename.write_text(json.dumps(record, indent=2, allow_nan=False) + "\n")
            print(f"{record['status']}: {filename.relative_to(ROOT)}")
    print(json.dumps(sample, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
