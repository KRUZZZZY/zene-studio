# Cluster new-p10-249: the mixer input accumulation locked on the render path

**Verdict: FIXED**

## The finding

`Mixer::mixToChannel()` is on the audio path: every `AudioBusHandle::audioCallback()`
(`src/core/AudioBusHandle.cpp:265`) ends in it, and `AudioEngine` runs those callbacks on the
audio thread *and* on its worker threads (`AudioEngine.cpp:315`, `QThread::idealThreadCount()-1`),
so several play handles can feed one channel in one period. The accumulation therefore needs
mutual exclusion - but it took a `QMutex` for it, which is exactly what AGENTS.md rule 4 forbids
on an audio-thread path ("no allocation, no mutex, no logging"). A contending worker was handed to
the kernel, where its wake-up latency is unbounded.

The fork's own documentation already described the intended shape - `include/RackZoneInputs.h:84`
("a spin flag held for the length of one block add, the shape of `Mixer::mixToChannel`'s channel
lock") and `include/InstrumentOutputBuses.h:65` ("`Mixer::mixToChannel`'s own channel spin lock") -
while the code held a `QMutex`. The fix makes the code the shape the docs claim, and is the same
mechanism the rack feeds already use (`src/core/RackZoneInputs.cpp:103`).

## Evidence

- `include/Mixer.h:157` (before) / `include/Mixer.h:162` (after): `MixerChannel::m_lock` was
  `QMutex m_lock;` and is now `std::atomic_flag m_lock = ATOMIC_FLAG_INIT;`, with the reason
  written next to the declaration. `src/core/Mixer.cpp:227`: the constructor's `m_lock()` member
  initializer is gone with it.
- `src/core/Mixer.cpp:1849` / `src/core/Mixer.cpp:1855`: `mixToChannel()` takes and releases the
  guard with `test_and_set(std::memory_order_acquire)` / `clear(std::memory_order_release)` around
  the same three statements as before - `MixHelpers::add`, `quietChannels() &= ...`, `m_hasInput =
  true`. No allocation, no mutex, no syscall; the wait is bounded by one block add of the holder.
- `tests/src/core/MixerConcurrencyTest.cpp:868`
  (`aContendingFeederIsNeverHandedToTheKernel`): a second thread holds the channel's guard for
  60 ms while this thread's feeder adds a block to that channel, and the test measures the CPU the
  process burns while it waits. Red: `cpu_waited_s=0.0001` (the worker slept in the kernel).
  Green: `cpu_waited_s=0.0600` (it waited, on the CPU, for a bounded critical section).
- `tests/src/core/MixerConcurrencyTest.cpp:908`
  (`concurrentFeedersAccumulateEveryBlockIntoTheChannel`): four threads, 250 adds each, every add
  lands (the guard is still mutual exclusion - only its implementation changed).
- `tests/rt-safety-scope.txt:40`: `src/core/Mixer.cpp Mixer::mixToChannel` is now a declared
  audio-thread path in the rt-safety sweep, so a mutex here is a gate failure from now on. The
  allowlist is unchanged (the fix leaves zero hits, not a debt).
- `tests/upstream-modifications.txt:2428-2429`: why the inherited `Mixer.cpp` and `Mixer.h` changed.

## Regression and verification

The pre-fix run failed on the new slot only (16 passed, 1 failed); the recorded assertion is
`'cpuWaited > 0.02' returned FALSE`. The same failure reappears when the production patch is
reverted from the finished tree, and the same patch makes it pass again - the test is not a
tautology. The rt-safety sweep, widened to declare `Mixer::mixToChannel`, fails on the reverted
tree (`NEW rt-safety hit: src/core/Mixer.cpp:Mixer::mixToChannel:lock-call`) and passes with the
fix.

| Phase | Exact command (run from the worktree; ctest from `build/tests`) | Exit |
|---|---|---:|
| Initial red build | `cmake --build build --target MixerConcurrencyTest > $LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Initial red test | `ctest -R '^MixerConcurrencyTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Green build | `cmake --build build --target zene MixerConcurrencyTest > $LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Green test | `ctest -R '^MixerConcurrencyTest$' --output-on-failure > $LOGS/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Header dependents | `cmake --build build --target lmmsobjs > $LOGS/build-5-lmmsobjs.log 2>&1; echo EXIT=$?` | 0 |
| Mixer family build | `cmake --build build --target MixerListenTest MixerAbRegressionTest MixerChannelPanTest MixerRoutingBackwardCompatTest PdcMixerTest MixerStripTest VcaGroupTest > $LOGS/build-6-mixer-tests.log 2>&1; echo EXIT=$?` | 0 |
| Mixer family test (8 tests) | `ctest -R '^(MixerConcurrencyTest\|MixerListenTest\|MixerAbRegressionTest\|MixerChannelPanTest\|MixerRoutingBackwardCompatTest\|PdcMixerTest\|MixerStripTest\|VcaGroupTest)$' --output-on-failure > $LOGS/test-green-2-mixer-family.log 2>&1; echo EXIT=$?` | 0 |
| rt-safety sweep, fixed tree | `python3 tests/rt-safety-sweep.py --check > $LOGS/rt-safety-green-2-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| Save production patch | `git diff -- src/core/Mixer.cpp include/Mixer.h > $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Remove production patch | `git apply -R $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Reverted build | `cmake --build build --target zene MixerConcurrencyTest > $LOGS/build-7-revert.log 2>&1; echo EXIT=$?` | 0 |
| Reverted test (expected red) | `ctest -R '^MixerConcurrencyTest$' --output-on-failure > $LOGS/test-red-2-reverted.log 2>&1; echo EXIT=$?` | 8 |
| Reverted rt-safety sweep (expected red) | `python3 tests/rt-safety-sweep.py --check > $LOGS/rt-safety-red-reverted.log 2>&1; echo EXIT=$?` | 1 |
| Reapply production patch | `git apply $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target zene MixerConcurrencyTest > $LOGS/build-8-reapply.log 2>&1; echo EXIT=$?` | 0 |
| Restored test | `ctest -R '^MixerConcurrencyTest$' --output-on-failure > $LOGS/test-green-3-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-file-length.log 2>&1; echo EXIT=$?` | 1 |
| Upstream-divergence gate | `bash tests/no-upstream-regression-gate.sh > $LOGS/gate-upstream.log 2>&1; echo EXIT=$?` | 0 |

`$LOGS` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-249.r1/logs`.

### The two ratchet gates are red on this branch, and not because of this fix

Both gates exit 1, and every entry they name is a file this commit does not touch - pre-existing
baseline drift on the branch, not a regression from these edits (the diff touches only the files
listed at the end of this report, and none of them appears in either gate's output):

- `complexity-gate.sh --check` names `tests/src/gui/XyPadPairTest.cpp`,
  `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
  `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`,
  `src/gui/ControlCommandsShortcuts.cpp`, plus five stale baseline entries
  (`gate-complexity.log:97-108`). None of the functions added here
  (`takeGuard`, `releaseGuard`, the two slots) is over CCN 10.
- `file-length-gate.sh --check` names 19 files over 500 code lines, e.g.
  `src/core/ScriptBindings.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`,
  `tests/src/core/VideoTrackTest.cpp` (`gate-file-length.log`). This commit's longest edit,
  `tests/src/core/MixerConcurrencyTest.cpp`, is exempt in `tests/file-length-exempt.txt`, and
  `src/core/Mixer.cpp` / `include/Mixer.h` are not reported at all.

No gate limit or baseline was touched, and no gate was re-anchored.

## Test counts

- `MixerConcurrencyTest`: 15 slots (13 existing + 2 new) plus `initTestCase`/`cleanupTestCase`,
  i.e. 17 entries - 16 passed / 1 failed before the fix, 17/17 after (`test-red-1.log`,
  `test-green-evidence.log`).
- Mixer family run: 8/8 ctest targets passed (`MixerAbRegressionTest`, `MixerConcurrencyTest`,
  `MixerRoutingBackwardCompatTest`, `PdcMixerTest`, `VcaGroupTest`, `MixerChannelPanTest`,
  `MixerListenTest`, `MixerStripTest`).
- rt-safety sweep: 72 declared paths, 4 hits in 3 keys, all pre-existing allowlist lines, exit 0.

## Files committed

- `include/Mixer.h`
- `src/core/Mixer.cpp`
- `tests/src/core/MixerConcurrencyTest.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-249.md`

## Scope note

The finding's suggested fix was "a real-time-safe ownership or preallocated per-worker reduction
design". The accumulation is genuinely shared between concurrent workers, so it cannot simply drop
its mutual exclusion; the smallest correct change is the one this fork already documents and already
uses for the same pattern (`RackZoneInputs::deliver`, `src/core/RackZoneInputs.cpp:103`): a spin
flag held for the length of one block add. It removes the mutex from the audio path, changes no
audio result, and keeps the serialisation the render graph depends on. A per-worker reduction would
be a larger architectural change (a slot per worker per channel plus a reduction stage) with its
own correctness proof, and is not needed to satisfy the real-time rule.
