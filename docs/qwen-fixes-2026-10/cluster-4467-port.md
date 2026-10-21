ALREADY-FIXED

# Cluster 4467-port: SlicerT playback state allocation

## Decision and source evidence

The newest train already contains the product fix in commit `66717c2c9` (`fix(slicer): remove render-path playback-state allocation`). `git merge-base --is-ancestor 66717c2c9 HEAD` returned EXIT=0.

`plugins/SlicerT/SlicerT.h:73-81` stores 128 `PlaybackSlot`s in an instrument-owned `std::array`; each slot contains a `Sample::PlaybackState`. `plugins/SlicerT/SlicerT.cpp:79-91` claims a slot with an atomic compare-exchange and sets its frame index. In `plugins/SlicerT/SlicerT.cpp:132-145`, `playNoteImpl()` claims that preconstructed slot when `m_pluginData` is null and passes its state to sample playback; it does not allocate. `plugins/SlicerT/SlicerT.cpp:172-185` resets and releases the slot for reuse, and `include/Sample.h:62-69` resets the resampler and buffered span in place. The finding was real on the pre-fix implementation, but it is already fixed on this branch.

The first-note regression from accepted commit `e1bf1cdea` is carried into the newer `SlicerTPlayheadTest` implementation. Its fixture helper now supports adding a second SlicerT track without clearing the warmed track. The test checks the fresh instrument's first rendered note under the allocation probe. The file was already registered at `tests/CMakeLists.txt:461` and `tests/fork-sources.txt:2493`; no new file registration was needed.

## Regression evidence

For the RED proof, I temporarily simulated the original per-note `new Sample::PlaybackState` path in `plugins/SlicerT/SlicerT.cpp`; this was not retained. The saved `fix.patch` was the temporary source diff, and `git apply -R` restored the pre-existing slot-pool implementation. With that original behavior simulated, the new test failed at `tests/src/core/SlicerTPlayheadTest.cpp:373`: actual allocation count 1, expected 0. On the restored implementation, the same test passed with zero allocations.

The first test launch was only a setup failure: the SlicerT module had not been built, so the test reported “Plugin not found.” It is recorded below and is not counted as the regression RED result. The corrected build included the `slicert` target.

| Step | Exact command | Work directory | EXIT | Evidence |
| --- | --- | --- | ---: | --- |
| Initial build attempt | `cmake --build build --target zene SlicerTPlayheadTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/01-red-build.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | worktree root | 0 | Did not include the `slicert` module target. |
| Initial test setup attempt | `./SlicerTPlayheadTest firstNoteOnFreshSlicerInstrumentAllocatesNothing > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/02-red-test.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `build/tests` | 1 | Plugin not found; not a valid RED result. |
| RED build with original behavior simulated | `cmake --build build --target zene slicert SlicerTPlayheadTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/03-red-build-with-plugin.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | worktree root | 0 | Built the application, SlicerT module, and test. |
| Save the temporary source diff | `git diff -- plugins/SlicerT/SlicerT.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/fix.patch` | worktree root | 0 | Captured the simulated old path for restoration after RED. |
| RED regression | `./SlicerTPlayheadTest firstNoteOnFreshSlicerInstrumentAllocatesNothing > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/04-red-test-with-plugin.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `build/tests` | 1 | `test::tlAllocationCount` was 1; expected 0. |
| Restore current source | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | worktree root | 0 | Restored the existing pool implementation. |
| GREEN build | `cmake --build build --target zene slicert SlicerTPlayheadTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/05-green-build.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | worktree root | 0 | Rebuilt after restoring the pool implementation. |
| GREEN regression | `./SlicerTPlayheadTest firstNoteOnFreshSlicerInstrumentAllocatesNothing > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/06-green-targeted.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `build/tests` | 0 | 3 QtTest phases passed; allocation count was 0. |
| Affected CTest family | `ctest -R '^SlicerTPlayheadTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/07-green-family.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `build/tests` | 0 | 1/1 registered CTest passed. |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4467-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4467-port.r1/logs/08-feature-check-pre-report.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` returned EXIT=0. The table was all PASS: `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item), and `file-length` (no new item). The output is in `08-feature-check-pre-report.log`.

| Gate | EXIT |
| --- | ---: |
| `feature_check.py` | 0 |
| `submit_check.py` | pending |

## Self-check

- Regression reversal: with the original `new Sample::PlaybackState` path simulated, the fresh first-note test failed with actual allocation count 1 and expected 0; after `git apply -R` restored the already-fixed pool implementation, the same test passed with count 0. The exact commands, exit codes, and logs are in the evidence table.
- Determinism: `initTestCase()` stops the device and enables `AudioEngineWorkerThread::setDeterministicProcessing(true)`; the new test calls `renderNextPeriod()` synchronously while `AllocationProbe` counts allocations. The new test contains no sleep or timing race.
- Cause and sibling paths: `acquirePlaybackSlot()` has one caller, `playNoteImpl()`. `NotePlayHandle` calls `InstrumentTrack::deleteNotePluginData()`, which reaches SlicerT's `deleteNotePluginData()` to reset and release the slot. Pool exhaustion uses the existing sentinel and counter path; the existing 130-note overflow assertion remains in the same test. No production path was changed in this port.
- Compatibility: this port changes no sample format, project serialization, undo path, or control command. The 128-slot bound and its overflow behavior are already in the newest train; this port adds no limit or check.
- ALREADY-FIXED evidence: current source uses the preconstructed slot array and allocation-free claim/reset paths at `plugins/SlicerT/SlicerT.h:73-81`, `plugins/SlicerT/SlicerT.cpp:79-91,132-145,172-185`, and `include/Sample.h:62-69`.
- Registration: this changes an existing forked test file, already present in `tests/fork-sources.txt` and registered in `tests/CMakeLists.txt`; no new file is unregistered.
- Completion check: `submit_check.py` will be rerun after the commit; the earlier pre-commit check returned EXIT=1 only because the test and report were uncommitted.

## Files in this port commit

- `tests/src/core/SlicerTPlayheadTest.cpp` — first-note allocation regression and helper to keep the warmed and fresh tracks alive together.
- `docs/qwen-fixes-2026-10/cluster-4467-port.md` — decision, source evidence, test proof, and gates.
