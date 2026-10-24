FIXED

# Cluster new-p15-90: user LFO waveform buffer handoff

## Decision

REAL. Before the fix, `LfoControllerDialog.cpp:216` replaced `m_userDefSampleBuffer` directly, while `LfoController.cpp:130` passed that member's raw pointer to `Oscillator::userWaveSample()` during rendering. The render path held no owner, so replacement could free the buffer in use. The regression reproduced that lifetime failure deterministically.

## Fix

Rendering now increments a lock-free reader count before loading an atomic raw pointer (`src/core/LfoController.cpp:120`), samples through that pointer (`:139`), and decrements the count afterward (`:157`). The control-thread setter retains the previous `shared_ptr`, publishes the replacement, and reclaims retired owners only when the reader count is zero (`src/core/LfoController.cpp:164-209`). A very-coarse one-second Qt timer retries reclamation when a reader was active at publication. The timer and all vector/shared-pointer destruction stay on the control thread. Both the waveform picker (`src/gui/LfoControllerDialog.cpp:214-218`) and project loading (`src/core/LfoController.cpp:306-310`) use the setter. `saveSettings()` still writes the same `userwavefile` attribute.

## Regression and verification

The new `LfoControllerUserWaveTest::replacedWaveRemainsAliveWhileRenderLeaseIsHeld` uses the production acquire/release seam and a `weak_ptr`; it has no threads, sleeps, or wall-clock timing. Without retirement it fails because the old buffer expires while the lease is held. With the fix it remains alive until control-thread reclamation after release.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target zene LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-2.log 2>&1` (worktree root) | 0 | Built the regression against the unsafe handoff. |
| RED | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/test-1.log 2>&1` (`build/tests`) | 8 | `originalLifetime.expired()` failed as expected. |
| Reverse-patch command | `git diff -- include/LfoController.h src/core/LfoController.cpp src/gui/LfoControllerDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/fix.patch` (worktree root) | 0 | Removed only the retirement fix. |
| Reverted build | `cmake --build build --target zene LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-5.log 2>&1` (worktree root) | 0 | Rebuilt without retirement. |
| RED with fix reverted | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/test-4.log 2>&1` (`build/tests`) | 8 | The same lifetime assertion failed. |
| Restore command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/fix.patch` (worktree root) | 0 | Restored the product fix. |
| Fixed build | `cmake --build build --target zene LfoControllerUserWaveTest LfoGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-6.log 2>&1` (worktree root) | 0 | Rebuilt the fixed executable and neighboring LFO test. |
| GREEN and neighboring LFO test | `ctest -R '^(LfoGraphTest|LfoControllerUserWaveTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/test-5.log 2>&1` (`build/tests`) | 0 | Both LFO tests passed (2/2). |
| Header dependents | `cmake --build build --target zene RackDeferredLifetimeTest MidiControllerMenuTest UndoHonestyTest UndoHonestyStateTest PhaseDSidechainTest AutomationPrepassProofTest LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-8.log 2>&1` (worktree root) | 0 | Rebuilt `zene` and all test sources found including `LfoController.h`. |
| First affected-family run | `ctest -R '^(LfoControllerUserWaveTest|LfoGraphTest|RackDeferredLifetimeTest|MidiControllerMenuTest|UndoHonestyTest|UndoHonestyStateTest|PhaseDSidechainTest|AutomationPrepassProofTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/test-6.log 2>&1` (`build/tests`) | 8 | `RackDeferredLifetimeTest` could not load the not-yet-built `amplifier` fixture; after building it, the identical command passed 8/8 (`test-8.log`). |
| Amplifier fixture | `cmake --build build --target amplifier > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-9.log 2>&1` (worktree root) | 0 | Built the plugin required by `RackDeferredLifetimeTest`. |
| Final regression build | `cmake --build build --target LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/build-10.log 2>&1` (worktree root) | 0 | Rebuilt after the final test-source include edit. |
| Affected test family | `ctest -R '^(LfoControllerUserWaveTest|LfoGraphTest|RackDeferredLifetimeTest|MidiControllerMenuTest|UndoHonestyTest|UndoHonestyStateTest|PhaseDSidechainTest|AutomationPrepassProofTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/test-8.log 2>&1` (`build/tests`) | 0 | All eight header-dependent LFO/controller tests passed (8/8). |
| Initial feature-gate run | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-90 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/gate-1.log 2>&1` (worktree root) | 1 | The new test was not yet in Git's tracked file list, so source registration/reproduction failed; it was registered and the manifest re-derived before the final gate. |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-90 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r1/logs/gate-4.log 2>&1` (worktree root) | 0 | All rows PASS, including source registration and reproduction; complexity/file-length found no new violations. |

The unqualified `cmake --build build` command was rejected with exit 97 by the workspace `bunny-guard`, which requires named targets. The named dependent builds above completed successfully.

`tests/all-sources.txt` was regenerated from its published recipe. This registers the new test and removes the inherited 1,148 vendored sfizz entries that recipe excludes, allowing `all-sources-reproduce` to pass.

## Self-check

- Regression proof: the reverse-patch build exited 0 and the same ctest exited 8 at the weak-pointer lifetime assertion (`build-5.log`, `test-4.log`). Restoring the patch and rebuilding exited 0; the focused LFO tests exited 0 (2/2, `build-6.log`, `test-5.log`). The affected family passed 8/8 (`test-8.log`).
- Determinism: the test manually holds/releases the same reader-count seam used by `updateValueBuffer()`, replaces via `setUserDefSampleBuffer()`, and observes `weak_ptr` expiration. It does not start a thread or wait for time.
- Completeness: the only publication callers are the GUI picker at `src/gui/LfoControllerDialog.cpp:216` and project loader at `src/core/LfoController.cpp:310`; both use the setter. `saveSettings()` only reads the control-thread owner to preserve the existing file attribute. The `m_userDefSampleBuffer` search found no other writes.
- Collateral and realtime: project XML fields and `SampleBuffer::fromFile()` behavior are unchanged. Render-side work is two lock-free atomics around the raw-pointer read; vector mutation, shared-pointer destruction, and timer activity run on the control thread. No control commands or undo behavior changed.
- The ALREADY-FIXED/FALSE-POSITIVE evidence item does not apply to this FIXED verdict; the original direct assignment/read path and the RED assertion establish the REAL finding.

## Files committed

- `include/LfoController.h`
- `src/core/LfoController.cpp`
- `src/gui/LfoControllerDialog.cpp`
- `tests/src/core/LfoControllerUserWaveTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p15-90.md`
