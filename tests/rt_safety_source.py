#!/usr/bin/env python3
"""rt_safety_source.py - the rt-safety sweep's source reader and rule set.

The static half of the real-time-safety whole-tree sweep (row 52, board card
#678): what a construct IS, and how a declared `path:symbol` pair is resolved to
the lines it owns. The judgement half - the scope and allowlist ledgers, the
sweep and its report - is tests/rt_safety_lib.py, which imports this module.

THE RULES. AGENTS.md's realtime rule has exactly three words, and every rule
below is one of them:

    allocation  - a new-expression, a C heap call, a smart-pointer factory, a
                  container/string construction that is not a reference
    locking     - a scope guard that takes a lock, or an explicit acquisition
    growth      - container growth, which reallocates

A rule is a regex over source text with comments and literals REMOVED, so a
`new` inside a comment or a message string is not a hit. That filtering is
deliberately conservative: it blanks comment and literal CONTENTS and keeps
every newline and every offset, so the line numbers a report prints are the
line numbers a reader's editor shows.

WHAT THIS FILE DOES NOT DO (the programme's stated bound, printed by every run)
-------------------------------------------------------------------------------
It reads text. It does not parse C++: a macro that expands to an allocation, an
include that brings one in, a construct reached through virtual dispatch or a
function pointer - none of those are visible here. It does not follow calls:
regions_for() resolves a symbol the caller NAMES, never a symbol it discovers.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

#: The rule's own three categories (AGENTS.md: allocation, locking, unbounded growth).
CATEGORIES = ("allocation", "locking", "growth")


@dataclass(frozen=True)
class Rule:
    """One forbidden construct: a regex, its category, and why it is forbidden."""

    id: str
    category: str
    pattern: str
    note: str
    #: If the MATCHED LINE also matches this, the match is not a hit. Used for the
    #: few places where a construct's name appears without the construct
    #: (`operator new` declares; `new` allocates).
    except_line: Optional[str] = None


RULES: Tuple[Rule, ...] = (
    Rule(
        id="alloc-new",
        category="allocation",
        pattern=r"(?<![\w.>])new\b",
        note="a new-expression allocates (and may throw); the audio thread may do neither",
        except_line=r"\boperator\s*new\b",
    ),
    Rule(
        id="alloc-c",
        category="allocation",
        pattern=r"\b(malloc|calloc|realloc|strdup|aligned_alloc|posix_memalign)\s*\(",
        note="C heap allocation",
    ),
    Rule(
        id="alloc-smart",
        category="allocation",
        pattern=r"\bmake_(shared|unique)\s*[<(]",
        note="std::make_shared/make_unique allocate the object and its control block",
    ),
    Rule(
        id="alloc-string",
        category="allocation",
        pattern=r"\b(QString|QByteArray|std::string|std::wstring|std::vector|std::map|"
                r"std::unordered_map|std::set|std::list)\s*[A-Za-z_]*\s*\(",
        note="a container/string CONSTRUCTION (not a reference, not a parameter): "
             "most of these allocate on first use",
    ),
    Rule(
        id="lock-guard",
        category="locking",
        pattern=r"\b(QMutexLocker|QReadLocker|QWriteLocker|std::lock_guard|std::unique_lock|"
                r"std::scoped_lock|std::shared_lock)\b",
        note="a scope guard that takes a lock: the audio thread must not block on another thread",
    ),
    Rule(
        id="lock-call",
        category="locking",
        pattern=r"(\.\s*(lock|lockForRead|lockForWrite|tryLock)\s*\(\s*\)|"
                r"\bpthread_mutex_lock\s*\(|\bEnterCriticalSection\s*\()",
        note="an explicit lock acquisition",
    ),
    Rule(
        id="grow-container",
        category="growth",
        pattern=r"\.\s*(push_back|emplace_back|emplace|insert|resize|reserve|append|prepend)\s*\(",
        note="container growth reallocates; unbounded growth is the third half of the rule",
    ),
    Rule(
        id="grow-script",
        category="growth",
        pattern=r"\b(QStringList|QList|QVariantList|QVector)\b[^;]*\.\s*"
                r"(append|operator<<|push_back|insert)\b",
        note="Qt implicit-sharing containers grow the same way",
    ),
)

RULES_BY_ID: Dict[str, Rule] = {rule.id: rule for rule in RULES}

#: Every run prints this. A programme whose bound is not on every report is a
#: programme whose reader will assume it proves more than it does.
BOUND: Tuple[str, ...] = (
    "STATIC ONLY: source text is read; the engine is not run. The runtime half of the "
    "rule remains the AllocationProbe tests (tests/src/core/AllocationProbe.h).",
    "DECLARED SCOPE, NOT A CALL-GRAPH: only the path:symbol pairs in the scope file are "
    "measured. A new audio-thread path that nobody declares is measured by nothing here.",
    "NO VIRTUAL DISPATCH / FUNCTION POINTERS / MACROS: an implementation reached through "
    "an interface, a macro or an include is checked only if the scope names it too.",
    "NO COST, NO SYSCALL AND NO I/O CHECK: only allocation, locking and container growth, "
    "the three words of the AGENTS.md rule.",
    "NO ALIASING ANALYSIS: a hit is a mention of a construct, not proof that it runs on "
    "the audio thread. An allowlist line with a reason is how a tolerated mention is said.",
)


@dataclass
class Region:
    """A declared body resolved to a line range (1-based, inclusive)."""

    path: str
    symbol: str
    start: int
    end: int
    #: "qualified" (Class::method found as written), "in-class" (declared
    #: Class::method, defined unqualified inside the class) or "whole-file" (`*`).
    mode: str = "qualified"


# ---------------------------------------------------------------------------
# reading source without being fooled by its comments and its literals
# ---------------------------------------------------------------------------

def _skip_line_comment(text: str, index: int, out: List[str]) -> int:
    """Blank a `//` comment; the terminating newline survives."""
    while index < len(text):
        if text[index] == "\n":
            out.append("\n")
            return index + 1
        out.append(" ")
        index += 1
    return index


def _skip_block_comment(text: str, index: int, out: List[str]) -> int:
    """Blank a `/* ... */` comment; every newline inside it survives."""
    index += 2
    while index < len(text):
        if text[index] == "*" and index + 1 < len(text) and text[index + 1] == "/":
            out.append("  ")
            return index + 2
        out.append("\n" if text[index] == "\n" else " ")
        index += 1
    return index


def _skip_literal(text: str, index: int, out: List[str], quote: str) -> int:
    """Blank a string or char literal; an unterminated one never crosses a line."""
    out.append(" ")
    index += 1
    while index < len(text):
        character = text[index]
        if character == "\\" and index + 1 < len(text):
            out.append("  ")
            index += 2
            continue
        if character == quote:
            out.append(" ")
            return index + 1
        if character == "\n":
            out.append("\n")
            return index + 1
        out.append(" ")
        index += 1
    return index


def strip_comments_and_literals(text: str) -> str:
    """Blank every comment and literal, keeping every newline and every offset.

    A scan that counts a `new` inside a comment is a false positive, and one that
    counts a brace inside a string is a mis-extracted body. Both are cheap to
    prevent and expensive to explain away, so the text is filtered once, here,
    with the line structure preserved (every newline survives, so line numbers
    still mean what the reader thinks they mean).
    """
    out: List[str] = []
    index = 0
    while index < len(text):
        character = text[index]
        follower = text[index + 1] if index + 1 < len(text) else ""
        if character == "/" and follower == "/":
            index = _skip_line_comment(text, index, out)
        elif character == "/" and follower == "*":
            index = _skip_block_comment(text, index, out)
        elif character == '"' or character == "'":
            index = _skip_literal(text, index, out, character)
        else:
            out.append(character)
            index += 1
    return "".join(out)


def scan_rules(text: str, rules: Sequence[Rule] = RULES) -> List[Tuple[int, Rule, str]]:
    """Every (lineno, rule, line) hit in already-stripped `text`."""
    hits: List[Tuple[int, Rule, str]] = []
    for lineno, line in enumerate(text.split("\n"), start=1):
        for rule in rules:
            match = re.search(rule.pattern, line)
            if match is None:
                continue
            if rule.except_line and re.search(rule.except_line, line):
                continue
            hits.append((lineno, rule, line.strip()))
    return hits


# ---------------------------------------------------------------------------
# resolving a declared symbol to its body
# ---------------------------------------------------------------------------

def _signature_pattern(symbol: str) -> "re.Pattern[str]":
    """A pattern for `<Qual::>tail(` that cannot match a longer identifier."""
    parts = symbol.split("::")
    tail = parts[-1]
    if len(parts) == 1:
        return re.compile(r"(?<![\w:.>])" + re.escape(tail) + r"\s*\(")
    qual = r"\s*::\s*".join(re.escape(part) for part in parts[:-1])
    return re.compile(r"(?<![\w:])" + qual + r"\s*::\s*" + re.escape(tail) + r"\s*\(")


def _first_open_brace(lines: Sequence[str], start_line: int,
                      start_col: int) -> Optional[Tuple[int, int]]:
    """The body's `{`, walked past a parameter list; None for a declaration."""
    depth = 1
    for line_index in range(start_line, len(lines)):
        line = lines[line_index]
        col = start_col if line_index == start_line else 0
        while col < len(line):
            character = line[col]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
            elif depth == 0:
                if character == ";":
                    return None  # `... );` is a declaration, not a definition
                if character == "{":
                    return (line_index, col)
            col += 1
    return None


