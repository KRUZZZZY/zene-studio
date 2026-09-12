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
import base64
import glob
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import mmpz_git as M  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TOOL = os.path.join(HERE, "..", "mmpz_git.py")


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


class MergeDepth(unittest.TestCase):
    """The deep (whole-subtree) modification test that replaced the shallow one.

    The shallow test compared attributes and direct-child count, so an edit
    nested two levels down (a note inside a pattern inside a track) was
    invisible; a delete or rename on one side then discarded the other side's
    notes while reporting a clean merge.
    """

    def _variant(self, tag, edit_fn):
        """A copy of the fixture with `edit_fn(doc)` applied, container-preserving."""
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        dst = "/tmp/mmpz_depth_%s.mmpz" % tag
        doc = M.parse(M.load_any(src))
        edit_fn(doc)
        write(dst, M.compress(M.to_bytes(doc, canonical=False)))
        return dst

    @staticmethod
    def _track(doc, name):
        for t in doc.documentElement.getElementsByTagName("track"):
            if t.getAttribute("name") == name:
                return t
        raise AssertionError("no track %r" % name)

    @classmethod
    def _add_note(cls, doc, track_name, pat_name, key, pos):
        t = cls._track(doc, track_name)
        for p in t.getElementsByTagName("pattern"):
            if p.getAttribute("name") == pat_name:
                n = doc.createElement("note")
                for k, v in (("key", key), ("vol", "100"), ("pos", pos),
                             ("pan", "0"), ("len", "24")):
                    n.setAttribute(k, v)
                p.appendChild(n)
                return
        raise AssertionError("no pattern %r" % pat_name)

    @classmethod
    def _delete_track(cls, doc, name):
        t = cls._track(doc, name)
        t.parentNode.removeChild(t)

    @classmethod
    def _rename_track(cls, doc, name, new):
        cls._track(doc, name).setAttribute("name", new)

    def _merge(self, ours_fn, theirs_fn):
        base = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        ours = self._variant("ours", ours_fn)
        theirs = self._variant("theirs", theirs_fn)
        rc = subprocess.run([sys.executable, TOOL, "merge", base, ours, theirs,
                             "7", "project.mmpz"], capture_output=True, text=True)
        return rc, ours, theirs

    def _note_present(self, path, key, pos):
        doc = M.parse(M.load_any(path))
        return any(n.getAttribute("key") == key and n.getAttribute("pos") == pos
                   for n in doc.documentElement.getElementsByTagName("note"))

    def test_delete_of_a_track_vs_a_nested_edit_is_a_conflict(self):
        """Ours deletes track Kick; theirs adds a note inside it.  The old code
        reported a clean merge and silently dropped theirs' note."""
        rc, ours, _ = self._merge(lambda d: self._delete_track(d, "Kick"),
                                  lambda d: self._add_note(d, "Kick", "Kick", "36", "96"))
        self.assertEqual(rc.returncode, 1, "must not be reported as a clean merge")
        self.assertIn("CONFLICT", rc.stderr)
        self.assertIn('track "Kick"', rc.stderr, "the conflict must name the track")
        self.assertTrue(self._note_present(ours, "36", "96"),
                        "theirs' added note must survive the merge")

    def test_rename_of_a_track_vs_a_nested_edit_is_a_conflict(self):
        rc, ours, _ = self._merge(lambda d: self._rename_track(d, "Kick", "Kick X"),
                                  lambda d: self._add_note(d, "Kick", "Kick", "36", "96"))
        self.assertEqual(rc.returncode, 1)
        self.assertTrue(self._note_present(ours, "36", "96"),
                        "theirs' added note must survive a rename on our side")

    def test_clean_delete_of_an_untouched_subtree_still_merges(self):
        """Regression guard: the deeper test must not turn every delete into a
        conflict.  Nobody touched track Kick, so ours' delete is clean."""
        rc, ours, _ = self._merge(lambda d: self._delete_track(d, "Kick"),
                                  lambda d: self._add_note(d, "Bass", "I", "60", "48"))
        self.assertEqual(rc.returncode, 0, rc.stderr)
        doc = M.parse(M.load_any(ours))
        names = [t.getAttribute("name")
                 for t in doc.documentElement.getElementsByTagName("track")]
        self.assertNotIn("Kick", names, "the clean delete must be applied")
        self.assertTrue(self._note_present(ours, "60", "48"),
                        "the other side's edit must survive")
        # arithmetic check: 368 base - 24 (the deleted track) + 1 (the edit,
        # which is only present because the merge kept both sides) = 345
        self.assertEqual(len(doc.documentElement.getElementsByTagName("note")), 345)

    def test_deep_change_is_invisible_to_a_shallow_test(self):
        """Why the fix was needed: the note is two levels below the track, so
        attributes and direct-child count of the track do not change."""
        doc = M.parse(M.load_any(os.path.join(
            ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")))
        before = self._track(doc, "Kick")
        shallow_before = (M._attrs(before), len(M._children(before)))
        deep_before = M.fingerprint(before)
        self._add_note(doc, "Kick", "Kick", "36", "96")
        after = self._track(doc, "Kick")
        shallow_after = (M._attrs(after), len(M._children(after)))
        deep_after = M.fingerprint(after)
        self.assertEqual(shallow_before, shallow_after,
                         "the shallow signal really is blind to this edit")
        self.assertNotEqual(deep_before, deep_after,
                            "the deep fingerprint must see it")

    def test_audit_catches_a_dropped_addition(self):
        base = M.parse(b'<r><a x="1"/></r>')
        ours = M.parse(b'<r><a x="1"><n k="60"/></a></r>')   # ours added a note
        theirs = M.parse(b'<r><a x="1"/></r>')
        merged_dropped = M.parse(b'<r><a x="1"/></r>')       # ...and it vanished
        merged_kept = M.parse(b'<r><a x="1"><n k="60"/></a></r>')
        args = lambda d: M.symbolic_index(d.documentElement)
        lost = M.audit_no_lost_edits(args(base), args(ours), args(theirs),
                                     args(merged_dropped))
        self.assertEqual([r[0] for r in lost], ["ours"], lost)
        self.assertEqual(M.audit_no_lost_edits(args(base), args(ours), args(theirs),
                                              args(merged_kept)), [])

    def test_audit_is_clean_for_a_real_disjoint_merge(self):
        rc, ours, theirs = self._merge(
            lambda d: self._add_note(d, "Bass", "I", "60", "48"),
            lambda d: self._add_note(d, "Kick", "Kick", "36", "96"))
        self.assertEqual(rc.returncode, 0, rc.stderr)
        base = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        idx = lambda p: M.symbolic_index(M.parse(M.load_any(p)).documentElement)
        lost = M.audit_no_lost_edits(idx(base), idx(ours), idx(theirs), idx(ours))
        self.assertEqual(lost, [], "a clean disjoint merge must lose nothing")



    def test_no_conflict_marker_lands_where_the_loader_hangs(self):
        """A comment as a direct child of a <trackcontainer> makes the DAW's
        loader hang (measured: `render` never returns, vs ~10 s normally).  A
        conflict at the container's children must be marked somewhere else."""
        rc, ours, _ = self._merge(lambda d: self._delete_track(d, "Kick"),
                                  lambda d: self._add_note(d, "Kick", "Kick", "36", "96"))
        self.assertEqual(rc.returncode, 1)
        doc = M.parse(M.load_any(ours))
        self.assertEqual(M.unsafe_comment_hosts(doc), [],
                         "marker landed in a <trackcontainer>")
        self.assertGreaterEqual(len(M.marker_records(doc)), 1)

    def test_every_marker_host_is_safe_and_the_file_still_parses(self):
        """Both a note-level and a track-level conflict, one file each."""
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        for tag, ours_fn, theirs_fn in (
                ("note", lambda d: self._set_note_vol(d, "Bass", "I", "0", "30", "40"),
                 lambda d: self._set_note_vol(d, "Bass", "I", "0", "30", "90")),
                ("track", lambda d: self._delete_track(d, "Kick"),
                 lambda d: self._add_note(d, "Kick", "Kick", "36", "96"))):
            base = src
            ours = self._variant("safe_%s_ours" % tag, ours_fn)
            theirs = self._variant("safe_%s_theirs" % tag, theirs_fn)
            rc = subprocess.run([sys.executable, TOOL, "merge", base, ours, theirs,
                                 "7", "project.mmpz"], capture_output=True, text=True)
            self.assertEqual(rc.returncode, 1, rc.stderr)
            doc = M.parse(M.load_any(ours))
            self.assertEqual(M.unsafe_comment_hosts(doc), [], tag)
            self.assertGreaterEqual(len(M.marker_records(doc)), 1, tag)

    @classmethod
    def _set_note_vol(cls, doc, track_name, pat_name, pos, key, vol):
        t = cls._track(doc, track_name)
        for p in t.getElementsByTagName("pattern"):
            if p.getAttribute("name") == pat_name:
                for n in p.getElementsByTagName("note"):
                    if n.getAttribute("pos") == pos and n.getAttribute("key") == key:
                        n.setAttribute("vol", vol)
                        return
        raise AssertionError("note %s/%s not found" % (pos, key))


class BinarySafety(unittest.TestCase):
    """The DAW must be able to load every file the tool writes."""

    # Wave R renamed the built binary from lmms to zene; accept either so a
    # pre-rename build tree still runs these tests instead of skipping them.
    BIN = os.path.join(ROOT, "build", "zene")
    BIN_PRE_RENAME = os.path.join(ROOT, "build", "lmms")

    def setUp(self):
        if not (os.path.isfile(self.BIN) and os.access(self.BIN, os.X_OK)):
            if os.path.isfile(self.BIN_PRE_RENAME) and os.access(self.BIN_PRE_RENAME, os.X_OK):
                type(self).BIN = self.BIN_PRE_RENAME
            else:
                self.skipTest("build/zene (or build/lmms) not built (see docs/MMPZ-GIT-DEPTH.md)")

    def _loads(self, path, what):
        """Load the project the way the DAW does and prove it produced audio.

        `render` goes through DataFile::loadData (src/core/DataFile.cpp:2128),
        which parses the XML first and only falls back to qUncompress -- the
        path a real project-open takes.  The `dump` action is NOT the right
        probe here: it calls qUncompress directly with no XML fallback
        (src/core/main.cpp:461), so on the uncompressed form the merge driver
        writes it prints "qUncompress: Input data is corrupted" and exits 0
        with no output, which would look like a pass.
        """
        out = path + ".loadprobe.wav"
        if os.path.exists(out):
            os.unlink(out)
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        r = subprocess.run(["timeout", "180", self.BIN, "render", path,
                            "-o", out, "-f", "wav"],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0,
                         "%s: the DAW could not load %s (exit %d, 124 = hang)\n%s"
                         % (what, path, r.returncode, r.stderr[-800:]))
        self.assertTrue(os.path.exists(out) and os.path.getsize(out) > 1024,
                        "%s: %s loaded but produced no audio" % (what, path))
        return out

    def test_a_clean_merge_output_loads(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        out = "/tmp/mmpz_binsafe_clean.mmpz"
        edit = os.path.join(HERE, "..", "demo_edits.py")
        write(out, read(src))
        subprocess.run([sys.executable, edit, "add-note", out, "--track", "Bass",
                        "--pattern", "I", "--key", "60", "--pos", "48"], check=True)
        self._loads(out, "clean merge output")

    def test_a_conflicted_merge_output_loads(self):
        """The worst outcome available here is handing the DAW a project it
        cannot open, so the marker placement is validated by loading it."""
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        base, ours, theirs = "/tmp/mmpz_bs_b.mmpz", "/tmp/mmpz_bs_o.mmpz", "/tmp/mmpz_bs_t.mmpz"
        for p in (base, ours, theirs):
            write(p, read(src))
        edit = os.path.join(HERE, "..", "demo_edits.py")
        # ours deletes a track, theirs edits inside it -> conflict at the
        # container's child level, which is the placement that used to hang
        subprocess.run([sys.executable, edit, "remove-track", ours, "--track", "Kick"],
                       check=True)
        subprocess.run([sys.executable, edit, "add-note", theirs, "--track", "Kick",
                        "--pattern", "Kick", "--key", "36", "--pos", "96"], check=True)
        rc = subprocess.run([sys.executable, TOOL, "merge", base, ours, theirs,
                             "7", "project.mmpz"], capture_output=True, text=True)
        self.assertEqual(rc.returncode, 1, rc.stderr)
        self._loads(ours, "conflicted merge output")


class MusicalPresentation(unittest.TestCase):
    """Conflicts must be presented as music, not as XML noise."""

    def _bpm_conflict(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        paths = {}
        for name in ("base", "ours", "theirs"):
            p = "/tmp/mmpz_pres_%s.mmpz" % name
            write(p, read(src))
            paths[name] = p
        edit = os.path.join(HERE, "..", "demo_edits.py")
        for p, v in ((paths["ours"], "140"), (paths["theirs"], "96")):
            subprocess.run([sys.executable, edit, "set-bpm", p, v], check=True)
        rc = subprocess.run([sys.executable, TOOL, "merge", paths["base"],
                             paths["ours"], paths["theirs"], "8", "project.mmpz"],
                            capture_output=True, text=True)
        return rc, paths

    def test_note_conflict_is_reported_in_musical_terms(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        paths = {}
        for name in ("base", "ours", "theirs"):
            p = "/tmp/mmpz_mus_%s.mmpz" % name
            write(p, read(src))
            paths[name] = p
        edit = os.path.join(HERE, "..", "demo_edits.py")
        for p, v in ((paths["ours"], "40"), (paths["theirs"], "90")):
            subprocess.run([sys.executable, edit, "set-note-vol", p, "--track",
                            "Bass", "--pattern", "I", "--pos", "0", "--key", "30",
                            "--vol", v], check=True)
        rc = subprocess.run([sys.executable, TOOL, "merge", paths["base"],
                             paths["ours"], paths["theirs"], "8", "project.mmpz"],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 1)
        err = rc.stderr
        self.assertIn('track "Bass"', err)
        self.assertIn('pattern "I"', err)
        self.assertIn("note F#1", err)
        self.assertIn("bar 1 beat 1 in the pattern", err)
        self.assertIn("note velocity", err)
        self.assertIn("base 100 -> ours 40, theirs 90", err)
        # the primary output must not be an XML dump
        self.assertNotIn("======= mmpz-git CONFLICT", err)

    def test_bpm_conflict_is_named_as_tempo(self):
        rc, _ = self._bpm_conflict()
        self.assertEqual(rc.returncode, 1)
        self.assertIn("tempo (BPM)", rc.stderr)

    def test_conflicts_subcommand_reprints_and_exits_nonzero(self):
        rc, paths = self._bpm_conflict()
        self.assertEqual(rc.returncode, 1)
        again = subprocess.run([sys.executable, TOOL, "conflicts", paths["ours"]],
                               capture_output=True, text=True)
        self.assertEqual(again.returncode, 1)
        self.assertIn("tempo (BPM)", again.stdout)
        self.assertIn("base 175 -> ours 140, theirs 96", again.stdout)
        json_out = "/tmp/mmpz_conflicts.json"
        if os.path.exists(json_out):
            os.unlink(json_out)
        subprocess.run([sys.executable, TOOL, "conflicts", paths["ours"],
                        "--json", json_out], capture_output=True, text=True)
        self.assertTrue(os.path.exists(json_out))
        import json as _json
        data = _json.load(open(json_out))
        self.assertEqual(data["conflicts"][0]["field"], "bpm")

    def test_conflict_comment_embeds_machine_data(self):
        rc, paths = self._bpm_conflict()
        xml = M.load_any(paths["ours"]).decode()
        self.assertIn(M.CONFLICT_BANNER, xml)
        self.assertIn(M.CONFLICT_DATA_TAG, xml)
        doc = M.parse(xml)
        recs = M.marker_records(doc)
        self.assertEqual(len(recs), 1)
        self.assertEqual(recs[0]["field"], "bpm")
        self.assertEqual(recs[0]["base"], "175")
        self.assertEqual(recs[0]["ours"], "140")
        self.assertEqual(recs[0]["theirs"], "96")

    def test_clean_project_reports_no_conflicts(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        rc = subprocess.run([sys.executable, TOOL, "conflicts", src],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0)
        self.assertIn("no conflicts", rc.stdout)


class LargeAssets(unittest.TestCase):
    """Embedded samples live in the project XML as base64 attributes.

    LMMS writes them via AudioFileProcessor (`sampledata`,
    plugins/AudioFileProcessor/AudioFileProcessor.cpp:199) and SampleClip
    (`data`, src/core/SampleClip.cpp:299).  The merge copies the bytes
    verbatim; what it must not do is spray them into a comment.
    """

    SAMPLE = base64.b64encode(bytes(range(256)) * 32).decode()   # 8192 bytes

    def _with_sample(self, tag, blob, extra=None):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        dst = "/tmp/mmpz_asset_%s.mmpz" % tag
        doc = M.parse(M.load_any(src))
        for t in doc.documentElement.getElementsByTagName("track"):
            if t.getAttribute("name") != "Kick":
                continue
            afp = t.getElementsByTagName("audiofileprocessor")
            self.assertTrue(afp, "fixture should carry an audiofileprocessor")
            afp[0].setAttribute("sampledata", blob)
        if extra:
            extra(doc)
        write(dst, M.compress(M.to_bytes(doc, canonical=False)))
        return dst

    def test_embedded_sample_survives_a_clean_merge(self):
        base = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        ours = self._with_sample("ours", self.SAMPLE)
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        theirs_doc = M.parse(M.load_any(src))
        p = None
        for t in theirs_doc.documentElement.getElementsByTagName("track"):
            if t.getAttribute("name") == "Bass":
                p = [x for x in t.getElementsByTagName("pattern")
                     if x.getAttribute("name") == "I"][0]
        n = theirs_doc.createElement("note")
        for k, v in (("key", "60"), ("vol", "100"), ("pos", "48"), ("pan", "0"),
                     ("len", "24")):
            n.setAttribute(k, v)
        p.appendChild(n)
        theirs = "/tmp/mmpz_asset_theirs.mmpz"
        write(theirs, M.compress(M.to_bytes(theirs_doc, canonical=False)))

        rc = subprocess.run([sys.executable, TOOL, "merge", base, ours, theirs,
                             "9", "project.mmpz"], capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr)
        xml = M.load_any(ours)
        self.assertIn(self.SAMPLE.encode(), xml,
                      "the embedded sample must survive byte-for-byte")
        # and the other side's edit is there too
        self.assertIn(b'pos="48"', xml)

    def test_large_asset_conflict_is_summarised_not_inlined(self):
        base = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        ours = self._with_sample("ours", self.SAMPLE)
        theirs = self._with_sample("theirs", self.SAMPLE[:-16] + "A" * 16)
        sidecar = ours + ".mmpz-git-conflicts.json"
        if os.path.exists(sidecar):
            os.unlink(sidecar)
        rc = subprocess.run([sys.executable, TOOL, "merge", base, ours, theirs,
                             "9", "project.mmpz"], capture_output=True, text=True)
        self.assertEqual(rc.returncode, 1)
        self.assertIn("embedded sample data", rc.stderr)
        self.assertIn("chars, sha256=", rc.stderr)
        xml = M.load_any(ours)
        doc = M.parse(xml)
        recs = M.marker_records(doc)
        self.assertEqual(len(recs), 1)
        # the comment must be small: the 10924-char base64 blob is NOT in it
        self.assertEqual(xml.count(self.SAMPLE.encode()), 1,
                         "the sample should appear once (in the attribute), not in a comment")
        self.assertNotIn(self.SAMPLE.encode(), rc.stderr.encode())
        # ...but the full value is recoverable from the sidecar
        self.assertTrue(os.path.exists(sidecar),
                        "large values must be preserved in the sidecar")
        import json as _json
        data = _json.load(open(sidecar))
        self.assertEqual(data["conflicts"][0]["theirs"],
                         self.SAMPLE[:-16] + "A" * 16)

    def test_diff_summarises_a_large_value(self):
        a = self._with_sample("diffa", self.SAMPLE)
        b = self._with_sample("diffb", self.SAMPLE[:-16] + "A" * 16)
        rc = subprocess.run([sys.executable, TOOL, "diff", a, b],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr)
        lines = [l for l in rc.stdout.splitlines() if "@sampledata" in l]
        self.assertTrue(lines, rc.stdout)
        self.assertLess(max(len(l) for l in lines), 400,
                        "a large attribute must be summarised, not printed")


class PureAudioMaths(unittest.TestCase):
    """The audible-diff measurement itself, without needing a built binary.

    One test in here shells out to the CLI (the input-validation check); it skips
    when no renderer exists, because only then does it reach the code path it asserts.
    """

    def _wav(self, path, samples, rate=44100):
        import wave as _wave
        with _wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(b"".join(
                int(max(-1.0, min(1.0, v)) * 32767).to_bytes(2, "little", signed=True)
                for v in samples))

    def test_note_name_and_meter_follow_the_tree(self):
        self.assertEqual(M.note_name(60), "C4")     # PianoRoll.cpp:134, Note.h:79
        self.assertEqual(M.note_name(36), "C2")
        self.assertEqual(M.note_name(30), "F#1")
        doc = M.parse(M.load_any(os.path.join(
            ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")))
        self.assertEqual(M.project_meter(doc), (192, 4))
        self.assertEqual(M.bar_beat(192, 192, 4), (2, 1))
        self.assertEqual(M.bar_beat(48, 192, 4), (1, 2))

    def test_abbrev_value(self):
        self.assertEqual(M.abbrev_value("175"), "175")
        self.assertEqual(M.abbrev_value(None), "<absent>")
        big = "x" * 5000
        out = M.abbrev_value(big)
        self.assertIn("5000 chars", out)
        self.assertIn("sha256=", out)
        self.assertLess(len(out), 200)

    def test_bar_windows_and_curves(self):
        rate, bpm = 44100, 120          # 1 bar = 2 s = 88200 samples
        wins, per = M.bar_windows(88200 * 3, rate, bpm, 4)
        self.assertEqual(per, 88200)
        self.assertEqual(len(wins), 3)
        # a tone only in bar 2
        tone = [0.0] * (88200 * 3)
        for i in range(88200, 88200 * 2):
            tone[i] = 0.5
        curve = M.bar_db_curve(tone, rate, bpm, 4)
        self.assertEqual(len(curve), 3)
        self.assertLess(curve[0], -100)         # silence
        self.assertGreater(curve[1], -12)       # ~0.5 amplitude
        self.assertLess(curve[2], -100)

    def test_peak_delta_localises_a_change(self):
        rate, bpm = 44100, 120
        a = [0.0] * (88200 * 3)
        b = list(a)
        for i in range(88200 * 2, 88200 * 2 + 100):   # a change inside bar 3 only
            b[i] = 0.25
        wins, _ = M.bar_windows(len(a), rate, bpm, 4)
        pk = M.bar_peak_delta_db(a, b, wins)
        self.assertLess(pk[0], -190)
        self.assertLess(pk[1], -190)
        self.assertGreater(pk[2], -20)      # -12 dBFS-ish
        # identical signals produce no difference at all
        self.assertTrue(all(v <= -190 for v in M.bar_peak_delta_db(a, a, wins)))
        diffs = M.compare_bars(M.bar_rms_db(a, wins), M.bar_rms_db(b, wins),
                               pk, pk, threshold_db=1.0, peak_threshold_db=-60.0)
        self.assertEqual([d[0] for d in diffs], [3])

    def test_silent_on_both_sides_is_not_a_difference(self):
        rate, bpm = 44100, 120
        a = [0.0] * 88200
        wins, _ = M.bar_windows(len(a), rate, bpm, 4)
        zero = M.bar_rms_db(a, wins)
        self.assertEqual(M.compare_bars(zero, zero, [-200.0], [-200.0]), [])

    def test_wav_mono_roundtrip(self):
        path = "/tmp/mmpz_pure.wav"
        self._wav(path, [0.5] * 1000 + [-0.5] * 1000)
        mono, sr, ch, n = M.wav_mono(path)
        self.assertEqual((sr, ch, n), (44100, 1, 2000))
        self.assertAlmostEqual(mono[0], 0.5, places=3)
        self.assertAlmostEqual(mono[-1], -0.5, places=3)

    def test_missing_renderer_is_an_error_not_a_crash(self):
        """Two files that do not exist must be rejected by name, exit 2.

        audible-diff refuses in two different places. With no renderer anywhere it
        exits 2 early, printing "no renderer found" - before it ever looks at the
        paths this test passes. That first refusal is the built-artifact dependency
        the rest of this suite skips on, so this test skips on the same condition
        rather than failing on a message that describes a different refusal.

        The condition is the tool's own find_renderer(), not a hardcoded path, so
        the skip disappears exactly when audible-diff would get past it: a local
        build (build/zene, build/lmms), $MMPZ_GIT_RENDERER, --renderer, or a
        zene/lmms on $PATH.
        """
        if M.find_renderer() is None:
            self.skipTest("no renderer found (build/zene or build/lmms, $MMPZ_GIT_RENDERER, "
                          "--renderer, or zene/lmms on $PATH): audible-diff exits before its "
                          "input check, so the assertion below cannot run "
                          "(see docs/MMPZ-GIT-DEPTH.md)")
        rc = subprocess.run([sys.executable, TOOL, "audible-diff",
                             "/nonexistent-a.mmpz", "/nonexistent-b.mmpz"],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 2)
        self.assertIn("no such file", rc.stderr)


class AudibleDiffBinary(unittest.TestCase):
    """End-to-end audible diff, skipped unless the binary was built here."""

    # Wave R renamed the built binary from lmms to zene; accept either so a
    # pre-rename build tree still runs these tests instead of skipping them.
    BIN = os.path.join(ROOT, "build", "zene")
    BIN_PRE_RENAME = os.path.join(ROOT, "build", "lmms")

    def setUp(self):
        if not (os.path.isfile(self.BIN) and os.access(self.BIN, os.X_OK)):
            if os.path.isfile(self.BIN_PRE_RENAME) and os.access(self.BIN_PRE_RENAME, os.X_OK):
                type(self).BIN = self.BIN_PRE_RENAME
            else:
                self.skipTest("build/zene (or build/lmms) not built (see docs/MMPZ-GIT-DEPTH.md)")

    def test_identical_project_renders_identically(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        rc = subprocess.run([sys.executable, TOOL, "audible-diff", src, src,
                             "--mix-only", "--keep", "/tmp/mmpz_ad_id"],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr[-2000:])
        self.assertIn("render identically", rc.stdout)

    def test_a_real_edit_is_localised_to_track_and_bar(self):
        src = os.path.join(ROOT, "data", "projects", "shorties", "sv-DnB-Startup.mmpz")
        variant = "/tmp/mmpz_ad_track.mmpz"
        write(variant, read(src))
        edit = os.path.join(HERE, "..", "demo_edits.py")
        subprocess.run([sys.executable, edit, "set-note-vol", variant, "--track",
                        "Bass", "--pattern", "I", "--pos", "0", "--key", "30",
                        "--vol", "20"], check=True)
        rc = subprocess.run([sys.executable, TOOL, "audible-diff", src, variant,
                             "--track", "Bass", "--keep", "/tmp/mmpz_ad_tr"],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 1, rc.stderr[-2000:])
        self.assertIn('track "Bass"', rc.stdout)
        self.assertIn("bars 1", rc.stdout)       # the edit is in bar 1
        self.assertIn("dB RMS", rc.stdout)
        self.assertNotIn('track "Kick"', rc.stdout)   # only the edited track

    def test_render_recipe_script_runs(self):
        recipe = os.path.join(HERE, "..", "render-recipe.sh")
        if not os.path.exists(recipe):
            self.skipTest("render-recipe.sh not present")
        out = "/tmp/mmpz_recipe.wav"
        if os.path.exists(out):
            os.unlink(out)
        rc = subprocess.run(["bash", recipe,
                             os.path.join(ROOT, "data", "projects", "shorties",
                                          "sv-DnB-Startup.mmpz"),
                             "-o", out, "--build-dir", os.path.join(ROOT, "build")],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr[-2000:])
        self.assertTrue(os.path.getsize(out) > 1000)
        self.assertIn("sha256", rc.stdout)


class Cli(unittest.TestCase):
    def test_help_works(self):
        """--help used to crash on argparse's '%O' expansion (pre-existing)."""
        rc = subprocess.run([sys.executable, TOOL, "--help"],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr)
        for cmd in ("merge", "conflicts", "audible-diff"):
            self.assertIn(cmd, rc.stdout)



if __name__ == "__main__":
    unittest.main(verbosity=2)
