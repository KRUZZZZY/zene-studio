#!/usr/bin/env python3
"""Regenerate `zene_control/commands_snapshot.json` from a live instance.

The snapshot is what makes `zene_commands` (and therefore `tools/list`) work
before any instance exists — it is *derived* from a real `control.commands_list`,
never hand-written, and it carries the provenance of the instance it came from.

    python3 snapshot_commands.py --socket /tmp/zene.sock
    python3 snapshot_commands.py --from-file /tmp/commands.json

Exit code 0 only when the snapshot was written.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from zene_control import registry as R  # noqa: E402
from zene_control.config import Config, ZeneControlError  # noqa: E402
from zene_control.protocol import ControlClient  # noqa: E402

DEFAULT_OUT = HERE / "zene_control" / "commands_snapshot.json"
DEFAULT_LANE = HERE.parent / "zene-pa-agentctl"


def sha256_file(path: str) -> str | None:
    try:
        digest = hashlib.sha256()
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def git_head(repo: str) -> str | None:
    try:
        out = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.strip()


def instance_provenance(socket_path: str, banner: str | None, lane_dir: str) -> dict:
    lane = Path(lane_dir)
    binary = os.environ.get("ZENE_CONTROL_BINARY", str(lane / "build" / "lmms"))
    provenance: dict = {
        "source_socket": socket_path,
        "binary": binary if os.path.exists(binary) else None,
        "binary_bytes": os.path.getsize(binary) if os.path.exists(binary) else None,
        "binary_sha256": sha256_file(binary) if os.path.exists(binary) else None,
        "lane": str(lane),
        "lane_head": git_head(str(lane)),
    }
    if banner:
        with ControlClient(socket_path, 10.0) as client:
            ping = client.handshake(wait_ready=False)
        provenance.update({"version": ping.get("version"), "proto": ping.get("proto")})
    return provenance


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=os.environ.get("ZENE_CONTROL_SOCKET"),
                        help="control socket of a running instance (live capture)")
    parser.add_argument("--from-file", default=None,
                        help="a previously dumped commands array (a list, or an object with 'commands')")
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    parser.add_argument("--lane", default=str(DEFAULT_LANE),
                        help="the DAW lane directory, recorded for provenance")
    parser.add_argument("--proto", type=int, default=1)
    args = parser.parse_args(argv)

    if args.from_file:
        with open(args.from_file, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        commands = data.get("commands") if isinstance(data, dict) else data
        result = {"commands": commands, "count": len(commands or []), "proto": args.proto}
        provenance = {"source_file": os.path.abspath(args.from_file)}
    elif args.socket:
        config = Config(socket_path=os.path.abspath(args.socket), workdir=os.getcwd(),
                        state_dir=str(HERE / ".state"), proto=args.proto)
        try:
            with ControlClient(config.socket_path, 30.0, config.proto) as client:
                ping = client.handshake(wait_ready=False)
                result = client.request("control.commands_list")
        except ZeneControlError as exc:
            print(f"FAIL: {exc.kind}: {exc.message}")
            if exc.hint:
                print(f"hint: {exc.hint}")
            return 1
        result.setdefault("proto", ping.get("proto"))
        provenance = instance_provenance(config.socket_path, "banner", args.lane)
    else:
        print("FAIL: give --socket <path> (live) or --from-file <json>")
        return 2

    if not result.get("commands"):
        print("FAIL: the source carried no commands")
        return 1

    bundle = R.bundle_from_result(result, provenance)
    R.save_bundle(str(args.out), bundle)
    print(f"wrote {args.out}: {bundle['count']} commands, proto {bundle.get('proto')}, "
          f"version {provenance.get('version')}, lane head {provenance.get('lane_head')}")
    print("ids: " + ", ".join(sorted(c["id"] for c in bundle["commands"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
