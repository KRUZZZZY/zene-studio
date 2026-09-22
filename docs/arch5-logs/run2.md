# Gate 6 run2 (post-commit re-run) — 2026-09-22 — lane 9 (zene-arch5, 040/arch5-own)

Command: `bash tests/no-upstream-regression-gate.sh > docs/arch5-logs/run2.log 2>&1; echo EXIT=$?`
Result: **EXIT=0** — the gate stays green with the run1 evidence committed. `grep -c VIOLATION` = 0; no `ledger error`.

- New changed path vs run1: `docs/arch5-logs/run1.md` → verdict `docs (allowed)` (run2 output line 275).
- Machine diff of run2's raw output against run1's raw body (kept verbatim in run1.md): exactly one inserted line — the `docs/arch5-logs/run1.md  docs (allowed)` row — and nothing else removed or changed; the `426 declared / 464 ledger entries / PASS` summary is identical.
- `diff <body-of-run1> <run2>` reported only: `0a1` (header row, an off-by-one in the extraction range), `273a275 > docs/arch5-logs/run1.md ... docs (allowed)`, `1735d1736 < \`\`\`` (the fence the extraction range caught). No verdict line changed from allowed/declared to VIOLATION.

Raw tail of run2 verbatim:

```text
tools/wasm/wat2wasm.cpp                                  fork tooling (allowed by construction: tools/ does not exist upstream)
vcpkg.json                                               declared divergence -> wave R rename (018d2041f): the vcpkg manifest's "name" field lmms -> zene (the manifest's product identity, not a dependency).; wave R rename: vcpkg manifest name follows the CMake project rename
.yamllint                                                declared divergence -> wave R rename (018d2041f): the inline comment on the line-length rule still read 'be conforming to LMMS coding rules' -> 'Zene Studio coding rules'. Comment only; no rule, value or threshold changed.; wave R rename: yamllint ignore path follows the renamed desktop file (no runtime effect)

PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is declared
      (426 changed path(s) declared; the ledger holds 464 entries)
```

Ledger diff state for this wave: tests/upstream-modifications.txt untouched — 0 added lines, 0 touched-existing lines (0 violations to clear on this branch).

## Appendix: Gate 11 (tests/evidence-gate.sh) with run1.md committed

Run after commit 92b8dd113, unpiped: `bash tests/evidence-gate.sh; echo EXIT=$?` → **EXIT=0**.

```text
=== evidence gate: what this tree commits, beyond code ===
cap      : 1048576 bytes per file (EVIDENCE_SIZE_CAP_BYTES)
exempt   : tests/evidence-gate-exempt.txt (4 entry(ies))
tree     : git ls-files

evidence-gate: 6765 file(s) scanned, 0 refused (cap 1048576 bytes, 4 exemption(s))
PASS: no committed evidence file types and nothing over the cap.
```

This is the second reason the evidence is `.md`: Gate 11 refuses the `.log` suffix anywhere in the
tracked tree (tests/evidence-gate.sh:19-21), so `docs/arch5-logs/run1.log` would have failed both
Gate 6 (classifier, no-upstream-regression-gate.sh:92-111) and Gate 11. `run1.md` is ~340 KB, under
the 1 MiB cap; `run2.md` is ~2 KB. Gates 4/7/8 (complexity, file-length, duplication) read their
source manifests (`tests/all-sources.txt`, `tests/fork-sources.txt`), so `docs/arch5-logs/*.md` is
outside their scope.
