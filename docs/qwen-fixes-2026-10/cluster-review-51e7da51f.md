FIXED
review-51e7da51f: FIXED
1663: ALREADY-FIXED
new-p12-245: ALREADY-FIXED

The three findings are assessed separately below. Commands ran in this worktree; direct Qt test binaries ran from `build/tests`. All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs`.

## review-51e7da51f — script effect removal and structural restore

**Decision: REAL; FIXED.** The removal inverse was already integrated by `b9234d896`: `src/core/ScriptDawEdit.cpp:231` journals before `detachEffect()` and `deleteLater()`. The new registration handoff also had a real failure path: `registerAutomatableModel()` can return false, so the restore must refuse to append a device whose models are absent from the render-visible registry.

`src/core/ControlStructuralSupport.cpp:357-390` now unregisters any models published before a later failure, discards the unattached effect, records a structural restore failure, and returns false before `appendEffect()`. Both callers propagate that result: `recreateEffectFromState()` returns null at `:404`, and the removal undo closure clears its result and returns at `:590`.

The plugin-backed journal regression is `ScriptDawRemovalUndoTest::aScriptsRemovedEffectIsUndoneByItsOwnStep` (`tests/src/core/ScriptDawRemovalUndoTest.cpp:109`). RED: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-journal.patch` (EXIT=0), then from `build/tests` `QT_QPA_PLATFORM=offscreen ./ScriptDawRemovalUndoTest aScriptsRemovedEffectIsUndoneByItsOwnStep` (EXIT=1, `red-journal-2.log`, 0 skipped). GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-journal.patch` (EXIT=0), then the same test command (EXIT=0, `green-journal-3.log`, 0 skipped). The test checks the device returns on its own undo and is removed again by redo.

The registration-failure RED/GREEN proof uses the real loadable amplifier and an injected failure at registration:

- RED: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix.patch` (EXIT=0); `cmake --build build/tests --target ScriptDawModelRegistrationTest -j12` (EXIT=0, `build-41.log`); from `build/tests`, `QT_QPA_PLATFORM=offscreen ./ScriptDawModelRegistrationTest aRecordedDeviceIsNotAttachedWhenModelRegistrationFails` (EXIT=1, `red-registration-5.log`: `restored == nullptr` was false, 0 skipped).
- GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix.patch` (EXIT=0); the same build command (EXIT=0, `build-42.log`); the same test command (EXIT=0, `green-registration-8.log`, 0 skipped). It verifies the original effect remains the only chain member and the restore failure is Refused.

The audio-change-lock regression is `aRecordedDeviceIsBuiltWithoutTheAudioChangeLock` in `tests/src/core/ScriptDawModelRegistrationTest.cpp:98`. Its test seam signals immediately before the real registration mutex acquisition. A holder thread releases the mutex only after that signal or after the restore completes. With the deferral scope removed, the test takes the real lock-taking path and fails deterministically; with deferral restored, the build completes while the mutex is held.

- RED: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-lock.patch` (EXIT=0); `cmake --build build/tests --target ScriptDawModelRegistrationTest -j12` (EXIT=0, `build-39.log`); from `build/tests`, `QT_QPA_PLATFORM=offscreen ./ScriptDawModelRegistrationTest aRecordedDeviceIsBuiltWithoutTheAudioChangeLock` (EXIT=1, `red-lock-4.log`, 0 skipped).
- GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-lock.patch` (EXIT=0); the same build command (EXIT=0, `build-40.log`); from `build/tests`, `QT_QPA_PLATFORM=offscreen ./ScriptDawModelRegistrationTest` (EXIT=0, `green-registration-7.log`: 4 passed, 0 failed, 0 skipped, including both registration cases).

## 1663 — stale Lua wrappers

**Decision: ALREADY-FIXED.** `LuaMixerChannel` stores the stable channel id and resolves it against live mixer channels for every access (`src/core/ScriptDawBindings.cpp:467-477`); `gain()`, `setGain()`, and `isValid()` use that resolution. The corresponding device wrapper resolves stable channel/device ids (`src/core/ScriptDawEffects.cpp`), and `LuaTrack` uses `QPointer` (`include/ScriptBindings.h:231`). The requested removed-channel script path is covered by `aRemovedChannelsViewIsInert` (`tests/src/core/ScriptDawBindingTest.cpp:546`); the added guard fails safely before a stale raw pointer could be dereferenced.

RED: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-1663.patch` (EXIT=0), then from `build/tests` `QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest aRemovedChannelsViewIsInert` (EXIT=1, `red-1663-1.log`: the removed view remained valid). GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-1663.patch` (EXIT=0), then the same test command (EXIT=0, `green-1663-2.log`, 0 skipped). The channel-id fix predates this round (`e228edf8b`); this round made the negative test detect the stale view without touching freed memory.

## new-p12-245 — Lua view-pool growth

**Decision: ALREADY-FIXED.** `ViewPool::add()` reuses its wrapper by stable identity in `src/core/ScriptDawBindings.cpp:100-127`; `beginRun()` clears the three pools at `:242-246`. The test `aRepeatedChannelReadDoesNotGrowTheViewPool` (`tests/src/core/ScriptDawBindingTest.cpp:588`) compares one read with 500 reads and expects the pool size to stay constant. The fix is on this branch in `52f3e5bf8`.

- RED: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-new-p12.patch` (EXIT=0); from `build/tests`, run `QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest aRepeatedChannelReadDoesNotGrowTheViewPool` (EXIT=1, `red-new-p12-1.log`: 500 wrappers versus 1, 0 skipped).
- GREEN: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-51e7da51f.r2/logs/fix-new-p12.patch` (EXIT=0); run the same command (EXIT=0, `green-new-p12-2.log`, 0 skipped).

## Verification

- `cmake --build build --target zene -j12` — EXIT=0 (`build-32.log`); this rebuilt dependents of the changed `AudioEngine.h`.
- `cmake --build build/tests --target ScriptDawBindingTest ScriptDawEffectsTest ScriptDawRemovalUndoTest ScriptDawModelRegistrationTest -j12` — EXIT=0 (`build-37.log`). `ScriptDawModelRegistrationTest` was rebuilt after its final edit with `cmake --build build/tests --target ScriptDawModelRegistrationTest -j12` — EXIT=0 (`build-43.log`).
- `ctest -R '^(ScriptDawBindingTest|ScriptDawEffectsTest|ScriptDawRemovalUndoTest|ScriptDawModelRegistrationTest|LuaApiSurface)$' --output-on-failure` from `build/tests` — EXIT=0 (`test-5.log`), 5/5 passed. `ScriptDawEffectsTest` also ran directly — EXIT=0, 8 passed, 0 skipped (`green-effects-final.log`).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-51e7da51f` — EXIT=0 (`gate-final-1.log`): all gates PASS, including complexity and file length.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-51e7da51f review-51e7da51f` — EXIT=0 (`submit-final-1.log`): clean worktree, verdicts, tests, registration, and gates PASS; all five requested self-check answers are included here.

A combined build command that incorrectly named `LuaApiSurface` as a build target exited 2 (`build-36.log`: “No rule to make target”). The corrected build above passed; `LuaApiSurface` is included in the passing ctest run.

## Self-check

1. **Each regression fails with the relevant fix reverted and passes restored.** Journal: `aScriptsRemovedEffectIsUndoneByItsOwnStep`, `red-journal-2.log` EXIT=1 / `green-journal-3.log` EXIT=0. Registration failure: `aRecordedDeviceIsNotAttachedWhenModelRegistrationFails`, `red-registration-5.log` EXIT=1 / `green-registration-8.log` EXIT=0. Lock: `aRecordedDeviceIsBuiltWithoutTheAudioChangeLock`, `red-lock-4.log` EXIT=1 / `green-registration-7.log` EXIT=0. Stale channel: `red-1663-1.log` EXIT=1 / `green-1663-2.log` EXIT=0. Pool growth: `red-new-p12-1.log` EXIT=1 / `green-new-p12-2.log` EXIT=0.
2. **The tests are deterministic.** Pool growth compares counts, not time. The stale-view test checks `isValid()` after removal before dereferencing any result. The lock case uses an atomic event and a promise handshake with no deadlines, sleeps, or polling; the registration failure case uses the explicit failure seam. The journal test checks undo/redo state.
3. **The fixes cover their callers and siblings.** Both callers of `attachRecordedDevice()` propagate false. Previously registered models are unregistered before the fresh unattached effect is discarded. All channel, chain, and effect pool makers use keyed `add()`; every `LuaMixerChannel` accessor resolves the stable id. Device and track wrappers already have live-id/QPointer checks.
4. **No collateral change to saved data or legitimate limits.** No save format or project-load schema changed. View pools remain per-run and have no new cap. Deferred model registration is scoped to recorded-device construction; ordinary models still register normally, and only attached models become render-visible. No control command was added. The journaled removal remains one undo step.
5. **ALREADY-FIXED evidence.** Cluster 1663 is shown by `LuaMixerChannel::resolve()` at `src/core/ScriptDawBindings.cpp:472-477` and the passing removed-channel regression. Cluster new-p12-245 is shown by keyed reuse in `src/core/ScriptDawBindings.cpp:100-127`, per-run clearing at `:242-246`, and the 500-read regression.

## Commits and files

Commits for this pass:

- `b0c307ed6` — `include/AudioEngine.h`, `src/core/AudioEngine.cpp`, `src/core/ControlStructuralSupport.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/src/core/ScriptDawModelRegistrationTest.cpp`, `tests/src/core/ScriptDawRemovalUndoTest.cpp`, `tests/upstream-modifications.txt`.
- `31c81bdc5` — `tests/src/core/ScriptDawBindingTest.cpp`.
- `057e1b7be` — `include/AudioEngine.h`, `src/core/AudioEngine.cpp`, `tests/src/core/ScriptDawModelRegistrationTest.cpp`.
- This report is committed separately after the code commits.

The already-present journal, stable-id wrapper, and keyed-pool fixes are in `b9234d896`, `e228edf8b`, and `52f3e5bf8`, respectively.
