#!/usr/bin/env python3
"""The offline copy's staleness is a CHECK, in both directions.

    python3 -m unittest tests.test_offline_staleness -v
    python3 tests/test_offline_staleness.py

WHY THIS IS ITS OWN MODULE. The bridge generates its tools from a live instance
and serves a cache or the committed snapshot when none is up. Until 2026-09-15 an
offline copy carried a `captured_at` stamp and a count, and nothing measured them
against anything: a 70-id 0.1.0-alpha copy sat in a state directory while the tree
behind it carried 144 ids, and the only way to learn that was to compare figures
by hand. `registry.surface_fingerprint` records what surface a copy describes and
`registry.surface_drift` measures one copy against a live list, naming the ids
that differ. This module is the pair's own test — it needs no socket and no DAW,
because a comparison between two recorded surfaces is arithmetic, and the
end-to-end half (a flag that agrees with the running binary) is
tests/control-mcp-group-coverage.py.

BOTH DIRECTIONS, on purpose. A flag that fires on everything is as useless as one
that never fires: `stale` must be True when a live id is absent from the copy, True
when the copy carries an id no live instance registers, and False when the two
surfaces are the same. The last case is what keeps the check from becoming a
permanent red light after the snapshot is regenerated.

No instance, no binary: this module runs anywhere the bridge package does.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
sys.path.insert(0, str(BRIDGE_DIR))

import zene_control.config as C  # noqa: E402
import zene_control.registry as R  # noqa: E402
import zene_control.tools as T  # noqa: E402

#: Three ids over three groups, one of which no offline copy in this tree carries
#: (`wasm.*` — compiled out of every build here, see test_declared_surface.py).
LIVE = [{"id": "mixer.set_volume", "group": "mixer"},
        {"id": "browser.query", "group": "browser"},
        {"id": "wasm.list", "group": "wasm"}]
OFFLINE = [{"id": "mixer.set_volume", "group": "mixer"},
           {"id": "browser.query", "group": "browser"}]


def config_for(snapshot: dict | None) -> C.Config:
    """A bridge with one planted offline copy and an empty state directory."""
    state = tempfile.mkdtemp(prefix="zene-stale-state-", dir="/tmp")
    snapshot_path = os.path.join(state, "planted-snapshot.json")
    if snapshot is not None:
        with open(snapshot_path, "w", encoding="utf-8") as handle:
            json.dump(snapshot, handle)
    return C.Config(socket_path="/tmp/none/zene.sock", workdir=state, state_dir=state,
                    snapshot_path=snapshot_path)


def planted(commands: list[dict]) -> dict:
    """A bundle as the bridge writes it, with the surface its ids imply."""
    return R.bundle_from_result({"commands": commands, "proto": 1}, {"version": "planted"})


class TestSurfaceFingerprint(unittest.TestCase):
    def test_the_three_numbers_describe_the_list(self):
        surface = R.surface_fingerprint(LIVE)
        self.assertEqual(surface["id_count"], 3)
        self.assertEqual(surface["group_count"], 3)
        self.assertEqual(len(surface["ids_sha256"]), 64)

    def test_the_hash_moves_when_any_id_moves(self):
        base = R.surface_fingerprint(LIVE)
        renamed = [dict(entry) for entry in LIVE]
        renamed[2]["id"] = "wasm.process"
        extra = LIVE + [{"id": "wasm.process"}]
        self.assertNotEqual(R.surface_fingerprint(renamed)["ids_sha256"], base["ids_sha256"])
        self.assertNotEqual(R.surface_fingerprint(extra)["ids_sha256"], base["ids_sha256"])

    def test_the_hash_ignores_order_duplicates_and_blank_ids(self):
        shuffled = [LIVE[2], LIVE[0], LIVE[1], LIVE[0], {"id": ""}, {}]
        self.assertEqual(R.surface_fingerprint(shuffled), R.surface_fingerprint(LIVE))

    def test_a_bundle_records_the_surface_it_carries(self):
        bundle = R.bundle_from_result({"commands": LIVE, "proto": 1}, {"version": "v"})
        self.assertEqual(bundle["surface"], R.surface_fingerprint(LIVE))
        self.assertEqual(bundle["count"], 3)


class TestStalenessFlag(unittest.TestCase):
    """`surface_drift` fires when the surfaces differ, and only then."""

    def test_a_missing_live_id_is_named(self):
        drift = R.surface_drift(LIVE, OFFLINE)
        self.assertTrue(drift["stale"])
        self.assertEqual(drift["missing_ids"], ["wasm.list"])
        self.assertEqual((drift["live_count"], drift["offline_count"]), (3, 2))
        self.assertEqual((drift["missing_count"], drift["extra_count"]), (1, 0))
        self.assertIn("different surface", drift["verdict"])

    def test_an_id_the_instance_does_not_register_is_named(self):
        drift = R.surface_drift(OFFLINE, LIVE)
        self.assertTrue(drift["stale"])
        self.assertEqual(drift["extra_ids"], ["wasm.list"])
        self.assertEqual(drift["missing_count"], 0)

    def test_the_same_surface_is_not_stale(self):
        drift = R.surface_drift(LIVE, LIVE)
        self.assertFalse(drift["stale"])
        self.assertEqual((drift["missing_count"], drift["extra_count"]), (0, 0))
        self.assertIn("same 3 id(s)", drift["verdict"])

    def test_a_long_gap_is_counted_in_full_and_listed_to_the_limit(self):
        many = [{"id": "x.cmd%d" % index} for index in range(25)]
        drift = R.surface_drift(many, [], limit=20)
        self.assertEqual(len(drift["missing_ids"]), 20)
        self.assertEqual(drift["missing_count"], 25)
        self.assertEqual(drift["missing_truncated"], 5)

    def test_the_bridge_reports_a_planted_offline_copy_as_stale(self):
        bridge = T.Bridge(config_for(planted(OFFLINE)))
        drift = bridge.offline_drift(planted(LIVE))
        self.assertTrue(drift["checked"])
        self.assertTrue(drift["stale"])
        self.assertEqual(drift["stale_copies"], ["snapshot"])
        self.assertEqual(drift["copies"][0]["missing_ids"], ["wasm.list"])
        self.assertEqual(drift["live_surface"], R.surface_fingerprint(LIVE))
        self.assertIn("snapshot_commands.py", drift["fix"])

    def test_a_matching_offline_copy_is_not_reported_stale(self):
        bridge = T.Bridge(config_for(planted(LIVE)))
        drift = bridge.offline_drift(planted(LIVE))
        self.assertFalse(drift["stale"])
        self.assertEqual(drift["stale_copies"], [])
        self.assertFalse(drift["copies"][0]["stale"])

    def test_no_readable_offline_copy_is_reported_as_unchecked_not_as_clean(self):
        bridge = T.Bridge(config_for(None))
        drift = bridge.offline_drift(planted(LIVE))
        self.assertTrue(drift["checked"])
        self.assertEqual(drift["copies"], [])
        self.assertFalse(drift["stale"])

    def test_the_served_payload_carries_its_surface(self):
        bridge = T.Bridge(config_for(planted(OFFLINE)))
        payload = bridge.commands_payload({})
        self.assertEqual(payload["source"], "snapshot")
        self.assertEqual(payload["surface"], R.surface_fingerprint(OFFLINE))
        self.assertEqual(payload["commands"][0]["id"], "mixer.set_volume")


if __name__ == "__main__":
    unittest.main(verbosity=2)
