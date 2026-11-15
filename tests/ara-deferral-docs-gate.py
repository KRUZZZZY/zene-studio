#!/usr/bin/env python3
"""ara-deferral-docs-gate.py - ARA2 and the Melodyne-style ARA items are DEFERRED, and every doc says so.

WHY. Q050-344 (R-editing-41 / R-workflow-118) settles that the ARA family is not a 0.5.0 lane. ARA2
hosting, Melodyne-style clip editing, track-wide ARA and ARA processing-order extensions need an ARA2
SDK decision and a real plugin first. The feature list still carried an old "Licence-gated" paragraph
that reads like a plan, and the public pages mention ARA with no deferral wording at all. This gate
keeps the wording consistent and stops a support or 0.5.0-lane claim from coming back.

WHAT IT CHECKS (file reads only; no network, no build):

  1. The inputs exist: docs/FEATURE-LIST-0.3.0.md, docs/KNOWN-LIMITATIONS.md, README.md,
     data/manual/index.html and the committed command snapshot. A missing input is a setup error (exit 2).
  2. docs/KNOWN-LIMITATIONS.md has a "## ... ARA ... deferred" section. Each of the four candidate
     phrases sits on a line of that section that also says "deferred"; the section carries
     "No 0.5.0 implementation lane" and "no support promise".
  3. docs/FEATURE-LIST-0.3.0.md has a "**Deferred, not 0.5.0 - ..." paragraph. Each candidate sits in a
     sentence of that paragraph that also says "deferred"; the paragraph carries
     "No 0.5.0 implementation lane"; and the old "**Licence-gated.** ARA2 hosting..." line is gone.
  4. Public sweep: README.md, data/manual/index.html (tags stripped) and the top-level docs/*.md (not
     subdirectories, not docs/qwen-fixes-*), outside the check-2 and check-3 regions. A line naming
     ARA / ARA2 / Melodyne must carry a deferral marker; a line that ties it to 0.5.0 must say
     deferred / not / No; a support or lane verb needs a negation or deferral marker.
  5. Command claim: no command id in the committed snapshot starts with `ara.`, and no scanned line
     outside the deferral regions mentions an `ara.<name>` command.

--self-test builds its own green baseline from a synthetic fixture (it does not depend on the real
docs), then asserts the exact exit code and a named problem for each mutation: a candidate losing
"deferred" in either section, a public support or 0.5.0-lane claim, a leaked `ara.` command id, a
missing input file, and a dropped lane phrase.

Usage:  python3 tests/ara-deferral-docs-gate.py [--root DIR] [--self-test]
Exit:   0 every check holds; 1 a mismatch or a stale claim; 2 usage/setup error (missing input).
"""
import argparse
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

CANDIDATES = (
    "ARA2",
    "Melodyne-style clip editing",
    "track-wide ARA",
    "ARA processing-order extensions",
)

FEATURE_LIST = "docs/FEATURE-LIST-0.3.0.md"
KNOWN_LIMITATIONS = "docs/KNOWN-LIMITATIONS.md"
README = "README.md"
MANUAL = "data/manual/index.html"
SNAPSHOT = "tools/mcp-zene-control/zene_control/commands_snapshot.json"
INPUTS = (FEATURE_LIST, KNOWN_LIMITATIONS, README, MANUAL, SNAPSHOT)

LANE_PHRASE = "No 0.5.0 implementation lane"
SUPPORT_PHRASE = "no support promise"
OLD_LICENCE_LINE = "**Licence-gated.** ARA2 hosting: it needs an ARA2 SDK decision and a real plugin."

