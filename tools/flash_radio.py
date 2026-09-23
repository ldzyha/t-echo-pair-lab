#!/usr/bin/env python3
"""Update one identified T-Echo Plus through application-only serial DFU, without factory reset."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile
import serial
from serial.tools import list_ports
from meshtastic.serial_interface import SerialInterface
from meshtastic.__main__ import export_config

ROOT = Path(__file__).resolve().parents[1]


def node_id(value):
    try:
        result = int(value.removeprefix("!").removeprefix("0x"), 16)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "Use a hexadecimal node ID, such as !11223344."
        ) from error
    if not 0 < result < 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(
            "Use an actual nonzero, non-broadcast node ID."
        )
    return result


def validated_package(path):
    if (
        not path.is_file()
        or path.suffix.lower() != ".zip"
        or path.name.lower().endswith("-ota.zip")
    ):
        raise ValueError("Choose the generated serial DFU .zip file, not -ota.zip.")
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("DFU package is too large for this nRF52 application update.")
    data = path.read_bytes()
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as package:
            names = package.namelist()
            if len(names) != len(set(names)) or "manifest.json" not in names:
                raise ValueError("DFU package has missing or duplicate entries.")
            if any(
                "/" in name or "\\" in name or name in (".", "..") for name in names
            ):
                raise ValueError("DFU package entries must be plain filenames.")
            if sum(item.file_size for item in package.infolist()) > 2 * 1024 * 1024:
                raise ValueError("Expanded DFU package is too large.")
            manifest = json.loads(package.read("manifest.json"))["manifest"]
            if not isinstance(manifest, dict) or any(
                value
                for key, value in manifest.items()
                if key not in ("application", "dfu_version")
            ):
                raise ValueError(
                    "Only application DFU is supported; bootloader/SoftDevice updates are rejected."
                )
            application = manifest["application"]
            binary, init = application["bin_file"], application["dat_file"]
            if (
                not isinstance(binary, str)
                or not isinstance(init, str)
                or not binary.endswith(".bin")
                or not init.endswith(".dat")
                or set(names) != {"manifest.json", binary, init}
            ):
                raise ValueError(
                    "Expected a serial DFU application .bin, .dat and manifest.json."
                )
            if (
                not 0 < package.getinfo(binary).file_size <= 1024 * 1024
                or not package.getinfo(init).file_size
            ):
                raise ValueError("Invalid application or init-packet size.")
            if package.testzip() is not None:
                raise ValueError("DFU ZIP integrity check failed.")
    except (zipfile.BadZipFile, KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError("Invalid serial DFU application package.") from error
    return data


def bounded_flush(self):
    deadline = time.monotonic() + 2
    while self.out_waiting:
        if time.monotonic() > deadline:
            raise serial.SerialTimeoutException("USB drain timeout")
        time.sleep(0.02)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument(
        "--expected-node",
        required=True,
        type=node_id,
        help="Actual node ID, for example !11223344",
    )
    p.add_argument(
        "--package",
        required=True,
        type=Path,
        help="Generated serial DFU zip, not the -ota.zip",
    )
    args = p.parse_args()
    expected = args.expected_node
    package = args.package.resolve()
    try:
        package_bytes = validated_package(package)
    except (OSError, ValueError) as error:
        p.error(str(error))
    utility = shutil.which("adafruit-nrfutil")
    if not utility:
        p.error("Activate the virtual environment containing adafruit-nrfutil.")
    device = next(
        (
            v
            for v in list_ports.comports()
            if os.path.normcase(os.path.realpath(v.device))
            == os.path.normcase(os.path.realpath(args.port))
        ),
        None,
    )
    if not device or not device.serial_number:
        p.error(
            "USB device must expose a serial number so its bootloader can be identified."
        )
    os.umask(0o077)
    private = ROOT / ".private"
    if private.is_symlink():
        p.error(".private must be a real directory inside this checkout.")
    private.mkdir(mode=0o700, exist_ok=True)
    private.chmod(0o700)
    output = private / "flash" / str(time.time_ns())
    output.mkdir(mode=0o700, parents=True)
    snapshot = output / "firmware.zip"
    snapshot.write_bytes(package_bytes)
    digest = hashlib.sha256(package_bytes).hexdigest()
    serial.Serial.flush = bounded_flush
    with (output / "serial.log").open("w") as log:
        radio = SerialInterface(args.port, debugOut=log, timeout=40)
        try:
            if radio.localNode.nodeNum != expected:
                raise RuntimeError(
                    "Connected radio ID differs from --expected-node. Nothing flashed."
                )
            model = radio.nodesByNum.get(expected, {}).get("user", {}).get("hwModel")
            if model != "T_ECHO_PLUS":
                raise RuntimeError(
                    "This helper requires a physically identified T_ECHO_PLUS. Nothing flashed."
                )
            (output / "before.yaml").write_text(export_config(radio))
            radio.localNode.enterDFUMode()
            time.sleep(2)
        finally:
            try:
                radio.close()
            except OSError:
                pass
            finally:
                timer = getattr(radio, "heartbeatTimer", None)
                if timer:
                    timer.cancel()
                stream = getattr(radio, "stream", None)
                if stream is not None:
                    stream.close()
    deadline = time.monotonic() + 40
    boot = None
    while time.monotonic() < deadline:
        candidates = [
            v
            for v in list_ports.comports()
            if v.serial_number == device.serial_number
            and v.vid == device.vid
            and v.pid != device.pid
        ]
        if len(candidates) > 1:
            raise RuntimeError("Bootloader identity is ambiguous; nothing flashed.")
        boot = candidates[0] if candidates else None
        if boot:
            break
        time.sleep(0.5)
    if not boot:
        raise RuntimeError(
            "Bootloader not visible. Share the new USB device with ChromeOS Linux; see docs/BUILD-FLASH.md."
        )
    command = [
        utility,
        "dfu",
        "serial",
        "--package",
        str(snapshot),
        "--port",
        boot.device,
        "--baudrate",
        "115200",
        "--singlebank",
    ]
    timed_out = False
    try:
        run = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=180,
        )
        log = run.stdout
        ok = run.returncode == 0 and "Device programmed" in log
    except subprocess.TimeoutExpired as error:
        timed_out = True
        log = error.stdout or ""
        if isinstance(log, bytes):
            log = log.decode("utf-8", errors="replace")
        log += (
            "\nDFU process exceeded the 180-second limit. Device state is unverified.\n"
        )
        ok = False
    (output / "dfu.log").write_text(log, encoding="utf-8")
    (output / "result.json").write_text(
        json.dumps(
            {"programmed": ok, "timed_out": timed_out, "sha256": digest}, indent=2
        )
    )
    if not ok:
        raise RuntimeError(f"DFU failed; see {output}/dfu.log")
    print(
        "Firmware programmed. Verify identity, preserved keys/settings and live sensor readings after restart."
    )


if __name__ == "__main__":
    main()
