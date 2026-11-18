# Cluster new-p9-531 — Editing an Xpressive wavetable races active note rendering

Verdict: **FIXED** on this branch (`bugs/qwen-new-p9-531`).

Luna's round-1 QA points: the brief says to "address every point below", but no list of
round-1 points is in the brief and none was found on disk (`~/.local/share/zene-bunny-fix/qwen/runs/new-p9-531.r2/`
holds only `brief.md`, `build.log`, `session.log`). This report therefore addresses the
finding itself and the procedure steps. The earlier session committed nothing for this
cluster (`git log --all --grep=new-p9-531` is empty).

## The defect (confirmed in the code)

- The instrument owns `m_W1`, `m_W2`, `m_W3` (`WaveSample`, float buffers). A note stores
  a pointer to them and reads them on every frame: `ExprSynth.cpp:742` passes
  `m_W1->m_samples` to `add_cyclic_vector`, and `renderOutput` reads through it.
- GUI edits and project loads overwrite the same buffers with `WaveSample::copyFrom`
  (`Xpressive.cpp:194-196` in `loadSettings`, `Xpressive.cpp:653-659` in `smoothChanged`,
  `Xpressive.cpp:672-678` in `graphDrawn`). Before this fix `copyFrom` was a bare
  `memcpy` (`ExprSynth.h` old lines 85-88) with no synchronisation.
- The render thread holds `m_changeMutex` for the whole period (`AudioEngine.cpp:914-916`,
  `renderNextPeriod`). Note playback runs inside that period, so a GUI thread that takes the
  same lock is excluded from any period. The sanctioned GUI-to-audio seam is
  `AudioEngine::requestChangeInModel` / `requestChangesGuard` (`AudioEngine.cpp:1381-1385`,
  `include/AudioEngine.h:535`), which every other GUI-side model write in the tree uses.

## Design choice

The finding suggests immutable snapshots with an RT-safe handoff. I did not build that. The
existing render-period lock already gives each period a consistent table, because writes are
excluded while a period runs. A snapshot scheme would need a retire/free protocol that frees
buffers off the audio thread, which is a larger change than this defect needs. If the owner
wants lock-free reads later, that is a separate design.

The fix lives in `WaveSample::copyFrom` rather than at the six call sites, so every current and future
writer gets the lock. It is out of line in `ExprSynth.cpp` because `ExprSynthTest.cpp` includes
`ExprSynth.h` under `#define private public`, and pulling `AudioEngine.h` into that header is
avoidable risk.

Realtime rule: no mutex is added to the audio path. `copyFrom` is only called from GUI and load
threads. `requestChangeInModel` returns early on the render thread, so a call from there cannot
self-deadlock.

## Why the regression is at the table, not at `restoreState`

`AutomatableModel::setValue` (`AutomatableModel.cpp:387`) and `setInitValue` (`:920`) each take
`modelChangeGuard()`, so `Xpressive::loadSettings` blocks on its first model setting, before it
reaches the table copy. A `restoreState`-level test therefore passes or fails for the wrong reason,
on both the original and fixed code. The test instead drives `WaveSample::copyFrom` directly on a
second thread while the test thread holds the change lock.

## Changes (committed)

| File | Change |
| --- | --- |
| `plugins/Xpressive/ExprSynth.h:85-86` | `copyFrom` becomes a declaration (was inline `memcpy`). |
| `plugins/Xpressive/ExprSynth.cpp:709-716` | Definition: holds `Engine::audioEngine()->requestChangesGuard()` around the `memcpy` (null-engine safe). Adds `AudioEngine.h` and `Engine.h` includes (`:35-36`). |
| `tests/src/plugins/ExprSynthTest.cpp:84-116` | Regression slot `waveTableCopyWaitsForRenderPeriod`; adds `<atomic>`, `<chrono>`, `<thread>`, `AudioEngine.h`. |
| `tests/upstream-modifications.txt` | Two registry lines for the inherited `ExprSynth.h` and `ExprSynth.cpp` (reason required by the gate). |
| `docs/qwen-fixes-2026-10/cluster-new-p9-531.md` | This report. |

Not changed: `Xpressive.cpp` (its call sites now get the lock through `copyFrom`), the gates, any baseline.

## Regression test

