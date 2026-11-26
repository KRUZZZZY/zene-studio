FIXED

# Cluster new-p20-86: Monstro drops a note render when no scratch slot is available

## The finding

`MonstroInstrument::playNoteImpl()` leases one of a fixed number of modulation scratch buffers and,
when the pool is empty, returns before rendering: no audio for that period and no advance of that
note's synth state (phases, envelopes, LFO), so the note resumes with a discontinuity.

## Verdict: REAL, on a narrower trigger than the finding's wording

`plugins/Monstro/Monstro.cpp:1084-1096` is the drop: the lease scan, then
`if (scratch == nullptr) { return; }` before `MonstroSynth::renderOutput()` and before any state
update. The pool was sized from `renderThreadCount()` alone
(`plugins/Monstro/Monstro.cpp:1051-1053` before this change).

The finding's *when* clause — "the engine calls more simultaneous render callbacks than
`renderThreadCount()`" — is not what makes this reachable, and on its own it is false:

- The engine has exactly `renderThreadCount()` = `m_numWorkers + 1` concurrent render contexts: the
  pool workers it starts plus the inline (engine) context (`src/core/AudioEngine.cpp:552-562`,
  `src/core/AudioEngine.cpp:581-584`, `include/AudioEngine.h:162-163`).
- Every render thread owns one render slot, and a caller outside a render job has none
  (`include/AudioEngineWorkerThread.h:151-154`: "The render slot of the calling thread, or -1
  outside a render job").
- Every production note render runs inside one of those contexts: a play handle is queued as a job
  and drained by `startAndWaitForJobs()` (`src/core/AudioEngine.cpp:1115-1116`), and the only path
  into `playNoteImpl()` is `NotePlayHandle::play()` (`src/core/NotePlayHandle.cpp:368`) ->
  `InstrumentTrack::playNote()` -> `Instrument::playNote()` (`src/tracks/InstrumentTrack.cpp:783`).

So the engine alone can never exceed the old capacity. What the pool did not cover is the caller the
same render-slot API already accounts for elsewhere: its sibling consumer reserves one slot beyond
the render contexts, for exactly this caller — `include/Mixer.h:186-193` ("one slot per render
thread, plus one for a caller that is not on a render thread (the control side and the tests)"),
sized as `renderThreadCount() + 1` in `src/core/Mixer.cpp:427`. A note rendered by that caller while
the render contexts hold every lease hits `Monstro.cpp:1096`: it is dropped, and its state does not
advance. That is the defect this change fixes — capacity for the caller set the codebase documents,
at the same size its sibling uses.

Confidence in the drop's consequence: measured, not argued — the regression test below renders the
same period twice, once with every render-context lease held, and the voice rendered outside a render
job is silent and out of step with the serial render without the fix.

## The fix (at the cause, one size)

`plugins/Monstro/Monstro.cpp:1051-1057`: the pool is `renderThreadCount() + 1` slots — one per render
context plus one for the caller that is not on a render thread, the rule `MixerChannel::m_inputPartials`
follows over the same slots. The lease itself is unchanged, so the extra slot is not reserved for one
particular thread: whoever calls, including a second concurrent non-render caller, takes a free slot
and no two callers ever share one. Nothing else in the plugin changes — the buffers are still
allocated once, on the control thread, at construction (and resized off the audio path by
`updateSamplerate()`), and the render path still allocates, locks, waits and logs nothing.

The drop branch stays as a defensive guard for a caller count beyond `renderThreadCount() + 1`, which
no engine path and no documented caller can produce. The fix removes the gap; it does not paper over
it with a fallback that would change a note's sound.

## Test matrix

| Path | Case | Covered by |
| --- | --- | --- |
| normal | `renderThreadCount()` render contexts each render a voice; all match a serial render | existing `NotePlaybackTest::monstroConcurrentVoicesKeepIndependentModulation` (PASS before and after) |
| failure / exhaustion | every render-context lease held, then a voice rendered by a caller outside a render job | new `NotePlaybackTest::monstroScratchPoolHoldsACallerOutsideARenderJob` (FAILS before, PASSES after) |
| reconfiguration | `sampleRateChanged` -> `updateSamplerate()` resizes every slot's buffers to the new period, on the control thread | existing sample-rate slots for Monstro (`NotePlaybackTest::monstroRandomLfoAtLowSampleRateRenders`, `PluginPortsMigrationTest` instrument loop); the loop runs over `m_modulationScratchCount`, so it covers the new slot unchanged |
| audio-thread constraints | note start allocates nothing; the probe is null in production (one atomic load and a branch) | `NotePlaybackTest::monstroNoteStartDoesNotAllocate` (PASS), `PluginPortsMigrationTest` realtime allocation counts |
| supported platforms | core seam is `std::atomic` + a function pointer (C++20, no Qt API, no POSIX); the test uses `testsupport::Latch` + `std::thread` only | builds here (Linux, Qt 6.12, gcc 16); no Qt-6-only call added |

## Regression test

`tests/src/core/NotePlaybackTestParts1.h:181` `monstroScratchPoolHoldsACallerOutsideARenderJob`,
declared at `tests/src/core/NotePlaybackTest.cpp:463`. Deterministic, no sleep and no wall clock:

1. Two Monstro tracks with the same settings and `renderThreadCount() + 1` voices each; every voice
   is primed once serially.
2. `Instrument::setVoiceScratchProbeForTesting(holdScratchLease)` installs the lease probe
   (`tests/src/core/NotePlaybackTest.cpp:270-289`).
3. `renderThreadCount()` threads call `playNote()`; the probe holds each of the first
   `renderThreadCount()` leases on a `testsupport::Latch`, so the pool is provably full
   (`allHeld.wait()` returns only when all of them are inside the callback with a lease held).
4. This thread then renders the last voice — a caller outside a render job — releases the holders
   and joins them.
5. The same period is rendered serially on the reference track, and the whole voice set compared
   sample for sample. The test first proves the reference voice is not silent, so a dropped render
   cannot hide.

The probe is the synchronisation hook: it replaces a race with a barrier. It is a core seam
(`include/Instrument.h:120-128`, `src/core/Instrument.cpp:108-122`, installed in commit
`a851229aa9` so the RED run compiles the same test the GREEN run does); production installs none.

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-86.r1/logs/`.

| Command | EXIT | Log |
| --- | --- | --- |
| `cmake --build build --target zene monstro -j4` (seam) | 0 | `build-1-seam.log` |
| `cmake --build build --target NotePlaybackTest -j4` | 0 | `build-2-red.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (first RED) | 8 | `test-1-red.log` |
| `cmake --build build --target monstro NotePlaybackTest -j4` (with fix) | 0 | `build-3-green.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (GREEN) | 0 | `test-2-green.log` |
| `./NotePlaybackTest monstroScratchPoolHoldsACallerOutsideARenderJob` | 0 | `test-3-green-slot.log` |
| `git diff -- plugins/Monstro/Monstro.cpp > logs/fix.patch` | 0 | `fix.patch` (19 lines) |
| `git apply -R logs/fix.patch` | 0 | |
| `cmake --build build --target monstro NotePlaybackTest -j4` | 0 | `build-4-red-4b.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (4b RED, first test version) | 8 | `test-4-red-4b.log` |
| `git apply logs/fix.patch` | 0 | |
| `cmake --build build --target monstro NotePlaybackTest -j4` | 0 | `build-5-green-4b.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (4b GREEN) | 0 | `test-5-green-4b.log` |
| `cmake --build build --target zene monstro NotePlaybackTest MpePlaybackTest MpeSynthExpressionTest InstrumentRoutingTest PluginPortsMigrationTest -j4` (every target that includes the changed header) | 0 | `build-6-header-targets.log` |
| `git apply -R logs/fix.patch` (round trip re-run on the final test text) | 0 | |
| `cmake --build build --target monstro NotePlaybackTest -j4` | 0 | `build-7-final-red.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (final RED) | 8 | `test-6-final-red.log` |
| `git apply logs/fix.patch` | 0 | |
| `cmake --build build --target monstro NotePlaybackTest -j4` | 0 | `build-8-final-green.log` |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (final GREEN) | 0 | `test-7-final-green.log` |
| `cmake --build build --target MixerConcurrencyTest RenderJobQueueTest -j4` | 0 | `build-9-slots-tests.log` |
| `ctest -R '^(MixerConcurrencyTest|RenderJobQueueTest)$' --output-on-failure` (the render-slot siblings) | 0 | `test-9-slots.log` |
| `python3 .../qwen/feature_check.py <worktree>` | 0 | `feature-check.log` |

The 4b RED failure, verbatim from `test-6-final-red.log`:

```
FAIL!  : NotePlaybackTest::monstroScratchPoolHoldsACallerOutsideARenderJob() 'sameSamples(outsideOutput, subject.outputs[outsideVoice])' returned FALSE. (the voice rendered by a caller that is not on a render job was dropped)
Totals: 38 passed, 1 failed, 0 skipped, 0 blacklisted, 779ms
```

and the 4b GREEN run of the same test (`test-7-final-green.log`): `100% tests passed out of 1`.
The gate table (`feature-check.log`), unpiped exit 0:

```
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


## Self-check

Answers to `submit_check.py`'s five ASK items (its table is in `submit-check.log`, `RESULT: PASS`).

**ASK 1 — the regression test fails with the product change reverted and passes with it (exit codes).**
The round trip was run twice; the second run used the final test text. Commands and exits, unpiped:
`git apply -R logs/fix.patch` EXIT 0 -> `cmake --build build --target monstro NotePlaybackTest -j4`
EXIT 0 (`build-7-final-red.log`) -> `ctest -R '^NotePlaybackTest$' --output-on-failure` **EXIT 8**
(`test-6-final-red.log`), failing on
`sameSamples(outsideOutput, subject.outputs[outsideVoice])` — "the voice rendered by a caller that is
not on a render job was dropped", with 38 passing and 1 failing. Then `git apply logs/fix.patch`
EXIT 0 -> build EXIT 0 (`build-8-final-green.log`) -> the same ctest **EXIT 0**, "100% tests passed
out of 1" (`test-7-final-green.log`).

**ASK 2 — the test is deterministic: no wall-clock timing, sleeps or races (name the seam).**
The seam is `Instrument::setVoiceScratchProbeForTesting()` / `voiceScratchProbe()`
(`include/Instrument.h:120-128`, called from `plugins/Monstro/Monstro.cpp:1094-1095`), installed by
`tests/src/core/NotePlaybackTest.cpp:280` `holdScratchLease`. Each of the first `renderThreadCount()`
leases blocks on a `testsupport::Latch`; the test waits on `allHeld` before rendering the outside
voice, so the pool is provably full as a *happens-before* fact, not a scheduling bet. The holders
are released by a second `Latch` the test counts down after the outside call. There is no `sleep`,
no timeout, no elapsed-time check and no unguarded shared mutable state in the test (the probe's
counters are atomics, the latches carry the ordering).

**ASK 3 — the fix is at the cause and complete: every caller and sibling checked.**
- Callers of `playNoteImpl()`: one production path, `NotePlayHandle::play()` (`NotePlayHandle.cpp:368`)
  -> `InstrumentTrack::playNote()` (`InstrumentTrack.cpp:783`) -> `Instrument::playNote()`; it runs
  inside a render job, never re-entrantly and never nested (`InstrumentTrack::playNote()` adds chord
  and arpeggio children as play handles instead of calling `playNote()` again).
- Consumers of `AudioEngine::renderThreadCount()`: exactly two in the tree — `MixerChannel`
  (`src/core/Mixer.cpp:427`, already `+1`, unchanged) and this plugin. No other plugin keeps a
  render-slot workspace.
- Every place that touches the pool: the constructor sizing (`Monstro.cpp:1056-1058`), the buffer
  sizing loop (`:1059-1063`), `updateSamplerate()`'s resize loop, the lease in `playNoteImpl()`
  (`:1084-1096`) and the release at the end of it (`:1112-1117`). All of them use
  `m_modulationScratchCount`, so the one extra slot is covered by all of them.
- The header change (`include/Instrument.h`) was followed by rebuilding the targets that include it:
  `zene`, every instrument plugin and the note/instrument tests (`build-6-header-targets.log` EXIT 0).

**ASK 4 — no collateral damage; nothing old stops loading; no new limit rejects a legitimate caller.**
This change adds no limit — it adds capacity, so no caller that used to work can now be refused. No
saved state, project format, id, model or undo journal entry is touched (the pool is runtime-only,
constructed in `MonstroInstrument`'s constructor, never serialised), so old projects load exactly as
before. `updateSamplerate()` still resizes every slot on a sample-rate change. The other 37
NotePlaybackTest slots and the render-slot siblings (`MixerConcurrencyTest`, `RenderJobQueueTest`,
`test-9-slots.log` EXIT 0) pass.

**ASK 5 — ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED. For the
record, the finding's stated trigger was *not* the reachable one — the engine cannot exceed
`renderThreadCount()` render contexts (`AudioEngine.cpp:552-562`, `581-584`,
`AudioEngineWorkerThread.h:151-154`) — and the reachable gap is the caller outside a render job that
`Mixer.h:186-193` reserves capacity for. That is why the fix is a capacity match with the sibling
rather than a scheduler change.

**Other checks.**
- **The test cannot pass vacuously.** It asserts the reference voice is audible before comparing
  (`NotePlaybackTestParts1.h:243-246`), and the comparison is over every voice of the set.
- **Realtime.** The render path gains one atomic load of a null function pointer and a branch
  (`Instrument::voiceScratchProbe()`, `Monstro.cpp:1095`); no allocation, free, lock, wait or log.
  `monstroNoteStartDoesNotAllocate` passes. Undo and control commands are untouched (no CANARY move).
- **Registration.** `include/Instrument.h`, `src/core/Instrument.cpp` and `plugins/Monstro/Monstro.cpp`
  are declared in `tests/upstream-modifications.txt` with cluster new-p20-86 reasons; the test files
  are fork-new (`tests/fork-sources.txt:794`, `:2920`), so they need no ledger entry.
- **Not verified here.** A local full build is refused by the build guard
  (`bunny-guard: build a named target ... (full builds are the parent's job)`, `build-6-full.log`
  EXIT 97), so the header rebuild was done over named targets (above). ThreadSanitizer does not run
  on this box; the probe's hand-off is a release/acquire pair on an atomic function pointer, and the
  held leases are the test's own barrier.
- **Wider run.** `MixerConcurrencyTest` and `RenderJobQueueTest` (the render-slot siblings) pass
  (`test-9-slots.log` EXIT 0). `PluginPortsMigrationTest` fails 4 slots on this box for reasons
  unrelated to this change: three are Mallets/STK ("malletsstk rendered silence", "Mallets note 0 of
  instrument 0 was silenced") and one is a missing SWH Decimator fixture; its Monstro slot
  `monstroLfoAttackRampsWithinPeriod` passes (`test-8-family.log`). Those four are not caused by this
  change and are not fixed here.


## Other findings

- `src/core/AudioEngine.cpp:508` computes `m_numWorkers = QThread::idealThreadCount() - 1`, which is
  -2 when Qt cannot determine the CPU count (-1). `renderThreadCount()` would then be -1, and
  `src/core/Mixer.cpp:427` would size `m_inputPartials` to zero while `m_numWorkers + 1` workers are
  never created. Out of this cluster's scope; noted for the mixer lane.
- `plugins/Monstro/Monstro.cpp:1096` remains a defensive `return` for a caller count beyond
  `renderThreadCount() + 1`. Any future instrument that sizes a per-call workspace from
  `renderThreadCount()` must add the same `+1`, or the same drop returns.
- `PluginPortsMigrationTest` has 4 pre-existing red slots here (`malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange`, `slice8PluginsAreLiveAndExact` via "malletsstk rendered
  silence", and `nonRealtimeLadspaEffectsAreInvalid` via the missing SWH Decimator fixture). Left
  untouched; `test-8-family.log` has the verbatim messages.