def _match_brace(lines: Sequence[str], line_index: int, col: int) -> Optional[Tuple[int, int]]:
    """The line range from an opening brace to its closer."""
    depth = 0
    start_line = line_index
    while line_index < len(lines):
        line = lines[line_index]
        while col < len(line):
            character = line[col]
            if character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
                if depth == 0:
                    return (start_line + 1, line_index + 1)
            col += 1
        line_index += 1
        col = 0
    return None


def find_bodies(text: str, symbol: str) -> List[Tuple[int, int]]:
    """Brace-matched (start_line, end_line) for every definition of `symbol`.

    `text` must already be stripped (see strip_comments_and_literals). A
    declaration (`... );`) is skipped rather than mistaken for a body, and a
    parameter list containing `(` or `)` is walked with a paren depth so the
    body's own `{` is the one that is found.
    """
    pattern = _signature_pattern(symbol)
    lines = text.split("\n")
    bodies: List[Tuple[int, int]] = []
    for index, line in enumerate(lines):
        match = pattern.search(line)
        if match is None:
            continue
        opening = _first_open_brace(lines, index, match.end())
        if opening is None:
            continue
        closing = _match_brace(lines, opening[0], opening[1])
        if closing is not None:
            bodies.append(closing)
    return bodies


def regions_for(tree: str, entry) -> Tuple[List[Region], Optional[str]]:
    """Every declared region for one scope entry. `*` means the whole file.

    Returns (regions, error). An error is returned when the file does not exist
    (a setup problem: the sweep is pointed at the wrong tree) or when the
    declared symbol resolves to no body (a ledger problem: the entry names
    something that is not there any more).
    """
    full = os.path.join(tree, entry.path)
    if not os.path.isfile(full):
        return [], "no such file: %s" % entry.path
    with open(full, "r", encoding="utf-8", errors="replace") as handle:
        stripped = strip_comments_and_literals(handle.read())
    if entry.symbol == "*":
        return [Region(entry.path, "*", 1, len(stripped.split("\n")), "whole-file")], None
    bodies = find_bodies(stripped, entry.symbol)
    mode = "qualified"
    if not bodies and "::" in entry.symbol:
        # A member function defined INSIDE its class body is written unqualified
        # (include/AutomatableModel.h's incrementPeriodCounter is one). The
        # declared name is still the qualified one, so the fallback tries the
        # tail - and the resolution says which form it used, so a reader can
        # tell a real in-class definition from a wrong entry.
        bodies = find_bodies(stripped, entry.symbol.split("::")[-1])
        mode = "in-class"
    if not bodies:
        return [], ("the declared symbol resolves to no body in %s: %s"
                    % (entry.path, entry.symbol))
    return [Region(entry.path, entry.symbol, start, end, mode) for start, end in bodies], None
