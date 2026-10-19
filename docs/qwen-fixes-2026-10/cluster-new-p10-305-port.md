# Cluster new-p10-305-port — quoted tag boundary

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-new-p10-305-port`).

## Evidence

The finding pointed at `src/core/ProjectContainerEntries.cpp` (historical line 248):
a `>` inside a quoted content-element attribute was treated as the end of the
start tag, and the `/` before it made a non-empty element look self-closing.

The newest line already contains the quote-aware boundary scan:

- `src/core/ProjectContainerEntries.cpp:211-234` — `findStartTagEnd()` walks from
  `tagStart + 1` while tracking single- and double-quoted attribute values, and
  only returns on an unquoted `>`.
- `src/core/ProjectContainerEntries.cpp:273` — `walkToContentStart()` calls
  `findStartTagEnd( bytes, tagStart )` and then checks `bytes.at( tagEnd - 1 ) == '/'`,
  so a `/` that lives inside a quoted attribute value no longer trips the
  self-closing refusal.

This landed on the newest line via commit `5c96df06b fix: quote-aware container
reassembly (cluster new-p8-184)`, which is an ancestor of this branch's HEAD
(`ded355440`). The accepted fix for this cluster (commit `de9850b7c` on
`bugs/qwen-new-p10-305`) added the same quote-tracking helper; the newest line
already had it, so there was nothing to re-apply beyond the regression test.

The regression test was added to lock the behaviour:

- `tests/src/core/ProjectContainerEntriesTest.cpp` —
  `derivingAcceptsSlashAndGreaterThanInsideContentAttribute()` builds a document
  with `<song data="/>value">`, derives its section entries, reassembles, and
  asserts raw byte identity.

## Verification

Commands run in the worktree root unless noted. `EXIT` is the captured exit code.

| Stage | Working directory | Exact command | EXIT |
| --- | --- | --- | ---: |
| Build regression test (initial) | worktree root | `cmake --build build --target ProjectContainerEntriesTest > LOGS/build-1.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with current (fixed) sources | `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | 0 |
| Verbose (15 cases) | `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure --verbose > LOGS/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Reverse proof: make `findStartTagEnd` quote-unaware | worktree root | edit to drop quote tracking; `cmake --build build --target ProjectContainerEntriesTest > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 |
| RED after removing quote tracking | `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > LOGS/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Restore quote tracking | worktree root | edit restored; `cmake --build build --target ProjectContainerEntriesTest zene > LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure --verbose > LOGS/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | worktree root | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | worktree root | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo EXIT=$?` | 1 |

The RED log reports `the <song> element is empty, so there is no interior to
place sections into`, confirming the regression test fails without the
quote-aware scan and passes with it. The final verbose run reports
`ProjectContainerEntriesTest::derivingAcceptsSlashAndGreaterThanInsideContentAttribute() PASS`.

Both ratchet gates exit 1 on this branch in their pre-existing state (verified
identical failure on an unmodified tree); the failures are not introduced by
this change, which touches only the test file and this report. No re-anchor was
performed.
