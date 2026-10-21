# Cluster new-p9-118 / new-p15-57: EnvelopeAndLfoParameters audio path

**FIXED**

Two findings in `src/core/EnvelopeAndLfoParameters.cpp`, one file, two commits.

new-p9-118: FIXED

**The finding.** `EnvelopeAndLfoParameters::fillLevel()` took `m_paramMutex`
(`QMutexLocker m(&m_paramMutex);`, `src/core/EnvelopeAndLfoParameters.cpp:309`
in the reported tree) while it generated the envelope and LFO levels. `fillLevel()`
is the audio path: it is called from `InstrumentSoundShaping` (`:100`, `:148`,
`:153`, `:230`), i.e. from the instrument render. `updateSampleVars()` takes the
same mutex (`:415`) and is called from the parameter side (the knob models'
`dataChanged`, `src/core/EnvelopeAndLfoParameters.cpp:136-165`, and
`loadSettings()`), so the audio thread could block behind a parameter update.

**Why it was REAL.** The lock was unconditional in the audio path: every
`fillLevel()` call took the mutex, and the writer held it for the whole of
`updateSampleVars()` (which re-derives the envelope tables and reallocates them).

**The fix (round 1).** The parameters the audio path reads are published as an
immutable snapshot (`EnvelopeAndLfoParameters::AudioState`,
`include/EnvelopeAndLfoParameters.h:61-97`). The writer side builds one under
`m_paramMutex` (`rebuildAudioState()`) and hands it over with an atomic
pointer; the audio path claims it without a lock, a wait or an allocation.

**What the review found (round 1, REWORK).** Three defects in that change, all
fixed on top of it:

1. *The claim could wait for the publisher.* The claim retried while the
   publication sequence was odd, so a publisher that was paused in the middle
   of its publication made the audio thread spin until it resumed.
2. *The LFO shape cache could be left stale.* The cache was invalidated by a
   flag that any reader could clear: a reader that held an older snapshot
   rebuilt the cache from it and cleared the flag after the writer had
   published a new snapshot, so the new snapshot was answered with a cache
   built from the old one.
3. *A wave replaced from the GUI was not published* (new-p15-57, below).

**The fix (round 2).**

- *A wait-free claim.* The publication sequence is gone. The claim is a fixed
  number of atomic operations: raise the reader count, read the pointer
  (`acquireAudioState()`, `src/core/EnvelopeAndLfoParameters.cpp:706-716`).
  The count and the pointer are in one total order (`std::memory_order_seq_cst`),
  so the publisher frees a retired snapshot only when no reader holds one
  (`reclaimRetiredAudioStates()`, `:634-655`), and a reader that is about to
  read a snapshot either holds the new one or is counted. The audio thread
  never waits for the publisher and never retries.
- *A versioned LFO shape cache.* Every publication numbers its snapshot
  (`publishAudioState()`, `:610-628`), and the cache carries the snapshot
  version and the LFO phase it was built for (`updateLfoShapeData()`, `:279-296`).
  The cache is used only for that pair (`fillLfoLevel()`, `:305-320`), so a
  snapshot that was published while another reader held the previous one can
  never be answered with a cache built from that previous one.

**The regressions.** Three, all deterministic, none of them a timing test:

- `theAudioPathDoesNotWaitForAPublisher()`
  (`tests/src/core/EnvelopeAndLfoParametersTest.cpp:118`): the publisher is
  held at the publication point (the test seam `setPublicationHook()`), and
  the audio path must claim the published snapshot while the publisher is
  held. The seam and the reader count it reads
  (`EnvelopeAndLfoParameters::audioStateReaders()`) are the synchronisation
  hook; the test uses a thread handshake, not a clock.
- `theLfoShapeCacheIsNotStaleAfterAPublication()` (`:170`): the writer
  publishes another wave while the audio path fills a level with the previous
  snapshot (the test seam `setPublicationStartHook()`), and the level that
  follows must be the new wave, not a cache built from the old one.