KL_HEADING_RE = re.compile(r"^## .*\bara\b.*deferred", re.I)
FL_DEFERRAL_RE = re.compile(r"\*\*Deferred.*\bARA\b")
ARA_RE = re.compile(r"\bARA2?\b|Melodyne")
ARA_CMD_RE = re.compile(r"\bara\.[a-z*_]")
TAG_RE = re.compile(r"<[^>]*>")
# A sentence ends at . ; ! or ? (optionally followed by bold markers) and whitespace.
SENT_SPLIT_RE = re.compile(r"(?<=[.;!?])\*{0,2}\s+")
DEFERRED_RE = re.compile(r"\bdeferred\b", re.I)
SWEEP_MARK_RE = re.compile(r"\b(deferred|not|no|without|out of scope|outside)\b", re.I)
SUPPORT_MARK_RE = re.compile(r"\b(deferred|not|no|without)\b", re.I)
LANE_MARK_RE = re.compile(r"\b(deferred|not|no)\b", re.I)
SUPPORT_VERB_RE = re.compile(
    r"\b(supports?|supported|hosts?|hosting|ships?|lands?|planned|scheduled|roadmap)\b", re.I)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def flat(text):
    """Collapse all whitespace so a phrase matches across a soft line wrap."""
    return re.sub(r"\s+", " ", text).strip()


def mentions(text, phrase):
    return re.search(re.escape(phrase), text, re.I) is not None


def paragraphs(lines):
    """(start, end) index ranges of the blank-line-separated blocks."""
    spans, start = [], None
    for i, line in enumerate(lines):
        if line.strip():
            if start is None:
                start = i
        elif start is not None:
            spans.append((start, i))
            start = None
    if start is not None:
        spans.append((start, len(lines)))
    return spans


def kl_section(lines):
    """(heading index, end index) of the ARA deferral section, or None."""
    for i, line in enumerate(lines):
        if KL_HEADING_RE.match(line):
            for j in range(i + 1, len(lines)):
                if lines[j].startswith("## "):
                    return i, j
            return i, len(lines)
    return None


def fl_paragraph(lines):
    """(start, end) of the **Deferred ... ARA paragraph, or None."""
    for s, e in paragraphs(lines):
        if FL_DEFERRAL_RE.match(flat("\n".join(lines[s:e]))):
            return s, e
    return None


def check_known_limitations(lines, problems):
    span = kl_section(lines)
    if span is None:
        problems.append(f"{KNOWN_LIMITATIONS}: no '## ... ARA ... deferred' section")
        return set()
    head, end = span
    body = lines[head + 1:end]
    for cand in CANDIDATES:
        if not any(mentions(line, cand) and DEFERRED_RE.search(line) for line in body):
            problems.append(f"{KNOWN_LIMITATIONS}: '{cand}' is not on a 'deferred' line "
                            f"in the ARA deferral section")
    text = flat("\n".join(body)).lower()
    for phrase in (LANE_PHRASE, SUPPORT_PHRASE):
        if phrase.lower() not in text:
            problems.append(f"{KNOWN_LIMITATIONS}: ARA deferral section lacks '{phrase}'")
    return set(range(head, end))


def check_feature_list(lines, problems):
    for s, e in paragraphs(lines):
        if OLD_LICENCE_LINE in flat("\n".join(lines[s:e])):
            problems.append(f"{FEATURE_LIST}:{s + 1}: the old 'Licence-gated' ARA2 line is still "
                            f"ambiguous (replace it with the Deferred paragraph)")
    span = fl_paragraph(lines)
    if span is None:
        problems.append(f"{FEATURE_LIST}: no '**Deferred, not 0.5.0 - ... ARA' paragraph")
        return set()
    s, e = span
    para = flat("\n".join(lines[s:e]))
    sentences = SENT_SPLIT_RE.split(para)
    for cand in CANDIDATES:
        if not any(mentions(x, cand) and DEFERRED_RE.search(x) for x in sentences):
            problems.append(f"{FEATURE_LIST}: '{cand}' is not in a 'deferred' sentence "
                            f"of the Deferred paragraph")
    if LANE_PHRASE.lower() not in para.lower():
        problems.append(f"{FEATURE_LIST}: Deferred paragraph lacks '{LANE_PHRASE}'")
    return set(range(s, e))


