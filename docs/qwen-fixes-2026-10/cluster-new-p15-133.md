ALREADY-FIXED

## new-p15-133 - ALREADY-FIXED

The finding describes the instrument-lifetime hole that clusters 5186 / new-p12-159 already fixed on
this branch: the audio view's raw `Instrument*` is now covered by the SAME per-slot liveness token
and writer drain as the parameter model, `InstrumentTrack` notifies before its instrument's derived
destructor runs, and every instrument deletion goes through that one notifier. Both fix commits are
ancestors of the branch tip (`3436a808b6` was HEAD when the tests below ran; they remain ancestors
of this report's commit); no product change is needed or made in this unit.

The finding's line numbers are the pre-fix snapshot: `include/ModulationLayer.h:304` is
`ModulationAudioView::Entry::instrument` at HEAD `:316`, and
`src/core/ModulationLayerPublisher.cpp:211,227-230` is the token/model-watch block that now also
connects the instrument watch at `:233-237`.

### The fix that is already on this branch

- `eb81efcba1` "fix(core): tie per-voice modulation target to instrument lifetime (cluster 5186)" -
  `git merge-base --is-ancestor eb81efcba1 HEAD` EXIT=0. Its diff adds
  `include/ModulationLayer.h` `m_instrumentWatch`, the `Instrument::aboutToBeDestroyed` signal
  (`include/Instrument.h`), the emit in `Instrument::~Instrument()` (`src/core/Instrument.cpp`), the
  `publishView()` watch (
  `src/core/ModulationLayerPublisher.cpp`) and the first `destroyInstrument()` call in the track
  destructor.
- `f5cd870bbd` "fix(tracks): close the modulation target before an instrument tears down (cluster
  5186)" - `git merge-base --is-ancestor f5cd870bbd HEAD` EXIT=0. Its diff adds
  `InstrumentTrack::destroyInstrument()` (`include/InstrumentTrack.h`, `src/tracks/InstrumentTrack.cpp`)
  and routes every replacement branch through it.

### Code evidence at HEAD (all line numbers read this session)