The test holds `Engine::audioEngine()->requestChangesGuard()` (as a render period does), starts
a thread that calls `wave.copyFrom(&graph)`, and waits up to 1 s to see whether the copy completes.
The assertion is `!copiedWhileHeld`. With the lock in place the copy blocks until the guard is
released, so the assertion holds. Without it, the copy finishes immediately and the assertion fails.
The copy is then checked to have happened after release.

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-531.r2/logs/`. All commands run
from the worktree; ctest only from `build/tests`. Exit codes are unpiped, captured with `; echo EXIT=$?`
or `echo "...=$?"`.

| # | Command | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| 1 | `cmake --build build --target ExprSynth` | build-1.log | 2 | Failed on ccache temp dir `/run/user/1000/ccache-tmp` (permission denied, outside the write-jail). Not a code error. |
| 2 | `CCACHE_TEMPDIR=/tmp/qwen-ccache-tmp-531 cmake --build build --target ExprSynth` | build-2.log | 0 | RED build (product code unfixed, new test present). |
| 3 | `cd build/tests && ctest -R '^ExprSynth$' --output-on-failure` | test-1.log | **8 (RED)** | 1 test run, failed: `waveTableCopyWaitsForRenderPeriod() '!copiedWhileHeld' returned FALSE`. |
| 4 | `CCACHE_TEMPDIR=… cmake --build build --target ExprSynth` (fix applied) | build-3.log | 0 | |
| 5 | `ctest -R '^ExprSynth$' --output-on-failure` | test-2.log | **0 (GREEN)** | 1/1 passed. |
| 6 | `ctest -R '^ExprSynth$' -V` | test-3.log | 0 | Per-function: `expressionTimeContinuesPast32BitFrameCount` PASS, `waveTableCopyWaitsForRenderPeriod` PASS (4 passed, 0 failed). |
| 7 | `git diff -- plugins/Xpressive/ExprSynth.h plugins/Xpressive/ExprSynth.cpp > logs/fix.patch` | — | 0 | Product diff saved. |
| 8 | `git apply -R logs/fix.patch` | — | 0 | Fix reverted. |
| 9 | `cmake --build build --target ExprSynth` (unfixed) | build-4.log | 0 | |
| 10 | `ctest -R '^ExprSynth$' -V` (unfixed) | test-4.log | **8 (RED)** | Same assertion failure as row 3. |
| 11 | `git apply logs/fix.patch` | — | 0 | Reapplied; `git diff … \| cmp - logs/fix.patch` → EXIT 0 (byte-identical). |
| 12 | `cmake --build build --target ExprSynth` (reapplied) | build-5.log | 0 | |
| 13 | `ctest -R '^ExprSynth$' -V` (reapplied) | test-5.log | **0 (GREEN)** | Same passes as row 6. |
| 14 | `cmake --build build --target xpressive ExprSynthAllocationTest PluginAudioPortsTest PluginPortsMigrationTest` | build-6.log | 0 | Rebuild of every target that includes `ExprSynth.h` (`DEPENDENTS_BUILD_EXIT=0`, log line 1095). |
| 15 | `ctest -R '^(ExprSynth\|ExprSynthAllocation\|PluginAudioPortsTest\|PluginPortsMigrationTest)$'` | test-6.log | 8 | `PluginPortsMigrationTest` failed: `nonRealtimeLadspaEffectsAreInvalid` "SWH Decimator test fixture was not loaded". The `decimator_1202` target was configured but never built in this tree, so `decimator_1202.so` was absent. Not related to the fix (see row 16). |
| 16 | `cmake --build build --target decimator_1202` | build-7.log | 0 | Built `build/plugins/ladspa/decimator_1202.so`. |
| 17 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | test-7.log | 0 | 1/1 passed. |
| 18 | `ctest -R '^(ExprSynth\|ExprSynthAllocation\|PluginAudioPortsTest\|PluginPortsMigrationTest)$' -V` | test-8.log | **0** | **Affected family 4/4 passed**. Totals: PluginAudioPortsTest 11 passed; ExprSynth 4 passed; ExprSynthAllocation 3 passed; PluginPortsMigrationTest 45 passed, 2 skipped. |
| 19 | `ctest -N -R 'ExprSynth\|Xpressive\|PluginPortsMigration\|PluginAudioPorts\|xpressive\|WaveSample'` | — | — | Lists 5 tests (ids 139, 182, 472, 476, 477). **No test named `Xpressive`** is registered in this tree, so the `^Xpressive$` ctest named in the brief does not exist here. |

Gates (both required ratchets, run after the final code and test edits):

| # | Command | Log | EXIT | Note |
| --- | --- | --- | --- | --- |
| G1 | `bash tests/file-length-gate.sh --check` on unmodified HEAD (before any edit) | gate-flen-head-1.log | **1** | 14 pre-existing `REGRESSION` lines in unrelated test files (e.g. `tests/src/core/RackMacrosTest.cpp (535)`). |
| G2 | `bash tests/file-length-gate.sh --check` after the fix | gate-flen-fix-1.log | **1** | Same list. `diff` of the REGRESSION/FAIL lines against G1: EXIT 0 (identical). |
| G3 | `bash tests/complexity-gate.sh --check` on unmodified HEAD | gate-ccn-head-1.log | **1** | 6 pre-existing `REGRESSION` CCN entries in unrelated files (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, …). |
| G4 | `bash tests/complexity-gate.sh --check` after the fix | gate-ccn-fix-1.log | **1** | Same list. `diff` against G3: EXIT 0 (identical). Neither gate names `Xpressive` or `ExprSynth`. |
| G5 | `bash tests/no-upstream-regression-gate.sh` after the fix | gate-upstream-fix-1.log | **0** | Both new registry lines recognised as declared divergences. |

**Gate status:** G2 and G4 exit 1, as G1 and G3 did before any edit. The failures are in
files this change does not touch, so this fix neither causes nor clears them. Fixing them would mean
editing other owners' files, which is out of scope for this cluster. The gate limits and baselines
were not changed.

## Observations not fixed (out of scope, not verified further)

- `Xpressive::playNoteImpl` (`Xpressive.cpp:204-241`) constructs `ExprFront`/`ExprSynth` objects on the
  audio thread for each new note. That is a pre-existing realtime-rule issue, separate from this race.
