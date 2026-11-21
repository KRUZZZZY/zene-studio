**Verdict: FALSE-POSITIVE** (for the scenario as written; one narrower residual is recorded below and not changed)

# Cluster new-p12-284: routing cannot restore a child's "no channel" state

The finding says `-1` is a valid prior mixer channel for a child and is also the "no binding" result of `savedChannelFor()`, so `unrouteChild()` skips a child whose prior channel was `-1`. The collision of the two meanings is real in the data structure. The scenario is not: no live child can hold channel `-1`, so it cannot be routed from `-1` and cannot be restored to it.

## Evidence (file:line in this worktree)

**1. A child's mixer channel cannot be `-1`.** Both child track types create their channel model with minimum 0 and set the range from 0:

- `src/tracks/InstrumentTrack.cpp:70` `m_mixerChannelModel(0, 0, 0, ...)`; `:86` and `:1080` `setRange(0, numChannels()-1)`.
- `src/tracks/SampleTrack.cpp:60` `m_mixerChannelModel(0, 0, 0, ...)`; `:66` and `:340` `setRange(0, numChannels()-1)`.
- `src/gui/MixerView.cpp:432` and `:438` `setRange(0, ...)`.
- `src/tracks/TrackFolder.cpp:304` and `:420` widen or shrink with `model->minValue()`, which is 0.

Every write to the model is fitted into `[minValue, maxValue]`:

- `src/core/AutomatableModel.cpp:415` `setValueInternal`: `m_value = fittedValue(value);`
- `src/core/AutomatableModel.cpp:504` `value = std::clamp(value, m_minValue, m_maxValue);`
- `src/core/AutomatableModel.cpp:514` `return std::clamp(value, m_minValue, m_maxValue);`

So `setValue(-1)` on a child stores `0`. The finding's "route a track with mixer channel -1" cannot be produced through any code path. Routing records only fitted values: `src/tracks/TrackFolder.cpp:306` `recordBinding( child->id(), model->value() )`.

**2. The sentinel is documented as "no binding".** `include/TrackFolder.h:171-172` ("…or -1 when no binding was recorded for it") and `src/tracks/TrackFolder.cpp:269-275` (`savedChannelFor` returns `-1` when no binding matches). `unrouteChild` at `:260-267` correctly does nothing for "no binding".

**3. Residual, not changed (NEEDS-CONTEXT on the contract).** The only way to store a `-1` binding is a hand-edited or corrupt `prevch` attribute. `parseBindings` (`src/tracks/TrackFolder.cpp:73-96`) checks that both halves convert (`:83-92`) but has no lower bound, so `prevch="3:-1"` loads as a `-1` binding. `unrouteChild` then returns at `:265`, leaving the child on the folder channel. The existing contract for malformed pairs (`tests/src/core/TrackFolderTest.cpp` `aMalformedSavedBindingIsDiscardedNotTakenAsChannelZero`) discards non-numeric pairs; it does not say what a negative channel should restore to. Changing that is a product decision, so no change was made here.

## What changed

- `tests/src/core/TrackFolderTest.cpp`: one new test, `aChildsMixerChannelCannotHoldMinusOneSoTheSentinelIsNeverALiveBinding` (`:595`). It sets a child's model to `-1` and checks it reads back `0` and that the model minimum is `0`. It routes that child (channel 0), checks `savedChannelFor(child)` is `0`, releases routing, and checks the child is back on `0`. It pins the contract the finding assumes away. It uses no clock, sleep or thread.
- No product file changed. No fix was made, because the reported scenario does not occur.
- This report: `docs/qwen-fixes-2026-10/cluster-new-p12-284.md`.

## RED/GREEN for the new test (a premise guard, not a fix)

There is no product fix to revert, so the usual RED-then-GREEN proof does not apply. Instead I checked that the test is not vacuous: I temporarily removed both range clamps in `AutomatableModel.cpp` (the mutation saved as `logs/mutation.patch`), confirmed the test fails on the expected assertion, reversed the mutation, and confirmed the test passes again.