- `include/ModulationLayer.h:316` - `ModulationAudioView::Entry::instrument` ("valid while the
  entry's token is alive"): the pointer the finding names.
- `include/ModulationLayer.h:409-441` - `targetAlive()` / `beginTargetWrite()` / `endTargetWrite()`:
  the pin is taken only while the slot's token is the live one, and `beginTargetWrite()` re-checks
  `targetAlive()` after its CAS, so a pin cannot be taken once the slot's closing bit is set.
- `include/ModulationLayer.h:517` - `m_instrumentWatch`, the per-slot instrument connection.
- `src/core/ModulationLayerPublisher.cpp:233-237` - `publishView()` connects
  `Instrument::aboutToBeDestroyed` to `retireTarget()` with the same `alive`/`writers`/`token` the
  model watch uses; `:198-199` disconnects and clears it when a slot is re-published.
- `src/core/ModulationLayerPublisher.cpp:130-158` - `retireTarget()`: sets the slot's closing bit,
  waits (`std::this_thread::yield()`, control thread) until the audio writer count drains, and only
  then clears the alive token. The wait covers `applyVoiceEntry()` because the audio thread holds a
  `TargetWritePin` across it (`:302-314`), and `:266` is the `entry.instrument->modulateNote(...)`
  call the finding is about.
- `src/core/ModulationLayerPublisher.cpp:273-282` - `drivingSource()` skips the entry when the token
  is not alive; `:300-302` re-checks with `beginTargetWrite()`. A slot whose model is null is never
  issued a token (publishView `continue`s before the watch), so no instrument call can reach an
  unwatched slot either.
- `src/tracks/InstrumentTrack.cpp:252-258` - `destroyInstrument()`: `emit
  m_instrument->aboutToBeDestroyed()` then `delete m_instrument`. The emit is on the control thread,
  before the instrument's derived destructor, so the target is closed and the audio pin drained
  before teardown starts.
- `src/tracks/InstrumentTrack.cpp:236, 1292, 1327, 1357, 1364, 1445, 1466` - every deletion site:
  the track destructor, both branches of `loadTrackSpecificSettings()`, both failure/replacement
  paths of `loadConvertedInstrument()`, `loadInstrument()`, and `setInstrumentForTest()`.
- `src/core/Instrument.cpp:76-79` - `~Instrument()` keeps the emit as the fallback for an instrument
  destroyed outside a track, and `include/Instrument.h:287` declares the signal.

Siblings checked: the only `delete` of an `Instrument` in product code is
`src/tracks/InstrumentTrack.cpp:256`; the four `Instrument::instantiate()` call sites in `src/` are
all preceded by `destroyInstrument()` (`:1292+1293`, `:1327+1328`, `:1357+1360`, `:1445+1448`), and
`InstrumentLayer`'s instrument is hosted in a private `InstrumentTrack`. Nothing else calls
`modulateNote()` on an instrument: the only call is
`src/core/ModulationLayerPublisher.cpp:266`.

### Existing regressions (tests/src/core/ModulationAudioViewTest.cpp)

- `:330` `anInstrumentTargetIsRetiredWhenDestroyed` - deleting an instrument retires the slot.
- `:366` `aLoadedInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown` - the loader's
  `instrument` node path replaces the probe instrument; the probe's own destructor reads the
  publisher's atomic token and asserts the target was already closed.
- `:384` `aLegacyInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown` - the same through
  the loader's legacy node path.

They are deterministic: the probe checks `publisher.targetAlive(0, token)` synchronously from its
destructor, inside `destroyInstrument()`. No sleep, timer or thread is involved.

### Test matrix (walked in this session)

| path | coverage | result |
|---|---|---|
| normal block through the view | `ModulationAudioViewTest::theViewWritesWhatTheRuntimeWrites`, `aBlockThroughTheViewAllocatesNothing` | PASS (test-4) |
| instrument teardown while a per-voice route targets it | the three tests above | PASS on HEAD (test-1, test-4); FAIL with the instrument watch removed (test-2) |
| reconfiguration / re-publish | `aRepublishRetiresEveryOlderView`; `publishView()` disconnects both watches at `:198-199` | PASS (test-4) |
| voice-pool exhaustion | `voicesFromTracksNoLongerFollowedAreDiscarded` (fixed pool, no growth) | PASS (test-4) |
| audio-thread constraints | the fix adds one `QObject::connect` per published slot on the control thread; the audio path still takes one atomic CAS and one `fetch_sub`, no allocation/lock/log; `aBlockThroughTheViewAllocatesNothing` | PASS (test-4) |
| platforms | plain C++20 + Qt 5/6 functor connect, `std::atomic`, `std::this_thread::yield`; no platform API | built here; MSVC/Qt5 not run here |

### Commands and EXIT codes (all run in this worktree this session)

```
cmake --build build --target ModulationAudioViewTest -j4 > <logs>/build-1.log 2>&1      EXIT=0  (build-exit.txt)
cd build/tests && ctest -R '^ModulationAudioViewTest$' --output-on-failure > <logs>/test-1.log 2>&1   EXIT=0
./build/tests/ModulationAudioViewTest > <logs>/test-4-green-direct.log 2>&1             EXIT=0  ("Totals: 16 passed, 0 failed")
```

Negative control (the product hunk that IS the fix, removed temporarily so the red proof is real):

```
git diff -- src/core/ModulationLayerPublisher.cpp > <logs>/instrument-watch-removed.patch   EXIT=0
cmake --build build --target ModulationAudioViewTest -j4 > <logs>/build-2-red-control.log 2>&1  EXIT=0
cd build/tests && ctest -R '^ModulationAudioViewTest$' --output-on-failure > <logs>/test-2-red-control.log 2>&1  EXIT=8
git apply -R <logs>/instrument-watch-removed.patch                                          EXIT=0 (tree restored, diff -q clean)
cmake --build build --target ModulationAudioViewTest -j4 > <logs>/build-3-restored.log 2>&1  EXIT=0
cd build/tests && ctest -R '^ModulationAudioViewTest$' --output-on-failure > <logs>/test-3-green-restored.log 2>&1  EXIT=0
```

The red run is `13 passed, 3 failed` with exactly the three instrument tests failing:

```
:359 '!publisher.targetAlive(0, view.entries[0].token)' returned FALSE. (an instrument's destruction did not retire its modulation target)
:378 'g_probeGateWasClosed' returned FALSE. (the modulation target was still open when the instrument's own teardown began)
:396 'g_probeGateWasClosed' returned FALSE. (the modulation target was still open when the instrument's own teardown began)
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > <logs>/feature-check-1.log 2>&1  EXIT=0
  PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
       no-tautology / doc-figures / complexity / file-length  -> RESULT: PASS
```

`<logs>` is `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-133.r1/logs`.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  This unit has no new product change (ALREADY-FIXED), so the equivalent proof was run against the
  product hunk that implements the fix: with `m_instrumentWatch`'s `connect` removed
  (`instrument-watch-removed.patch`) the suite exits 8 with the three instrument tests failing
  (`test-2-red-control.log`); restored with `git apply -R`, it exits 0 with 16/16 passing
  (`test-3-green-restored.log`, `test-4-green-direct.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the probe
  instrument's own destructor: it calls `publisher.targetAlive(0, g_probeToken)` synchronously
  inside `destroyInstrument()`'s `emit`, so the observation happens at the exact teardown moment,
  driven by the control thread. No timer, no thread, no sleep; the token is a captured constant.
- **The fix is at the cause and complete.** The cause is "the token protected only the model"; the
  instrument now retires the same token through its own `aboutToBeDestroyed` connection, and the
  owner emits before the derived destructor. Every deletion site in product code goes through
  `InstrumentTrack::destroyInstrument()` (7 call sites, listed above); the only `delete` of an
  instrument is in that helper; the four `instantiate()` sites all follow it; `~Instrument` keeps a
  fallback emit for instruments destroyed outside a track.
- **No collateral damage.** No product, save-format or control-command file changes in this unit,
  so old projects load, save and render exactly as before. The existing gate adds one QObject
  connection per published slot on the control thread only; nothing is added to the audio path, and
  a route whose instrument has no per-note target (`noteTarget < 0`) still falls back to the newest
  note as before.
- **ALREADY-FIXED evidence.** `src/core/ModulationLayerPublisher.cpp:233-237` (instrument watch ->
  `retireTarget`), `:130-158` (close and drain before the token dies), `:266` (the call the pin
  covers), `include/ModulationLayer.h:316,517`, and
  `src/tracks/InstrumentTrack.cpp:252-258` (emit before delete), with the fix commits
  `eb81efcba1` and `f5cd870bbd` confirmed ancestors of HEAD.

## Other findings

- None noticed inside this unit's scope. The finding text is a duplicate of the instrument half of
  cluster 5186 / new-p12-159; the patrol's line numbers were taken before those fixes merged.