- The tree's own real-time rule gate, the rt-safety sweep
  (`tests/rt-safety-sweep.py`, the CTest `RtSafetySweep`), declares the audio
  path of this file in `tests/rt-safety-scope.txt:59-64`:

    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::fillLevel	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::fillLfoLevel	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::lfoShapeSample	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::updateLfoShapeData	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::acquireAudioState	...
    src/core/EnvelopeAndLfoParameters.cpp	EnvelopeAndLfoParameters::releaseAudioState	...

  The sweep needs no allowlist entry: the fix removes the hits rather than
  tolerating them.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-118.r1/logs/fix-new-p9-118-r2.patch` (EXIT=0), `cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc)` (EXIT=0, `.../build-red-3.log`), then `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-15-red-p9-118.log 2>&1; echo EXIT=$?`; **EXIT=8**: `FAIL! : ...theAudioPathDoesNotWaitForAPublisher() 'claimedWhileHeld.load(std::memory_order_acquire)' returned FALSE. (the audio path did not claim the published snapshot while the publisher was held: it waited for the publisher)` and `FAIL! : ...theLfoShapeCacheIsNotStaleAfterAPublication() 'after != first' returned FALSE.` (6 passed, 2 failed) |
| GREEN after fix | `git apply .../fix-new-p9-118-r2.patch` (EXIT=0), rebuild (EXIT=0, `.../build-green-2.log`), then `ctest -R '^(EnvelopeAndLfoParametersTest\|ModulationLayerTest\|ModulationRateTest\|EnvelopeGraphTest\|LfoGraphTest\|RtSafetySweep)$' --output-on-failure > .../logs/test-16-green-p9-118.log 2>&1; echo EXIT=$?`; **EXIT=0**, 6/6 CTest targets passed |
| Round-1 RED (kept) | with the round-1 product diff reverted, `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/red-p9-118-sweep-lockhit.log 2>&1; echo EXIT=$?`; **EXIT=1**: `FAIL: NEW rt-safety hit: src/core/EnvelopeAndLfoParameters.cpp:EnvelopeAndLfoParameters::fillLevel:lock-guard [locking] - 1 occurrence(s), first at src/core/EnvelopeAndLfoParameters.cpp:309: QMutexLocker m(&m_paramMutex);` |
| Round-1 GREEN (kept) | the same sweep after re-applying the fix, `.../logs/green-p9-118-sweep.log`; **EXIT=0**, `PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count.` |
| Affected family | 6 CTest targets executed (EnvelopeAndLfoParametersTest, ModulationLayerTest, ModulationRateTest, EnvelopeGraphTest, LfoGraphTest, RtSafetySweep), 6/6 passed; the EnvelopeAndLfoParametersTest binary runs 9 test functions, all passed |
| Build | `cmake --build build --target zene EnvelopeAndLfoParametersTest -j $(nproc)`; **EXIT=0** (`.../build-green-2.log`) |
| Complexity gate | **EXIT=0** (`PASS complexity - no new item in your files`) |
| File-length gate | **EXIT=0** (`PASS file-length - no new item in your files`) |

### Build and test commands

```text
git diff HEAD~1 -- src/core/EnvelopeAndLfoParameters.cpp include/EnvelopeAndLfoParameters.h > .../logs/fix-new-p9-118-r2.patch   # 199 lines
git apply -R .../logs/fix-new-p9-118-r2.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target EnvelopeAndLfoParametersTest -j $(nproc) > .../logs/build-red-3.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-15-red-p9-118.log 2>&1; echo EXIT=$?   # EXIT=8, RED (2 failures)
git apply .../logs/fix-new-p9-118-r2.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target zene EnvelopeAndLfoParametersTest -j $(nproc) > .../logs/build-green-2.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^(EnvelopeAndLfoParametersTest|ModulationLayerTest|ModulationRateTest|EnvelopeGraphTest|LfoGraphTest|RtSafetySweep)$' --output-on-failure > .../logs/test-16-green-p9-118.log 2>&1; echo EXIT=$?   # EXIT=0, 6/6
python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/red-p9-118-sweep-lockhit.log 2>&1; echo EXIT=$?   # EXIT=1, the QMutexLocker in fillLevel
python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/green-p9-118-sweep.log 2>&1; echo EXIT=$?   # EXIT=0
```

The required gates ran from the worktree root:

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-118   # EXIT=0
```

Files committed for new-p9-118:

