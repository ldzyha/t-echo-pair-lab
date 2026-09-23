#!/usr/bin/env python3
"""Read node IDs, back up settings, or import physically verified peer contacts over USB."""

import argparse
import base64
import json
import os
from pathlib import Path
import time
import serial
from meshtastic.serial_interface import SerialInterface
from meshtastic.__main__ import export_config
from meshtastic.protobuf import admin_pb2

ROOT = Path(__file__).resolve().parents[1]


def bounded_flush(stream):
    deadline = time.monotonic() + 2
    while stream.out_waiting:
        if time.monotonic() > deadline:
            raise serial.SerialTimeoutException("USB drain timeout")
        time.sleep(0.02)


def close_radio(radio):
    try:
        radio.close()
    finally:
        timer = getattr(radio, "heartbeatTimer", None)
        if timer:
            timer.cancel()
        stream = getattr(radio, "stream", None)
        if stream is not None:
            stream.close()


def public_key(user):
    try:
        key = base64.b64decode(user.get("publicKey", ""), validate=True)
    except (ValueError, TypeError) as error:
        raise RuntimeError("Invalid public key returned by the radio.") from error
    if len(key) != 32 or not any(key):
        raise RuntimeError(
            "Physical peer has no usable public key. Configure region/PKI first."
        )
    return key


def physical_key(radio):
    key = bytes(radio.localNode.localConfig.security.public_key)
    own = radio.nodesByNum.get(radio.localNode.nodeNum, {}).get("user", {})
    if key != public_key(own):
        raise RuntimeError(
            "Physical peer configuration and own NodeInfo public keys disagree."
        )
    return key


def checked_contact(radio, key):
    url = radio.localNode.getContactURL(radio.localNode.nodeNum, manually_verified=True)
    encoded = url.rsplit("/#", 1)[-1]
    contact = admin_pb2.SharedContact()
    contact.ParseFromString(
        base64.urlsafe_b64decode(encoded + "=" * (-len(encoded) % 4))
    )
    if (
        contact.node_num != radio.localNode.nodeNum
        or not contact.HasField("user")
        or bytes(contact.user.public_key) != key
        or not contact.manually_verified
        or contact.should_ignore
    ):
        raise RuntimeError(
            "Contact changed while preparing the physical key import. Retry without other clients."
        )
    return url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("identify", "backup", "trust"))
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    args = parser.parse_args()
    if os.path.normcase(os.path.realpath(args.port_a)) == os.path.normcase(
        os.path.realpath(args.port_b)
    ):
        parser.error("The two radios must use different USB ports.")
    os.umask(0o077)
    interfaces = {}
    expected = (
        json.loads((ROOT / ".private/pair.json").read_text())
        if args.action == "trust"
        else None
    )
    if expected is not None and not (
        isinstance(expected, dict)
        and isinstance(expected.get("node_a"), int)
        and isinstance(expected.get("node_b"), int)
        and 0 < expected["node_a"] < 0xFFFFFFFF
        and 0 < expected["node_b"] < 0xFFFFFFFF
        and expected["node_a"] != expected["node_b"]
    ):
        parser.error(".private/pair.json must contain two distinct valid node IDs.")
    folder = None
    if args.action == "backup":
        private = ROOT / ".private"
        if private.is_symlink():
            parser.error(".private must be a real directory inside this checkout.")
        private.mkdir(mode=0o700, exist_ok=True)
        private.chmod(0o700)
        folder = private / "backups" / str(time.time_ns())
        folder.mkdir(mode=0o700, parents=True)
    keys = {}
    serial.Serial.flush = bounded_flush
    try:
        for label, port in (("a", args.port_a), ("b", args.port_b)):
            radio = SerialInterface(port, timeout=40)
            interfaces[label] = radio
            node = radio.localNode.nodeNum
            if expected and node != expected["node_" + label]:
                raise RuntimeError(
                    f"Radio {label.upper()} identity does not match .private/pair.json"
                )
            print(
                f"Radio {label.upper()}: !{node:08x}, firmware {radio.metadata.firmware_version}"
            )
            if args.action == "backup":
                with (folder / f"radio-{label}.yaml").open(
                    "x", encoding="utf-8"
                ) as output:
                    output.write(export_config(radio))
        if interfaces["a"].localNode.nodeNum == interfaces["b"].localNode.nodeNum:
            raise RuntimeError(
                "Both ports report the same node ID. Pairing requires two distinct radios."
            )
        if args.action == "trust":
            keys = {label: physical_key(radio) for label, radio in interfaces.items()}
            if keys["a"] == keys["b"]:
                raise RuntimeError(
                    "The two physical radios share a public key. Resolve the copied identity before pairing."
                )
            contacts = {
                label: checked_contact(radio, keys[label])
                for label, radio in interfaces.items()
            }
            for label, radio in interfaces.items():
                radio.localNode.addContactURL(contacts["b" if label == "a" else "a"])
                print(
                    f"Imported physical peer as a verified contact on Radio {label.upper()}."
                )
            time.sleep(3)
    finally:
        for radio in interfaces.values():
            try:
                close_radio(radio)
            except OSError:
                pass
    if args.action == "trust":
        for label, port in (("a", args.port_a), ("b", args.port_b)):
            radio = SerialInterface(port, timeout=40)
            try:
                if (
                    radio.localNode.nodeNum != expected["node_" + label]
                    or physical_key(radio) != keys[label]
                ):
                    raise RuntimeError(
                        "Physical radio identity changed during contact verification."
                    )
                peer_label = "b" if label == "a" else "a"
                peer = radio.nodesByNum.get(expected["node_" + peer_label], {})
                if (
                    not peer.get("isKeyManuallyVerified", False)
                    or peer.get("isIgnored", False)
                    or public_key(peer.get("user", {})) != keys[peer_label]
                ):
                    raise RuntimeError(
                        "Contact readback does not match the physically verified peer key and flags."
                    )
            finally:
                close_radio(radio)
        print(
            "Verified peer keys and flags read back. Confirm delivery separately with a message in each direction."
        )


if __name__ == "__main__":
    main()
