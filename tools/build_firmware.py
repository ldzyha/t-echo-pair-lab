#!/usr/bin/env python3
"""Build T-Echo Plus and collect locally generated artifacts (never public by default)."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not (ROOT / "src/mesh/PairSettings.local.h").exists():
        print(
            "No local pair configuration: fixed-pair queue and thermal corrections are disabled; Peer may use a favorite node."
        )
    subprocess.run(
        [
            sys.executable,
            "-m",
            "platformio",
            "run",
            "-e",
            "t-echo-plus",
            "-t",
            "mtjson",
        ],
        cwd=ROOT,
        check=True,
    )
    target = ROOT / ".artifacts"
    target.mkdir(exist_ok=True)
    hashes = {}
    for source in (ROOT / ".pio/build/t-echo-plus").glob("firmware-t-echo-plus-*"):
        if source.suffix in (".zip", ".uf2", ".hex", ".elf"):
            dest = target / source.name
            shutil.copy2(source, dest)
            hashes[dest.name] = hashlib.sha256(dest.read_bytes()).hexdigest()
    if not hashes:
        raise RuntimeError("No firmware artifacts found.")
    manifest = {
        "source_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip(),
        "dirty": bool(
            subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)
        ),
        "files": hashes,
        "private": "Artifacts may contain locally configured node IDs.",
    }
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print("Firmware and SHA256 manifest saved under .artifacts/ (ignored by Git).")


if __name__ == "__main__":
    main()
