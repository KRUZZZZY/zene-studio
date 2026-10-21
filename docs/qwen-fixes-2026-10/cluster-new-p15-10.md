FIXED

# Cluster new-p15-10: applying unchanged Console settings still adds an undo step

Finding (Luna patrol): `mixer.console_apply` with settings that existing Consoles already carry, and no Console to add,
replies `changed: 0` but records an undo step and marks the project modified.

Branch `bugs/qwen-new-p15-10`, base `2da2ed356`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-10.r1/logs/`.

## Evidence (original code, base 2da2ed356)

- **Undo step recorded for every write**: `src/core/ControlCommandsDeviceTools.cpp:84-90` (base) - `writeModelsRecorded`
  checkpointed every supplied model with `addUndoStep` and wrote every value, with no check against the value the model
  already had. `consoleApply` (`:291-309` base) took its `changed` count from a separate `countChangedWrites` but still
  passed every write on.
- **Project marked modified unconditionally**: `src/core/ControlCommandsDeviceTools.cpp:306` (base) -
  `Engine::getSong()->setModified();` with no condition.
- **Second source of the step, found by the RED run**: `consoleTemplate` (`:224-234` base) builds the requested Console on a
  scratch chain. Fixing `writeModelsRecorded` alone left the undo depth at 2 (test-3, test-7 logs), so the step was coming
  from building the scratch Console. A scratch `setValue` that is not journalled removed nothing (test-7). Muting the
  project journal around the scratch build removed it (test-8: undo depth 1). The mechanism is the same one the registry
  uses: `ControlRegistry.cpp:295-300` (`changedNothing`) keeps any command step whose handler pushed a checkpoint
  (`step != 0`), and `AutomatableModel::setValue` / `JournallingObject::addJournalCheckPoint` push checkpoints for objects
  that are not in the project (`src/core/AutomatableModel.cpp:395-397`, `src/core/JournallingObject.cpp:70-76`,
  `src/core/ProjectJournal.cpp:202-220`). The existing guard for the same situation is `ScopedJournalMute` in
  `src/core/ControlChainPresetSupport.cpp:110-130`.
- **Sibling with the same bug**: `plugin.randomise` / `plugin.blend` `writeValues` (`src/core/ControlCommandsDeviceParams.cpp:294`
  base) pre-filters raw equal values, then reported `changed = writes.size()` and marked the song modified from that list.
  It now takes the count `writeModelsRecorded` actually recorded.
- Note: `docs/bunny-fixes-2026-10-05/cmds1/batch-09.md:91` says `writeModelsRecorded()` already had the no-op rule. At base
  it did not (the rule was only in `writeValues`). After this fix it does.

## The fix

1. `writeModelsRecorded` (`src/core/ControlCommandsDeviceTools.cpp:85-98`) skips every write whose clamped value is the value
   the model already has. It records one undo step for the rest, writes them, and returns how many models changed. The
   declaration in `src/core/ControlDeviceTools.h:53-57` matches (`int`, documented).
2. `consoleApply` (`src/core/ControlCommandsDeviceTools.cpp:325-326`) takes `changed` from `writeModelsRecorded`, and marks
   the project modified only when a Console was added or a setting changed (`added > 0 || changed > 0`). The now-unused
   `countChangedWrites` is removed (it was the same predicate).
3. `consoleTemplate` (`src/core/ControlCommandsDeviceTools.cpp:233-268`, the guard class at `:233-254`) builds the scratch Console under a file-local
   `ScopedJournalMute` (the same guard as `ControlChainPresetSupport.cpp:110-130`). The scratch Console is not in the
   project, so building it takes no undo step. The mute is restored on return.
4. `writeValues` (`src/core/ControlCommandsDeviceParams.cpp:294-297`) uses the count `writeModelsRecorded` returned, for both
   `changed` and the modified flag.

Behaviour that does not change: a Console apply that changes something still takes exactly one undo step, and
`added`/`changed` keep their meaning. `plugin.randomise` / `plugin.blend` change only for a value outside the parameter's
range that clamps to the value the model already has: before, that was an undo step and `changed: 1`; now it is neither.
No save format, command id or snapshot changed.

Known limit, not changed: a value within float epsilon of a range bound is still compared on its clamped value, the same
rule the existing `changed` count used. `AutomatableModel::fittedValue` (which `setValue` snaps with) is protected, so the
control layer cannot call it. The console Drive model has no strict step size, so the snap is epsilon-only.

## Regression test

`tests/src/core/DeviceDspTest.cpp:954-981` - `DeviceDspTest::applyingTheSameSettingsTakesNoUndoStepAndLeavesTheProjectClean`
(adds `#include "ProjectJournal.h"` at line 142). It creates a Console on each channel, saves the project to a `QTemporaryDir`
file and `noteProjectSaved`, then reapplies the same drive. It asserts `added == 0`, `changed == 0`, the undo depth
unchanged, and the project still clean. No wall-clock timing, sleeps or races. The same method as `MixerToolsTest`
(`aClampOnlyAdjustmentDoesNothing`).

