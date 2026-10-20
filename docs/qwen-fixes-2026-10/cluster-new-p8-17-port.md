# Cluster new-p8-17-port

**Verdict: FIXED (port, round 5 - the regression proves the provenance CONTENT survives the rollback, not only its count).**

Finding: "A failed built-in document creation can leave the old session replaced"
(`src/core/ControlCommandsDocuments.cpp`, `newFromRecipe`).

## What changed in this round

Luna's review of the previous attempt (round 3) asked for one thing: the
regression only compared the provenance log's COUNT and newest sequence, which
does not show that the original ENTRIES survived - a rollback that keeps the
count but swaps the entries for the new document's would have passed.

This round changes the regression, not the product: the fix is already on the
branch (rounds 1-3) and is unchanged.

- `tests/src/core/ControlSuccessPathsTest.cpp`
  - New helper `provenanceEntries()` (line 100): every `provenance::Change` the
    document's log holds, in order.
  - `failedBuiltInDocumentSaveKeepsCurrentSession` snapshots the whole log
    before the induced save failure (line 519) and asserts the newest entry is
    the seeded `track.add` change (line 523).
  - After the rollback it compares EVERY entry with `provenance::Change`'s
    content equality (`operator==`, `include/ProvenanceSection.h:71`: seq, at,
    actor, instance, command, target, before, after, nested) - lines 562-579.
    The first differing entry is named in the failure message, and the count
    has to match as well.
  - The seeded `track.add` entry is named again after the rollback (line 584),
    so the test states in words that the entry it planted is the one that came
    back.
  - The earlier count/sequence assertions are kept (lines 580-583) - the
    entry-by-entry comparison is added on top of them, not in place of them.

The product is untouched in this round: `src/core/ControlCommandsDocuments.cpp`
is byte-identical to HEAD (`git diff --exit-code -- src/core/ControlCommandsDocuments.cpp`
-> EXIT 0). The provenance capture/restore it carries is the one the reversal
proof below takes away.

## The product fix this round's regression covers

- `src/core/ControlCommandsDocuments.cpp:157-169` - `recipeTemplateWritable()`:
  the cache destination is probed BEFORE `project.new` can discard the session.
- `src/core/ControlCommandsDocuments.cpp:200-255` - `newFromRecipe()`: the
  session is captured as an undo snapshot first, and every later failure
  restores it (`restoreSession`, `restoreUndoSnapshot`).
- `src/core/ControlCommandsDocuments.cpp:222-234` - the session's provenance
  log is serialised into a persistent `QDomDocument` before `project.new`,
  because `Song::restoreUndoSnapshot` keeps the log CURRENT AT RESTORE TIME
  (`src/core/SongUndoSnapshot.cpp:128-129`) - the new document's log, not this
  session's.
- `src/core/ControlCommandsDocuments.cpp:242-244` - on a successful rollback
  the section is cleared and the original element loaded, so the original
  entries and their sequence come back.

## Evidence

All commands were run from the worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-17-port`;
logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-17-port.r5/logs/`.

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build (fix) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `build-6-green.log` |
| GREEN | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 0 | `test-6-green.log`, 1/1 passed, 17/17 slots |
| Proof: reverse the product | `git apply -R logs/fix.patch` | 0 | `fix.patch` = the round-3 product diff for `src/core/ControlCommandsDocuments.cpp` |
| Build (reversed) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `build-7-reversed.log` |
| RED | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 8 | `test-7-reversed.log`: `'sameEntries' returned FALSE. (a save-time failure did not give the original session's provenance back: 0 entries instead of 53)`, 16 passed / 1 failed |
| Re-apply | `git apply logs/fix.patch` | 0 | |
| Build (restored) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `build-8-restored.log` |
| GREEN (final) | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 0 | `test-8-restored.log`, 1/1 passed |
| feature_check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `feature-check-2.log`: 8 rows PASS, `RESULT: PASS` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log`: 6 REGRESSION lines, none in `src/core/ControlCommandsDocuments.cpp` or `tests/src/core/ControlSuccessPathsTest.cpp` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log`: 14 REGRESSION lines, none in the two files above (`ControlSuccessPathsTest.cpp` measures 446 code lines, under the 500 limit) |

Affected ctest family: `ControlSuccessPathsTest` - 1 registered ctest, 17 Qt
slots. RED 16 passed / 1 failed, GREEN 17 passed / 0 failed.

`ArrangingCatchUpTest` (the other test that drives `project.new_document`,
`tests/src/core/ArrangingCatchUpTest.cpp:255`) was run as well
(`test-5-family.log`): its `newDocumentsCarryTheirTypesTemplate` slot passes;
its `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` slot fails with
`Actual (track->loadInstrument(...)->nodeName()): "dummyinstrument"` because
this build tree has no plugin shared objects (`find build/plugins -name '*.so'`
-> nothing; the log shows "The plugin \"tripleoscillator\" wasn't found").
That failure is environmental and pre-existing - it names no file this cluster
touched, and the product sources are at HEAD.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-17-port.md`
- `tests/src/core/ControlSuccessPathsTest.cpp`

## Self-check

- **Regression test fails with the product change reverted and passes with it**:
  RED exit 8 (`'sameEntries' returned FALSE ... 0 entries instead of 53`),
  GREEN exit 0. The reversal takes away exactly the round-3 product hunk
  (`logs/fix.patch`, the diff of `src/core/ControlCommandsDocuments.cpp` between
  `9057d23ef` and `40d64819b`); the test file is never reverted.
- **Deterministic**: no sleeps, no wall-clock, no races. The failure is induced
  by making `mastering.mpt.new` a DIRECTORY in the cache template folder, so
  `DataFile::writeFile` cannot write its temporary file; the log is compared
  entry by entry on content, with no timestamp of the test's own.
- **The content is what is compared, not the count**: the snapshot is the whole
  `QList<provenance::Change>` and the comparison is `provenance::Change`'s
  `operator==` (`include/ProvenanceSection.h:71`), which covers seq, at, actor,
  instance, command, target, before, after and nested - the seeded
  `track.add` entry among them (asserted to be the newest entry before the
  failure and after the rollback).
- **Cause complete**: the product change is in `newFromRecipe` only; its two
  siblings (`failedBuiltInDocumentKeepsCurrentSession`, the preflight path) and
  the other entry into it (`newDocument`'s user-template and default-template
  paths) are untouched and still pass. `Song.cpp` unchanged, no header changed,
  no new control command.
- **No collateral damage**: the change is in the test only. The product file is
  byte-identical to HEAD; old projects and saved data are unaffected.
- **Verdict FIXED**, not ALREADY-FIXED / FALSE-POSITIVE: the finding is real
  (`newFromRecipe` replaced the session before it knew the template could be
  written) and the branch carries the fix; this round closes the review's last
  gap, the proof that the original provenance CONTENT comes back.