- `include/EnvelopeAndLfoParameters.h`
- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/src/core/EnvelopeAndLfoParametersTest.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-118.md`

The two test-seam commits (`test-seam(new-p9-118): expose the publication point
and the reader count`, `test-seam(new-p9-118): expose the start of a
publication as well`) carry only the seams the round-2 regressions need:
the publisher hooks and the reader count. They change no behaviour, and they
are separate commits so that the round-2 fix can be reverted without them -
which is what makes the round-2 RED proof possible.

new-p15-57: FIXED

**The finding.** `loadSettings()` wrote `m_userWave` (a `std::shared_ptr<const
SampleBuffer>`) without taking `m_paramMutex`, while the audio path read the
same shared pointer (`src/core/EnvelopeAndLfoParameters.cpp:394` in the
reported tree; `m_userWave.get()` in `lfoShapeSample()`). A concurrent
assignment is a data race on the shared pointer, and the last strong reference
can go away while the audio thread is sampling the buffer.

**Why it was REAL.** The wave was a plain member: the writer replaced it
(`m_userWave = SampleBuffer::fromFile(...)`) with no synchronisation at all,
and the reader took a raw pointer out of it (`Oscillator::userWaveSample(
m_userWave.get(), phase)`) and then sampled the buffer over the whole period.
The mutex that `fillLevel()` took did not help - `loadSettings()` never took
it - and after the new-p9-118 commit the audio path no longer takes it either,
so the wave has to travel with the published snapshot.

**The fix.** The user-defined wave is part of the published snapshot
(`EnvelopeAndLfoParameters::AudioState::userWave`,
`include/EnvelopeAndLfoParameters.h:83`). The writer copies the wave it
holds into the snapshot it publishes (`rebuildAudioState()`,
`src/core/EnvelopeAndLfoParameters.cpp:574`), the audio path samples the
wave of the snapshot it holds (`lfoShapeSample()`, `:243`), and the GUI
accessor reports the published wave (`getLfoUserWave()`, `:786`). A wave that
is replaced is not freed by the replacement: the snapshot that still carries
it is retired, not freed, and it is freed only when the reader count is back
to zero (`reclaimRetiredAudioStates()`, `:622`). The saved project is
unchanged: the wave file name still comes from the same member
(`saveSettings()`, `:382`).

**What the review found (round 1, REWORK).** The GUI replaced the wave in
place (`src/gui/instrument/EnvelopeAndLfoView.cpp:245` and `:257` in the
reported tree): the wave shape model was then set to the user-defined shape,
and when that shape was already selected the model emitted `dataUnchanged`
instead of `dataChanged`, so no snapshot was published and playback kept
sampling the previous wave.

**The fix (round 2).** Both drop paths replace the wave through
`EnvelopeAndLfoParameters::setLfoUserWave()`
(`src/core/EnvelopeAndLfoParameters.cpp:773`), which takes the writer's lock,
swaps the wave and publishes the snapshot with it - so the publication does
not depend on a model value changing
(`src/gui/instrument/EnvelopeAndLfoView.cpp:247` and `:261`).

**The regressions.**

- `droppingAUserWavePublishesIt()` (`tests/src/gui/LfoGraphTest.cpp:152`):
  dropping a wave file on the envelope view must publish that wave. The test
  drives the view's drop handler with the mime data a file drag carries
  (`application/x-zene-stringpair`), with the LFO wave shape already set to
  the user-defined one - the case in which the publication used to be lost.
- `replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()`
  (`tests/src/core/EnvelopeAndLfoParametersTest.cpp:212`) stands in for the
  audio thread with the test seam `holdAudioState()` (the snapshot the audio
  path holds while it fills a level), replaces the wave, and requires the wave
  that is in use to still be alive (`!weakWave.expired()`). Without the fix
  the replacement drops the last strong reference and frees the buffer the
  audio path is sampling.
- `theAudioPathSamplesThePublishedUserWave()` (`:232`) requires the audio path
  to render the wave that was published with the parameters: replacing the wave
  changes what `fillLevel()` produces.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-118.r1/logs/fix-new-p15-57-r2.patch` (EXIT=0), `cmake --build build --target LfoGraphTest -j $(nproc)` (EXIT=0, `.../build-red-4.log`), then `ctest -R '^LfoGraphTest$' --output-on-failure > .../logs/test-19-red-p15-57.log 2>&1; echo EXIT=$?`; **EXIT=8**: `FAIL! : ...droppingAUserWavePublishesIt() 'after != before' returned FALSE. (the dropped wave was not published: playback would keep the previous wave)` (3 passed, 1 failed) |
