#!/usr/bin/env python3
"""Generate private, mutually compatible profiles. Does not connect to a radio."""

import argparse
import base64
import json
import os
from pathlib import Path
import secrets
import yaml
from meshtastic.protobuf import apponly_pb2, config_pb2

ROOT = Path(__file__).resolve().parents[1]


def node_id(value):
    try:
        return int(value.removeprefix("!").removeprefix("0x"), 16)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "Use a hexadecimal node ID, such as !11223344."
        ) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node-a", required=True, type=node_id)
    parser.add_argument("--node-b", required=True, type=node_id)
    args = parser.parse_args()
    if not (
        0 < args.node_a < 0xFFFFFFFF
        and 0 < args.node_b < 0xFFFFFFFF
        and args.node_a != args.node_b
    ):
        parser.error("Use two distinct actual node IDs.")
    os.umask(0o077)
    private = ROOT / ".private"
    target = private / "pair.json"
    local = ROOT / "src/mesh/PairSettings.local.h"
    destinations = [private / "radio-a.yaml", private / "radio-b.yaml", target, local]
    if any(path.exists() or path.is_symlink() for path in destinations):
        parser.error(
            "A pair profile, pair.json or PairSettings.local.h already exists. No files changed."
        )
    if private.is_symlink():
        parser.error(".private must be a real directory inside this checkout.")
    profiles = {
        label: yaml.safe_load(
            (ROOT / f"config-examples/radio-{label}.yaml").read_text()
        )
        for label in ("a", "b")
    }
    if not all(isinstance(profile, dict) for profile in profiles.values()):
        parser.error("Each example profile must be a YAML mapping.")
    if not local.parent.is_dir():
        parser.error("Missing src/mesh directory; run from a complete checkout.")
    channel_set = apponly_pb2.ChannelSet()
    channel = channel_set.settings.add()
    channel.name = "Pair"
    channel.psk = secrets.token_bytes(32)
    channel.module_settings.position_precision = 32
    channel_set.lora_config.region = config_pb2.Config.LoRaConfig.EU_868
    channel_set.lora_config.modem_preset = config_pb2.Config.LoRaConfig.LONG_FAST
    channel_set.lora_config.use_preset = True
    channel_set.lora_config.channel_num = 1
    channel_set.lora_config.hop_limit = 3
    channel_set.lora_config.tx_enabled = True
    url = "https://meshtastic.org/e/#" + base64.urlsafe_b64encode(
        channel_set.SerializeToString()
    ).decode().rstrip("=")
    contents = {}
    for label, profile in profiles.items():
        profile["channel_url"] = url
        contents[private / f"radio-{label}.yaml"] = yaml.safe_dump(
            profile, sort_keys=False, allow_unicode=True
        )
    contents[local] = (
        f"#pragma once\n#define PAIR_NODE_A 0x{args.node_a:08x}u\n"
        f"#define PAIR_NODE_B 0x{args.node_b:08x}u\n#define PAIR_ENABLE_THERMAL_EXPERIMENTAL 0\n"
    )
    contents[target] = json.dumps(
        {"node_a": args.node_a, "node_b": args.node_b, "channel_url": url}, indent=2
    )
    private.mkdir(mode=0o700, exist_ok=True)
    private.chmod(0o700)
    created = []
    try:
        for path, content in contents.items():
            with path.open("x", encoding="utf-8") as output:
                created.append(path)
                output.write(content)
                output.flush()
                os.fsync(output.fileno())
    except BaseException:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    print(
        "Created .private/radio-a.yaml, radio-b.yaml, pair.json and ignored PairSettings.local.h."
    )
    print(
        "The channel URL and keys are private. Do not publish those files or CLI export output."
    )


if __name__ == "__main__":
    main()
