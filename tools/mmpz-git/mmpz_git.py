#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# mmpz-git — git-friendly workflow for LMMS project files (.mmp / .mmpz / .xpt / .xptz)
#
# Stdlib only. Implements:
#   * qCompress/qUncompress-compatible (de)compression of .mmpz containers
#   * git clean/smudge filter (dump/compress) so git stores readable XML
#   * canonical form + `canonicalize` (stable diffs across Qt5/Qt6 producers)
#   * XML-aware semantic diff -> operation list
#   * LMMS-aware 3-way merge driver that emits human-resolvable conflicts:
#       - a deep (whole-subtree) modification test, so a delete or rename on
#         one side can never discard an edit nested below it
#       - a musical conflict report (track / pattern / bar / note, tempo),
#         with each conflict marked in the file by a machine-readable comment
#       - an independent post-merge audit that refuses to report success if
#         any edit either side made is missing from the result
#   * `conflicts`  — re-present a conflicted project's markers as music
#   * `audible-diff` — render both versions and report which bars of which
#                      track differ, with a real measurement
#
# Container format (verified against src/core/DataFile.cpp:410 `qCompress(xml.toUtf8())`):
#   [4-byte big-endian uncompressed length][zlib stream, default level]
#
# Copyright (c) 2026 LMMS contributors
"""mmpz-git: git-friendly LMMS project files."""

from __future__ import annotations

import argparse
import array
import base64
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import zlib
from collections import Counter
from xml.dom import Node, minidom

PROG = "mmpz-git"
XML_HEADER = b'<?xml version="1.0"?>\n'
CONFLICT_BANNER = "mmpz-git CONFLICT"
# Marker that carries the machine-readable conflict record inside the XML
# comment.  The payload is base64 because XML comments may not contain "--".
CONFLICT_DATA_TAG = "mmpz-git-data(base64):"

# ---------------------------------------------------------------------------
# container format
# ---------------------------------------------------------------------------


def is_container(data: bytes) -> bool:
    """True if `data` looks like a qCompress() container (.mmpz)."""
    if len(data) < 6:
        return False
    n = struct.unpack(">I", data[:4])[0]
    if n == 0 or n > (1 << 31):
        return False
    try:
        zlib.decompress(data[4:])
    except zlib.error:
        return False
    return True


def decompress(data: bytes) -> bytes:
    n = struct.unpack(">I", data[:4])[0]
    xml = zlib.decompress(data[4:])
    if len(xml) != n:
        raise ValueError(
            "declared length %d != decompressed length %d" % (n, len(xml))
        )
    return xml


def compress(xml: bytes) -> bytes:
    """Byte-identical to Qt's qCompress(xml) for the same zlib (verified)."""
    return struct.pack(">I", len(xml)) + zlib.compress(xml, 6)


def load_any(path: str) -> bytes:
    """Return the XML bytes of a project file, whatever the container."""
    with open(path, "rb") as fh:
        data = fh.read()
    return decompress(data) if is_container(data) else data


def dump_bytes(path: str) -> bytes:
    return load_any(path)


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------------------
# XML serialisation (matches QDomDocument::save(ts, 2) shape)
# ---------------------------------------------------------------------------


def _esc_text(t: str) -> str:
    # Qt (QDomDocument::save): text escapes &, < and CR only. '>' and '"'
    # are left literal (verified empirically against Qt 6.4.2).
    return (t.replace("&", "&amp;").replace("<", "&lt;")
             .replace("\r", "&#xd;"))


def _esc_attr(t: str) -> str:
    # Qt (QDomDocument::save): attribute values escape &, <, ", CR, LF, TAB.
    return (t.replace("&", "&amp;").replace("<", "&lt;")
             .replace('"', "&quot;").replace("\r", "&#xd;")
             .replace("\n", "&#xa;").replace("\t", "&#x9;"))


def _is_ws_only(data: str) -> bool:
    return data.strip() == ""


def _sorted_attrs(elem) -> list:
    attrs = elem.attributes
    out = [(attrs.item(i).name, attrs.item(i).value) for i in range(attrs.length)]
    out.sort(key=lambda kv: kv[0])
    return out


def serialize(node, depth: int, out: list, canonical: bool = True) -> None:
    """Serialise a DOM to a list of strings.

    canonical=True  -> attributes sorted, whitespace-only text dropped,
                       synthetic 2-space indentation
    canonical=False -> verbatim: document order, text nodes as-is, no
                       synthetic indentation (reproduces the input bytes)
    """
    t = node.nodeType
    if t == Node.DOCUMENT_NODE:
        out.append(XML_HEADER.decode())
        for c in node.childNodes:
            serialize(c, depth, out, canonical)
        # Qt's save() ends the document with a newline
        if out and not out[-1].endswith("\n"):
            out.append("\n")
    elif t == Node.DOCUMENT_TYPE_NODE:
        out.append("<!DOCTYPE %s>\n" % node.name)
    elif t == Node.PROCESSING_INSTRUCTION_NODE:
        out.append("<?%s %s?>\n" % (node.target, node.data))
    elif t == Node.COMMENT_NODE:
        out.append("<!--%s-->" % node.data)
    elif t == Node.ELEMENT_NODE:
        out.append("<" + node.tagName)
        if canonical:
            for name, value in _sorted_attrs(node):
                out.append(' %s="%s"' % (name, _esc_attr(value)))
        else:
            for i in range(node.attributes.length):
                a = node.attributes.item(i)
                out.append(' %s="%s"' % (a.name, _esc_attr(a.value)))
        kids = list(node.childNodes)
        if canonical:
            kids = [
                k
                for k in kids
                if not (k.nodeType == Node.TEXT_NODE and _is_ws_only(k.data))
            ]
        if not kids:
            out.append("/>")
            return
        out.append(">")
        for c in kids:
            if c.nodeType == Node.TEXT_NODE:
                out.append(_esc_text(c.data))
            elif c.nodeType == Node.CDATA_SECTION_NODE:
                out.append("<![CDATA[" + c.data + "]]>")
            elif c.nodeType == Node.COMMENT_NODE:
                out.append("<!--" + c.data + "-->")
            elif c.nodeType == Node.ELEMENT_NODE:
                if canonical:
                    out.append("\n" + "  " * (depth + 1))
                serialize(c, depth + 1, out, canonical)
            elif c.nodeType == Node.PROCESSING_INSTRUCTION_NODE:
                out.append("<?%s %s?>" % (c.target, c.data))
            else:
                serialize(c, depth, out, canonical)
        if canonical and kids[-1].nodeType == Node.ELEMENT_NODE:
            out.append("\n" + "  " * depth)
        out.append("</" + node.tagName + ">")
    elif t == Node.TEXT_NODE:
        out.append(_esc_text(node.data))
    elif t == Node.CDATA_SECTION_NODE:
        out.append("<![CDATA[" + node.data + "]]>")


def parse(xml):
    if isinstance(xml, str):
        xml = xml.encode("utf-8")
    return minidom.parseString(xml)


def to_bytes(doc, canonical: bool = True) -> bytes:
    out: list = []
    serialize(doc, 0, out, canonical)
    return "".join(out).encode("utf-8")


# ---------------------------------------------------------------------------
# canonical form
# ---------------------------------------------------------------------------

NOTE_ORDER = ("pos", "key", "vol", "pan", "len")
TIME_ORDER = ("pos",)


def _sort_key(elem, names):
    vals = []
    for n in names:
        v = elem.getAttribute(n)
        try:
            vals.append((0, int(v)))
        except ValueError:
            vals.append((1, v))
    return tuple(vals)


def canonicalize_dom(doc) -> int:
    """Enforce the canonical ordering rules in-place.

    Returns the number of element blocks that actually had to be reordered
    (whitespace stripping is part of normal serialisation, not a reorder).
    """
    changed = 0

    def walk(elem):
        nonlocal changed
        # 1. drop whitespace-only text nodes (re-added by the pretty printer)
        for child in list(elem.childNodes):
            if child.nodeType == Node.TEXT_NODE and _is_ws_only(child.data):
                elem.removeChild(child)
        # 2. canonical order for unordered collections
        for child in elem.childNodes:
            if child.nodeType != Node.ELEMENT_NODE:
                continue
            if child.tagName == "note":
                order = NOTE_ORDER
            elif child.tagName == "time":
                order = TIME_ORDER
            else:
                order = None
            if order:
                kids = [
                    c
                    for c in elem.childNodes
                    if c.nodeType == Node.ELEMENT_NODE and c.tagName == child.tagName
                ]
                if len(kids) > 1:
                    srt = sorted(kids, key=lambda e: _sort_key(e, order))
                    if srt != kids:
                        changed += 1
                        for k in kids:
                            elem.removeChild(k)
                        # re-insert as a block at the position of the first note
                        ref = None
                        for c in elem.childNodes:
                            if c.nodeType == Node.ELEMENT_NODE and c.tagName == child.tagName:
                                ref = c
                                break
                        for k in srt:
                            if ref is not None:
                                elem.insertBefore(k, ref)
                            else:
                                elem.appendChild(k)
            walk(child)

    root = doc.documentElement
    walk(root)
    return changed