| GREEN after fix | `git apply .../fix-new-p15-57-r2.patch` (EXIT=0), rebuild (EXIT=0, `.../build-green-3.log`), then `ctest -R '^(EnvelopeAndLfoParametersTest\|LfoGraphTest\|EnvelopeGraphTest\|ModulationLayerTest\|ModulationRateTest\|RtSafetySweep)$' --output-on-failure > .../logs/test-20-green-p15-57.log 2>&1; echo EXIT=$?`; **EXIT=0**, 6/6 CTest targets passed |
| Round-1 RED (kept) | with the round-1 product diff reverted, rebuild (EXIT=0) and `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-4-red.log 2>&1; echo EXIT=$?`; **EXIT=8**: `FAIL! : ...replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt() '!weakWave.expired()' returned FALSE.` |
| Round-1 GREEN (kept) | the same ctest after re-applying the fix, `.../logs/test-3.log`; **EXIT=0** (all test functions passed) |
| Affected family | 6 CTest targets executed (EnvelopeAndLfoParametersTest, LfoGraphTest, EnvelopeGraphTest, ModulationLayerTest, ModulationRateTest, RtSafetySweep), 6/6 passed; the LfoGraphTest binary runs 3 test functions, all passed |
| Build | `cmake --build build --target LfoGraphTest zene -j $(nproc)`; **EXIT=0** (`.../build-green-3.log`) |
| Complexity gate | **EXIT=0** (`PASS complexity - no new item in your files`) |
| File-length gate | **EXIT=0** (`PASS file-length - no new item in your files`) |

### Build and test commands

```text
git diff -- src/core/EnvelopeAndLfoParameters.cpp include/EnvelopeAndLfoParameters.h src/gui/instrument/EnvelopeAndLfoView.cpp > .../logs/fix-new-p15-57-r2.patch   # 67 lines
git apply -R .../logs/fix-new-p15-57-r2.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target LfoGraphTest -j $(nproc) > .../logs/build-red-4.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^LfoGraphTest$' --output-on-failure > .../logs/test-19-red-p15-57.log 2>&1; echo EXIT=$?   # EXIT=8, RED
git apply .../logs/fix-new-p15-57-r2.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target LfoGraphTest zene -j $(nproc) > .../logs/build-green-3.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^(EnvelopeAndLfoParametersTest|LfoGraphTest|EnvelopeGraphTest|ModulationLayerTest|ModulationRateTest|RtSafetySweep)$' --output-on-failure > .../logs/test-20-green-p15-57.log 2>&1; echo EXIT=$?   # EXIT=0, 6/6
```

