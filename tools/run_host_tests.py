#!/usr/bin/env python3
"""Compile/run production helpers and module harnesses. No radio or USB writes."""

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--core-only",
        action="store_true",
        help="Skip suites that need downloaded firmware libraries.",
    )
    args = p.parse_args()
    output = ROOT / ".build-host-tests"
    output.mkdir(exist_ok=True)
    flags = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DPAIR_IGNORE_LOCAL_SETTINGS",
        "-DPAIR_NODE_A=0x11223344u",
        "-DPAIR_NODE_B=0x55667788u",
        "-DPAIR_ENABLE_THERMAL_EXPERIMENTAL=1",
    ]
    suites = {
        name: [f"test/custom_audit/test_{name}.cpp"]
        for name in (
            "thermal_model",
            "environment_setup",
            "clock_sensor_status",
            "delivery_queue",
            "gps_fix",
            "messages",
            "peer_status",
            "time",
        )
    }
    suites["environment"] = [
        "-Isrc/mesh",
        "-Itest/custom_audit/test_environment_stubs",
        "test/custom_audit/test_environment.cpp",
        "src/mesh/Throttle.cpp",
    ]
    suites["rtc_policy"] = [
        "-DPIO_UNIT_TESTING",
        "-Itest/custom_audit/test_time_stubs",
        "-Isrc/mesh",
        "test/custom_audit/test_rtc_policy.cpp",
        "src/gps/RTC.cpp",
        "src/mesh/Throttle.cpp",
    ]
    if not args.core_only:
        lib = ".pio/libdeps/t-echo-plus"
        if not (ROOT / lib / "Nanopb/pb_encode.c").exists():
            p.error(
                "Build once, or run: python -m platformio pkg install -e t-echo-plus. Use --core-only for dependency-free suites."
            )
        proto = [
            "-ffunction-sections",
            "-fdata-sections",
            "-Isrc/mesh/generated",
            f"-I{lib}/Nanopb",
            "src/mesh/generated/meshtastic/mesh.pb.cpp",
            f"{lib}/Nanopb/pb_encode.c",
            f"{lib}/Nanopb/pb_common.c",
            "-Wl,--gc-sections",
        ]
        suites["delivery_payload"] = [
            "test/custom_audit/test_delivery_payload.cpp"
        ] + proto
        suites["quick_heart"] = [
            "-Itest/custom_audit/test_button_stubs",
            "-Isrc/mesh/generated",
            f"-I{lib}/OneButton/src",
            f"-I{lib}/Nanopb",
            "test/custom_audit/test_quick_heart.cpp",
            f"{lib}/OneButton/src/OneButton.cpp",
        ]
        suites["delivery_module"] = [
            "-Itest/custom_audit/test_delivery_stubs",
            "test/custom_audit/test_delivery_module.cpp",
            f"{lib}/Nanopb/pb_decode.c",
        ] + proto
        suites["rtc_restart"] = [
            "-DPIO_UNIT_TESTING",
            "-DTEST_HARDWARE_RTC",
            "-Itest/custom_audit/test_time_stubs",
            "-Isrc/mesh",
            f"-I{lib}/ErriezCRC32/src",
            "test/custom_audit/test_rtc_restart.cpp",
            "src/gps/RTC.cpp",
            "src/mesh/Throttle.cpp",
            f"{lib}/ErriezCRC32/src/ErriezCRC32.c",
        ]
    for name, extra in suites.items():
        executable = output / name
        subprocess.run(
            flags + extra + ["-Isrc", "-o", str(executable)], cwd=ROOT, check=True
        )
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print(
        f"PASS: {len(suites)} host suites. Hardware, RF, phone apps and physical calibration are separate checks."
    )


if __name__ == "__main__":
    main()
