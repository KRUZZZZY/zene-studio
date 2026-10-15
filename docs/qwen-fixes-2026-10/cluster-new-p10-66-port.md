# Cluster new-p10-66-port: undo-history persistence reversibility

**Verdict: FIXED** for the WF-033 finding (`project.set_undo_history`). The `control.set_undo_depth` half of the
earlier attempt (commit `556aa786b`) is **ALREADY-FIXED** on this line (see below). The fix is verified RED→GREEN,
but both ratchet gates exit 1 on items in files this change does not touch (see "Gates").

## Finding and evidence

`handleSet` in `src/core/ControlCommandsUndoHistory.cpp` turned persistence off through
`applySnapshotPolicy()`, which drops the snapshots of existing undo steps (`src/core/ProjectJournal.h:281`,
`src/core/ProjectJournalSnapshots.cpp:158-174`: with persistence off, every step's snapshot is reset). The
transaction nevertheless hard-coded `reversible: true`. Turning persistence back on cannot recapture those
snapshots, so the inverse does not restore what the call changed.

Before the fix the handler emitted `"reversible", true` unconditionally (base line 127); the reversibility
table row (`src/core/ControlReversibilityTableSnapshot.cpp`, `project.set_undo_history`) also says `true`
as the class default and is left unchanged (see follow-ups).

## What changed

- `src/core/ControlCommandsUndoHistory.cpp:72-81` - `savedSnapshotCount()`: counts undo steps whose
  snapshot is still held (`ProjectJournal::StepSnapshot::snapshot`, a `shared_ptr`).
- `src/core/ControlCommandsUndoHistory.cpp:127-133` - the count before and after `applySnapshotPolicy()`;
  `dropped = before - after`; `reversible = dropped == 0`.
- `src/core/ControlCommandsUndoHistory.cpp:143-151` - the transaction uses `reversible`; the mechanism
  text for the irreversible case names the number of dropped snapshots. The original text is kept for the
  reversible case.
- `tests/src/core/UndoHistoryPersistenceTest.cpp:152-155` - helper `lastTransaction()`.
- `tests/src/core/UndoHistoryPersistenceTest.cpp:555-580` (slot `aSettingThatDropsSnapshotsIsNotReversible`):
  - Off when persistence was never on: nothing to drop, record stays reversible (negative control).
  - On, two renames, then off: the record must be `reversible: false` (line 576) and `control.undo` must
    refuse with `ControlErrorKind::Irreversible`, leaving the setting `off`.

No new command, no new file (the test file was already registered in `tests/fork-sources.txt`,
`tests/all-sources.txt` and `tests/CMakeLists.txt`). No header was changed.

## Already-fixed part of commit 556aa786b

`control.set_undo_depth` already reports `reversible = dropped == 0` on this line
(`src/core/ControlCommandsUndo.cpp:178-201`, the mechanism branches at 181-187). Verdict for that half:
**ALREADY-FIXED**. Nothing was ported.

## Commands and results

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-66-port.r1/logs/`.

| Step | Exact command | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene UndoHistoryPersistenceTest -j2 > .../build-1.log 2>&1` | 2 | ccache could not write `/run/user/1000/ccache-tmp` (permission denied). Environment, not code. |
| Build, workaround | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-new-p10-66 cmake --build build --target zene UndoHistoryPersistenceTest -j2 > .../build-2.log 2>&1` | 0 | Built, test compiled with the regression (RED code). |
| **RED** | `cd build/tests && ctest -R UndoHistoryPersistenceTest --output-on-failure > .../test-1-red.log 2>&1` | **8** | Failed at **test line 576** (`reversible` is true; base code). |
| Build with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene UndoHistoryPersistenceTest -j2 > .../build-3-green.log 2>&1` | 0 | |
| GREEN, before plugin | `ctest -R UndoHistoryPersistenceTest ... > .../test-2-green.log` | 8 | Only `aSavedHistoryUndoesToEveryPreEditState` failed, on its plugin precondition (below). The new slot passed. |
| Plugin build | `cmake --build build --target tripleoscillator -j2 > .../build-5-plugin.log 2>&1` | 0 | Builds `build/plugins/libtripleoscillator.so`. |
| GREEN, full family | `ctest -R UndoHistoryPersistenceTest -V > .../test-4-green-verbose.log 2>&1` | **0** | `Totals: 7 passed, 0 failed` (all slots). |
| Mutation proof, revert | `git diff -- src/core/ControlCommandsUndoHistory.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 / 0 | Product file back to base. |
| Mutation, build | `cmake --build build --target UndoHistoryPersistenceTest -j2 > .../build-6-reverted.log 2>&1` | 0 | |
| **Mutation RED** | `cd build/tests && ctest -R UndoHistoryPersistenceTest -V > .../test-5-reverted-RED.log 2>&1` | **8** | `Totals: 6 passed, 1 failed`; failure at **test line 576**. |
| Reapply | `git apply .../fix.patch` | 0 | |
| Build, reapplied | `cmake --build build --target zene UndoHistoryPersistenceTest -j2 > .../build-7-reapplied.log 2>&1` | 0 | |
| **GREEN** | `cd build/tests && ctest -R UndoHistoryPersistenceTest -V > .../test-6-reapplied-GREEN.log 2>&1` | **0** | `Totals: 7 passed, 0 failed`. |

Affected-family test count (undo / reversibility / session checkpoint / undo panel / registry):
`ctest -R "UndoHonesty|UndoBounds|ReversibilityContract|ReversibilityUndo|SessionCheckpoint|UndoHistoryPanel|UndoHistoryPersistence|ControlRegistryTest"`
(`test-7-family.log`, EXIT 8, then the two Not-Run targets built in `build-9-notrun.log`).

- 11 ctest entries matched; 9 passed, 2 failed, both environment:
  - `UndoHonestyTest` (`test-8-undohonesty.log`): `armingSeveralTracksIsOneStep` cannot open a take file under
    `~/Documents/Zene Studio/` (write refused in this sandbox). Undo-history logic is not reached.
  - `ReversibilityUndoDeviceTest` (`test-9-notrun.log`): two tests need the `ladspaeffect` host plugin, not built
    in this targeted tree.
- `UndoBoundsTest`, `ReversibilityContractTest`, `ReversibilityUndoTest`, `SessionCheckpointTest`,
  `SessionCheckpointBudgetTest`, `UndoHistoryPanelTest`, `ControlRegistryTest`, `UndoHonestyStateTest`: pass.

Environment notes: `aSavedHistoryUndoesToEveryPreEditState` needs `libtripleoscillator.so`, which the targeted
build had not produced (`build/plugins/TripleOscillator/` held only CMake files). It now passes after
`cmake --build build --target tripleoscillator`. The test sets `LMMS_PLUGIN_DIR` itself; nothing was exported
globally.

## Gates

| Gate | Exact command | EXIT |
| --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check > .../gate-1-complexity.log 2>&1` | **1** |
| File length | `bash tests/file-length-gate.sh --check > .../gate-2-filelength.log 2>&1` | **1** |

Both fail on items this change does not touch. The complexity gate lists six `REGRESSION` lines
(`XyPadPairTest.cpp` `dotCentreAlong` CCN 16, `DawProjectReadTracks.cpp` `parseNotes` 13, `ControlCommandsMixerTools.cpp`
`adjust` 12, `ControlCommandsMixerStrip.cpp` `setStrip` 11, `ProjectRevisions.cpp` `rotateProjectRevision` 11,
`ControlCommandsShortcuts.cpp` `importedOverrides` 11). The file-length gate lists fourteen `REGRESSION` lines for
files such as `TakeLane.cpp` (520) and `CustomAccessTest.cpp` (556). I grepped both logs for
`ControlCommandsUndoHistory`, `handleSet`, `savedSnapshotCount` and `UndoHistoryPersistenceTest`: no matches.
The diff touches only two files (`git diff --stat`: 54 insertions, 4 deletions).

I did **not** run the gates on the unmodified base, so "pre-existing" rests on that file-scope argument. The
gate limits and baselines were not changed; I did not refactor the other clusters to clear them. The owner should
decide whether to re-anchor the baseline or clear those items on this line.

`git diff --check`: EXIT 0.

## Files committed

- `src/core/ControlCommandsUndoHistory.cpp`
- `tests/src/core/UndoHistoryPersistenceTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-66-port.md`

## Follow-ups (not done here)

- `src/core/ControlReversibilityTableSnapshot.cpp` still declares `project.set_undo_history` with the default
  verdict `true`. The per-call verdict is now correct; the table's default text should be aligned with
  `control.set_undo_depth`'s conditional row. Changing it moves descriptive text, so it was left for a separate change.
- The gate regressions listed above are on this line already; they are not caused by this fix.