def canonical_xml(xml: bytes) -> bytes:
    doc = parse(xml)
    canonicalize_dom(doc)
    return to_bytes(doc, canonical=True)


# ---------------------------------------------------------------------------
# semantic diff -> operation list
# ---------------------------------------------------------------------------


def _identity(elem):
    """Base identity of an element among its siblings (no ordinal)."""
    tag = elem.tagName
    if tag == "note":
        return ("note", elem.getAttribute("pos"), elem.getAttribute("key"))
    if tag == "time":
        return ("time", elem.getAttribute("pos"))
    if tag == "track":
        return ("track", elem.getAttribute("type"), elem.getAttribute("name"))
    if tag == "pattern":
        # several patterns in one track may share a name; pos disambiguates
        return ("pattern", elem.getAttribute("type"), elem.getAttribute("name"),
                elem.getAttribute("pos"))
    if tag == "fxchannel":
        return ("fxchannel", elem.getAttribute("num"))
    if tag == "effect":
        return ("effect", elem.getAttribute("name"))
    if tag == "automationpattern":
        return ("automationpattern", elem.getAttribute("name"))
    if tag == "instrument":
        return ("instrument", elem.getAttribute("name"))
    return (tag,)


def keyed_children(elem):
    """[(sibling-unique key, element)] for element children of `elem`.

    The key is the base identity plus the occurrence index of that identity
    among the siblings, so duplicate names/positions never collapse two
    distinct elements into one.
    """
    counts = {}
    out = []
    for e in _children(elem):
        base = _identity(e)
        n = counts.get(base, 0)
        counts[base] = n + 1
        out.append(((base, n), e))
    return out


def _children(elem):
    return [c for c in elem.childNodes if c.nodeType == Node.ELEMENT_NODE]


def _label(elem) -> str:
    tag = elem.tagName
    for a in ("name", "type", "pos", "num", "key"):
        if elem.hasAttribute(a):
            return '%s %s="%s"' % (tag, a, elem.getAttribute(a))
    return tag


def _path(parent: str, elem) -> str:
    return parent + "/" + _label(elem)


def diff_dom(a, b, path, ops, moves):
    """Recursive semantic diff. Appends human-readable ops."""
    if a is None:
        ops.append("  + %s (added)" % _path(path, b))
        return
    if b is None:
        ops.append("  - %s (removed)" % _path(path, a))
        return

    # attributes
    an = {a.attributes.item(i).name: a.attributes.item(i).value
          for i in range(a.attributes.length)}
    bn = {b.attributes.item(i).name: b.attributes.item(i).value
          for i in range(b.attributes.length)}
    for k in sorted(set(an) | set(bn)):
        if k not in an:
            ops.append("  + %s @%s = %s" % (path, k, abbrev_value(bn[k])))
        elif k not in bn:
            ops.append("  - %s @%s (was %s)" % (path, k, abbrev_value(an[k])))
        elif an[k] != bn[k]:
            ops.append("  ~ %s @%s: %s -> %s"
                       % (path, k, abbrev_value(an[k]), abbrev_value(bn[k])))

    # children, keyed
    am, bm = keyed_children(a), keyed_children(b)
    akeys = [k for k, _ in am]
    bkeys = [k for k, _ in bm]
    adict, bdict = dict(am), dict(bm)

    # order changes (report only when the set of keys is unchanged)
    if set(akeys) == set(bkeys) and akeys != bkeys:
        moves.append("  > %s: child order changed\n      old: %s\n      new: %s"
                     % (path, ", ".join(_label(e) for _, e in am),
                        ", ".join(_label(e) for _, e in bm)))

    for k in akeys:
        if k not in bdict:
            ops.append("  - %s (removed)" % _path(path, adict[k]))
        else:
            diff_dom(adict[k], bdict[k], _path(path, adict[k]), ops, moves)
    for k in bkeys:
        if k not in adict:
            ops.append("  + %s (added)" % _path(path, bdict[k]))

    # note move heuristic: pair removed/added notes with identical shape
    if all(k[0][0] == "note" for k in akeys + bkeys if k):
        gone = [adict[k] for k in akeys if k not in bdict]
        new = [bdict[k] for k in bkeys if k not in adict]
        used = set()
        for g in gone:
            for i, n in enumerate(new):
                if i in used:
                    continue
                if (g.getAttribute("key") == n.getAttribute("key")
                        and g.getAttribute("vol") == n.getAttribute("vol")
                        and g.getAttribute("pan") == n.getAttribute("pan")
                        and g.getAttribute("len") == n.getAttribute("len")
                        and g.getAttribute("pos") != n.getAttribute("pos")):
                    moves.append(
                        "  > %s: note key=%s moved pos %s -> %s"
                        % (path, g.getAttribute("key"),
                           g.getAttribute("pos"), n.getAttribute("pos")))
                    used.add(i)
                    break


def cmd_diff(args) -> int:
    xa, xb = load_any(args.a), load_any(args.b)
    da, db = parse(xa), parse(xb)
    ops: list = []
    moves: list = []
    diff_dom(da.documentElement, db.documentElement,
             "/" + _label(da.documentElement), ops, moves)
    lines = ["# mmpz-git diff: %s -> %s" % (args.a, args.b)]
    adds = sum(1 for o in ops if o.startswith("  +"))
    rems = sum(1 for o in ops if o.startswith("  -"))
    chgs = sum(1 for o in ops if o.startswith("  ~"))
    if ops:
        lines.append("# operations: %d added, %d removed, %d changed, %d moved"
                     % (adds, rems, chgs, len(moves)))
        lines.extend(ops)
    if moves:
        lines.append("# position/order changes:")
        lines.extend(moves)
    if not ops and not moves:
        lines.append("# no semantic changes")
    text = "\n".join(lines) + "\n"
    if args.output:
        with open(args.output, "w") as fh:
            fh.write(text)
    sys.stdout.write(text)
    return 0


# ---------------------------------------------------------------------------
# musical vocabulary (grounded in this tree's own constants)
# ---------------------------------------------------------------------------

# include/TimePos.h:38  `const int DefaultTicksPerBar = 192;`
TICKS_PER_BAR = 192
# include/Note.h:79-80  FirstOctave = -1, KeysPerOctave = 12, which is what
# PianoRoll::getNoteString() (src/gui/editors/PianoRoll.cpp:134) uses, so
# key 60 prints as "C4" exactly as the DAW's piano roll does.
FIRST_OCTAVE = -1
KEYS_PER_OCTAVE = 12
# src/gui/editors/PianoRoll.cpp:129 s_noteStrings, with "#"/"b" in place of the
# sharp/flat glyphs (U+266F / U+266D) so the report survives an ASCII locale.
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

# Long attribute values (embedded samples, VST/CLAP state chunks) must never be
# sprayed into a human-facing report or a conflict comment: one embedded sample
# is base64 and can dwarf the rest of the project.
MAX_INLINE_VALUE = 256


