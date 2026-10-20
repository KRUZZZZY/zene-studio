# Cluster new-p8-17-port

**Verdict: FIXED (port, round 3 - provenance preserved on rollback).**

Finding: "A failed built-in document creation can leave the old session replaced"
(`src/core/ControlCommandsDocuments.cpp`, `newFromRecipe`).

## What changed in this round

Round 2 preserved undo/redo history on rollback but dropped the original session's
provenance log: `captureUndoSnapshot()` strips provenance from the snapshot, and
`restoreUndoSnapshot()` keeps the log current at restore time - which after
`project.new` is the new document's log, not the original one. Round 3 captures
the original session's provenance before `project.new` and restores it on rollback.

- `src/core/ControlCommandsDocuments.cpp`
  - Includes `DataFile.h` and `ProvenanceSection.h`.
  - `newFromRecipe()`: before `project.new`, serialize the current provenance
    section into a persistent `QDomDocument` (`originalLog`). In `restorePrevious`,
    after `restoreSession`/`reattachHistory`, clear the provenance section and
    `load(originalLog)` when it is non-empty, so the original entries and their
    sequence survive.
- `tests/src/core/ControlSuccessPathsTest.cpp`
  - Includes `ProvenanceSection.h`.
  - `failedBuiltInDocumentSaveKeepsCurrentSession` now seeds provenance via the
    pre-existing `track.add`, asserts a non-empty original log, and after the
    induced save failure asserts size and last seq are unchanged.

No new control commands; no header changes; `Song.cpp` unchanged.

## Evidence

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build (fix) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | |
| GREEN | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from build/tests) | 0 | 1/1 passed |
| Proof: reverse product | `git apply -R logs/fix.patch` | 0 | |
| Build (reversed) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | |
| RED | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from build/tests) | 8 | `provenanceSizeAfter == previousProvenanceSize` FALSE |
| Re-apply | `git apply logs/fix.patch` | 0 | |
| Build (fixed) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | |
| GREEN (final) | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from build/tests) | 0 | 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | pre-existing only; no new item in changed files |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | pre-existing only; no new item in changed files |
| feature_check | `python3 feature_check.py <worktree>` | 0 | all rows PASS |

Affected ctest family: `ControlSuccessPathsTest`, 1 registered test, 17 Qt slots;
RED 16 passed / 1 failed, GREEN 17 passed / 0 failed.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-17-port.md`
- `src/core/ControlCommandsDocuments.cpp`
- `tests/src/core/ControlSuccessPathsTest.cpp`

## Self-check

- Regression test fails with product change reverted and passes with it: RED exit code 8
  (`provenanceSizeAfter == previousProvenanceSize` FALSE), GREEN exit code 0.
- Deterministic: no sleeps/timing/races. Failure is induced by creating a directory
  named `mastering.mpt.new` so `DataFile::writeFile` cannot write its temp file;
  provenance is compared by size and last seq.
- Cause complete: only `newFromRecipe` changed; `restoreSession`/`restoreUndoSnapshot`
  signatures unchanged; siblings (`failedBuiltInDocumentKeepsCurrentSession`) untouched.
  `Song.cpp` not modified.
- No collateral damage: rollback still restores content, file, modified flag, document
  type, undo/redo history; provenance restored only on rollback. Success path
  unchanged.
- Verdict FIXED, not ALREADY-FIXED/FALSE-POSITIVE.