Note on the RED run: the first RED attempt (`test-1-red.log`, EXIT 8) skipped the test ("this build has no console module"),
because no plugin shared library was built in this tree. That run proves nothing and is not used as evidence. I built
the `console` plugin target (`build-3-console-plugin.log`, EXIT 0) and the RED below is on that build.

## Commands and exit codes

Build directory `build/`. All runs from the worktree or `build/tests` as stated. `ccache` could not write its temp file
under `/run/user/1000` (permission), so later builds used `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp` (environment only; first
attempt `build-1-red.log` EXIT 2 for that reason).

| Step | Command | EXIT | Result |
|---|---|---|---|
| RED (original product code, console plugin built) | `cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure` | **8** | `applyingTheSame...` FAIL: `Actual (journal->undoDepth()): 2`, `Expected: 1`, `DeviceDspTest.cpp(979)` (`test-2-red.log`) |
| Proof, revert product diff | `git diff -- <3 product files> > fix.patch`; `git apply -R fix.patch`; `cmake --build build --target zene DeviceDspTest -j4` | 0; 0; 0 | reverted state (`build-9-proof-red-unfixed.log`) |
| Proof, RED on reverted code | `ctest -R '^DeviceDspTest$' --output-on-failure` | **8** | same assertion, `Actual 2`, `Expected 1`, line 979 (`test-13-proof-red-unfixed.log`) |
| Proof, restore | `git apply fix.patch`; `cmake --build build --target zene DeviceDspTest -j4` | 0; 0 | fix restored (`build-10-proof-green-fixed.log`) |
| Proof, GREEN on restored fix | `ctest -R '^DeviceDspTest$' --output-on-failure` | **0** | (`test-14-proof-green-fixed.log`) |
| Final build (zene + 14 Device family test targets) | `cmake --build build --target zene <14 targets> -j4` | **0** | `build-11-final.log` |
| Affected family, count | `ctest -N -R 'Device'` | 0 | **17 tests** |
| Affected family, run | `ctest -R 'Device' --output-on-failure` | **0** | **17 of 17 passed** (`test-15-final-family.log`) |
| New test, verbose | `ctest -R '^DeviceDspTest$' -V` | **0** | `PASS : DeviceDspTest::applyingTheSame...()`; `Totals: 32 passed, 0 failed, 8 skipped` (`test-16-final-dspverbose.log`) |

