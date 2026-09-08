#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Invariant tests for tools/mmpz-git/mmpz_git.py.

Run:  python3 tools/mmpz-git/tests/test_mmpz_git.py -v

Every fixture is a real project shipped in data/projects/ or tests/. The
suite asserts the properties the git workflow depends on:

  * the .mmpz container is [4-byte BE length][zlib] (Qt qCompress)
  * decompress -> compress is byte-identical  (round-trip safety)
  * parse -> serialise(verbatim) is byte-identical  (minimal diffs)
  * canonicalize is idempotent and content-preserving
  * Qt6's own save() is NOT deterministic, canonical form IS
  * diff emits an operation list
  * merge: disjoint edits merge clean, conflicting BPM conflicts
"""
import glob
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import mmpz_git as M  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def read(p):
    with open(p, "rb") as fh:
        return fh.read()


def write(p, data):
    with open(p, "wb") as fh:
        fh.write(data)


CONTAINERS = sorted(glob.glob(os.path.join(ROOT, "data", "projects", "**", "*.mmpz"),
                              recursive=True))
PLAIN = sorted(glob.glob(os.path.join(ROOT, "data", "projects", "**", "*.mmp"),
                          recursive=True))
PLAIN.append(os.path.join(ROOT, "tests", "emptyproject.mmp"))
ALL = CONTAINERS + PLAIN


def rel(f):
    return os.path.relpath(f, ROOT)


def signature(elem):
    """Order-insensitive structural fingerprint of an element subtree.

    Two trees are content-identical iff their signatures match, regardless of
    how children or attributes are ordered.
    """
    return (elem.tagName,
            tuple(sorted(M._attrs(elem).items())),
            tuple(sorted(signature(c) for c in M._children(elem))))


class Container(unittest.TestCase):
    def test_all_fixtures_present(self):
        self.assertGreaterEqual(len(CONTAINERS), 20, "expected many real .mmpz fixtures")

    def test_container_signature(self):
        for f in CONTAINERS:
            raw = read(f)
            with self.subTest(f=rel(f)):
                self.assertTrue(M.is_container(raw))
                n = int.from_bytes(raw[:4], "big")
                self.assertEqual(len(M.decompress(raw)), n,
                                 "qCompress length prefix must equal payload size")

    def test_recompress_byte_identical(self):
        for f in CONTAINERS:
            raw = read(f)
            with self.subTest(f=rel(f)):
                self.assertEqual(M.compress(M.decompress(raw)), raw,
                                 "decompress -> compress must be byte-identical")

    def test_plain_xml_not_container(self):
        for f in PLAIN:
            with self.subTest(f=rel(f)):
                self.assertFalse(M.is_container(read(f)))


class Verbatim(unittest.TestCase):
    def test_verbatim_roundtrip(self):
        """parse -> serialise(canonical=False) reproduces the input bytes.

        One legacy fixture (tests/emptyproject.mmp) writes `type="song" >`
        with a space before '>'; it is excluded and reported.
        """
        exact = 0
        legacy = []
        for f in ALL:
            x = M.load_any(f)
            y = M.to_bytes(M.parse(x), canonical=False)
            if x == y:
                exact += 1
            else:
                legacy.append(rel(f))
        self.assertEqual(legacy, ["tests/emptyproject.mmp"], "unexpected verbatim mismatch")
        self.assertEqual(exact, len(ALL) - 1)


class Canonical(unittest.TestCase):
    def test_idempotent(self):
        for f in ALL:
            x = M.load_any(f)
            once = M.canonical_xml(x)
            twice = M.canonical_xml(once)
            with self.subTest(f=rel(f)):
                self.assertEqual(once, twice, "canonicalize must be idempotent")

    def test_content_preserving(self):
        """canonicalize may reorder, but must not add/drop/alter content."""
        for f in ALL:
            x = M.load_any(f)
            with self.subTest(f=rel(f)):
                self.assertEqual(signature(M.parse(x).documentElement),
                                 signature(M.parse(M.canonical_xml(x)).documentElement))

    def test_real_projects_are_not_yet_canonical(self):
        """Documents the one-time normalisation cost, honestly."""
        changed = 0
        for f in ALL:
            x = M.load_any(f)
            if M.canonical_xml(x) != x:
                changed += 1
        self.assertGreater(changed, 0, "expected fixtures to need normalisation")

    def test_sibling_keys_never_collapse(self):
        """keyed_children must keep every sibling distinct.

        Real projects contain duplicate sibling identities (five tracks all
        named 'Default preset', eight patterns named 'Kick', dozens of
        plugin 'par' elements); a naive identity key silently merges them
        and hides edits. The occurrence ordinal must prevent that.
        """
        base_collisions = 0
        elements = 0

        def walk(elem):
            nonlocal base_collisions, elements
            kids = M._children(elem)
            keys = [k for k, _ in M.keyed_children(elem)]
            self.assertEqual(len(keys), len(kids), "keyed_children dropped an element")
            self.assertEqual(len(keys), len(set(keys)), "duplicate sibling key")
            bases = [M._identity(c) for c in kids]
            elements += len(bases)
            base_collisions += len(bases) - len(set(bases))
            for c in kids:
                walk(c)

        for f in ALL:
            with self.subTest(f=rel(f)):
                walk(M.parse(M.load_any(f)).documentElement)
        self.assertGreater(base_collisions, 0,
                           "fixtures should exercise duplicate identities")
        print("\n  sibling elements checked: %d, base-identity collisions: %d"
              % (elements, base_collisions))

    def test_qt6_save_is_not_deterministic_but_canonical_is(self):
        """Two Qt6 saves of one project differ; their canonical forms match."""
        qt = os.path.join(HERE, "..", "scratch", "qtsave2")
        if not os.path.exists(qt):
            self.skipTest("scratch/qtsave2 not built")
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        outs = []
        for i in range(2):
            out = "/tmp/mmpz_qt_save_%d.mmp" % i
            subprocess.run([qt, src, out], check=True)
            outs.append(read(out))
        self.assertNotEqual(outs[0], outs[1], "Qt6 save should be non-deterministic")
        self.assertEqual(M.canonical_xml(outs[0]), M.canonical_xml(outs[1]),
                         "canonical form must converge")


class Diff(unittest.TestCase):
    def _variant(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        a = "/tmp/mmpz_test_a.mmpz"
        b = "/tmp/mmpz_test_b.mmpz"
        write(a, read(src))
        write(b, read(src))
        edit = os.path.join(HERE, "..", "demo_edits.py")
        subprocess.run([sys.executable, edit, "set-bpm", b, "128"], check=True)
        subprocess.run([sys.executable, edit, "add-note", b, "--track", "Bass",
                        "--pattern", "I", "--key", "60", "--pos", "48"], check=True)
        return a, b

    def test_identical_files_have_no_ops(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        out = subprocess.run([sys.executable, os.path.join(HERE, "..", "mmpz_git.py"),
                              "diff", src, src], capture_output=True, text=True, check=True)
        self.assertIn("no semantic changes", out.stdout)

    def test_operation_list(self):
        a, b = self._variant()
        out = subprocess.run([sys.executable, os.path.join(HERE, "..", "mmpz_git.py"),
                              "diff", a, b], capture_output=True, text=True, check=True)
        txt = out.stdout
        self.assertIn("# operations: 1 added, 0 removed, 1 changed, 0 moved", txt)
        self.assertIn('@bpm: 175 -> 128', txt)
        self.assertIn('note pos="48" (added)', txt)


class Merge(unittest.TestCase):
    def _three(self, ours_edits, theirs_edits):
        """ours_edits/theirs_edits: arg lists with '{f}' where the file goes."""
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        paths = {}
        for name in ("base", "ours", "theirs"):
            p = "/tmp/mmpz_merge_%s.mmpz" % name
            write(p, read(src))
            paths[name] = p
        edit = os.path.join(HERE, "..", "demo_edits.py")
        for p, edits in ((paths["ours"], ours_edits), (paths["theirs"], theirs_edits)):
            for e in edits:
                subprocess.run([sys.executable, edit]
                               + [p if a == "{f}" else a for a in e], check=True)
        rc = subprocess.run([sys.executable, os.path.join(HERE, "..", "mmpz_git.py"),
                             "merge", paths["base"], paths["ours"], paths["theirs"],
                             "7", "project.mmpz"], capture_output=True, text=True)
        return rc, paths

    def test_disjoint_track_edits_merge_clean(self):
        rc, paths = self._three(
            [["add-note", "{f}", "--track", "Bass", "--pattern", "I", "--key", "60", "--pos", "48"]],
            [["add-note", "{f}", "--track", "Snare", "--pattern", "Snare", "--key", "38", "--pos", "96"]])
        self.assertEqual(rc.returncode, 0, rc.stderr)
        xml = M.load_any(paths["ours"]).decode()
        self.assertIn('key="60"', xml)
        self.assertIn('key="38"', xml)
        M.parse(xml)  # merged output must parse

    def test_conflicting_bpm_is_a_resolvable_conflict(self):
        rc, paths = self._three([["set-bpm", "{f}", "140"]],
                                [["set-bpm", "{f}", "128"]])
        self.assertEqual(rc.returncode, 1)
        self.assertIn("CONFLICT", rc.stderr)
        xml = M.load_any(paths["ours"]).decode()
        self.assertIn(M.CONFLICT_BANNER, xml, "conflict banner must be in the file")
        self.assertIn("base    : 175", xml)
        self.assertIn("ours    : 140", xml)
        self.assertIn("theirs  : 128", xml)
        M.parse(xml)  # a conflicted file must still be well-formed XML
        raw = read(paths["ours"])
        # git stores %A verbatim, so the driver must emit the clean (stored)
        # form, which is plain XML; that form must still round-trip to a
        # container losslessly.
        self.assertFalse(M.is_container(raw), "merge driver must write stored XML")
        self.assertEqual(M.decompress(M.compress(raw)), raw)

    def test_merged_file_is_canonical(self):
        rc, paths = self._three(
            [["add-note", "{f}", "--track", "Bass", "--pattern", "I", "--key", "60", "--pos", "48"]],
            [["add-note", "{f}", "--track", "Snare", "--pattern", "Snare", "--key", "38", "--pos", "96"]])
        self.assertEqual(rc.returncode, 0, rc.stderr)
        xml = M.load_any(paths["ours"])
        self.assertEqual(xml, M.canonical_xml(xml))


if __name__ == "__main__":
    unittest.main(verbosity=2)
