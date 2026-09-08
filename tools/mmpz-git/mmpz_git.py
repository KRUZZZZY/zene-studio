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
#   * LMMS-aware 3-way merge driver that emits human-resolvable conflicts
#
# Container format (verified against src/core/DataFile.cpp:410 `qCompress(xml.toUtf8())`):
#   [4-byte big-endian uncompressed length][zlib stream, default level]
#
# Copyright (c) 2026 LMMS contributors
"""mmpz-git: git-friendly LMMS project files."""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import zlib
from xml.dom import Node, minidom

PROG = "mmpz-git"
XML_HEADER = b'<?xml version="1.0"?>\n'
CONFLICT_BANNER = "mmpz-git CONFLICT"

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
            ops.append("  + %s @%s = %s" % (_path(path, a), k, bn[k]))
        elif k not in bn:
            ops.append("  - %s @%s (was %s)" % (_path(path, a), k, an[k]))
        elif an[k] != bn[k]:
            ops.append("  ~ %s @%s: %s -> %s" % (_path(path, a), k, an[k], bn[k]))

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
    diff_dom(da.documentElement, db.documentElement, "", ops, moves)
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


def _conflict_comment(doc, path, what, base, ours, theirs):
    pad = "  " * path.count("/")

    def clean(s):
        # XML comments must not contain "--"
        return (s if s is not None else "<absent>").replace("--", "==")

    body = (
        "\n%s  ======= %s =======\n"
        "%s  element : %s\n"
        "%s  field   : %s\n"
        "%s  base    : %s\n"
        "%s  ours    : %s\n"
        "%s  theirs  : %s\n"
        "%s  resolve : keep the correct value, then delete this comment.\n"
        "%s  ===========================================\n%s"
    ) % (pad, CONFLICT_BANNER,
         pad, path or "/", pad, what,
         pad, clean(base), pad, clean(ours), pad, clean(theirs),
         pad, pad, pad)
    return doc.createComment(body)


def merge_elem(doc, o, a, b, path, conflicts):
    """3-way merge element `a` (ours) with `b` (theirs) against `o` (base).

    Mutates `a`. Returns the merged element (may be `a` or `b`).
    """
    if o is None and b is None:
        return a
    if o is None and a is None:
        return b
    if b is None:  # deleted by theirs
        if _attrs(a) == _attrs(o) and len(_children(a)) == len(_children(o)):
            return None  # clean delete
        conflicts.append(("delete/modify", path, "theirs deleted, ours modified"))
        return a
    if a is None:  # deleted by ours
        if _attrs(b) == _attrs(o) and len(_children(b)) == len(_children(o)):
            return None
        conflicts.append(("modify/delete", path, "ours deleted, theirs modified"))
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
            conflicts.append(("attribute", path, k))
            a.appendChild(_conflict_comment(doc, path, "attribute " + k, ov, av, bv))

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
        conflicts.append(("order", path, "children reordered differently"))
        a.appendChild(_conflict_comment(
            doc, path, "child order",
            ", ".join(str(k) for k in okeys),
            ", ".join(str(k) for k in akeys),
            ", ".join(str(k) for k in bkeys)))
        order = okeys  # keep base order

    # rebuild child list in merged order
    for e in _children(a):
        a.removeChild(e)
    for k in order:
        oe, ae, be = odict.get(k), adict.get(k), bdict.get(k)
        if ae is not None and be is None and oe is not None:
            # deleted by theirs
            if _attrs(ae) != _attrs(oe):
                conflicts.append(("delete/modify", path + "/" + str(k),
                                  "theirs deleted, ours modified"))
                a.appendChild(ae)
            # else: clean delete -> drop
        elif ae is None and be is not None and oe is not None:
            if _attrs(be) != _attrs(oe):
                conflicts.append(("modify/delete", path + "/" + str(k),
                                  "ours deleted, theirs modified"))
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
                conflicts.append(("add/add", path + "/" + str(k),
                                  "both added different content"))
                a.appendChild(ae)
                a.appendChild(_conflict_comment(
                    doc, path + "/" + str(k), "add/add",
                    None, _snippet(ae), _snippet(be)))
        elif ae is not None and be is not None:
            merged = merge_elem(doc, oe, ae, be, path + "/" + _label(ae), conflicts)
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
    return to_bytes_sig(elem).decode("utf-8", "replace")[:200]


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
    conflicts: list = []
    merged = merge_elem(do, do.documentElement, da.documentElement,
                        db.documentElement, "", conflicts)
    if merged is None:
        merged = da.documentElement
    if merged is not da.documentElement:
        da.removeChild(da.documentElement)
        da.appendChild(merged)
    # normalise ordering so the merge result is deterministic and readable
    canonicalize_dom(da)
    xml = to_bytes(da, canonical=True)
    with open(args.ours, "wb") as fh:
        fh.write(xml)  # stored (clean) form; see note above
    for kind, where, what in conflicts:
        sys.stderr.write("CONFLICT (%s) at %s: %s\n" % (kind, where or "/", what))
    if conflicts:
        sys.stderr.write("mmpz-git: %d conflict(s); resolve in %s\n"
                         % (len(conflicts), args.ours))
        return 1
    return 0


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

    m = sub.add_parser("merge", help="git merge driver (%O %A %B)")
    m.add_argument("base")
    m.add_argument("ours")
    m.add_argument("theirs")
    m.add_argument("marker", nargs="?", default="")
    m.add_argument("path", nargs="?", default="")
    m.set_defaults(func=cmd_merge)

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
