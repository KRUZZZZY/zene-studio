FIXED

# cluster new-p10-545: Carla allocated period-sized buffers on the stack

## What was wrong (REAL)

`CarlaInstrument::playImpl()` built its stereo scratch from the caller's output span with
two variable-length stack arrays, and on MSVC with two `_alloca` reservations:

- pre-fix `plugins/CarlaBase/Carla.cpp:535-541` (from `git show HEAD:plugins/CarlaBase/Carla.cpp`):
  - `535 #ifndef _MSC_VER`
  - `536     float buf1[bufsize];`
  - `537     float buf2[bufsize];`
  - `539     float *buf1 = static_cast<float *>(_alloca(bufsize * sizeof(float)));`
  - `540     float *buf2 = static_cast<float *>(_alloca(bufsize * sizeof(float)));`
- `bufsize` is `out.size()` (`Carla.cpp:512`).

The Rack and Patchbay instruments are played from `InstrumentPlayHandle::play()`
(`src/core/InstrumentPlayHandle.cpp:44-68`, called from `AudioEngine` STAGE 1), so the
device's period was reserved on the audio thread's stack once per instance, per period.
The engine's contract bounds a period by `MAXIMUM_BUFFER_SIZE = 4096` frames
(`include/AudioEngine.h:77`), but the function itself had no bound: any span reached
`alloca`/a VLA unchanged, and it was non-standard C++ on the non-MSVC path. The severity
the patrol claimed ("exceed the thread's stack capacity") needs a span far beyond the
engine's per-period contract, but the construct is real, unbounded in the function, and
on the render path, so it is fixed rather than dismissed.