def note_name(key) -> str:
    try:
        k = int(key)
    except (TypeError, ValueError):
        return str(key)
    return "%s%d" % (NOTE_NAMES[k % 12], FIRST_OCTAVE + k // KEYS_PER_OCTAVE)


def project_meter(doc):
    """(ticks_per_bar, beats_per_bar) from the project's <head timesig_*>."""
    beats = 4
    den = 4
    heads = doc.documentElement.getElementsByTagName("head")
    if heads:
        try:
            beats = int(heads[0].getAttribute("timesig_numerator") or 4)
        except ValueError:
            beats = 4
        try:
            den = int(heads[0].getAttribute("timesig_denominator") or 4)
        except ValueError:
            den = 4
    if beats <= 0:
        beats = 4
    if den <= 0:
        den = 4
    # include/TimePos.h:128  ticksPerBar(sig) = DefaultTicksPerBar * num / den
    return TICKS_PER_BAR * beats // den, beats


def bar_beat(pos, ticks_per_bar, beats_per_bar):
    """1-based (bar, beat) for a tick position, or None if it is not a number."""
    try:
        p = int(pos)
    except (TypeError, ValueError):
        return None
    if ticks_per_bar <= 0 or beats_per_bar <= 0:
        return None
    tick_per_beat = max(1, ticks_per_bar // beats_per_bar)
    return p // ticks_per_bar + 1, (p % ticks_per_bar) // tick_per_beat + 1


def abbrev_value(value, limit: int = MAX_INLINE_VALUE) -> str:
    """Human-facing rendering of an attribute value.

    Small values are shown verbatim (the existing behaviour).  A large value
    (embedded sample, plugin state chunk) is replaced by its length and a
    content hash, so a report stays readable and a conflict comment cannot
    double the size of the project file.  The full value is preserved
    separately: see `--json` / the sidecar written by `cmd_merge`.
    """
    if value is None:
        return "<absent>"
    if len(value) <= limit:
        return value
    return "<%d chars, sha256=%s, prefix=%r>" % (
        len(value), sha256(value.encode("utf-8", "surrogatepass"))[:16], value[:32])


# ---------------------------------------------------------------------------
# deep structure fingerprints (order-insensitive)
# ---------------------------------------------------------------------------


def sig_index(elem) -> dict:
    """{id(element): fingerprint} for every element under `elem`, one pass.

    The fingerprint is order-insensitive (attributes and children sorted), so
    two subtrees compare equal exactly when they hold the same content,
    however the producer happened to order it.  Building the whole map in a
    single bottom-up pass keeps this O(n log n) instead of the O(n^2) a
    recompute-per-node recursive signature would cost on a 3 MB project.
    """
    order = []
    stack = [elem]
    while stack:
        e = stack.pop()
        order.append(e)
        stack.extend(_children(e))
    out = {}
    for e in reversed(order):
        out[id(e)] = (e.tagName,
                      tuple(sorted(_attrs(e).items())),
                      tuple(sorted(out[id(c)] for c in _children(e))))
    return out


def fingerprint(elem) -> tuple:
    """Deep fingerprint of one element (convenience for tests/callers)."""
    return sig_index(elem)[id(elem)]


def tag_counts(elem) -> Counter:
    """Element tag census of a subtree — used to describe a deep change."""
    c = Counter()
    stack = [elem]
    while stack:
        e = stack.pop()
        c[e.tagName] += 1
        stack.extend(_children(e))
    return c


def describe_change(new, old) -> str:
    """Short "+N note, -1 pattern" summary of a subtree difference."""
    nc, oc = tag_counts(new), tag_counts(old)
    bits = []
    for tag in sorted(set(nc) | set(oc)):
        d = nc[tag] - oc[tag]
        if d:
            bits.append("%+d %s" % (d, tag))
    return ", ".join(bits) if bits else "attributes only"


# ---------------------------------------------------------------------------
# conflict records and their musical presentation
# ---------------------------------------------------------------------------

# field name -> producer-facing wording.  Anything not listed is reported by
# its raw name, so a new element type is never silently mislabelled.
FIELD_WORDS = {
    "vol": "note velocity",
    "pos": "position (ticks)",
    "key": "pitch",
    "len": "note length",
    "pan": "panning",
    "bpm": "tempo (BPM)",
    "mastervol": "master volume",
    "masterpitch": "master pitch",
    "muted": "mute",
    "solo": "solo",
    "name": "name",
    "type": "type",
    "sampledata": "embedded sample data",
    "data": "embedded sample data",
    "chunk": "plugin state chunk",
    "state": "plugin state",
}

KIND_WORDS = {
    "attribute": "both sides changed the same field",
    "delete/modify": "theirs removed this, ours edited it",
    "modify/delete": "ours removed this, theirs edited it",
    "add/add": "both sides added a different version",
    "order": "the children were reordered differently",
    "lost-edit": "an edit from one side is missing from the merge result",
}


def unpack_loc(loc):
    """(track, pattern, pattern_pos, note, path) from a location tuple of any length."""
    vals = list(loc or ()) + [None] * 5
    return tuple(vals[:5])


def conflict_record(kind, loc, field, base, ours, theirs, detail="", path=""):
    """One conflict, as both a producer-facing sentence and machine data."""
    track, pat, pat_pos, note, lpath = unpack_loc(loc)
    rec = {
        "kind": kind,
        "location": {
            "track": track,
            "pattern": pat,
            "pattern_pos": pat_pos,
            "note": None,
        },
        "field": field,
        "base": base,
        "ours": ours,
        "theirs": theirs,
        "detail": detail,
        "path": path or lpath,
    }
    if note:
        pos, key = note
        # `<note pos>` is relative to its pattern, whose own `pos` places it on
        # the song timeline; report both so the bar number is not misleading.
        rec["location"]["note"] = {"key": key, "name": note_name(key), "pos": pos}
    return rec


def format_location(rec, meter):
    """One line naming where the conflict is, in musical terms.

    Note positions are pattern-relative and pattern positions are
    song-relative, so both are printed; conflating them would put every
    conflict in bars 1-2.
    """
    where = rec["location"]
    bits = []
    tpb, bpb = meter
    if where.get("track"):
        bits.append('track "%s"' % where["track"])
    if where.get("pattern"):
        pat = 'pattern "%s"' % where["pattern"]
        pp = where.get("pattern_pos")
        try:
            song_bar = int(pp) // tpb + 1
            pat += " (song bar %d)" % song_bar
        except (TypeError, ValueError):
            pass
        bits.append(pat)
    note = where.get("note")
    if note:
        bb = bar_beat(note["pos"], tpb, bpb)
        at = "bar %d beat %d in the pattern" % bb if bb else "pos %s" % note["pos"]
        bits.append("note %s (key=%s) at %s" % (note["name"], note["key"], at))
    if not bits:
        bits.append("project root")
    return " > ".join(bits)


def format_conflict(rec, meter, index):
    """The producer-facing paragraph for one conflict."""
    lines = ["  [%d] %s" % (index, format_location(rec, meter))]
    words = FIELD_WORDS.get(rec["field"], rec["field"] or "content")
    if rec["kind"] == "attribute":
        lines.append("      %s: base %s -> ours %s, theirs %s"
                     % (words, abbrev_value(rec["base"]),
                        abbrev_value(rec["ours"]), abbrev_value(rec["theirs"])))
    else:
        lines.append("      %s%s" % (
            KIND_WORDS.get(rec["kind"], rec["kind"]),
            " (%s)" % rec["detail"] if rec["detail"] else ""))
        if rec["base"] is not None or rec["ours"] is not None or rec["theirs"] is not None:
            lines.append("      base %s / ours %s / theirs %s"
                         % (abbrev_value(rec["base"]), abbrev_value(rec["ours"]),
                            abbrev_value(rec["theirs"])))
    lines.append("      location: %s" % (rec.get("path") or "/"))
    return "\n".join(lines)


def render_report(recs, meter, project):
    """The primary conflict output: musical, actionable, not an XML dump."""
    out = ["mmpz-git: %d conflict(s) to resolve in %s" % (len(recs), project), ""]
    for i, rec in enumerate(recs, 1):
        out.append(format_conflict(rec, meter, i))
    out.append("")
    out.append("Resolve by keeping the correct value in the project file")
    out.append("(ours is already in place) and deleting the marked CONFLICT")
    out.append("comment; `mmpz-git conflicts <file>` re-prints this report.")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# merge driver
# ---------------------------------------------------------------------------


class Conflict(Exception):
    pass


def _attrs(elem):
    return {elem.attributes.item(i).name: elem.attributes.item(i).value
            for i in range(elem.attributes.length)}


def _set_attrs(elem, values):
    for k, v in values.items():
        elem.setAttribute(k, v)


def comment_host(elem):
    """The element that may safely hold a conflict comment.

    A comment that is a *direct child of a <trackcontainer>* makes the DAW's
    loader hang: `render` on such a project never returns (measured; the
    container's children are walked in src/core/TrackContainer.cpp:108-144 and
    the project editor builds its tracks from them).  Every other placement is
    harmless -- the root, <head>, <song>, <bbtrack>, <track>, <pattern>,
    <note>, <fxchannel> were all rendered successfully -- so a comment aimed at
    a container is moved up to the container's parent instead.
    """
    node = elem
    while node is not None and getattr(node, "nodeType", None) == Node.ELEMENT_NODE:
        if node.tagName != "trackcontainer":
            return node
        node = node.parentNode
    return elem


def host_of(elem, parent_host):
    """Where a comment about `elem` may be attached.

    Normally `elem` itself.  A <trackcontainer> may not hold comment children
    (its children are walked by the loader, see `comment_host`), so a comment
    about one goes to the host of its parent, which the caller knows.
    """
    if elem is None:
        return parent_host
    return parent_host if elem.tagName == "trackcontainer" else elem


def unsafe_comment_hosts(doc):
    """Elements that hold a conflict comment somewhere the loader cannot take.

    Returns a list of (host tag, comment prefix); empty means the merged file
    is safe to hand to the DAW.
    """
    bad = []
    stack = [doc.documentElement]
    while stack:
        e = stack.pop()
        for c in e.childNodes:
            if c.nodeType == Node.ELEMENT_NODE:
                stack.append(c)
            elif c.nodeType == Node.COMMENT_NODE and CONFLICT_BANNER in c.data:
                if e.tagName == "trackcontainer":
                    bad.append((e.tagName, c.data.strip()[:60]))
    return bad


def _conflict_comment(doc, rec, meter, index, indent=0):
    """The in-file marker for one conflict.

    Producer-facing lines first (track / pattern / note / what changed), then a
    base64 JSON payload so `mmpz-git conflicts` can re-present the conflict
    without re-running the merge.  base64 is used because XML comments may not
    contain "--" and plugin state strings can contain anything.
    """
    where = rec["location"]
    note = where.get("note")
    note_line = "<none>"
    if note:
        bb = bar_beat(note["pos"], meter[0], meter[1])
        note_line = "%s (key=%s, pos=%s%s)" % (
            note["name"], note["key"], note["pos"],
            ", bar %d beat %d" % bb if bb else "")

    def lab(name):
        return name + " " * max(0, 8 - len(name)) + ": "

    what = (FIELD_WORDS.get(rec["field"], rec["field"] or "content")
            if rec["kind"] == "attribute"
            else KIND_WORDS.get(rec["kind"], rec["kind"]))
    # The embedded machine-readable record uses the same abbreviations as the
    # human lines: re-inlining a 10 kB base64 sample here would undo the point
    # of abbreviating it.  Full values live in the sidecar (--json).
    small = dict(rec)
    for k in ("base", "ours", "theirs"):
        v = rec.get(k)
        small[k] = None if v is None else abbrev_value(v)
    payload = base64.b64encode(
        json.dumps(small, ensure_ascii=True).encode("utf-8")).decode("ascii")
    banner = "======= %s #%d =======" % (CONFLICT_BANNER, index)
    bar = "=" * len(banner)
    pad = "  " * indent
    body = "\n".join([
        "",
        pad + banner,
        pad + lab("track") + (where.get("track") or "<none>"),
        pad + lab("pattern") + (where.get("pattern") or "<none>"),
        pad + lab("note") + note_line,
        pad + lab("change") + what
        + (" (%s)" % rec["detail"] if rec["detail"] else ""),
        pad + lab("base") + abbrev_value(rec["base"]),
        pad + lab("ours") + abbrev_value(rec["ours"]),
        pad + lab("theirs") + abbrev_value(rec["theirs"]),
        pad + lab("path") + (rec.get("path") or "/"),
        pad + lab("resolve") + "keep the value you want, then delete this comment.",
        pad + CONFLICT_DATA_TAG + " " + payload,
        pad + bar,
        "",
    ])
    # XML comments must not contain "--"; base64 cannot, so this is a no-op
    # on the payload and only protects the human-readable lines.
    return doc.createComment(body.replace("--", "=="))


def symbolic_index(root):
    """{key-path: fingerprint} for every element under `root`.

    A key path is the tuple of sibling keys the merge itself uses (element
    identity plus the occurrence ordinal), so two documents agree on a path
    exactly when the merge would pair those elements up.
    """
    sigs = sig_index(root)
    out = {}
    stack = [(root, ("/", root.tagName))]
    while stack:
        elem, path = stack.pop()
        out[path] = sigs[id(elem)]
        for k, child in keyed_children(elem):
            stack.append((child, path + (k,)))
    return out


def key_path_label(path):
    """Human label for the tail of a key path."""
    if not path or len(path) < 2:
        return "/"
    base = path[-1][0]
    return "%s %s" % (base[0], "/".join(str(x) for x in base[1:]))


def audit_no_lost_edits(base, ours, theirs, merged):
    """Prove that no edit from either side was dropped silently.

    Compares per element key path, not per fingerprint set: an element that
    merely *changed* (so its whole-subtree fingerprint is new) must not be
    mistaken for an added subtree, or every ancestor of every edit would look
    like a lost addition.

    For each side, an element that exists in the base but was changed by that
    side, or that the side added, must still be present in the merged
    document.  Dropping one of those is exactly the silent-loss failure mode,
    so it is reported rather than passed off as a clean merge.  Returns
    [(side, tag, label, digest)].
    """
    lost = []
    seen = set()
    for side, idx in (("ours", ours), ("theirs", theirs)):
        for path, fp in idx.items():
            if path in base and fp == base[path]:
                continue                      # this side left it alone
            if path in merged:
                continue                      # still present in the result
            if (side, path) in seen:
                continue
            seen.add((side, path))
            tag = path[-1][0][0] if path else "?"
            digest = hashlib.sha256(
                repr(fp).encode("utf-8", "replace")).hexdigest()[:12]
            lost.append((side, tag, key_path_label(path), digest))
    lost.sort(key=lambda r: r[2])
    return lost


def _loc_for(elem, parent_loc, path):
    """Musical location of `elem`, inheriting the enclosing track/pattern/note."""
    track, pat, pat_pos, note, _ = unpack_loc(parent_loc)
    if elem is not None:
        if elem.tagName == "track":
            return (elem.getAttribute("name"), None, None, None, path)
        if elem.tagName == "pattern":
            return (track, elem.getAttribute("name"), elem.getAttribute("pos"),
                    None, path)
        if elem.tagName == "note":
            return (track, pat, pat_pos,
                    (elem.getAttribute("pos"), elem.getAttribute("key")), path)
    return (track, pat, pat_pos, note, path)


def merge_elem(doc, o, a, b, path, conflicts, sigs=None, loc=(), meter=None,
               host=None):
    """3-way merge element `a` (ours) with `b` (theirs) against `o` (base).

    Mutates `a`.  Returns the merged element (may be `a`, `b` or None).

    `sigs` carries {side: sig_index} snapshots taken *before* any mutation, so
    "was this subtree modified?" is answered over the whole subtree instead of
    by attributes plus direct-child count.  The shallow test was the silent
    data-loss bug: an edit nested two levels down (a note inside a pattern
    inside a track) is invisible to it, so a delete or rename on one side
    discarded the other side's notes while reporting a clean merge.
    """
    if meter is None:
        meter = (TICKS_PER_BAR, 4)
    if sigs is None:
        sigs = {}
    # The caller passed the safe host for a comment about *this* element.
    # Children are detached before recursion (the parent rebuilds its child
    # list), so parentNode cannot be consulted here -- it is gone.
    if host is None:
        host = host_of(a, doc.documentElement)
    if host is None:
        host = doc.documentElement
    io, ia, ib = sigs.get("o", {}), sigs.get("a", {}), sigs.get("b", {})

    def record(kind, where, field, base, ours, theirs, detail="", where_loc=None):
        conflicts.append(conflict_record(kind, where_loc or loc, field, base,
                                         ours, theirs, detail=detail, path=where))
        return conflicts[-1]

    if o is None and a is None:
        return b
    if o is None and b is None:
        return a
    if o is None:
        # both sides have it, base does not: an add/add
        if to_bytes_sig(a) == to_bytes_sig(b):
            return a
        record("add/add", path, None, None, _snippet(a), _snippet(b),
               detail="both sides added different content")
        host.appendChild(_conflict_comment(doc, conflicts[-1], meter, len(conflicts),
                                        path.count("/")))
        return a
    if b is None:  # deleted by theirs
        if a is None:
            return None
        if ia.get(id(a)) == io.get(id(o)):
            return None  # clean delete: ours left it alone
        rec = record("delete/modify", path, None, None, None, None,
                     detail="theirs deleted, ours changed it (%s)" % describe_change(a, o))
        host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                        path.count("/")))
        return a
    if a is None:  # deleted by ours
        if ib.get(id(b)) == io.get(id(o)):
            return None
        rec = record("modify/delete", path, None, None, None, None,
                     detail="ours deleted, theirs changed it (%s)" % describe_change(b, o))
        # `b` is the surviving copy; mark it so the file names the conflict too
        host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                        path.count("/")))
        return b

    # --- attributes ---
    ao, aa, ab = _attrs(o), _attrs(a), _attrs(b)
    for k in sorted(set(ao) | set(aa) | set(ab)):
        ov, av, bv = ao.get(k), aa.get(k), ab.get(k)
        if av == bv:
            continue
        if av == ov:                      # theirs changed it
            if bv is None:
                if a.hasAttribute(k):
                    a.removeAttribute(k)
            else:
                a.setAttribute(k, bv)
        elif bv == ov:                    # ours changed it
            pass
        else:                             # both changed differently
            rec = record("attribute", path, k, ov, av, bv)
            host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                            path.count("/")))

    # --- children ---
    om, am, bm = keyed_children(o), keyed_children(a), keyed_children(b)
    odict, adict, bdict = dict(om), dict(am), dict(bm)
    order = [k for k, _ in om]
    for k in [k for k, _ in am] + [k for k, _ in bm]:
        if k not in order:
            order.append(k)

    # order conflict detection
    akeys = [k for k, _ in am]
    bkeys = [k for k, _ in bm]
    okeys = [k for k, _ in om]
    if akeys != bkeys and set(akeys) == set(bkeys) == set(okeys):
        rec = record("order", path, None,
                     ", ".join(str(k) for k in okeys),
                     ", ".join(str(k) for k in akeys),
                     ", ".join(str(k) for k in bkeys),
                     detail="children reordered differently")
        host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                        path.count("/")))
        order = okeys  # keep base order

    # rebuild child list in merged order
    for e in _children(a):
        a.removeChild(e)
    for k in order:
        oe, ae, be = odict.get(k), adict.get(k), bdict.get(k)
        elem = ae if ae is not None else be
        child_path = path + "/" + _label(elem)
        child_loc = _loc_for(elem, loc, child_path)
        child_host = host_of(elem, host)
        if ae is not None and be is None and oe is not None:
            # deleted by theirs
            if ia.get(id(ae)) != io.get(id(oe)):
                rec = record("delete/modify", child_path, None, None, None, None,
                             detail="theirs deleted, ours changed it (%s)"
                                    % describe_change(ae, oe),
                             where_loc=_loc_for(ae, loc, child_path))
                child_host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                                        child_path.count("/")))
                a.appendChild(ae)
            # else: clean delete -> drop
        elif ae is None and be is not None and oe is not None:
            if ib.get(id(be)) != io.get(id(oe)):
                rec = record("modify/delete", child_path, None, None, None, None,
                             detail="ours deleted, theirs changed it (%s)"
                                    % describe_change(be, oe),
                             where_loc=_loc_for(be, loc, child_path))
                child_host.appendChild(_conflict_comment(doc, rec, meter, len(conflicts),
                                                        child_path.count("/")))
                a.appendChild(be)
        elif ae is None and be is not None:
            a.appendChild(be)             # added by theirs only
        elif ae is not None and be is None and oe is None:
            a.appendChild(ae)             # added by ours only
        elif ae is not None and be is not None and oe is None:
            # add/add
            if to_bytes_sig(ae) == to_bytes_sig(be):
                a.appendChild(ae)
            else:
                record("add/add", child_path, None, None, _snippet(ae), _snippet(be),
                       detail="both sides added different content",
                       where_loc=_loc_for(ae, loc, child_path))
                a.appendChild(ae)
                child_host.appendChild(_conflict_comment(doc, conflicts[-1], meter,
                                                         len(conflicts), child_path.count("/")))
        elif ae is not None and be is not None:
            merged = merge_elem(doc, oe, ae, be, child_path, conflicts, sigs,
                                child_loc, meter, child_host)
            if merged is not None:
                a.appendChild(merged)
        elif ae is not None:
            a.appendChild(ae)
    return a


