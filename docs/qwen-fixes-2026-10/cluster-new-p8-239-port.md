FIXED

# cluster new-p8-239-port — tuner source changes could mix old and new frames

## Decision and evidence

The finding is **REAL**. Before the port, `include/TunerTap.h:67-68` stored the selected source and then
requested the ring reset separately. `src/core/TunerTap.cpp:53-60` accepted a push of the newly selected
source while `m_resetPending` was still false, so it appended those frames at the old source's head. A reader
could then copy both sources. The deterministic regression fails at that exact interleaving in
`tests/src/core/TunerTapTest.cpp:82-92` with “a window read at the source change mixed the old source's frames
with the new one's.”

The port also fixes and tests the matching race in `PhaseScopeTap`, whose newer seqlock rejected overlapping
ring writes but still allowed the new channel to append before `reset()` ran. The regression at
`tests/src/core/PhaseScopeTapTest.cpp:185-198` reproduces that gap.

## Fix

`TunerTap` stores the source in the low two bits of a 64-bit state word and the reset generation above it.
`setSource()` publishes both in one CAS (`include/TunerTap.h:65-115`). `push()` reads one state, rebases the
ring when that generation is new, then publishes the applied generation only after the first write is
complete (`src/core/TunerTap.cpp:53-89`). `latest()` refuses an unapplied generation and checks that the state
did not change during the copy.

`PhaseScopeTap` uses the same generation handoff with its channel encoded as index + 1, preserving `Off == -1`
and the full nonnegative `int` index range (`include/PhaseScopeTap.h:63-123`). Its multiple-worker write guard
and seqlock remain in place; `written()` reports zero until the producer applies the selected generation
(`src/core/PhaseScopeTap.cpp:50-107`). `channel()` decodes the largest valid index without signed overflow.

Both audio push paths remain allocation-free and lock-free. They add only lock-free atomic state reads and
stores; compile-time assertions enforce lock-free ring atomics. Test hooks exist only under `LMMS_TESTING`.
No project format, saved data, control command, or undo behavior changed. Callers checked: master/input pushes
in `src/core/AudioEngine.cpp:914,996`; tuner selection/reset/read in `src/core/ControlCommandsTuner.cpp:97-107`
and `src/core/WorkspaceWindows.cpp:248-259`; phase-scope pushes and reads in `src/core/MixerStrip.cpp:298`,
`src/gui/PhaseScopeView.cpp:168-192`, and `src/gui/ControlCommandsMixerView.cpp:202`. The tuner's existing extra
`reset()` after `setSource()` remains a second generation bump, as before it was a second reset request.

## Regression and verification evidence

The hooks run synchronously between selection publication and return. The regression itself invokes the push
and `latest()` on that same call stack; it uses no sleeps, timestamps, thread scheduling, or race.