def check_public(targets, exempt, problems):
    for rel, lines in targets:
        skip = exempt.get(rel, set())
        for n, line in enumerate(lines):
            if n in skip or not line.strip():
                continue
            where = f"{rel}:{n + 1}"
            snip = line.strip()[:100]
            if ARA_RE.search(line):
                if "0.5.0" in line and not LANE_MARK_RE.search(line):
                    problems.append(f"{where}: ARA/Melodyne tied to 0.5.0 without 'deferred', "
                                    f"'not' or 'No' (0.5.0 lane claim): {snip}")
                elif SUPPORT_VERB_RE.search(line) and not SUPPORT_MARK_RE.search(line):
                    problems.append(f"{where}: ARA/Melodyne support or lane claim without a "
                                    f"deferral or negation word: {snip}")
                elif not SWEEP_MARK_RE.search(line):
                    problems.append(f"{where}: ARA/Melodyne mention without a deferral marker: "
                                    f"{snip}")
            if ARA_CMD_RE.search(line):
                problems.append(f"{where}: 'ara.<name>' command mention outside the deferral "
                                f"sections: {snip}")


def walk_ids(node, out):
    if isinstance(node, dict):
        for key, value in node.items():
            if isinstance(key, str) and key.startswith("ara."):
                out.append(key)
            walk_ids(value, out)
    elif isinstance(node, list):
        for value in node:
            walk_ids(value, out)
    elif isinstance(node, str) and node.startswith("ara."):
        out.append(node)


def check_snapshot(raw, problems):
    try:
        data = json.loads(raw)
    except ValueError as exc:
        problems.append(f"{SNAPSHOT}: not valid JSON ({exc})")
        return
    found = []
    walk_ids(data, found)
    for cmd in found:
        problems.append(f"{SNAPSHOT}: command id '{cmd}' starts with 'ara.' "
                        f"(ARA commands are not on the agent surface)")


def check(root):
    """Return (exit_code, messages). 2 = setup error, 1 = mismatch, 0 = every check holds."""
    missing = [rel for rel in INPUTS if not os.path.isfile(os.path.join(root, rel))]
    if missing:
        return 2, [f"ara-deferral-docs-gate: missing input file: {rel}" for rel in missing]
    raw = {rel: read(os.path.join(root, rel)) for rel in INPUTS}
    problems = []
    exempt = {
        KNOWN_LIMITATIONS: check_known_limitations(raw[KNOWN_LIMITATIONS].splitlines(), problems),
        FEATURE_LIST: check_feature_list(raw[FEATURE_LIST].splitlines(), problems),
    }
    targets = [
        (README, raw[README].splitlines()),
        (MANUAL, [TAG_RE.sub(" ", line) for line in raw[MANUAL].splitlines()]),
    ]
    docs_dir = os.path.join(root, "docs")
    for name in sorted(os.listdir(docs_dir)):
        rel = f"docs/{name}"
        if (not name.endswith(".md") or name.startswith("qwen-fixes-")
                or not os.path.isfile(os.path.join(root, rel))):
            continue
        text = raw[rel] if rel in raw else read(os.path.join(root, rel))
        targets.append((rel, text.splitlines()))
    check_public(targets, exempt, problems)
    check_snapshot(raw[SNAPSHOT], problems)
    return (1 if problems else 0), problems


# ---- self-test: a synthetic green baseline plus one mutation per case ----

KL_CANDIDATE_LINES = {
    "ARA2": "- ARA2 hosting is deferred.",
    "Melodyne-style clip editing": "- Melodyne-style clip editing is deferred.",
    "track-wide ARA": "- track-wide ARA is deferred.",
    "ARA processing-order extensions": "- ARA processing-order extensions are deferred.",
}

FL_CANDIDATE_SENTENCES = {
    "ARA2": "The ARA2 item is deferred.",
    "Melodyne-style clip editing": "Melodyne-style clip editing is deferred.",
    "track-wide ARA": "The track-wide ARA item is deferred.",
    "ARA processing-order extensions": "ARA processing-order extensions are deferred.",
}

KL_TEMPLATE = """# Known limitations

## ARA and Melodyne are deferred

{lines}

{lane} exists for these items, and no support promise is made.

## Other limits

Nothing else applies here.
"""

FL_TEMPLATE = """# Feature list 0.3.0

Some intro text.

**Deferred, not 0.5.0 - ARA and Melodyne are out of the 0.5.0 build.** {sentences} No 0.5.0 implementation lane exists for these items, and no support promise is made.

## Shipped

Plain shipped items.
"""