def to_bytes_sig(elem) -> bytes:
    out: list = []
    serialize(elem, 0, out, canonical=False)
    return "".join(out).encode()


def _snippet(elem) -> str:
    return abbrev_value(to_bytes_sig(elem).decode("utf-8", "replace"), 200)


def cmd_merge(args) -> int:
    o, a, b = load_any(args.base), load_any(args.ours), load_any(args.theirs)
    # Git copies %A into the work tree *and* into the object database verbatim:
    # neither the clean nor the smudge filter is applied to a merge driver's
    # output (measured on git 2.43.0; merge.renormalize does not change it).
    # So the driver must write the *stored* form, which is the clean-filter
    # output: plain XML.  LMMS loads an uncompressed .mmpz transparently
    # (DataFile::loadData falls back to qUncompress only when XML parsing
    # fails, src/core/DataFile.cpp:2132), so the work-tree file stays a valid
    # project file until the next checkout re-compresses it.
    do, da, db = parse(o), parse(a), parse(b)
    meter = project_meter(do)
    # Snapshots taken before any mutation: this is what lets the merge answer
    # "was this subtree modified?" over the whole subtree rather than by
    # attributes + direct-child count.
    sigs = {
        "o": sig_index(do.documentElement),
        "a": sig_index(da.documentElement),
        "b": sig_index(db.documentElement),
    }
    # Key-path snapshots, taken before the merge mutates ours.
    sym_pre = {"o": symbolic_index(do.documentElement),
               "a": symbolic_index(da.documentElement),
               "b": symbolic_index(db.documentElement)}
    conflicts: list = []
    merged = merge_elem(da, do.documentElement, da.documentElement,
                        db.documentElement, "", conflicts, sigs, (), meter,
                        da.documentElement)
    if merged is None:
        merged = da.documentElement
    if merged is not da.documentElement:
        da.removeChild(da.documentElement)
        da.appendChild(merged)
    # normalise ordering so the merge result is deterministic and readable
    canonicalize_dom(da)
    xml = to_bytes(da, canonical=True)

    # Independent safety net.  Re-read the bytes about to be written and prove
    # that every edit either side made is still there.  A merge that drops an
    # edit silently is worse than one that conflicts, so a violation is both
    # reported and marked in the file, never passed off as success.
    lost = []
    try:
        lost = audit_no_lost_edits(
            sym_pre["o"], sym_pre["a"], sym_pre["b"],
            symbolic_index(parse(xml).documentElement))
    except Exception as exc:  # defensive: never turn a parse failure into silence
        lost = [("audit", "lost-edit", "audit crashed: %s" % exc, "?")]
    if lost:
        for side, tag, label, digest in lost:
            rec = conflict_record(
                "lost-edit", (), None, None, None, None,
                detail="%s-side %s (fingerprint %s) is not in the merged result"
                       % (side, label, digest),
                path=args.ours)
            conflicts.append(rec)
            comment_host(da.documentElement).appendChild(
                _conflict_comment(da, rec, meter, len(conflicts), 0))
        canonicalize_dom(da)
        xml = to_bytes(da, canonical=True)

    # Refuse to hand the DAW a file it cannot load.  A comment in a
    # <trackcontainer> is the one placement measured to hang the loader, and a
    # merge that corrupts a project is the worst outcome available here, so it
    # is an error rather than a warning.
    bad = unsafe_comment_hosts(parse(xml))
    if bad:
        sys.stderr.write("mmpz-git: refusing to write %s: a conflict comment "
                         "landed in <%s>, which the DAW loader cannot load "
                         "(%s)\n" % (args.ours, bad[0][0], bad[0][1]))
        return 2

    with open(args.ours, "wb") as fh:
        fh.write(xml)  # stored (clean) form; see note above

    if conflicts:
        sys.stderr.write(render_report(conflicts, meter, args.ours))
        _conflict_artifacts(args, conflicts, meter)
        return 1
    if getattr(args, "report", None):
        with open(args.report, "w") as fh:
            fh.write(render_report(conflicts, meter, args.ours))
    return 0


