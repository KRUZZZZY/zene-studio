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

- New `plugins/CarlaBase/CarlaRenderBuffers.h` owns the two planes. Its capacity is the
  engine's largest period (`CarlaRenderBuffers::MaxPeriodFrames = 4096`,
  `CarlaRenderBuffers.h:47`), the storage is allocated by its constructor
  (`CarlaRenderBuffers.h:49-55`) when the instrument is built, and `prepare()`
  (`CarlaRenderBuffers.h:59-65`) clears at most `capacity()` frames and returns the count.
  It allocates, frees, locks and waits nothing.
- `plugins/CarlaBase/Carla.h:241-243` adds `CarlaRenderBuffers m_renderBuffers;` to the
  instrument, so the memory lives with the instance instead of on a stack.
- `plugins/CarlaBase/Carla.cpp:544-546` replaces the VLA/`_alloca` block with
  `m_renderBuffers.prepare(out.size())`; the process callback and the copy-back use the
  returned frame count (`Carla.cpp:547-563`). The output span was already zeroed at
  `Carla.cpp:519`, so a span longer than the planes (outside the engine's contract) is
  clipped and its tail stays silent instead of writing past the storage.
- `plugins/CarlaBase/Carla.cpp:67-68` static-asserts the planes' capacity against
  `MAXIMUM_BUFFER_SIZE`, so the two bounds cannot drift apart silently.
- Registration: `tests/fork-sources.txt`, `tests/all-sources.txt` (new header),
  `tests/upstream-modifications.txt` (Carla.cpp / Carla.h reason lines),
  `tests/rt-safety-scope.txt` (`CarlaRenderBuffers::prepare` declared; the allocating
  constructor is on the control thread and deliberately outside the declared render path).

No command, undo, save-format or GUI behaviour is touched. Nothing new on the audio path
allocates, frees, locks, waits or logs.

## Test matrix

| path | how it is covered |
| --- | --- |
| normal period | `renderPlanesArePreallocatedAndBounded`: `prepare(32)` returns 32 and clears both planes. |
| failure / exhaustion | same slot: `prepare(capacity * 4 + 1)` returns `capacity()` and the last plane frame is still zero. |
| reconfiguration | same slot: 1000 further periods, each with a different size, reuse the same `left()`/`right()` pointers, with 0 allocations and 0 frees on the thread. |
| audio-thread constraint | the allocation probe (`tests/src/core/AllocationProbe.h`) wraps the repeated-period loop; the source slot rejects any `_alloca` or `float ... [bufsize]` in `playImpl()`. |
| supported platforms | header-only C++20 with no platform-specific API; the MSVC `_alloca` branch is gone. Built here on Linux/GCC 16.2.1; `carlabase` compiled, so the `#if CARLA_VERSION_HEX` process branch used by this build is the one exercised. |

## Commands and results (all logs in the job log folder)

RED before the product behaviour change (test slot exists, helper seam compiled, `playImpl` still has the stack arrays):

```
cd build/tests && ctest -R CarlaMidiHandoffTest --output-on-failure
EXIT=8
  FAIL! CarlaMidiHandoffTest::playImplRendersIntoPreallocatedPlanes()
    '!body.contains(QRegularExpression(...float\\s+\\w+\\s*\\[\\s*bufsize...))' returned FALSE.
    (playImpl() still sizes a stack array from the period)
log: test-1-red.log
```

The same RED through the focused QtTest slot, after reverting the product behaviour with
the patch from 4b (below): `test-4-reverted.log`, `EXIT=1`.

4b, proof that the test catches the bug. `fix.patch` holds the behaviour change
(`plugins/CarlaBase/Carla.h`, `plugins/CarlaBase/Carla.cpp`); the new helper header stays
in place because the test compiles against that seam, and the only caller is `playImpl()`:

```
git diff -- plugins/CarlaBase/Carla.h plugins/CarlaBase/Carla.cpp > .../logs/fix.patch
EXIT=0
git apply -R .../logs/fix.patch
EXIT=0
cmake --build build --target carlabase CarlaMidiHandoffTest -j4   # build-5-reverted.log
EXIT=0
cd build/tests && ./CarlaMidiHandoffTest playImplRendersIntoPreallocatedPlanes   # test-4-reverted.log
EXIT=1   (FAIL! playImpl() still sizes a stack array from the period)

git apply .../logs/fix.patch
EXIT=0
cmake --build build --target carlabase CarlaMidiHandoffTest -j4   # build-6-restored.log
EXIT=0
cd build/tests && ./CarlaMidiHandoffTest playImplRendersIntoPreallocatedPlanes renderPlanesArePreallocatedAndBounded
EXIT=0   (4 passed, 0 failed; test-5-restored.log)
```

GREEN with the full family, final tree:

```
cd build/tests && ctest -R CarlaMidiHandoffTest --output-on-failure   # test-7-green-ctest-final.log
EXIT=0   (1/1 test passed)
```

Realtime scope sweep (the new declared path):

```
cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure   # test-6-rtsafety.log
EXIT=0
```

Gates:

```
FEATURE_CHECK_LOG_DIR=.../logs/feature-check \
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # feature-check.log
EXIT=0
  PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
       no-tautology / doc-figures / complexity / file-length
  RESULT: PASS
```

## Round 2: deterministic stress-test rendezvous

The reviewer sent back the focused concurrency test: it could observe no producer input
during rendering. The QA commit on this branch (`01308f2afd`) added a first-event barrier,
but that barrier has its own race. Producers 1-3 enqueue as soon as `renderStarted` is set,
so they can fill the fixed 512-event queue while the renderer waits for `accepted[0]`;
producer 0's first enqueue is then refused and both sides wait forever. Measured before
this change: 30 direct runs of the slot, each under a 10 s bound, timed out 3 times
(`EXIT=124`, no test result) and passed 27 times (`test-1-qa-loop.log`).

The rendezvous now uses two one-way handshakes, and nothing is queued before the measured
period:

1. Each producer increments `activeProducers`, then `producersReady`, then spins on
   `renderObservedProducers`. It has not queued an event yet.
2. The renderer waits for `producersReady == ProducerCount`, then enters the period loop.
   Its first `renderPeriod()` sees `activeProducers != 0`, increments
   `periodsWithProducerInput` and publishes `renderObservedProducers`.
3. The producers start enqueueing only after that publish; the renderer drains while they
   run. The overlap, ordering and allocation assertions are unchanged.

There is no sleep, timeout or wall-clock condition in this path; the only waits are the two
atomic handshakes. After the change:

```
cmake --build build --target CarlaMidiHandoffTest -j4   # build-2-handshake.log
EXIT=0
100 direct runs of the slot, each under a 10 s bound   # test-2-handshake-loop.log
EXIT=0   (100 passed, 0 timed out)
20 ctest -R CarlaMidiHandoffTest runs                   # test-3-ctest-loop.log
EXIT=0   (20 passed)
```

4b rerun with the new test file. The product fix is already committed, so the patch is the
branch's product diff rather than an unstaged diff:

```
git diff c6b40333d8..HEAD -- plugins/CarlaBase/Carla.h plugins/CarlaBase/Carla.cpp > .../logs/fix.patch
EXIT=0
git apply -R .../logs/fix.patch
EXIT=0
cmake --build build --target carlabase CarlaMidiHandoffTest -j4   # build-4-reverted.log
EXIT=0
cd build/tests && ctest -R CarlaMidiHandoffTest --output-on-failure   # test-5-reverted.log
EXIT=8   (FAIL! playImpl() still sizes a stack array from the period)
git apply .../logs/fix.patch
EXIT=0
cmake --build build --target carlabase CarlaMidiHandoffTest -j4   # build-5-restored.log
EXIT=0
cd build/tests && ctest -R CarlaMidiHandoffTest --output-on-failure   # test-6-restored.log
EXIT=0
```

## Self-check

- **Regression test fails with the product change reverted, passes with it.** Reverted
  `Carla.h`/`Carla.cpp` via `git apply -R fix.patch` (`EXIT=0`), rebuilt (`EXIT=0`), test
  `EXIT=1`; restored with `git apply fix.patch` (`EXIT=0`), rebuilt (`EXIT=0`), test
  `EXIT=0`. Logs `test-4-reverted.log`, `test-5-restored.log`, `fix.patch`.
- **The test is deterministic.** It injects the period sizes (`32`, `capacity * 4 + 1`,
  1000 sizes below 64 frames) and counts allocations on the calling thread; there is no
  wall-clock assertion, sleep, thread or race in either new slot. The source slot reads
  the shipping `Carla.cpp` through the compile-time `CARLA_SOURCE_PATH`, the same pattern
  as the existing `opulenzZeroFrameCountStopsLoop` source regression. The focused
  concurrency slot now injects its overlap through the `producersReady` /
  `renderObservedProducers` atomics (Round 2 above): it passed 100 direct runs and 20
  focused CTest runs with no timeout, and its overlap, ordering and allocation assertions
  are unchanged. Before the rendezvous, 3/30 direct runs hung on the old barrier.
- **Cause and completeness.** The cause was the two stack arrays in `playImpl()`; they
  are gone from both branches (VLA and `_alloca`). Every caller was checked:
  `InstrumentPlayHandle` (audio thread) and `CarlaVst2Instrument::renderPeriod` (worker,
  bounded by `Renderer::MaxPeriodFrames = 4096`). The sibling `CarlaVst2Effect::Rack`
  already preallocates planar buffers and is unchanged. `Carla.h` declares the member and
  `Carla.cpp` uses it, and `carlabase` plus the test were rebuilt after the header change.
- **No collateral damage.** A period never exceeds 4096 frames in the engine
  (`m_framesPerPeriod = min(framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`, with
  `framesPerAudioBuffer` clamped to `[32, 4096]`), so every legitimate period now renders
  exactly the frames and samples it did before. The planes are 32 KiB per instance,
  allocated once at construction, and repeated periods reuse the same pointers (asserted
  in the test). A span above the engine's maximum (outside the contract) is clipped and
  the already-zeroed tail is silent, instead of reading/writing past a stack array. No
  saved data, save format or load path changes. No limit rejects another caller: the
  bound equals the engine's own maximum, and `CarlaVst2Renderer` already clamps to it.
- **Verdict evidence.** This is a FIXED verdict, so the ALREADY-FIXED / FALSE-POSITIVE
  rule does not apply; the pre-fix code is cited above at
  `plugins/CarlaBase/Carla.cpp:535-541` (HEAD).
- **Sibling commands (`app.*`, `arrangement.*`).** The fix commit `05b41a3b7` changes no
  command file: `git show --stat 05b41a3b7` lists only `Carla.cpp`, `Carla.h`, the new
  `CarlaRenderBuffers.h`, three registration manifests, `rt-safety-scope.txt`,
  `tests/CMakeLists.txt`, the Carla test and this report. The sibling list in the
  submit check comes from the branch's accumulated diff against its base, not from this
  fix. No control command, undo step or reversibility class is added or changed, so none
  of those commands is affected; the engine-level period bound they all share is
  unchanged.

## Other findings

- None unrelated to this fix. The focused stress-test race reported in Round 1 is fixed by
  the handshake in Round 2 above and is no longer outstanding.
