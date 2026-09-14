#!/usr/bin/env python3
"""The shared plumbing of the `mastering.*` socket proof.

Kept out of tests/control-mastering-commands.py for the same reason
tests/agent_surface_lib.py is kept out of tests/agent-surface-gate.py and
tests/control_socket_flows.py out of tests/control_socket_harness.py: this fork's
gates measure a FILE, and the transcript plus its plumbing do not fit inside one
500-line file without either the evidence prose or the checks being shortened
(Gate 7; Gate 4's CCN limit is the other reason the pieces are small and named).

Nothing here decides anything: it is the socket session, the recorder, the fixture
loader and the wire readers. The checks that make a claim about the product live in
the transcript, which is what a reader should have to read.
"""

from __future__ import annotations

import hashlib
import importlib.util
import os

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

#: The wave-1 candidate set's size, and the file that carries the single render.
CANDIDATES = 5
SOURCE_RENDER = "00_source-mix.wav"

#: The EBU R 128 target's own published numbers (docs/AUTO-MASTERING.md section 4).
EBU_LUFS, EBU_TOLERANCE, EBU_CEILING = -23.0, 0.5, -1.0

#: The capture bound mastering.run refuses to record an inverse beyond (64 MiB).
CAPTURE_LIMIT = 64 * 1024 * 1024

#: The keys a candidate may carry: settings only, so no ranking can hide in one.
CANDIDATE_KEYS = ("chain_settings", "dynamics", "name", "target")

#: The targets the engine's candidate set is graded against, as (integrated LUFS-I,
#: tolerance LU, ceiling dBTP). Held as DATA so a check is one comparison rather than
#: a chain of conditions - which is what keeps the transcript inside Gate 4's CCN.
EXPECTED_TARGETS = {
    "streaming-14": (-14.0, 1.0, -1.0),
    "streaming-16": (-16.0, 1.0, -1.0),
    "streaming-14-ceiling-2": (-14.0, 1.0, -2.0),
    "streaming-16-dynamics": (-16.0, 1.0, -1.0),
    "ebu-r128": (EBU_LUFS, EBU_TOLERANCE, EBU_CEILING),
}


def target_tuple(row):
    """A candidate row's target as the comparable tuple above."""
    target = row.get("target") or {}
    return (target.get("integrated_lufs"), target.get("tolerance_lu"),
            target.get("ceiling_dbtp"))


def standard_less_names(rows):
    """The candidates whose target names no source document."""
    names = []
    for row in rows:
        if not (row.get("target") or {}).get("standard"):
            names.append(row.get("name"))
    return names


def dynamics_names(rows):
    """The candidates whose dynamics stage is on."""
    names = []
    for row in rows:
        if (row.get("dynamics") or {}).get("enabled"):
            names.append(row.get("name"))
    return names


def candidate_key_sets(rows):
    """One tuple of keys per candidate, so the payload's SHAPE is comparable."""
    return {tuple(sorted(row.keys())) for row in rows}


def ranked_keys(payload):
    """Any key of a payload that could carry an ordering or a preference."""
    return [key for key in payload.keys() if "rank" in key or "best" in key]


def wav_names(directory):
    """The .wav entries of a directory, by name, or () when it does not exist."""
    if not os.path.isdir(directory):
        return ()
    return tuple(sorted(name for name in os.listdir(directory)
                        if name.lower().endswith(".wav")))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def shipped_fixture(directory):
    """Builds the session with the product's own generator; its .mmp path, or None.

    The generator is a script (its name is not importable as a module), so it is
    loaded by path. It is the tool docs/AUTO-MASTERING.md's reproduction section
    names, which makes this fixture the documented one rather than a second fixture
    invented for a test.
    """
    path = os.path.join(REPO_ROOT, "tools", "auto-mastering-demo.py")
    if not os.path.exists(path):
        return None
    spec = importlib.util.spec_from_file_location("auto_mastering_demo", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.make_fixture(directory)
    project = os.path.join(directory, "demo.mmp")
    return project if os.path.exists(project) else None


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None, timeout=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript,
                                timeout=timeout)

    def result(self, command, args=None, timeout=None):
        """The reply's result, or {'error': ...} so a failed call is visible."""
        reply = self.call(command, args, timeout=timeout)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}

    def command_entry(self, command):
        """One command's entry out of control.commands_list."""
        for entry in self.result("control.commands_list").get("commands") or []:
            if entry.get("id") == command:
                return entry
        return {}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]