def _conflict_artifacts(args, conflicts, meter):
    """Keep large field values recoverable outside the project file.

    A conflict comment abbreviates a large value (an embedded sample is
    base64 and can dwarf the project), so the full value is written next to
    the file instead.  The sidecar is only written when something was
    actually abbreviated, or when --json asked for a report.
    """
    big = [r for r in conflicts
           if any(isinstance(r.get(k), str) and len(r[k]) > MAX_INLINE_VALUE
                  for k in ("base", "ours", "theirs"))]
    payload = {
        "project": args.ours,
        "meter": {"ticks_per_bar": meter[0], "beats_per_bar": meter[1]},
        "conflicts": conflicts,
    }
    out = getattr(args, "json", None)
    if not out and big and not getattr(args, "no_sidecar", False):
        out = args.ours + ".mmpz-git-conflicts.json"
    if out:
        with open(out, "w") as fh:
            json.dump(payload, fh, indent=2, ensure_ascii=True)
            fh.write("\n")
        sys.stderr.write("mmpz-git: %d large field(s) abbreviated in the file; "
                         "full values in %s\n" % (len(big), out))
    if getattr(args, "report", None):
        with open(args.report, "w") as fh:
            fh.write(render_report(conflicts, meter, args.ours))
        sys.stderr.write("mmpz-git: text report: %s\n" % args.report)
    return out


# ---------------------------------------------------------------------------
# conflicts: re-present an already-marked project file
# ---------------------------------------------------------------------------