| Phase | Exact command | Status and evidence |
|---|---|---|
| Initial combined-target attempt | `mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs && cmake --build build --target zene TunerTapTest PhaseScopeTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-red-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=2; `zene` built, then the native build rejected `TunerTapTest`; building each target separately succeeded |
| Initial test target build | `cmake --build build --target TunerTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-red-tuner-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Initial sibling test target build | `cmake --build build --target PhaseScopeTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-red-phase-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| RED on original implementations | `ctest -R '^(TunerTapTest\|PhaseScopeTapTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/test-red-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=8; 2/2 failed at the expected mixed-window assertions |
| Save final product patch | `git diff -- include/TunerTap.h src/core/TunerTap.cpp include/PhaseScopeTap.h src/core/PhaseScopeTap.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/fix.patch; code=$?; echo EXIT=$code` | EXIT=0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/fix.patch; code=$?; echo EXIT=$code` | EXIT=0 |
| Preserve only test seams for proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/test-seams.patch; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild application without fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-red-zene-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild tuner regression without fix | `cmake --build build --target TunerTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-red-tuner-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild phase regression without fix | `cmake --build build --target PhaseScopeTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-red-phase-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| RED with final product patch reversed | `ctest -R '^(TunerTapTest\|PhaseScopeTapTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/test-proof-final-red-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=8; both mixed-window regressions failed again |
| Remove proof-only seams | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/test-seams.patch; code=$?; echo EXIT=$code` | EXIT=0 |
| Restore final product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/fix.patch; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild restored application | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-green-zene-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild restored tuner regression | `cmake --build build --target TunerTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-green-tuner-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild restored phase regression | `cmake --build build --target PhaseScopeTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-green-phase-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| Rebuild existing tuner command test | `cmake --build build --target CustomAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/build-proof-final-green-custom-access-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0 |
| GREEN family | `ctest -R '^(TunerTapTest\|CustomAccessTest\|PhaseScopeTapTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/test-proof-final-green-family-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0; 3/3 registered tests passed |
| Initial feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-239-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/feature-check-1.log 2>&1; code=$?; echo EXIT=$code` | EXIT=1; the new source was not yet staged and `latest()` exceeded complexity 10; both were corrected |
| Feature check after corrections | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-239-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/feature-check-2.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0; all eight rows PASS |
| Feature check after report registration | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-239-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/feature-check-3.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0; all eight rows PASS |
| Final post-commit submission check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-239-port new-p8-239-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-239-port.r1/logs/submit-check-2.log 2>&1; code=$?; echo EXIT=$code` | EXIT=0; `RESULT: PASS - submit`, with all mechanical rows PASS |

## Files committed

- `include/TunerTap.h`, `src/core/TunerTap.cpp` — atomic source/reset generation.
- `include/PhaseScopeTap.h`, `src/core/PhaseScopeTap.cpp` — matching channel/reset generation and snapshot validation.
- `tests/src/core/TunerTapTest.cpp`, `tests/src/core/PhaseScopeTapTest.cpp` — deterministic regressions and the maximum-index round trip.
- `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` — target and source registration.
- `docs/qwen-fixes-2026-10/cluster-new-p8-239-port.md` — this port report.

## Self-check

- **The bug is real.** The old source store/reset ordering and push acceptance path are identified above; both original implementations fail the deterministic assertions.
- **The regression catches the fix.** `TunerTapTest::aSourceChangeCannotMixTheOldSourcesFrames()` and `PhaseScopeTapTest::aChannelChangeCannotMixTheOldChannelsFrames()` both fail with the product patch reversed (RED exit 8), then the restored family passes 3/3 (GREEN exit 0); exact commands and logs are in the table.
- **The test is deterministic.** `setSourceWithTestHook()` calls `pushWhileTheSourceIsPublished()`; `setChannelWithTestHook()` calls `pushWhileTheChannelIsPublished()`. Each callback invokes `push()` and `latest()` synchronously using fixed `SampleFrame` data, without clocks, sleeps, threads, or scheduling assumptions.
- **The fix is complete.** All tuner `setSource`, `reset`, `push`, `latest`, and `source` call paths were checked, plus the sibling phase-scope `setChannel`, `reset`, `push`, `latest`, `written`, and `channel` paths listed above. `ControlCommandsTuner::setTuner()`'s additional reset remains a valid extra generation bump.
- **No collateral damage.** No save/load or render format changed, and no new limit or rejection was added. `PhaseScopeTapTest::channelRoundTripsTheLargestIndex()` proves the packed channel encoding preserves `INT_MAX`; existing project data remains outside the changed serialization paths.
- **Realtime and commands.** The audio push paths still allocate nothing, lock nothing, wait for nothing, and log nothing; lock-free atomics are compile-time checked. No command or undo surface changed.
- **Verdict-specific evidence.** `ALREADY-FIXED` and `FALSE-POSITIVE` do not apply: this is `FIXED` because the original implementations fail the tests and the restored implementation passes them.
- **Registration and gates.** The new test is listed in `tests/CMakeLists.txt`, `tests/all-sources.txt`, and `tests/fork-sources.txt`; inherited edits are recorded in `tests/upstream-modifications.txt`. The feature check and post-commit submission check both exit 0, as recorded in the table.
