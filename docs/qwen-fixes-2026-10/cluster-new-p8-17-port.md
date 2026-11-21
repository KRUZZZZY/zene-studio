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
| GREEN | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 0 | `test-6-green.log`, 1/1 passed |
| Proof: reverse the product | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-17-port.r5/logs/fix.patch` | 0 | `fix.patch` = the round-3 product diff for `src/core/ControlCommandsDocuments.cpp` |
| Build (reversed) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `build-7-reversed.log` |
| RED | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 8 | `test-7-reversed.log`: `'sameEntries' returned FALSE. (a save-time failure did not give the original session's provenance back: 0 entries instead of 53)`, 16 passed / 1 failed |
| Re-apply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-17-port.r5/logs/fix.patch` | 0 | |
| Build (restored) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `build-8-restored.log` |
| GREEN (final) | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | 0 | `test-8-restored.log`, 1/1 passed |
| Slot count (final) | `QT_QPA_PLATFORM=offscreen ./ControlSuccessPathsTest` (from `build/tests`) | 0 | `test-10-slots.log`: `Totals: 17 passed, 0 failed, 0 skipped` |
| feature_check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `feature-check-3.log`: 8 rows PASS, `RESULT: PASS` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log`: 6 REGRESSION lines, none in `src/core/ControlCommandsDocuments.cpp` or `tests/src/core/ControlSuccessPathsTest.cpp` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log`: 14 REGRESSION lines, none in the two files above (`ControlSuccessPathsTest.cpp` measures 446 code lines, under the 500 limit) |

Affected ctest family: `ControlSuccessPathsTest` - 1 registered ctest, 17 Qt
functions. RED 16 passed / 1 failed, GREEN 17 passed / 0 failed.

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

Each item of the completion check, answered with its evidence.

1. **The regression test FAILS with the product change reverted and PASSES with
   it (exit codes).** RED: `ctest -R '^ControlSuccessPathsTest$' --output-on-failure`
   after `git apply -R .../logs/fix.patch` -> EXIT 8,
   `'sameEntries' returned FALSE. (a save-time failure did not give the original
   session's provenance back: 0 entries instead of 53)`, 16 passed / 1 failed
   (`test-7-reversed.log`). GREEN: the same ctest after
   `git apply .../logs/fix.patch` -> EXIT 0, 1/1 passed (`test-8-restored.log`);
   the identical run before the reversal was also EXIT 0 (`test-6-green.log`).
   The reversal takes away exactly the round-3 product hunk (`logs/fix.patch` =
   the diff of `src/core/ControlCommandsDocuments.cpp` between `9057d23ef` and
   `40d64819b`); the test file is never reverted.
2. **The test is deterministic: no wall-clock timing, sleeps or races (name the
   seam or injected values it uses).** The failure is induced by a filesystem
   seam, not by time: the test makes `mastering.mpt.new` a DIRECTORY inside the
   cache template folder (`tests/src/core/ControlSuccessPathsTest.cpp:489-491`),
   so `DataFile::writeFile` cannot write its temporary file and the save
   refuses. The rollback's effect is then read from the session itself (the
   tracks, the file name, the modified flag, the document type, the journal, the
   provenance log). No sleep, no timer, no thread, no timestamp of the test's
   own is used.
3. **The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent.** The change is in the test only.
   The product path it covers - `newFromRecipe`
   (`src/core/ControlCommandsDocuments.cpp:200-255`) - has one caller
   (`newDocument`, line 264) and three failure exits, and each of them is
   covered: the preflight refusal (`recipeTemplateWritable`, line 205) by
   `failedBuiltInDocumentKeepsCurrentSession`, the post-`project.new` refusals
   (lines 250, 252, 254) by `failedBuiltInDocumentSaveKeepsCurrentSession`. The
   sibling entry points of `newDocument` - the user's own template
   (`<template dir>/<type>.mpt`) and the default template - do not build a
   recipe and do not write a cache template, so they are unaffected.
   `ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate` (the success
   path of the same command) still passes. No header changed, no signature
   changed, `Song.cpp` unchanged, no new control command.
4. **No collateral damage: old projects and saved data still load as before, and
   any limit or check you added does not hit other callers that legitimately
   exceed it.** `src/core/ControlCommandsDocuments.cpp` is byte-identical to
   HEAD (`git diff --exit-code -- src/core/ControlCommandsDocuments.cpp` ->
   EXIT 0), so no product behaviour, no saved file and no old project changes.
   No limit or check was added to any product path. The test's own additions
   read the provenance section and compare it; they add no threshold.
5. **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
   the intended contract that shows it.** Not applicable - the verdict is
   FIXED. The finding is real: `newFromRecipe` replaced the session before it
   knew the cache template could be written
   (`src/core/ControlCommandsDocuments.cpp:249-252`), and the branch carries the
   fix (preflight at lines 205, rollback at lines 235-247, provenance capture
   and restore at lines 222-234 and 242-244). This round closes the review's
   last gap: the proof that the original provenance CONTENT comes back.