def _iter_elements(root):
    stack = [root]
    while stack:
        e = stack.pop()
        yield e
        stack.extend(_children(e))


def marker_records(doc):
    """Conflict records recovered from the CONFLICT comments in a project."""
    recs = []
    for elem in _iter_elements(doc.documentElement):
        for c in elem.childNodes:
            if c.nodeType != Node.COMMENT_NODE or CONFLICT_BANNER not in c.data:
                continue
            rec = None
            for line in c.data.splitlines():
                line = line.strip()
                if line.startswith(CONFLICT_DATA_TAG):
                    try:
                        rec = json.loads(base64.b64decode(
                            line[len(CONFLICT_DATA_TAG):].strip()).decode("utf-8"))
                    except Exception:
                        rec = None
            if rec is None:
                head = [l.strip() for l in c.data.strip().splitlines() if l.strip()]
                rec = {"kind": "unknown", "field": None, "base": None,
                       "ours": None, "theirs": None,
                       "location": {"track": None, "pattern": None, "note": None},
                       "detail": head[1] if len(head) > 1 else "",
                       "path": head[2] if len(head) > 2 else "/"}
            recs.append(rec)
    return recs


def cmd_conflicts(args) -> int:
    doc = parse(load_any(args.file))
    meter = project_meter(doc)
    recs = marker_records(doc)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"project": args.file,
                       "meter": {"ticks_per_bar": meter[0],
                                 "beats_per_bar": meter[1]},
                       "conflicts": recs}, fh, indent=2, ensure_ascii=True)
            fh.write("\n")
    if not recs:
        sys.stdout.write("mmpz-git: no conflicts marked in %s\n" % args.file)
        return 0
    sys.stdout.write(render_report(recs, meter, args.file))
    return 1


# ---------------------------------------------------------------------------
# audible diff: render both versions and measure where they differ
#
# The renderer exposes `render <project>` (one file) and `rendertracks
# <project> -o <dir>` (one file per track, named "<n>_<track>.wav"), which is
# what makes a per-track answer possible without faking it with mutes.
# ---------------------------------------------------------------------------

RENDERER_CANDIDATES = ("build/lmms", "build/bin/lmms", "build-ci/lmms",
                       "build-ci/bin/lmms")


def find_renderer(explicit=None):
    """First usable renderer: --renderer, $MMPZ_GIT_RENDERER, a local build, $PATH."""
    cands = []
    if explicit:
        cands.append(explicit)
    if os.environ.get("MMPZ_GIT_RENDERER"):
        cands.append(os.environ["MMPZ_GIT_RENDERER"])
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    cands.extend(os.path.join(root, rel) for rel in RENDERER_CANDIDATES)
    cands.append(shutil.which("lmms") or "")
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def _render_env():
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    return env


def _run_render(cmd, log):
    r = subprocess.run(cmd, capture_output=True, text=True, env=_render_env())
    if log:
        with open(log, "w") as fh:
            fh.write("$ %s\n--- stdout ---\n%s\n--- stderr ---\n%s\n"
                     % (" ".join(cmd), r.stdout, r.stderr))
    return r


def render_mix(renderer, project, out_wav, samplerate=None, log=None):
    cmd = [renderer, "render", project, "-o", out_wav, "-f", "wav"]
    if samplerate:
        cmd += ["-s", str(samplerate)]
    return _run_render(cmd, log)


def render_tracks(renderer, project, out_dir, samplerate=None, log=None):
    os.makedirs(out_dir, exist_ok=True)
    cmd = [renderer, "rendertracks", project, "-o", out_dir, "-f", "wav"]
    if samplerate:
        cmd += ["-s", str(samplerate)]
    return _run_render(cmd, log)


def track_wavs(directory):
    """{track name: wav path} from a `rendertracks` output directory."""
    out = {}
    for fn in sorted(os.listdir(directory)):
        if not fn.endswith(".wav"):
            continue
        stem = fn[:-4]
        # the renderer prefixes the track index: "1_Bass.wav"
        name = stem.split("_", 1)[1] if "_" in stem else stem
        out[name] = os.path.join(directory, fn)
    return out


def wav_mono(path):
    """(mono float samples, samplerate, channels, samplerate) from a PCM WAV."""
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 2:
        a = array.array("h")
    elif sw == 1:
        a = array.array("B")
    elif sw == 4:
        a = array.array("i")
    else:
        raise ValueError("unsupported sample width %d bytes in %s "
                         "(expected 8/16/32-bit PCM)" % (sw, path))
    a.frombytes(raw)
    if sys.byteorder == "big":
        a.byteswap()
    if sw == 1:
        scale, off = 1.0 / 128.0, -1.0
    elif sw == 2:
        scale, off = 1.0 / 32768.0, 0.0
    else:
        scale, off = 1.0 / 2147483648.0, 0.0
    if ch <= 1:
        mono = [v * scale + off for v in a]
    else:
        mono = [sum(a[i + c] for c in range(ch)) * scale / ch + off
                for i in range(0, len(a) - ch + 1, ch)]
    return mono, sr, ch, n


def project_bpm(doc):
    heads = doc.documentElement.getElementsByTagName("head")
    if not heads:
        return 0.0
    try:
        return float(heads[0].getAttribute("bpm") or 0)
    except ValueError:
        return 0.0


def bar_windows(nsamples, samplerate, bpm, beats_per_bar):
    """[(start, end)] sample windows, one per bar, plus the window length.

    A bar is `beats_per_bar` quarter-note beats, a beat is 60/bpm seconds —
    the relation this tree's TimePos encodes (ticksPerBar is DefaultTicksPerBar
    * numerator / denominator, include/TimePos.h:128, so a 4/4 bar is four
    beats).  Time signatures other than a plain 4/4 grid are reported as a
    limit rather than guessed at.
    """
    if bpm <= 0 or samplerate <= 0 or beats_per_bar <= 0:
        return [], 0
    per = int(round((beats_per_bar * 60.0 / bpm) * samplerate))
    if per <= 0:
        return [], 0
    wins = []
    for start in range(0, nsamples, per):
        if min(per, nsamples - start) < per // 2:
            break                      # drop a trailing partial bar
        wins.append((start, start + per))
    return wins, per


def bar_rms_db(mono, windows):
    """Per-bar RMS in dBFS."""
    out = []
    for start, end in windows:
        chunk = mono[start:end]
        if not chunk:
            out.append(-200.0)
            continue
        acc = 0.0
        for v in chunk:
            acc += v * v
        r = math.sqrt(acc / len(chunk))
        out.append(-200.0 if r <= 0 else 20.0 * math.log10(r))
    return out


def bar_peak_delta_db(a, b, windows):
    """Per-bar max |a[n]-b[n]| in dBFS.

    This is the metric that catches a small edit: adding one note to a busy bar
    moves the bar's RMS by ~0.15 dB but moves the sample-level difference far
    above the noise floor.  The renders are bit-deterministic (measured: two
    renders of one project agree to 0.000000 dB per bar), so a difference here
    is a change in the project, not render jitter.
    """
    out = []
    for start, end in windows:
        end = min(end, len(a), len(b))
        if end <= start:
            out.append(-200.0)
            continue
        m = 0.0
        for i in range(start, end):
            d = abs(a[i] - b[i])
            if d > m:
                m = d
        out.append(-200.0 if m <= 0 else 20.0 * math.log10(m))
    return out


def bar_db_curve(mono, samplerate, bpm, beats_per_bar):
    """Per-bar RMS curve (kept as the simple entry point for callers/tests)."""
    windows, _ = bar_windows(len(mono), samplerate, bpm, beats_per_bar)
    return bar_rms_db(mono, windows)


def compare_bars(curve_a, curve_b, peak_a=None, peak_b=None,
                 threshold_db=1.0, peak_threshold_db=-60.0, floor_db=-60.0):
    """[(bar_number, delta_rms_db, delta_peak_db)] for bars that differ.

    A bar counts as different when its loudness changed by at least
    `threshold_db` OR its sample-level difference reached `peak_threshold_db`.
    Bars quiet on both sides are ignored: two silences are not a difference.
    """
    out = []
    for i in range(min(len(curve_a), len(curve_b))):
        d_rms = abs(curve_a[i] - curve_b[i])
        d_peak = -200.0
        if peak_a is not None and peak_b is not None and i < min(len(peak_a), len(peak_b)):
            d_peak = peak_a[i]
        if curve_a[i] <= floor_db and curve_b[i] <= floor_db and d_peak <= peak_threshold_db:
            continue
        if d_rms >= threshold_db or d_peak >= peak_threshold_db:
            out.append((i + 1, d_rms, d_peak))
    return out