The 8 skips in `DeviceDspTest` are "this build has no X module" for modules not built in this tree (de-esser, transient
shaper, vocoder, dynamic EQ, convolution reverb, recorder); none is a console test. Three family tests needed their fixture
plugins (`patman`, `frequencyshifter`, `bassbooster`) built (`build-8-plugins.log`, EXIT 0) before they could run
(`test-10-family.log` EXIT 8 on the missing modules, then `test-12-family-plugins.log` EXIT 0 with 17/17).

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - **EXIT 1, no gate ran**:
the script creates `<worktree>-feature-check/` next to the worktree, in
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/`, and that directory is not writable from this
session (`PermissionError`, `gate-feature-check-1.log`). I did not work around it.

So I ran each gate that `feature_check.py` lists, from the worktree, with the same arguments, and wrote each log to the
run folder:

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 - see below |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 - see below |

Ratchets: the brief says both are red on the train. What `feature_check` fails on is a *new* item in a file this branch
changed. The base listing could not be produced here, because it needs a `git worktree add` in the shared `zene-cachyos`
repository, so I compared by hand:

- complexity: 6 REGRESSION items (`XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerTools`,
  `ControlCommandsMixerStrip`, `ProjectRevisions`, `ControlCommandsShortcuts`). None is in a file this branch changed.
- file-length: 15 REGRESSION items. One is in a changed file: `tests/src/core/DeviceDspTest.cpp (837)`. Its base has
  **812** code lines by the gate's own counting rule (`git show HEAD:tests/src/core/DeviceDspTest.cpp`, same awk as
  `tests/file-length-gate.sh:109-131`). So the same key (`new file over 500 lines: tests/src/core/DeviceDspTest.cpp`, which
  `feature_check.key()` strips numbers from) is already in the base listing. It is not new. My change added 25 code lines
  to a file that was already over the limit.

No new item in a changed product file. The ratchet exit 1 is the train's known state, not something this branch added.

## Self-check

Each ASK item of `submit_check.py`, answered with its evidence:

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes. Reverted:
   `test-13-proof-red-unfixed.log`, `ctest -R '^DeviceDspTest$'` EXIT **8**, `Actual 2 / Expected 1 / DeviceDspTest.cpp(979)`.
   Restored: `test-14-proof-green-fixed.log`, EXIT **0**. The reverted state is the whole product diff (the three product
   files), reversed with `git apply -R` and reapplied from `fix.patch`.
   Honest limit: the RED stops at the undo-depth assertion (line 979), the first that can fail. The clean-project
   assertion (line 980) is not reached on the reverted code, so I did not observe it fail. The original code sets the
   modified flag unconditionally (`ControlCommandsDeviceTools.cpp:306` at base), so it would fail too; that is read from
   the code, not run.
2. **The test is deterministic (no wall-clock timing, sleeps or races; name the seam or injected values).** Yes. There is no
   timing and no concurrency. The test runs the commands on the test thread, with the same registry and mixer as the other
   console tests. Its only fixture is a `QTemporaryDir` file, saved with `saveProjectFile` and marked clean with
   `noteProjectSaved` (the method `MixerToolsTest::aClampOnlyAdjustmentDoesNothing` uses). The inputs are fixed (drive 40).
3. **The fix is at the cause and complete (every caller and sibling checked and consistent).** `writeModelsRecorded` has
   two callers, both updated: `consoleApply` (`ControlCommandsDeviceTools.cpp:325`) and `writeValues`
   (`ControlCommandsDeviceParams.cpp:294`). The scratch build (`consoleTemplate`, `:257`) has one caller, `consoleApply`.
   `removeConsoles` already set modified only when something was removed (the same rule). `placeConsoles` is unchanged
   and still reports `added`. The cause is the undo step itself, not the reply text.
4. **No collateral damage (old projects load as before; any limit or check added does not hit legitimate callers).** No
   save or load code, no file format and no limit or check were added. A real Console change still takes one step and the
   same `added`/`changed` values (`oneConsoleApplyIsOneUndoStep`, `theConsoleGoesOnEveryChannel` pass in test-15). The
   only changed outcome is a no-op write (undo step and modified flag removed), and for `plugin.randomise` / `plugin.blend`,
   a value outside its range that clamps to the value the model already has (before: step and `changed: 1`; now: neither).
5. **Any alternative contract for the no-op case (ALREADY-FIXED or FALSE-POSITIVE evidence).** Not applicable: the verdict is
   FIXED. Evidence that the defect was real at base: `ControlCommandsDeviceTools.cpp:306` (base) and RED test-2.
6. **Sibling commands in the command files changed.** The check lists the `rack.*` group, because `rack.split` is
   registered in `ControlCommandsDeviceTools.cpp:392`. None of those commands calls `writeModelsRecorded`, `consoleTemplate`,
   or `ScopedJournalMute`; those are called only from `consoleApply` (`:322`, `:325`) and `writeValues`
   (`ControlCommandsDeviceParams.cpp:294`). The `mixer.console_apply` path (removal included) and `plugin.randomise` /
   `plugin.blend` are the consistent siblings, and both are covered above.
7. **Registration and gates.** The product files are registered already (`fork-sources` and `no-upstream-regression` gates
   exit 0; no new file was added, so `tests/fork-sources.txt` / `tests/upstream-modifications.txt` need no change). The
   six simple gates exit 0 when run by hand. The two ratchets exit 1, with no new item in a changed file (see Gates).
   **The `feature_check.py` gate itself still fails here** (`PermissionError` creating
   `zene-qwen-new-p15-10-feature-check` next to the worktree, outside this session's write-jail). That item is blocked
   from this session; the other checks and the hand-run gates are the evidence I have.

## Submit check

`submit_check.py new-p15-10` (`submit-check-1.log`, EXIT 1) after commit `c82e795dc`:

- PASS: commits on the branch; working tree clean; report present; tests changed (`DeviceDspTest.cpp`).
- FAIL: `gates (feature_check.py)`. Cause: `PermissionError` on the gate's log folder, described above. Not a code result.
- The six ASK items are answered in the Self-check section above.