Sibling check (also in this change's matrix):

- `plugins/CarlaVst2/CarlaVst2.cpp:304-307` renders `CarlaInstrument::playImpl()` on its
  own worker thread, with spans clamped to `Renderer::MaxPeriodFrames = 4096`
  (`plugins/CarlaVst2/CarlaVst2Renderer.h:84`, `CarlaVst2Renderer.cpp:59-63`), so the same
  bounded planes cover it.
- `plugins/CarlaVst2Effect/CarlaVst2Rack.cpp:221-238` already preallocates its planar
  vectors for the largest period; it needs no change and keeps its own bound.

## The fix

- `plugins/CarlaBase/CarlaRenderBuffers.h` owns the two planar planes. Capacity is the engine's largest
  period (`MaxPeriodFrames = 4096`, line 50); the storage is allocated by the constructor (lines 52-59) when
  the instrument is built. `prepare()` (line 62) clears at most `capacity()` frames and returns the count.
  **Round 3:** the whole render step is now `render(out, process)` (line 75): prepare, call `process(planes,
  frames)`, interleave into `out`. It keeps a fixed few pointers on the stack whatever the period, and it
  allocates, frees, locks and waits nothing. Extracting it lets a test run the shipping step directly.
- `plugins/CarlaBase/Carla.h` has the `CarlaRenderBuffers m_renderBuffers;` member, so the memory lives with
  the instance instead of on a stack.
- `plugins/CarlaBase/Carla.cpp:544` `playImpl()` calls `m_renderBuffers.render(out, ...)` and runs
  `fMidiHandoff.processPeriod(...)` with Carla's `process` inside it, replacing the VLA/`_alloca` block. The
  output was already zeroed at `Carla.cpp:519`, so a span longer than the planes (outside the engine's
  contract) is clipped and its tail stays silent.
- `plugins/CarlaBase/Carla.cpp:67` static-asserts the planes' capacity against `MAXIMUM_BUFFER_SIZE`.
- Registration: `tests/fork-sources.txt` and `tests/all-sources.txt` (new header and new test),
  `tests/upstream-modifications.txt` (Carla.cpp / Carla.h reasons), `tests/rt-safety-scope.txt`
  (`CarlaRenderBuffers::render` and `::prepare`; the allocating constructor runs on the control thread and
  is outside the declared path), `tests/CMakeLists.txt` (new test and its include directory).

No command, undo, save-format or GUI behaviour is touched. Nothing new on the audio path allocates, frees,
locks, waits or logs. TSan does not run on this box; there is no shared mutable state in the new code (each
instrument owns its planes and `playImpl()` is called from one thread at a time), which is the reasoning.

## Round 3: what changed after review

- The render-plane tests moved out of `CarlaMidiHandoffTest` into a standalone `CarlaRenderBuffersTest`, so
  this bug's gate no longer depends on the MIDI stress test.
- The regex grep of `Carla.cpp` and the `CARLA_SOURCE_PATH` define are gone (`tests/CMakeLists.txt` no longer
  defines it). The replacement is a runtime check: `maximumPeriodRendersOnASmallStack` renders a 4096-frame
  period on a thread whose usable stack is about 16 KiB, below the 32 KiB two period-sized arrays need.
- The stress-test edits of `01308f2` and `09dfc23` (producer-parking counter) were reverted, leaving
  `ConcurrentStressRun` as it was before this unit; then a separate deterministic rendezvous was added, as the
  review asked: `periodSequence` is odd while the renderer holds a period open inside the process callback;
  the renderer opens the period *before* releasing the producers and spins (bounded by 100,000,000 iterations,
  only a hang guard) until a producer's enqueue has begun and returned inside that same open period. Producers
  count `overlappedEnqueues` only when the sequence was odd before the enqueue and unchanged after it. The
  test asserts `overlappedEnqueues > 0`, plus the old `periodsWithProducerInput > 0`, now counted from the
  rendezvous. The first enqueue of the first producer always lands in the open period, because producers
  start only after the period opens and it stays open until one has landed. Every other assertion in that
  test is unchanged.

## Test matrix (`tests/src/plugins/CarlaRenderBuffersTest.cpp`)

| path | slot |
| --- | --- |
| capacity bound | `capacityIsTheEnginesLargestPeriod`: `capacity() == MAXIMUM_BUFFER_SIZE` |
| normal period | `renderClearsPlanesAndInterleavesOutput`: stale planes arrive silent, ramp is interleaved into the span |
| exhaustion | `renderClipsASpanLongerThanThePlanes`: span of `capacity*2+1` is clipped to `capacity`, tail untouched |
| short span | `renderLeavesFramesPastASmallSpanAlone` |
| reconfiguration + audio thread | `periodsAfterConstructionAllocateNothing`: 1000 periods of changing size, 0 allocations, 0 frees, same plane pointers (allocation probe) |
| the cause | `maximumPeriodRendersOnASmallStack`: maximum period on a ~16 KiB usable stack |
| platforms | header-only C++20; the small-stack slot uses pthreads and `QSKIP`s elsewhere; the MSVC `_alloca` branch is gone |

## Commands and results (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-545.r3/logs`)

All `EXIT` values below were reported by the delegated runs; the logs hold the test output.

| step | command | EXIT | log |
| --- | --- | --- | --- |
| build | `cmake --build build --target CarlaRenderBuffersTest -j4` (first attempt: target missing, Makefiles not yet regenerated) | 2 | `green-2-build.log` |
| build | `cmake --build build --target CarlaMidiHandoffTest -j4` | 0 | `green-2b-build-midi.log` |
| pre-unit stress test | `ctest -R CarlaMidiHandoffTest` (x5, new regression slots not yet registered) | 8 each | `green-3-test.log`, `green-7-midi-1..5.log` |
| build | `cmake --build build --target CarlaRenderBuffersTest -j4` | 0 | `green-5-build-render.log` |
| first run of new test | `ctest -R CarlaRenderBuffersTest` (small-stack slot failed: `pthread_create` returned 22, 32 KiB is below the system minimum; test then changed to probe the smallest startable stack) | 8 | `green-6-test-render.log` |
| GREEN build | `cmake --build build --target CarlaRenderBuffersTest CarlaMidiHandoffTest -j4` | 0 | `green-8-build.log` |
| GREEN | `ctest -R "CarlaRenderBuffersTest\|CarlaMidiHandoffTest" --output-on-failure` (from `build/tests`) | 0 | `green-9-test.log` |
| repeat | `ctest -R CarlaMidiHandoffTest` x30 and `ctest -R CarlaRenderBuffersTest` x30 | 30/30 and 30/30 passed | (counts reported; per-run output not kept) |
| RED, bug re-introduced | `cp CarlaRenderBuffers.vla.h plugins/CarlaBase/CarlaRenderBuffers.h` (render() with two period-sized stack arrays), build 0, `ctest -R CarlaRenderBuffersTest` | 8 | `red-1-vla-build.log`, `red-2-vla-test.log` |
| RED, product reversed | `git apply -R fix.patch` (the product diff `git diff c6b40333d8 -- plugins/CarlaBase`) | 0 | `fix.patch`, `red-3-revert-apply.log` |
| RED build | `cmake --build build --target CarlaRenderBuffersTest CarlaMidiHandoffTest -j4` | 2 | `red-4-reverted-build.log` |
| restore | `git apply fix.patch` | 0 | `green-11-restore-apply.log` |
| GREEN rebuild | same targets | 0 | `green-12-restored-build.log` |
| GREEN rerun | `ctest -R "CarlaRenderBuffersTest\|CarlaMidiHandoffTest" --output-on-failure` | 0 | `green-13-restored-test.log` |

What the RED runs show, stated exactly:

- **Bug re-introduced (`red-2-vla-test.log`).** `renderClipsASpanLongerThanThePlanes` fails
  (`record.frames` 8193, expected 4096, `CarlaRenderBuffersTest.cpp:166`), the slots before and after it
  up to `periodsAfterConstructionAllocateNothing` pass, and ctest reports `CarlaRenderBuffersTest (SEGFAULT)`.
  The log ends after `periodsAfterConstructionAllocateNothing`, so the crash is in the next slot,
  `maximumPeriodRendersOnASmallStack` (by elimination from the log order; not separately confirmed under a debugger).
- **Product reversed.** `git apply -R fix.patch` deletes `CarlaRenderBuffers.h`, so the test no longer
  compiles: `CarlaRenderBuffersTest.cpp:38:10: fatal error: CarlaRenderBuffers.h: No such file or directory`
  (`red-4-reverted-build.log`, EXIT=2). That is a build failure, not a runtime failure. The follow-up
  `ctest` (`red-5-reverted-test.log`, EXIT=8) ran the stale VLA binary, so it is the same result as the
  scratch variant, not independent evidence.
- The fix patch was taken against the pre-fix base `c6b40333d8` because earlier rounds had already
  committed part of the product change; a plain `git diff` would have held only the Round 3 `render()` step.

## Other findings

- `CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree` was nondeterministic before this unit:
  its overlap assertion (`periodsWithProducerInput > 0`) failed 5 of 5 runs on this box with the
  untouched pre-unit test (`green-3-test.log`, `green-7-midi-1..5.log`, all `EXIT=8`). It is fixed
  here only as far as the gate for this bug needs (Round 3 below); the rest of that test is as it was.
- `cmake --build` of a target that does not exist yet (`CarlaRenderBuffersTest`) fails with "No rule to make
  target" until another target's build has regenerated the Makefiles; building `CarlaMidiHandoffTest` first did it.

## Gates and completion check

- `python3 .../feature_check.py <worktree>`: first run `EXIT=1` (`gate-2.log`): `fork-sources` and
  `all-sources-reproduce` FAIL because the new test was listed out of the recipe's sort order in
  `tests/all-sources.txt`. After moving the line, `EXIT=0`, `RESULT: PASS` for all eight gates
  (`gate-3.log`; commit `62a455e6d7`).
- `python3 .../submit_check.py <worktree> new-p10-545`: first run `EXIT=1`, only the Self-check item
  failed (`submit-1.log`); the section below answers it.

## Self-check

- **Regression test fails with the product change reverted, passes with it.** (Round 4 below supersedes this
  bullet: the helper-level test could not see the `playImpl()` integration reversed, so the regression is now
  `CarlaPlayImplTest`, which drives `playImpl()` itself.) Two REDs, both with the
  exit codes above. (1) Bug re-introduced inside `CarlaRenderBuffers::render()` (two period-sized stack
  arrays): `ctest -R CarlaRenderBuffersTest` `EXIT=8`, `renderClipsASpanLongerThanThePlanes` failed and the
  binary ended in SEGFAULT (`red-2-vla-test.log`). (2) Whole product fix reversed with
  `git apply -R fix.patch` (`EXIT=0`): the test no longer builds, `EXIT=2`, because
  `CarlaRenderBuffers.h` is gone (`red-4-reverted-build.log`); this is a build failure, and the `EXIT=8`
  run after it used a stale binary and is not independent. Restored with `git apply fix.patch`
  (`EXIT=0`), rebuilt (`EXIT=0`), `ctest -R "CarlaRenderBuffersTest|CarlaMidiHandoffTest"` `EXIT=0`
  (`green-13-restored-test.log`).
- **The test is deterministic.** `CarlaRenderBuffersTest` has no clock, sleep or race: it injects the span
  sizes (64, 32, `capacity*2+1`, 1000 sizes in 64..127) and the process callback, and counts allocations on
  the calling thread. The one thread it starts is joined before anything is checked and its stack size is
  chosen by probing `pthread_create`, not by timing. The stress test now uses a rendezvous rather than
  scheduling luck: the renderer opens a period (odd `periodSequence`) before releasing the producers and keeps
  it open until an enqueue has begun and returned inside it, so `overlappedEnqueues > 0` holds for any
  thread order. 30/30 `CarlaMidiHandoffTest` runs and 30/30 `CarlaRenderBuffersTest` runs passed (counts
  reported by the delegated run; per-run output not kept). Before the change the stress test failed 5/5
  here.
- **Cause and completeness.** The cause was the two stack arrays in `playImpl()` (`Carla.cpp:536-540` before
  the fix); both branches (VLA and `_alloca`) are gone. Callers: `InstrumentPlayHandle` (audio thread) and
  `CarlaVst2Instrument::renderPeriod` (worker, clamped to `Renderer::MaxPeriodFrames = 4096`) both reach the
  same `playImpl()`. The sibling `CarlaVst2Effect::Rack` already preallocates planar vectors and is unchanged.
  `Carla.h` and `Carla.cpp` changed together and `carlabase` plus both tests were rebuilt
  (`green-4-carla-plugin.log` `EXIT=0`, `green-8-build.log` `EXIT=0`).
- **No collateral damage.** A period never exceeds 4096 frames in the engine (`AudioEngine.h:77`), so every
  legitimate period renders the same frames and samples as before; the planes are 32 KiB per instance,
  allocated once at construction. A span above the maximum (outside the contract) is clipped with a silent tail
  instead of overflowing. No saved data, save format or load path changes, and the bound equals the engine's own
  maximum, so no legitimate caller is rejected.
- **Verdict evidence.** The verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE rule does not apply; the
  pre-fix code is at `plugins/CarlaBase/Carla.cpp:535-541` in `c6b40333d8`.
- **Sibling commands (`app.*`, `arrangement.*`).** This change touches no command file: the diff against the
  pre-fix base `c6b40333d8` lists only `plugins/CarlaBase/*`, the registration manifests,
  `tests/CMakeLists.txt`, two Carla test files and this report. The command list in the check comes from the
  branch's accumulated diff against its base, not from this fix. No control command, undo step or
  reversibility class is added or changed.

Delegation: Haiku ran every build and ctest run (the GREEN runs, the 30x repeats and the RED/restore
sequence; four Haiku starts, the first stopped at the configure guard); I designed and wrote the fix, the tests and the
stress-test rendezvous, reviewed the results and wrote this report.

## Round 4: the regression now drives the production `playImpl()`

Review finding: with only the `Carla.cpp`/`Carla.h` integration reversed and `CarlaRenderBuffers.h` still
present, `CarlaRenderBuffersTest` and `CarlaMidiHandoffTest` both passed, because the test called the helper
directly. That is true and is confirmed again below (`r3-test.log`: both still pass in the reversed state).

New `tests/src/plugins/CarlaPlayImplTest.cpp` (registered in `tests/CMakeLists.txt` next to
`CarlaParamFloatModelTest`, in `tests/all-sources.txt` and `tests/fork-sources.txt`) builds a real
`CarlaInstrument(nullptr, nullptr, isPatchbay=false, nullptr, playNow=false)` and calls the public
`Instrument::play(std::span<SampleFrame>)`, which is `playImpl()`. The Carla native library in this tree is
a dummy whose `carla_get_native_rack_plugin()` returns null, so the test executable defines
`carla_get_native_rack_plugin()`, `carla_get_native_patchbay_plugin()` and `carla_get_library_folder()` itself
(`ENABLE_EXPORTS`); the executable comes first in the dynamic lookup order, so `libcarlabase.so` binds to the
fake descriptor, whose `process` callback records what it receives. The assertions:

- `playImplRendersIntoPlanesOffTheStack`: for a 256-frame period and a `MAXIMUM_BUFFER_SIZE*2+1` span, run on
  a default-stack thread, `process()` is called once with the right frame count (256, then clipped to 4096),
  neither plane pointer lies inside the rendering thread's stack (`pthread_getattr_np`), the planes arrive
  silent, the output is interleaved, and the clipped tail stays silent.
- `maximumPeriodRendersOnASmallStack`: a 4096-frame `play()` on a thread with about 16 KiB of usable stack
  (smallest startable stack found by probing `pthread_create`, plus 8 KiB), less than the two planes (32 KiB).

Deterministic: no clock, sleep or race; span sizes and the fake callback are injected; each thread is joined
before anything is checked. Non-POSIX platforms `QSKIP`.

Commands (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-545.r4/logs`; the product patch is
`git diff c6b40333d8 -- plugins/CarlaBase/Carla.cpp plugins/CarlaBase/Carla.h`, saved as `fix.patch`, which
leaves `CarlaRenderBuffers.h` in place):

| step | command | EXIT | log |
| --- | --- | --- | --- |
| regenerate | `cmake --build build --target CarlaMidiHandoffTest -j4` | 0 | `g1-regen.log` |
| build (first try) | `cmake --build build --target carlabase CarlaPlayImplTest -j4`: `CARLA_EXPORT` not declared in this Carla header branch | 2 | `g2-build.log` |
| build | same, after switching to `CARLA_API_EXPORT` | 0 | `g4-build.log` |
| first run | `ctest -R '^CarlaPlayImplTest$' --output-on-failure` | 0 | `g5-test.log` |
| RED: revert integration | `git apply -R fix.patch` | 0 | `r1-revert.log` |
| RED build | `cmake --build build --target zene carlabase CarlaRenderBuffersTest CarlaMidiHandoffTest CarlaPlayImplTest -j4` (builds: the helper header remains) | 0 | `r2-build.log` |
| RED run | `ctest -R '^(CarlaPlayImplTest\|CarlaRenderBuffersTest\|CarlaMidiHandoffTest)$' -V` | 8 | `r3-test.log` |
| restore | `git apply fix.patch` | 0 | `g6-restore.log` |
| GREEN build | same five targets | 0 | `g7-build.log` |
| GREEN run | same ctest, `-V` | 0 | `g8-test.log` |

RED result, exactly: `CarlaPlayImplTest::playImplRendersIntoPlanesOffTheStack` FAILS
(`'!g_seen.leftOnStack && !g_seen.rightOnStack' returned FALSE. (the render planes are on the thread's stack)`,
`CarlaPlayImplTest.cpp:222`); the process then ends in SIGSEGV, reported by ctest as `808 - CarlaPlayImplTest
(SEGFAULT)`. The log has no line for the next slot, `maximumPeriodRendersOnASmallStack`, so the crash is
in it by elimination: the period-sized arrays do not fit its small stack (not confirmed under a debugger).
In the same RED run `CarlaRenderBuffersTest` (8/8) and `CarlaMidiHandoffTest` (7/7) still pass, as the
review said; they test the helper and the MIDI hand-off, not the integration. GREEN: all three pass (3/3,
`CarlaPlayImplTest` slots `initTestCase`, `playImplRendersIntoPlanesOffTheStack`,
`maximumPeriodRendersOnASmallStack`, `cleanupTestCase`).

Gates: `feature_check.py` `EXIT=0`, `RESULT: PASS` for all eight gates (`gate-1.log`), commit `3a7be9a08f`.

Delegation (round 4): Haiku built and ran the tests in three starts (first build, rebuild, RED/restore
sequence); I read the code, wrote the test and its CMake entry, reviewed the logs and wrote this section.