def group_bars(bars):
    """[(first, last)] runs of consecutive bar numbers."""
    groups = []
    for b in sorted(bars):
        if groups and b == groups[-1][1] + 1:
            groups[-1][1] = b
        else:
            groups.append([b, b])
    return [(g[0], g[1]) for g in groups]


def _describe_bars(diffs):
    """'bars 1-2 differ (max 13.53 dB RMS, -14.5 dBFS peak); bar 5 differs (...)'"""
    by_bar = {b: (r, pk) for b, r, pk in diffs}
    parts = []
    for lo, hi in group_bars(by_bar):
        sel = [by_bar[b] for b in range(lo, hi + 1) if b in by_bar]
        max_rms = max(r for r, _ in sel)
        max_pk = max(pk for _, pk in sel)
        label = "bars %d-%d differ" % (lo, hi) if lo != hi else "bar %d differs" % lo
        parts.append("%s (max %.2f dB RMS, %s peak)"
                     % (label, max_rms,
                        "-inf dBFS" if max_pk <= -200 else "%.1f dBFS" % max_pk))
    return "; ".join(parts)


def _compare_rendered_pair(a_wav, b_wav, bpm, meter, args):
    """(diffs, fmt) comparing two rendered files bar by bar."""
    ma, sra, cha, na = wav_mono(a_wav)
    mb, srb, chb, nb = wav_mono(b_wav)
    if (sra, cha) != (srb, chb):
        raise ValueError("renders differ in format (%d Hz/%dch vs %d Hz/%dch)"
                         % (sra, cha, srb, chb))
    wins, _ = bar_windows(min(na, nb), sra, bpm, meter[1])
    rms_a, rms_b = bar_rms_db(ma, wins), bar_rms_db(mb, wins)
    pk = bar_peak_delta_db(ma, mb, wins)
    diffs = compare_bars(rms_a, rms_b, pk, pk,
                         args.threshold_db, args.peak_threshold_db, args.floor_db)
    return diffs, "%d Hz, %d channel(s)" % (sra, cha), len(wins)


def cmd_audible_diff(args) -> int:
    renderer = find_renderer(args.renderer)
    if renderer is None:
        sys.stderr.write(
            "mmpz-git audible-diff: no renderer found.\n"
            "  use --renderer PATH (or $MMPZ_GIT_RENDERER), or build once:\n"
            "    JOBS=4 tools/local-ci.sh --build-dir build --jobs 4\n")
        return 2
    for proj in (args.a, args.b):
        if not os.path.isfile(proj):
            sys.stderr.write("mmpz-git audible-diff: no such file: %s\n" % proj)
            return 2
    doc_a = parse(load_any(args.a))
    meter = project_meter(doc_a)
    bpm = project_bpm(doc_a)
    if bpm <= 0:
        sys.stderr.write("mmpz-git audible-diff: %s has no usable <head bpm>\n" % args.a)
        return 2
    tmp = args.keep or tempfile.mkdtemp(prefix="mmpz-audible-diff-")
    os.makedirs(tmp, exist_ok=True)

    results = []          # (label, diffs or None, note)
    fmt = None
    bars_total = 0
    only_a, only_b = [], []

    if not args.mix_only:
        da, db = os.path.join(tmp, "tracks-A"), os.path.join(tmp, "tracks-B")
        ra = render_tracks(renderer, args.a, da, args.samplerate,
                           os.path.join(tmp, "A.rendertracks.log"))
        rb = render_tracks(renderer, args.b, db, args.samplerate,
                           os.path.join(tmp, "B.rendertracks.log"))
        for proj, r in ((args.a, ra), (args.b, rb)):
            if r.returncode != 0:
                sys.stderr.write("mmpz-git audible-diff: rendertracks %s failed "
                                 "(exit %d)\n%s\n" % (proj, r.returncode, r.stderr[-1500:]))
                return 2
        ta, tb = track_wavs(da), track_wavs(db)
        names = sorted(set(ta) | set(tb))
        if args.track:
            if args.track not in ta and args.track not in tb:
                sys.stderr.write("mmpz-git audible-diff: no track named %r in "
                                 "either project (have: %s)\n"
                                 % (args.track, ", ".join(names)))
                return 2
            names = [n for n in names if n == args.track]
        for name in names:
            if name not in ta:
                only_b.append(name)      # rendered by B, not by A
                continue
            if name not in tb:
                only_a.append(name)      # rendered by A, not by B
                continue
            try:
                diffs, fmt, nb = _compare_rendered_pair(ta[name], tb[name], bpm,
                                                        meter, args)
            except ValueError as exc:
                sys.stderr.write("mmpz-git audible-diff: %s: %s\n" % (name, exc))
                return 2
            bars_total = max(bars_total, nb)
            results.append(('track "%s"' % name, diffs, None))

    if not args.track:
        wa, wb = os.path.join(tmp, "mix-A.wav"), os.path.join(tmp, "mix-B.wav")
        ra = render_mix(renderer, args.a, wa, args.samplerate,
                        os.path.join(tmp, "A.render.log"))
        rb = render_mix(renderer, args.b, wb, args.samplerate,
                        os.path.join(tmp, "B.render.log"))
        for proj, r in ((args.a, ra), (args.b, rb)):
            if r.returncode != 0:
                sys.stderr.write("mmpz-git audible-diff: render %s failed "
                                 "(exit %d)\n%s\n" % (proj, r.returncode, r.stderr[-1500:]))
                return 2
        diffs, fmt2, nb = _compare_rendered_pair(wa, wb, bpm, meter, args)
        fmt = fmt or fmt2
        bars_total = max(bars_total, nb)
        results.append(("full mix", diffs, None))

    changed = [r for r in results if r[1]]
    missing = len(only_a) + len(only_b)
    bar_s = meter[1] * 60.0 / bpm
    lines = ["mmpz-git audible-diff: %s vs %s" % (args.a, args.b),
             "  renderer   : %s" % renderer,
             "  tempo      : %g BPM, %d/4 -> 1 bar = %.4f s (%d ticks)"
             % (bpm, meter[1], bar_s, meter[0]),
             "  compared   : %d bar(s) per track%s"
             % (bars_total, ", render %s" % fmt if fmt else ""),
             "  thresholds : %.2f dB RMS, %.1f dBFS peak, floor %.1f dBFS"
             % (args.threshold_db, args.peak_threshold_db, args.floor_db)]
    if not args.mix_only:
        lines.append("  track sets : %s"
                     % ("identical" if not missing
                        else "%d only in A (%s), %d only in B (%s)"
                        % (len(only_a), ", ".join(only_a) or "-",
                           len(only_b), ", ".join(only_b) or "-")))
    if changed or missing:
        lines.append("  differences:")
        for label, diffs, _ in results:
            if diffs:
                lines.append("    %-16s %s" % (label, _describe_bars(diffs)))
        for n in only_a:
            lines.append("    %-16s present in A's render only "
                         "(removed from, or silent in, B)" % ('track "%s"' % n))
        for n in only_b:
            lines.append("    %-16s present in B's render only "
                         "(added to, or silent in, A)" % ('track "%s"' % n))
        lines.append("  summary    : %d track(s) differ, %d bar range(s) differ%s"
                     % (len([r for r in results if r[1]]),
                        sum(len(group_bars([b for b, _, _ in r[1]]))
                            for r in results if r[1]),
                        "" if not missing else ", %d track(s) added/removed" % missing))
    else:
        lines.append("  differences: none")
        lines.append("  summary    : the two versions render identically within "
                     "the thresholds")
    lines.append("  limits     : per-bar loudness and peak difference. It says WHICH "
                 "bars")
    lines.append("               and WHICH track differ, not which note; a change that")
    lines.append("               keeps a bar's energy identical is invisible; the bar")
    lines.append("               grid assumes a %d/4 bar." % meter[1])
    lines.append("  artifacts  : %s" % tmp)
    text = "\n".join(lines) + "\n"
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"a": args.a, "b": args.b, "renderer": renderer,
                       "bpm": bpm, "meter": meter, "bar_seconds": bar_s,
                       "thresholds": {"rms_db": args.threshold_db,
                                      "peak_db": args.peak_threshold_db,
                                      "floor_db": args.floor_db},
                       "bars_compared": bars_total,
                       "tracks_only_in_a": only_a, "tracks_only_in_b": only_b,
                       "results": [{"label": l,
                                    "differing_bars": [{"bar": b, "delta_rms_db": r,
                                                        "delta_peak_db": p}
                                                       for b, r, p in (d or [])]}
                                   for l, d, _ in results]},
                      fh, indent=2, ensure_ascii=True)
            fh.write("\n")
    sys.stdout.write(text)
    return 1 if (changed or missing) else 0


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------


