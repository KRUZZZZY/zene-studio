# MERGE-7 REPORT — 030/docs-agree + 030/crashbot into release/0.3.0

**Date:** 2026-09-19. **Worktree:** `zene-030`. Merged by the merge train (`deleg_44bea437`);
completed by the parent after the train stopped at an approval-gate denial (the user then consented).

## What merged

- `b20fb306ec6abf8b027d8181759bbdaabbb668b9` = `--no-ff` merge of **030/docs-agree** (`7742feeb7`,
  8 commits — the 67-row claim table, corrections to README / STATUS / KNOWN-LIMITATIONS /
  RELEASE-NOTES, the promise sentence added to all four documents).
- `d408e35076a3c6b69b04b583c975fb76fc2501b1` = `--no-ff` merge of **030/crashbot** (`7e1a1aa7b`,
  7 commits — tools/crashbot v0 + the 44-pack T3 pilot, its 616-case ledger and report).
- Net diff `b89d10a62..d408e3507`: 59 files, +9885/−70. Conflict-marker sweep clean; both branch
  tips verified ancestors.
- **README auto-merge audited against BOTH parents**: reverse-applying each parent's patch to the
  merged file reproduces the other parent byte-identically — every version-bump hunk and every
  docs-agree correction survived; promise sentence present 1/1/1/2 across the four documents.

## The bar on the merged tip (parent-run, after the stop)

```
re-configure + rebuild (merge train)   EXIT=0
focused ctest (ZeneApiBoundary | ReversibilityContractTest | ControlMcpGroupCoverage)
                                       3/3 passed, EXIT=0
full ctest -j4 --output-on-failure     100% tests passed, 0 failed out of 214  (145.04 s, EXIT=0)
run-all-gates.sh                       EXIT=3 PASS-WITH-SKIPS — 12 of 13 PASS (gates 1, 3–13
                                       incl. 5 mutation and 13 scripted-checks); gate 2 coverage
                                       skipped by policy; tree clean after the mutation sweep
```

The train's stop was an approval-gate denial on the focused ctest; the parent re-ran the chain with
the owner's consent. The commands carried the tree's vendored wasmtime `LD_LIBRARY_PATH` (the
scanner flags the variable CRITICAL; it points at `third_party/wasmtime/lib` and the built binary
will not start without it).

## CI context at this merge

- **Run #9 (`b89d10a62`, the pre-merge tip) concluded FULLY GREEN**: 7/7 build jobs plus
  checks / quality-gates / doxygen — including **linux-arm64 SUCCESS** (the last verdict) and every
  platform's release-version-guard PASS. **The 0.3.0-alpha version bump is CI-proven end to end.**
- This commit is pushed as **run #10** (`d408e3507` → `product/release/0.3.0`). The next CI run is on
  a tip carrying: the version bump + docs corrections + crashbot tooling and its first pilot. No tag
  — the tag remains the owner's act, on a commit whose own run is green.
