# cluster null-1

**FIXED** — every bug in this batch is fixed at its measured cause, each with its own
regression test and a recorded RED/GREEN proof; no verdict here is ALREADY-FIXED or
FALSE-POSITIVE.

null-1: FIXED
new-p9-551: FIXED
new-p12-313: FIXED

Three defects in `src/core/AudioEngine.cpp` (and the `Sample::play()` path it renders
through), each decided, fixed and proven on its own, with one commit per bug.

| unit | verdict | commit | product change | regression test |
|---|---|---|---|---|
| `null-1` | FIXED | `f121ea2340` | `src/core/Sample.cpp` | `tests/src/core/AudioEngineNullRenderTest.cpp` (`unityClipAtPositionZeroRendersItsSourceBitExactly`, `ninetyNinePercentVolumeDoesNotNull`) |
| `new-p9-551` | FIXED | `1a4587956a` | `src/core/AudioEngine.cpp`, `include/AudioEngine.h` | `tests/src/core/AudioEngineNullRenderTest.cpp` (`queuedAudioBufferReceiverSeesThePeriodItWasSignalledFor`) |
| `new-p12-313` | FIXED | `a8156f73f0` | `src/core/AudioEngine.cpp`, `include/AudioEngine.h` | `tests/src/core/AudioBusRegistrationTest.cpp` |

**Log location.** The given LOG FOLDER (`~/.local/share/zene-bunny-fix/…`) is not
writable in this session: every write there fails with `EACCES`, including `touch`
on the existing directories, and `chmod`/`setfacl` return 0 without changing it.
All logs and scratch for this cluster therefore live in
`$TMPDIR/logs-null-1/` (`$TMPDIR=/home/kruzzzzy/.cache/zene-tmp/zene-qwen-null-1`),
and nothing was left in `/tmp`. The same mount denies writes to the worktree's
sibling `…-feature-check/` directory, so `feature_check.py` cannot write its own log
files there; every gate it runs was executed directly in the worktree with the
identical command and logged to `$TMPDIR/logs-null-1/89-<gate>.log` (results below).

---

## null-1 — offline render is 1 sample late

**Verdict: FIXED.** Cause found and fixed at the source of the delay, not by trimming
the render.

**Decision and evidence.** The finding's `file:line` was a starting guess, so the
render was measured first. The end-to-end harness
(`research/null-test-2026-10-28/null_test.py`, 4 s float32 WAV, one sample track, clip
at position 0, volume 100, master unity, rendered 32-bit float at the source rate)
reproduced the defect exactly:

```
$TMPDIR/logs-null-1/01-null-test-baseline.log   EXIT=0
unity-48k-a: bitexact at lag 1, out_sha 40b44c5ab634
unity-48k-b: bitexact at lag 1, out_sha 40b44c5ab634   (two renders identical: deterministic)
unity-44k:   bitexact at lag 1, out_sha 461fa4295abf
vol99-48k:   bitexact False, residual -54.35 dBFS RMS
```

A probe of the first frames (`03-probe-first-frames.log`) showed the output starts with
source frame 0 **duplicated** (`out[0]=s[0], out[1]=s[0], out[2]=s[1], …`), i.e. the
engine's frame 0 *is* the master mix's frame 0 — the double buffer is not off by one.
Direct probes of the linked libsamplerate (`05-…` … `10-libsamplerate-delay.log`)
showed every converter mode delays at ratio 1.0, and the linear one — the default —
emits the first input frame twice:

```
LINEAR ratio 1.0: gen=N used=N out: 0 0 1 2 3 4 …
```