def _read_input(path):
    if path in (None, "-"):
        return sys.stdin.buffer.read()
    with open(path, "rb") as fh:
        return fh.read()


def cmd_dump(args) -> int:
    data = _read_input(args.file)
    xml = decompress(data) if is_container(data) else data
    if getattr(args, "canonical", False):
        xml = canonical_xml(xml)
    if args.output:
        with open(args.output, "wb") as fh:
            fh.write(xml)
    else:
        sys.stdout.buffer.write(xml)
    return 0


def cmd_compress(args) -> int:
    xml = _read_input(args.file)
    if is_container(xml):
        xml = decompress(xml)
    out = compress(xml)
    if args.output:
        with open(args.output, "wb") as fh:
            fh.write(out)
    else:
        sys.stdout.buffer.write(out)
    return 0


def cmd_textconv(args) -> int:
    data = _read_input(args.file)
    sys.stdout.buffer.write(decompress(data) if is_container(data) else data)
    return 0


def cmd_verify(args) -> int:
    rc = 0
    for path in args.files:
        raw = open(path, "rb").read()
        if not is_container(raw):
            print("SKIP %s (not a .mmpz container)" % path)
            continue
        xml = decompress(raw)
        again = compress(xml)
        same = again == raw
        print("%-6s %s\n       sha256(original) = %s\n       sha256(roundtrip)= %s"
              % ("OK" if same else "FAIL", path, sha256(raw), sha256(again)))
        rc |= 0 if same else 1
    return rc


def cmd_canonicalize(args) -> int:
    xml = load_any(args.file)
    doc = parse(xml)
    n = canonicalize_dom(doc)
    out = to_bytes(doc, canonical=True)
    if args.check:
        ok = out == xml
        if ok:
            print("CANONICAL %s" % args.file)
        else:
            a = xml.split(b"\n")
            b = out.split(b"\n")
            d = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
            print("NOT-CANONICAL %s (%d line(s) differ from the canonical form; "
                  "%d element block reorder(s))" % (args.file, d, n))
        return 0 if ok else 1
    if args.output:
        with open(args.output, "wb") as fh:
            fh.write(out)
    else:
        sys.stdout.buffer.write(out)
    return 0


def cmd_info(args) -> int:
    doc = parse(load_any(args.file))
    root = doc.documentElement
    assert root is not None
    def count(tag):
        return len(root.getElementsByTagName(tag))
    head = root.getElementsByTagName("head")
    bpm = head[0].getAttribute("bpm") if head else "?"
    print("file      : %s" % args.file)
    print("root      : <%s>" % root.tagName)
    print("bpm       : %s" % bpm)
    print("tracks    : %d" % count("track"))
    print("patterns  : %d" % count("pattern"))
    print("notes     : %d" % count("note"))
    print("fxchannels: %d" % count("fxchannel"))
    print("sha256    : %s" % sha256(_read_input(args.file)))
    return 0


def _git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args),
                          capture_output=True, text=True)


def cmd_install(args) -> int:
    repo = os.path.abspath(args.repo)
    tool = os.path.abspath(__file__)
    # Run the helper through the current interpreter so the recipe works even
    # when the script bit is not set (e.g. after a copy out of a tarball).
    py = args.python or sys.executable or "python3"
    cmd = '"%s" "%s"' % (py, tool)
    clean = '%s dump%s' % (cmd, " --canonical" if args.canonical else "")
    cfg = {
        "filter.mmpz.clean": clean,
        "filter.mmpz.smudge": '%s compress' % cmd,
        "filter.mmpz.required": "false",
        "diff.mmpz.textconv": '%s textconv' % cmd,
        "merge.mmpz.driver": '%s merge %%O %%A %%B %%L %%P' % cmd,
        "merge.mmpz.name": "LMMS project 3-way merge",
        "diff.mmpz.binary": "false",
    }
    for k, v in cfg.items():
        r = _git(repo, "config", k, v)
        if r.returncode:
            sys.stderr.write(r.stderr)
            return r.returncode
        print("git config %s = %s" % (k, v))
    return 0


def cmd_uninstall(args) -> int:
    repo = os.path.abspath(args.repo)
    for k in ("filter.mmpz.clean", "filter.mmpz.smudge", "filter.mmpz.required",
              "diff.mmpz.textconv", "merge.mmpz.driver", "merge.mmpz.name",
              "diff.mmpz.binary"):
        _git(repo, "config", "--unset", k)
        print("git config --unset %s" % k)
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog=PROG, description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("dump", help=".mmpz -> XML on stdout")
    d.add_argument("file", nargs="?")
    d.add_argument("-o", "--output")
    d.add_argument("--canonical", action="store_true")
    d.set_defaults(func=cmd_dump)

    c = sub.add_parser("compress", help="XML -> .mmpz on stdout")
    c.add_argument("file", nargs="?")
    c.add_argument("-o", "--output")
    c.set_defaults(func=cmd_compress)

    t = sub.add_parser("textconv", help="git textconv: container or XML -> XML")
    t.add_argument("file", nargs="?")
    t.set_defaults(func=cmd_textconv)

    v = sub.add_parser("verify", help="prove .mmpz -> XML -> .mmpz is byte-identical")
    v.add_argument("files", nargs="+")
    v.set_defaults(func=cmd_verify)

    k = sub.add_parser("canonicalize", help="enforce canonical element ordering")
    k.add_argument("file")
    k.add_argument("-o", "--output")
    k.add_argument("--check", action="store_true")
    k.set_defaults(func=cmd_canonicalize)

    f = sub.add_parser("diff", help="XML-aware semantic diff -> operation list")
    f.add_argument("a")
    f.add_argument("b")
    f.add_argument("-o", "--output")
    f.set_defaults(func=cmd_diff)

    m = sub.add_parser("merge", help="git merge driver (%%O %%A %%B)")
    m.add_argument("base")
    m.add_argument("ours")
    m.add_argument("theirs")
    m.add_argument("marker", nargs="?")
    m.add_argument("path", nargs="?")
    m.add_argument("--report", default=None,
                   help="write the musical conflict report to this file too")
    m.add_argument("--json", default=None,
                   help="write the machine-readable conflict report here "
                        "(implies no automatic sidecar)")
    m.add_argument("--no-sidecar", action="store_true",
                   help="never write the .mmpz-git-conflicts.json sidecar that "
                        "preserves large field values")
    m.set_defaults(func=cmd_merge)

    cf = sub.add_parser("conflicts",
                        help="present a conflicted project's conflicts musically")
    cf.add_argument("file")
    cf.add_argument("--json", default=None)
    cf.set_defaults(func=cmd_conflicts)

    ad = sub.add_parser("audible-diff",
                        help="render two versions and report which bars differ")
    ad.add_argument("a")
    ad.add_argument("b")
    ad.add_argument("--renderer", default=None,
                    help="path to the built binary (default: $MMPZ_GIT_RENDERER, "
                         "then build/lmms, then $PATH)")
    ad.add_argument("--track", default=None,
                    help="compare only this track (the renderer's per-track "
                         "output is used; the full mix is skipped)")
    ad.add_argument("--mix-only", action="store_true",
                    help="compare only the full mix, skip the per-track renders")
    ad.add_argument("--samplerate", type=int, default=None,
                    help="samplerate to render at (default: the renderer's own)")
    ad.add_argument("--threshold-db", type=float, default=1.0,
                    help="per-bar RMS change that counts as a loudness "
                         "difference (default: 1.0 dB)")
    ad.add_argument("--peak-threshold-db", type=float, default=-60.0,
                    help="per-bar sample-level difference that counts as a "
                         "change at all (default: -60 dBFS; the renders are "
                         "bit-deterministic, measured)")
    ad.add_argument("--floor-db", type=float, default=-60.0,
                    help="bars quieter than this on both sides are ignored "
                         "(default: -60 dBFS)")
    ad.add_argument("--keep", default=None,
                    help="directory for the WAVs and render logs "
                         "(default: a fresh temp dir)")
    ad.add_argument("--json", default=None)
    ad.set_defaults(func=cmd_audible_diff)

    i = sub.add_parser("info", help="summarise a project file")
    i.add_argument("file")
    i.set_defaults(func=cmd_info)

    n = sub.add_parser("install", help="configure filters/diff/merge in a git repo")
    n.add_argument("--repo", default=".")
    n.add_argument("--canonical", action="store_true")
    n.add_argument("--python", default=None,
                   help="interpreter for the filter/diff/merge commands "
                        "(default: the interpreter running this script)")
    n.set_defaults(func=cmd_install)

    u = sub.add_parser("uninstall", help="remove git config for mmpz-git")
    u.add_argument("--repo", default=".")
    u.set_defaults(func=cmd_uninstall)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