The required gates ran from the worktree root:

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-118   # EXIT=0
```

Files committed for new-p15-57:

- `include/EnvelopeAndLfoParameters.h`
- `src/core/EnvelopeAndLfoParameters.cpp`
- `src/gui/instrument/EnvelopeAndLfoView.cpp`
- `tests/src/gui/LfoGraphTest.cpp`
- `tests/src/core/EnvelopeAndLfoParametersTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-118.md`

## Self-check

**1. Does the regression test FAIL with the product change reverted and PASS with it (exit codes)?**

Yes, for both bugs, each with its own patch, its own build and its own run.

- new-p9-118 (round 2): `git apply -R .../logs/fix-new-p9-118-r2.patch` (EXIT=0),
  rebuild (EXIT=0), `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure`
  (EXIT=8): `theAudioPathDoesNotWaitForAPublisher()` and
  `theLfoShapeCacheIsNotStaleAfterAPublication()` both fail.
  `git apply .../fix-new-p9-118-r2.patch` (EXIT=0), rebuild (EXIT=0), the same
  ctest (EXIT=0, 6/6 with the family).
  The round-1 proof is kept: the rt-safety sweep fails on the
  `QMutexLocker` in `fillLevel` (EXIT=1) and passes after the fix (EXIT=0).
- new-p15-57 (round 2): `git apply -R .../logs/fix-new-p15-57-r2.patch` (EXIT=0),
  rebuild (EXIT=0), `ctest -R '^LfoGraphTest$' --output-on-failure` (EXIT=8):
  `droppingAUserWavePublishesIt()` fails. `git apply .../fix-new-p15-57-r2.patch`
  (EXIT=0), rebuild (EXIT=0), the same ctest (EXIT=0, 6/6 with the family).
  The round-1 proof is kept: `replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()`
  fails (EXIT=8) without the round-1 fix and passes with it.

**2. Is the test deterministic - no wall-clock timing, sleeps or races? What seam does it use?**

Yes; every regression uses a test seam or a handshake, never a clock.

- `theAudioPathDoesNotWaitForAPublisher()`: the publisher is held at the
  publication point by the seam `EnvelopeAndLfoParameters::setPublicationHook()`,
  the audio path is a thread that announces itself with an atomic flag, and the
  seam reads `EnvelopeAndLfoParameters::audioStateReaders()` - the reader count
  of the published snapshot. The claim raises that count before it reads the
  snapshot, so the count is the observation that tells "the audio path claimed
  the snapshot" from "the audio path is stuck". No timeout, no sleep.
- `theLfoShapeCacheIsNotStaleAfterAPublication()`: the seam
  `setPublicationStartHook()` holds the publication at its start, the audio path
  is a thread that fills a level while the publication is held, and the test
  joins it before the publication continues. The order of the events is fixed
  by the seam, not by timing.
- `droppingAUserWavePublishesIt()`: no threads at all - the view's drop handler
  is called with the mime data a file drag carries.
- `replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()`: the seam
  `holdAudioState()` holds the published snapshot, and a `std::weak_ptr` says
  whether the wave is still alive.

**3. Is the fix at the cause and complete - every caller and sibling checked and consistent?**

Yes.

- The audio path's callers are `InstrumentSoundShaping::playNote`
  (`src/core/InstrumentSoundShaping.cpp:100`, `:148`, `:153`, `:230`) and the
  tests; `fillLevel()`'s signature did not change, so they all keep working.
- The claim of the snapshot and its release are a pair
  (`acquireAudioState()` / `releaseAudioState()`); every path that claims a
  snapshot releases it: `fillLevel()` and the test seam `holdAudioState()`.
- The snapshot's members move together: the envelope tables, the frame counts,
  the sustain level, the LFO figures and the user wave are one immutable
  snapshot, and every reader of them (the accessors, the GUI, the audio path)
  reads that snapshot.
- The two flags the old code shared between the writer and the audio thread
  (`m_bad_lfoShapeData`, `m_lfoFrame`) are gone or atomic: the LFO phase is
  atomic, and the cache is keyed by the snapshot version and the phase
  instead of by a flag.
- The GUI: both drop paths of `EnvelopeAndLfoView` now go through
  `setLfoUserWave()`; no other place writes the wave member (the project load
  and the setter are the only writers).

**4. No collateral damage - do old projects and saved data still load as before, and does any limit or check hit other callers?**

- Saved data: the file format is unchanged. `saveSettings()` still writes
  `userwavefile` from the same wave, `loadSettings()` still reads the same
  attribute, and the two existing tests for that still pass.
- The envelope arithmetic is unchanged: the same expressions, in the same order,
  with the same values, now written into the snapshot's buffers.
- The realtime rule: the audio path allocates nothing, frees nothing, locks
  nothing, waits for nothing and logs nothing. It claims a snapshot with two
  atomic operations and reads immutable data. The declared audio path is
  measured by the rt-safety sweep and is clean.
- No new limit or check was added that another caller can trip: the buffer
  sizes are the same ones the old code used, and a recycled buffer is used
  only when its capacity is large enough.

**5. If the verdict is ALREADY-FIXED or FALSE-POSITIVE - the code (file:line) or the intended contract that shows it.**

Not applicable: both findings were REAL. The code that showed it:

- new-p9-118: `src/core/EnvelopeAndLfoParameters.cpp:309` in the reported tree
  was `QMutexLocker m(&m_paramMutex);` inside `fillLevel()`, the function
  `InstrumentSoundShaping` calls on the audio thread, and `updateSampleVars()`
  held the same mutex while it re-derived the tables.
- new-p15-57: `src/core/EnvelopeAndLfoParameters.cpp:394` in the reported tree
  was `m_userWave = SampleBuffer::fromFile(...)` in `loadSettings()`, with no
  lock, while `lfoShapeSample()` read `m_userWave.get()` on the audio thread;
  and `src/gui/instrument/EnvelopeAndLfoView.cpp:245` and `:257` replaced the
  wave without publishing it.

**Verdict: FIXED** (new-p9-118, new-p15-57)