## Evidence table

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-284.r1/logs/`.

| Step | Exact command (cwd = worktree unless noted) | EXIT |
| --- | --- | --- |
| Range and clamp evidence | `rg -n "setRange\|m_mixerChannelModel\(\|…" src/tracks/... src/gui/MixerView.cpp src/tracks/TrackFolder.cpp` plus clamp lines → `logs/evidence-1.log` | 0 |
| Feature gate script (as instructed) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → `logs/gates-1.log` | **1** (not a gate result: it failed on `PermissionError` creating `<worktree>-feature-check/`, a sibling directory outside the write jail, at `feature_check.py:71`) |
| Gate fork-sources (direct) | `bash tests/fork-sources-gate.sh` → `logs/gate2-fork-sources.log` | 0 |
| Gate all-sources-reproduce | `bash tests/all-sources-reproduce.sh` → `logs/gate2-all-sources-reproduce.log` | 0 |
| Gate no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` → `logs/gate2-no-upstream-regression.log` | 0 |
| Gate check-namespace | `tests/scripted/check-namespace` → `logs/gate2-check-namespace.log` | 0 |
| Gate no-tautology | `bash tests/no-tautology-gate.sh` → `logs/gate2-no-tautology.log` | 0 |
| Gate doc-figures | `python3 tests/doc-figures-gate.py` → `logs/gate2-doc-figures.log` | 0 |
| Ratchet complexity (red on train) | `bash tests/complexity-gate.sh --check` → `logs/gate2-complexity.log` | 1 (6 REGRESSION items, none in a changed file) |
| Ratchet file-length (red on train) | `bash tests/file-length-gate.sh --check` → `logs/gate2-file-length.log` | 1 (14 REGRESSION items, none in a changed file) |
| Build attempt 1 | `cmake --build build --target TrackFolderTest -j4` → `logs/build-1.log` | 2 (ccache could not write `/run/user/1000/ccache-tmp`, environment only) |
| Build attempt 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-284 cmake --build build --target TrackFolderTest -j4` → `logs/build-2.log` | 0 |
| Family ctest (GREEN, original test) | in `build/tests`: `ctest -R '^TrackFolderTest$' --output-on-failure` → `logs/test-1.log` | 0 (1/1 passed) |
| New test by name (GREEN) | in `build/tests`: `./TrackFolderTest aChildsMixer…` → `logs/test-2.log` | 0 (3 passed, 0 failed: init, the test, cleanup) |
| Sibling families build | `CCACHE_TEMPDIR=… cmake --build build --target TrackFolderMenuTest FolderSummaryLaneTest -j4` → `logs/build-3.log` | 0 |
| Sibling families ctest | in `build/tests`: `ctest -R '^(TrackFolderMenuTest\|FolderSummaryLaneTest\|TrackFolderTest)$'` → `logs/test-3.log` | 0 (3/3 passed) |
| Mutation saved | `git diff -- src/core/AutomatableModel.cpp > logs/mutation.patch` (mutation = both clamps removed) | 0 |
| Mutation build | `CCACHE_TEMPDIR=… cmake --build build --target TrackFolderTest -j4` → `logs/build-4-mutation.log` | 0 |
| RED ctest (mutation) | `ctest -R '^TrackFolderTest$'` → `logs/test-4-red.log` | **8** (family fails) |
| RED direct (mutation) | `./TrackFolderTest aChildsMixer…` → `logs/test-5-red-direct.log` | **1**: `FAIL! … Actual (static_cast<int>(model->value())): -1, Expected (0): 0` |
| Mutation reversed | `git apply -R logs/mutation.patch` → then `git status --short` shows only `tests/src/core/TrackFolderTest.cpp` modified and `git diff --stat -- src/core/AutomatableModel.cpp` is empty | 0 |
| Restored build | `CCACHE_TEMPDIR=… cmake --build build --target TrackFolderTest -j4` → `logs/build-6-restored.log` | 0 |
| GREEN ctest (restored) | `ctest -R '^TrackFolderTest$'` → `logs/test-6-green.log` | 0 (1/1 passed) |
| GREEN direct (restored) | `./TrackFolderTest aChildsMixer…` → `logs/test-7-green-direct.log` | 0 (3 passed, 0 failed) |
| Final build | `CCACHE_TEMPDIR=… cmake --build build --target zene TrackFolderMenuTest FolderSummaryLaneTest -j4` → `logs/build-7-final.log` | 0 |
| Final family ctest | `ctest -R '^(TrackFolderTest\|TrackFolderMenuTest\|FolderSummaryLaneTest)$'` → `logs/test-8-final-family.log` | 0 (3/3 passed) |

Test-count note: the family has three ctest targets (`TrackFolderTest`, `TrackFolderMenuTest`, `FolderSummaryLaneTest`), and all three ran and passed. None ran zero tests.

## Gate notes

The feature gate script (`feature_check.py`) could not run in this session. It writes its per-gate logs to `<worktree>/../<name>-feature-check/`, outside the write jail, so it failed before running any gate (EXIT 1). I did not edit the script or the gate limits. I ran each gate command the script lists, from the worktree, with its exit code (table above). For the two ratchet gates, the script reports PASS only when a flagged item sits in a file the branch changed (`feature_check.py:83-86`). The branch changes `tests/src/core/TrackFolderTest.cpp` and this report. None of the 6 complexity items or 14 file-length items names either file, so both ratchets would pass under the script's rule. The base-listing comparison the script performs was not run here.

## Self-check

- **RED fails and GREEN passes (exit codes).** No product fix exists to revert. The test is a premise guard. I checked it is not vacuous: with the model clamps removed (mutation), `build/tests` ctest exits 8 and the direct run exits 1 with `Actual -1, Expected 0` (`logs/test-4-red.log`, `logs/test-5-red-direct.log`). With the mutation reversed, both pass with exit 0 (`logs/test-6-green.log`, `logs/test-7-green-direct.log`). The test is not regression proof for a fix, because there is no fix.
- **Deterministic.** No clock, sleep, timer or thread. Inputs are injected directly: `IntModel::setValue(-1)` and `TrackFolder::setMode(Routing/Group)` on the test's own folder and child, both on the test thread.
- **At the cause and complete.** No product change. The checks were on the callers of the sentinel and the writers of the model: every range setter and every setValue path, listed in Evidence 1 above. Every writer is `>= 0` and the sentinel never appears as stored data for a live child.
- **No collateral damage.** No product code changed, so saved data loads exactly as before, and no check was added. The one loader path that can hold `-1` (a hand-edited `prevch`, Evidence 3) is untouched and reported as a residual.
- **Verdict evidence.** The verdict cites `AutomatableModel.cpp:504,514`, `InstrumentTrack.cpp:70,86`, `SampleTrack.cpp:60,66`, `MixerView.cpp:432,438`, `TrackFolder.cpp:265,269-275,306` and `include/TrackFolder.h:171-172`, and the parser at `TrackFolder.cpp:73-96`.
- **Submit check.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-284` → `logs/submit-check-2.log`, **EXIT=1**. PASS: commits on the branch, working tree clean, this report, tests added or changed. FAIL: `gates (feature_check.py)` only, because the check runs `feature_check.py`, which fails on `PermissionError` creating `<worktree>-feature-check/` outside the write jail (see Gate notes). Every gate command the script lists was run directly above and exits 0, except the two red-on-train ratchets, whose flagged items are not in a changed file. I did not edit the gate, the script, or any limit to make this row pass.