`Sample::play()` sent every read through that converter unconditionally, so a 1:1 read
(the common case: a clip at the source's own rate, default pitch) came back one sample
late. That is the cause, and it is in `src/core/Sample.cpp:100-135`, not in the
`AudioEngine` buffer swap.

**Fix (`src/core/Sample.cpp:111-156`).** When the composed ratio is exactly 1.0 there
is nothing to convert, so the read copies the same window/loop-bounded, amplified
frames the converter would have consumed: same cursor advance, same bounds, same
amplification. Every other ratio keeps the converter path and its exact arithmetic
(including `SrcQuality` selection and the mismatch-rate path). No output trimming, no
latency compensation, no change to the render pipeline.

**Regression test — no alignment, bit-exact.**
`tests/src/core/AudioEngineNullRenderTest.cpp` builds the fixture project (one sample
track, clip at position 0, master unity) and renders it through `ProjectRenderer`
exactly as the CLI does, comparing **frame 0 to frame 0** with `==` on the float bits,
at 48000 Hz and 44100 Hz. The source's first frame is deliberately non-zero and the
two channels differ, so a one-sample shift is a mismatch at index 0. The negative
control is the same project at volume 99 %: it must NOT null.

RED with only the product change reverted (the test is unchanged):

```
git diff -- src/core/Sample.cpp > $TMPDIR/logs-null-1/fix-1.patch   # 56 lines, EXIT=0
git apply -R $TMPDIR/logs-null-1/fix-1.patch                        # EXIT=0
cmake --build build --target AudioEngineNullRenderTest -j4          # EXIT=0  (20-red-proof-build.log)
ctest -R '^AudioEngineNullRenderTest$'                             # EXIT=8  (21-red-proof-run.log)
FAIL! ... 9598 mismatched samples, first at frame 1
          (render 0.5/-0.25, source 0.278782/-0.399834), peak residual 0.221218
```

GREEN with it restored:

```
git apply $TMPDIR/logs-null-1/fix-1.patch                          # EXIT=0
cmake --build build --target AudioEngineNullRenderTest zene -j4    # EXIT=0  (22-green-proof-build.log)
ctest -R '^AudioEngineNullRenderTest$'                             # EXIT=0  (23-green-proof-run.log)
100% tests passed out of 1
```

The original end-to-end harness, re-run against the fixed binary:

```
$TMPDIR/logs-null-1/19-null-test-after.log   EXIT=0
unity-48k-a: lag 0, bit-exact True, out_sha da394a577843
unity-44k:   lag 0, bit-exact True, out_sha a08f11e55196
vol99-48k:   bit-exact False, residual -54.35 dBFS RMS   (negative control intact)
```

---

## new-p9-551 — a queued audio-buffer signal can outlive its buffer contents

**Verdict: FIXED.** Confirmed real, with the exact site and a deterministic
reproduction.

**Decision and evidence.** `renderStageMix()` emitted
`nextAudioBuffer(m_outputBufferRead.get())` — a pointer into the engine's reusable
output buffer. `swapBuffers()` zeroes and refills that buffer at the start of the next
period (`AudioEngine::renderNextPeriod`), and the only consumer,
`src/gui/widgets/Oscilloscope.cpp`, connects with the default (queued) connection, so
its slot runs after the emit returns — after later renders have overwritten the data.
The engine's own doc comment on `renderNextPeriod()` already said the returned span is
non-owning and changes on subsequent calls; the signal handed that span across a thread
boundary anyway.

**Fix (`src/core/AudioEngine.cpp` render tap; `include/AudioEngine.h:220-256,700-720`).**
The mixed period is copied into a fixed rotation of `RenderTapSlots` (= 4) preallocated
snapshot buffers — one `memcpy`, no allocation, no lock, no wait — and that pointer is
emitted. It stays valid until the same slot comes round again four periods later, which
is a bounded handoff instead of a pointer into live engine state. The buffers are
allocated once in the constructor. `renderTapSlot()`/`renderTapBuffer()` expose the
rotation for tests and diagnostics; no existing declaration or call site changed.

**Regression test — deterministic, no sleeps.**
`queuedAudioBufferReceiverSeesThePeriodItWasSignalledFor` connects with
`Qt::QueuedConnection`, renders a warm-up period plus a measured period and a third
period, and only then delivers the queued calls with `QCoreApplication::processEvents()`.
The fixture is a ramp, so every period carries different audio and a stale read cannot
accidentally match; the receiver copies at delivery time (as the oscilloscope does).
The queued connection itself forces the late ordering, so no wall-clock timing, sleeps
or threads are involved.

RED with only this bug's product change reverted (`fix-2.patch`, 97 lines):

```
git diff -- include/AudioEngine.h src/core/AudioEngine.cpp > $TMPDIR/logs-null-1/fix-2.patch
git apply -R $TMPDIR/logs-null-1/fix-2.patch                        # EXIT=0
cmake --build build --target AudioEngineNullRenderTest -j4          # EXIT=0  (40-red-b2-build.log)
ctest -R '^AudioEngineNullRenderTest$'                             # EXIT=8  (41/43-red-b2-run.log)
FAIL! ... the queued receiver did not see the period it was signalled for:
          256 of 256 frames differ (at frame 0 it read -0.228667/0.228667,
          expected -0.25/0.25; the NEXT period was -0.239333/0.239333)
```

The receiver read the *next* period, which is precisely the reported defect.

GREEN restored:

```
git apply $TMPDIR/logs-null-1/fix-2.patch                           # EXIT=0
cmake --build build --target AudioEngineNullRenderTest zene -j4     # EXIT=0  (44-green-b2-build.log)
ctest -R '^AudioEngineNullRenderTest$'                              # EXIT=0  (45-green-b2-run.log)
```

Engine/tap family, same build: `AudioEngineNullRenderTest`, `AudioEngineTeardownTest`,
`AudioBusPlayHandleQueueTest`, `TrackSynthesisLoadTest`, `AudioEngineProfilerTest`,
`CpuDiagnosticsDialogTest` — 6/6 pass (`47-family-b2-run.log`, EXIT=0).

---

## new-p12-313 — audio bus registration can allocate or lock on the realtime path

**Verdict: FIXED.** Confirmed real; the reported body was accurate at
`src/core/AudioEngine.cpp:1447`.

**Decision and evidence.** `AudioBusHandle`'s constructor registers itself
(`src/core/AudioBusHandle.cpp:101`) and the class explicitly allows a standalone bus on
the audio path; `SamplePlayHandle.cpp:99` creates exactly such a bus
(`new AudioBusHandle("SamplePlayHandle", false)`) from the render thread.
`addAudioBusHandle()` then did:

```cpp
requestChangeInModel();                 // takes m_changeMutex — can block
m_audioBusHandles.push_back(busHandle); // may grow — allocates
```

both of which the realtime rule forbids. The per-handle play-handle paths next door had
already been made bounded (`AudioBusHandle::addPlayHandle`, cluster 5467); the bus list
itself had not.

**Fix (`src/core/AudioEngine.cpp` addAudioBusHandle and helpers;
`include/AudioEngine.h:225-256`).**
- Realtime scope or rendering thread: the pointer goes into a fixed, preallocated
  64-entry array (no allocation, no lock) and is published under `m_changeMutex` at the
  next period boundary (`renderNextPeriod()` drains it, already holding the lock).
- Any other thread: `try_lock()`; if the model lock is held (by a render period), the
  registration defers through the same array instead of blocking, and the
  control-thread timer publishes it off the audio path.
- `publishAudioBusRegistration()` is idempotent, so a second registration can never
  double-process a bus (which would otherwise leave a dangling pointer once one
  occurrence is erased). `removeAudioBusHandle()` drops a pending entry, so a destroyed
  handle can never be published later.
- Two diagnostic counters (`audioBusRegistrationDeferrals()`,
  `audioBusRegistrationOverflows()`) make the handoff observable; nothing here grows a
  container or waits on the realtime path.

**Regression test (`tests/src/core/AudioBusRegistrationTest.cpp`), two cases.**

1. `registeringBusesInsideRealtimeScopeDoesNotAllocate` — the bus is constructed up
   front (its own construction is not the subject), then registered inside a
   `RealtimeScope` with the allocation probe armed and the bus list shrunk to exactly
   its size so the old `push_back()` has to grow. Allocations and frees must be zero,
   and the next period must publish the bus.
2. `registrationWhileTheModelLockIsHeldDefersInsteadOfWaiting` — the test holds the
   model lock (exactly as a render period does) and registers a bus from another thread.
   The engine's deferral counter is the handshake: it is incremented before the
   registration returns, and the old code cannot increment it at all because it blocked
   on the held lock.

Both are deterministic: the only wait is a bounded yield loop that lets a regressed
build report a failure instead of hanging the suite; the verdict itself comes from the
allocation probe and the deferral counter.

RED with only this bug's product change reverted:

```
cp src/core/AudioEngine.cpp $TMPDIR/logs-null-1/AudioEngine.cpp.b3fixed
# addAudioBusHandle() body restored to the original blocking/allocating version
cmake --build build --target AudioBusRegistrationTest -j4      # EXIT=0  (72-red-b3-build.log)
ctest -R '^AudioBusRegistrationTest$'                         # EXIT=8  (73-red-b3-run.log)
FAIL! registeringBusesInsideRealtimeScopeDoesNotAllocate()
      a bus registered on the realtime path allocated 1 times
FAIL! registrationWhileTheModelLockIsHeldDefersInsteadOfWaiting()
      the registration did not defer while the model lock was held by another thread
      (deferrals 0, expected 1)
```

GREEN restored:

```
cp $TMPDIR/logs-null-1/AudioEngine.cpp.b3fixed src/core/AudioEngine.cpp
cmake --build build --target AudioBusRegistrationTest zene -j4  # EXIT=0  (74-green-b3-build.log)
ctest -R '^AudioBusRegistrationTest$'                          # EXIT=0  (75-green-b3-run.log)
```

Bus/mixer/engine family, same build (`79-family-b3-run.log`, EXIT=0, **12/12**):
`AudioBusRegistrationTest`, `AudioBusHandleTest`, `AudioBusPlayHandleQueueTest`,
`AudioBusTest`, `AudioEngineTeardownTest`, `AudioEngineNullRenderTest`,
`AudioEngineProfilerTest`, `TrackSynthesisLoadTest`, `PdcMixerTest`,
`MixerConcurrencyTest`, `MixerAbRegressionTest`, `MixerRoutingBackwardCompatTest`.

---

## Files changed and registration

| file | why |
|---|---|
| `src/core/Sample.cpp` | unity-ratio direct read (null-1) |
| `src/core/AudioEngine.cpp` | owned render-tap rotation (new-p9-551); deferred bus registration (new-p12-313) |
| `include/AudioEngine.h` | tap accessors/members (new-p9-551); registration seam/members (new-p12-313) |
| `tests/src/core/AudioEngineNullRenderTest.cpp` | new: null test + queued-receiver test |
| `tests/src/core/AudioBusRegistrationTest.cpp` | new: realtime registration tests |
| `tests/CMakeLists.txt` | registers both new tests |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | both new test files registered |
| `tests/upstream-modifications.txt` | the three inherited files' divergences recorded |

Final targeted run after all four commits
(`build/tests`, `$TMPDIR/logs-null-1/98-final-tests.log`, EXIT=0):

```
1/2 Test #14: AudioEngineNullRenderTest ........   Passed
2/2 Test #15: AudioBusRegistrationTest .........   Passed
100% tests passed out of 2
```

All new test sources are listed in `tests/fork-sources.txt` and
`tests/all-sources.txt`; the latter is regenerated from its own header recipe, so the
lists reproduce (`all-sources-reproduce.sh`: `REPRODUCES`, EXIT=0).

---

## Gates

`feature_check.py` runs eight gates. It can only write its log directory to
`<worktree>-feature-check/` — a sibling of the worktree — and this session's sandbox
denies every write outside the worktree, including that directory at mode 0777
(`touch` → `EACCES`; the directory's own `probe.txt` is likewise denied). The gates were
therefore run twice: each one directly in the worktree with the identical command
(logged to `$TMPDIR/logs-null-1/89-<gate>.log`), and then as the unmodified
`feature_check.py` with only its log path redirected into the worktree
(`build/feature-check/`, `$TMPDIR/logs-null-1/96-feature-check-inworktree.log`), which
reported **RESULT: PASS**. That redirection changes no gate logic: `feature_check.py`'s
only use of the path is `log.write_text(...)`:

| gate | command | exit |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 (2234 fork-NEW, 1213 whole-tree, 0 stale) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 (`REPRODUCES`) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 overall (train baseline), **0 hits in this cluster's files** |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 overall (train baseline), **0 hits in this cluster's files** |

The same eight gates through the unmodified runner (log path redirected only):

```
$TMPDIR/logs-null-1/96-feature-check-inworktree.log   EXIT=0
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The two ratchet gates are red on the train on purpose; they PASS when nothing new
appears in a changed file. Filtering their output to this cluster's files
(`src/core/Sample.cpp`, `src/core/AudioEngine.cpp`, `include/AudioEngine.h`,
`tests/src/core/AudioEngineNullRenderTest.cpp`, `tests/src/core/AudioBusRegistrationTest.cpp`)
yields **no** `REGRESSION:` lines. One real hit was found and fixed during this pass: the
new test's `readFloatWav` helper had CCN 16, so it was split into
`readFloatFormat`/`walkFloatChunks`/`parseFloatWav`/`decodeFloatFrames`, each under the
limit. Both new test files are under 500 lines.

---

## Self-check

**Q: The regression test FAILS with your product change reverted and PASSES with it
(exit codes).**

Yes, for each bug, with the test file untouched between the two runs.

| unit | revert | build | test (reverted) | restore | build | test (restored) |
|---|---|---|---|---|---|---|
| `null-1` | `git apply -R fix-1.patch` EXIT=0 | EXIT=0 | **EXIT=8**, 9598 mismatches, first at frame 1 | `git apply fix-1.patch` EXIT=0 | EXIT=0 | **EXIT=0** |
| `new-p9-551` | `git apply -R fix-2.patch` EXIT=0 | EXIT=0 | **EXIT=8**, 256/256 frames differ, read the next period | `git apply fix-2.patch` EXIT=0 | EXIT=0 | **EXIT=0** |
| `new-p12-313` | original body restored | EXIT=0 | **EXIT=8**, 1 allocation + no deferral | fixed body restored | EXIT=0 | **EXIT=0** |

Logs: `20/21` (null-1 RED), `22/23` (null-1 GREEN), `40/43` (new-p9-551 RED),
`44/45` (new-p9-551 GREEN), `72/73` (new-p12-313 RED), `74/75` (new-p12-313 GREEN),
all under `$TMPDIR/logs-null-1/`.

**Q: The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).**

- `null-1`: no timing at all. Pure value comparison of a deterministic float WAV
  rendered at a fixed rate; the source's first frame is injected as
  `SampleFrame(0.5f, -0.25f)` so a shift is a mismatch at index 0. The negative control
  is injected as track volume 99 (vs 100).
- `new-p9-551`: ordering is forced by `Qt::QueuedConnection` plus an explicit
  `QCoreApplication::processEvents()` after all three periods have rendered — no event
  loop runs in between, so the delivery order is fixed. The fixture is an injected ramp
  so every period's audio is distinguishable.
- `new-p12-313`: the allocation case uses the `RealtimeScope` + `AllocationProbe` seam
  (`test::resetAllocationCount()`, `tlAllocationCount`, `tlFreeCount`); the contention
  case holds `requestChangeInModel()` on the test thread, which is an injected held
  lock, and reads the engine's `audioBusRegistrationDeferrals()` counter as the
  handshake. The bounded yield loops only bound how long a *regressed* build is allowed
  to hang before the case reports failure; they never decide a passing verdict.

**Q: The fix is at the cause and complete: every caller and sibling of what you changed
was checked and stays consistent.**

- `null-1`: `Sample::play()` is the one resampling entry point for sample playback. All
  four users of the composed ratio were checked: `SamplePlayHandle` (sample tracks and
  the pooled metronome/frozen-take voices), `SessionAudioCells`, `SessionAudioBake` and
  `SampleBufferResample` all go through this function, so a 1:1 read is now the source
  for all of them; none keeps its own converter. Non-unity ratios (including
  `SrcQuality::Sinc*` and pitched/warped clips) still take the converter path unchanged,
  which `AudioResamplerRatioTest` pins
  (`theConvertersProduceDifferentSamples`, `theExportQualityReachesTheResamplerThroughSamplePlay`,
  `sampleTrackVoiceUsesPreparedConverterOnRenderPath` — all pass, `25-family-run.log`,
  8/8 with `ClipFadesRenderTest`, `SampleClipWindowTest`, `AudioEditingTest`,
  `AudioStretcherTest`, `AudioClipLoopTest`, `ClipOffsetFadePairTest`).
- `new-p9-551`: the only consumer, `Oscilloscope`, copies at slot time exactly as
  before, so it now receives the right period. The engine's own taps
  (`MasterLoudnessTap`, `TunerTap`, `LinkAudioStream`) run in-period and are unaffected;
  the emitted pointer is still valid for the whole delivery path. `AudioEngineTeardownTest`
  and the profiler test pass.
- `new-p12-313`: every construction site of `AudioBusHandle` was enumerated
  (`SamplePlayHandle.cpp:99`, `SessionAudioCells.cpp:55`, the `SampleVoicePool` bus,
  and the track/instrument buses) and all reach `addAudioBusHandle`; all now use the
  deferred path when realtime. The three readers of the list stay consistent because
  publication is still under `m_changeMutex`: `renderStageEffects()`
  (`fillJobQueue`), `Mixer::updateLatencyCompensation()`, and
  `topUpPlayHandleCapacityOnControlThread()`. `removeAudioBusHandle()` still erases
  under the same lock and now also voids a pending entry.

**Q: No collateral damage: old projects and saved data still load as before, and any
limit or check you added does not hit other callers that legitimately exceed it.**

- No project XML, save/load path, or control command changed. `loadProject` +
  `ProjectRenderer` is what both new tests exercise, and the pre-existing
  `AudioEngineTeardownTest`, `PdcMixerTest`, `MixerConcurrencyTest`,
  `MixerAbRegressionTest`, `MixerRoutingBackwardCompatTest` (which covers reading older
  mixer layouts) all pass.
- Default renders are unchanged: the null test proves a unity clip renders exactly as
  before the defect was introduced, and the end-to-end harness keeps its determinism
  check (`determinism: True`) and its negative control (−54.35 dBFS RMS).
- `null-1` changes no limit. `new-p9-551` adds four preallocated period buffers
  (4 × framesPerPeriod frames) and keeps a pointer valid longer, never shorter.
  `new-p12-313` adds a fixed 64-slot registration array — far above the number of
  standalone buses one period can create — and a refused registration is counted and
  retried rather than silently dropped; no existing caller is refused in practice
  (`audioBusRegistrationOverflows() == 0` after the realtime case).

**Q: If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
intended contract that shows it.**

Not applicable: all three verdicts are FIXED, each with a RED/GREEN pair above. The
one claim that could have been FALSE-POSITIVE — that `null-1` was an `AudioEngine`
double-buffer off-by-one — was disproved by measurement: engine frame 0 equalled master
frame 0, and the duplicated frame came from the unity-ratio converter
(`03-probe-first-frames.log`, `05…10-libsamplerate-*.log`). The finding's own
`file:line` was explicitly a guess, so the fix was placed at the measured cause.