def write_file(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def write_tree(root, kl_drop=None, kl_drop_lane=False, fl_drop=None, readme_extra="",
               snapshot_ids=("clip.trim",)):
    kl_lines = []
    for cand in CANDIDATES:
        line = KL_CANDIDATE_LINES[cand]
        if cand == kl_drop:
            line = line.replace("deferred", "pending")
        kl_lines.append(line)
    fl_sentences = []
    for cand in CANDIDATES:
        sentence = FL_CANDIDATE_SENTENCES[cand]
        if cand == fl_drop:
            sentence = sentence.replace("deferred", "pending")
        fl_sentences.append(sentence)
    write_file(root, KNOWN_LIMITATIONS, KL_TEMPLATE.format(
        lines="\n".join(kl_lines),
        lane="No lane" if kl_drop_lane else LANE_PHRASE))
    write_file(root, FEATURE_LIST, FL_TEMPLATE.format(sentences=" ".join(fl_sentences)))
    write_file(root, README, "# Zene\n\nA fixture README.\n" + readme_extra)
    write_file(root, MANUAL, "<html><body><p>ok</p></body></html>\n")
    write_file(root, SNAPSHOT, json.dumps({"commands": [{"id": c} for c in snapshot_ids]}))


def drop_feature_list(root):
    write_tree(root)
    os.remove(os.path.join(root, FEATURE_LIST))


def run_case(name, want, must, build):
    with tempfile.TemporaryDirectory(prefix="ara-deferral-gate-") as root:
        build(root)
        code, messages = check(root)
    missing = [m for m in must if not any(m in msg for msg in messages)]
    ok = code == want and not missing
    print(f"{'PASS' if ok else 'FAIL'} {name}: exit {code} (want {want})")
    if not ok:
        for msg in messages:
            print(f"    {msg}")
        for m in missing:
            print(f"    expected a message naming {m!r}")
    return ok


def self_test():
    cases = [
        ("(a) synthetic baseline is green", 0, [], write_tree),
    ]
    for cand in CANDIDATES:
        cases.append((f"(b) KNOWN-LIMITATIONS '{cand}' loses 'deferred'", 1, [cand],
                      lambda r, c=cand: write_tree(r, kl_drop=c)))
    for cand in CANDIDATES:
        cases.append((f"(c) FEATURE-LIST '{cand}' loses 'deferred'", 1, [cand],
                      lambda r, c=cand: write_tree(r, fl_drop=c)))
    cases += [
        ("(d) README 'Zene supports ARA2' claim", 1, ["ARA2"],
         lambda r: write_tree(r, readme_extra="Zene supports ARA2 clip editing.\n")),
        ("(e) README 0.5.0 lane claim", 1, ["0.5.0"],
         lambda r: write_tree(r, readme_extra="Melodyne-style editing is planned for 0.5.0.\n")),
        ("(f) snapshot command id 'ara.open'", 1, ["ara.open"],
         lambda r: write_tree(r, snapshot_ids=("clip.trim", "ara.open"))),
        ("(g) FEATURE-LIST deleted (setup error)", 2, [FEATURE_LIST], drop_feature_list),
        ("(h) lane phrase dropped from KNOWN-LIMITATIONS", 1, [LANE_PHRASE],
         lambda r: write_tree(r, kl_drop_lane=True)),
    ]
    results = [run_case(*case) for case in cases]
    print(f"ara-deferral-docs-gate self-test: {sum(results)}/{len(results)} cases PASS")
    return 0 if all(results) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description="ARA/Melodyne deferral docs gate (Q050-344).")
    parser.add_argument("--root", default=REPO_ROOT,
                        help="repo root to check (default: the parent of tests/)")
    parser.add_argument("--self-test", action="store_true",
                        help="run the gate's own red/green cases and exit")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    code, messages = check(args.root)
    if code == 2:
        for msg in messages:
            print(msg, file=sys.stderr)
        return 2
    for msg in messages:
        print(msg)
    if code == 0:
        print("ara-deferral-docs-gate: OK")
    else:
        print(f"ara-deferral-docs-gate: {len(messages)} problem(s)")
    return code


if __name__ == "__main__":
    sys.exit(main())
