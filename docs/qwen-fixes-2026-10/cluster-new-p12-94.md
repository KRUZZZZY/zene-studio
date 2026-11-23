FIXED

# Cluster new-p12-94: the arpeggio collected its notes into a QList on the audio thread

## Decision and evidence: REAL

- **The allocation is in the render path.** `InstrumentTrack::playNote` runs on the audio thread and calls the
  arpeggio for every note and period (`src/tracks/InstrumentTrack.cpp:776-777`). Pre-fix, `processNote` started
  with `ConstNotePlayHandleList cnphv = NotePlayHandle::nphsOfInstrumentTrack(...)` (pre-fix
  `src/core/InstrumentFunctions.cpp:365`).
- **That type is a heap container.** `ConstNotePlayHandleList` is `QList<const NotePlayHandle*>`
  (`include/NotePlayHandle.h:51-52`). `NotePlayHandle::nphsOfInstrumentTrack` fills it with `push_back`
  (`src/core/NotePlayHandle.cpp:593-607`), so every call with at least one matching note grew a QList on the heap
  - one allocation, plus one free when the list died at the end of `processNote`.
- **The fallback appends too.** When the track had no base note, `processNote` reassigned the list from
  `PresetPreviewPlayHandle::nphsOfInstrumentTrack(...)` and, if that was empty as well, appended the note being
  processed (`push_back( _n )`, pre-fix line 375) - a second, guaranteed allocation on the same path.
- **Reached as the finding says.** The `Sort`/`Sync` trigger (other notes active on the track) is the case
  `nphsOfInstrumentTrack` collects; the sweep's own measurement names the exact construct it is:
  `src/core/InstrumentFunctions.cpp:375: cnphv.push_back( _n );` (`logs/sweep-1-red.log`).

## The fix, at the cause

Everything `processNote` did with the list was one of three questions, so nothing has to be stored:

| the list was asked | answer now, without a list |
|---|---|
| `cnphv.size()` | a counter filled by one walk of `Engine::audioEngine()->playHandles()` |
| `cnphv.first()->totalFramesPlayed()`, `->noteOffset()` | the first note in play order; in sort mode the first note of the lowest key |
| `cnphv[arpIndex]->key()` after `std::sort` by key | `ArpBaseNotes::keyAt(arpIndex)`, read from a `std::array<int, NumKeys>` count per key on the stack |

- `ArpBaseNotes` + `arpBaseNotesOf()` (`src/core/InstrumentFunctions.cpp:362-424`) replace the collection and
  its preset-preview fallback. The fallback's single entry is the preview note, or the processed note, exactly as
  before. Nothing is allocated, freed, locked or logged.
- `Free` mode reads neither the notes nor their frame counters - it steps the note it is processing - so it now
  walks nothing at all (it used to build and discard the list).
- `PresetPreviewPlayHandle::nphsOfInstrumentTrack`, whose only caller was the arpeggio, is replaced by
  `previewNoteOfInstrumentTrack` (`include/PresetPreviewPlayHandle.h:61`, `src/core/PresetPreviewPlayHandle.cpp:254`),
  which hands back the one pointer. It also checks `s_previewTC` for null where the old code dereferenced it, which
  the fallback path needs to be callable at all in a test that never starts a preview.
- Sort mode's ordering: the sorted list was only ever read for its size, its first element and its per-slot key.
  The multiset of keys is identical by construction, so `keyAt()` returns exactly the key `cnphv[arpIndex]` had.
  Among notes that share the lowest key the first in play order is taken; `std::sort` left the order of equal keys
  to the implementation, so that tie was never defined and is now deterministic. Keys are bounded by `NumKeys`
  (`Note::setKey` clamps, `src/core/Note.cpp:356-360`), and the histogram index is clamped defensively anyway.

Behaviour is otherwise unchanged, and the golden run proves it: the same sort-mode render plays
`60, 64, 67, 72` with the fix reverted and with the fix applied (`test-6-reverted.log`, `test-7-reapplied.log`).

## Why the regression is the static sweep and not the allocation probe

The runtime probe (`tests/src/core/AllocationProbe.h`) replaces `operator new` only. Qt's implicitly shared
containers - `QList` among them - allocate through `QArrayData` with `malloc`, which the probe does not count; the
repo says so itself (`docs/KNOWN-LIMITATIONS.md:3228`, "What it still does not prove"), and it points at the static
sweep for those. I confirmed it rather than assumed it: a control measurement in the same armed window counted a
`QList<int>` push_back as **0** allocations (`logs/build-4-debug.log` run), while `AllocationProbeTest` passes in
this build tree (`logs/test-3-probe.log`), i.e. the probe works and the container is simply invisible to it.

So the regression vehicle is the programme's own tool for container growth on audio-thread paths: this function is
now a DECLARED audio-thread path, and the sweep fails on the pre-fix construct and passes on the fix.

## Test matrix

| path | vehicle | pre-fix | post-fix |
|---|---|---|---|
| sort mode, other notes sounding (the finding) | `RtSafetySweep`, scope line `tests/rt-safety-scope.txt:203` | FAIL: `grow-container` at `InstrumentFunctions.cpp:375` | PASS |
| sort mode, no note of the track sounding (preview/self fallback) | same declared region: the append was in the same body | FAIL (see above) | PASS |
| sync mode, free mode | same declared region | FAIL (the collection ran in every mode) | PASS |
| reconfiguration: the keys sort mode plays | `InstrumentTrackRenderPathTest::sortModeArpeggioStillPlaysTheAscendingKeys` | golden `60, 64, 67, 72` | the same |
| audio-thread constraint: no operator-new traffic either | `InstrumentTrackRenderPathTest::theArpeggioReadsItsNotesWithoutAllocating` (5 mode/layout cases) | 0 (passes; the probe cannot see the QList, see above) | 0 |
| the arpeggio's other behaviours (up/down, repeats, fractional repeats, cycle) | `NotePlaybackTest` (31 slots) | pass | pass |

The two new slots drive the real `InstrumentFunctionArpeggio::processNote()`; the second injects the played frame
count of the note the timing runs off, so no sleep, clock or second thread is involved. The 64-frame notes in the
first slot keep the render loop below the collection out of the measurement.

Supported-platform build requirements: none added. The product change is standard C++20 (`std::array`, `std::clamp`,
`std::div`); the new slots are plain QtTest with no platform guard.

## Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-94.r1/logs`.

| step | command | EXIT | log |
|---|---|---|---|
| build the new test (test-only compile error: `std::make_unique` with a braced list) | `cmake --build build --target InstrumentTrackRenderPathTest -j4` | 2 | build-1-red.log |
| build the new test | `cmake --build build --target InstrumentTrackRenderPathTest -j4` | 0 | build-2-red.log |
| first run (fails on a bug in my own test: the injected frames went to the wrong handle) | `cd build/tests && ctest -R InstrumentTrackRenderPathTest --output-on-failure` | 8 | test-1-red.log |
| probe control: the probe is alive in this tree | `cmake --build build --target AllocationProbeTest -j4`; `cd build/tests && ctest -R '^AllocationProbeTest$' --output-on-failure` | 0; 0 | build-3-probe.log, test-3-probe.log |
| build with the fix | `cmake --build build --target zene InstrumentTrackRenderPathTest -j4` | 0 | build-5-green.log |
| build every target that includes the changed header (`PresetPreviewPlayHandle.h`) | `cmake --build build --target zene InstrumentTrackRenderPathTest peakcontrollereffect -j4` | 0 | build-6-green.log |
| **sweep baseline**, scope line reverted (proves the gate was green before) | `git apply -R logs/scope.patch; python3 tests/rt-safety-sweep.py --check; git apply logs/scope.patch` | 0 | sweep-0-baseline.log: 4970 lines, 4 hits in 3 keys, all allowlisted |
| **RED** (scope line added, product NOT fixed) | `python3 tests/rt-safety-sweep.py --check` | 1 | sweep-1-red.log: `NEW rt-safety hit: src/core/InstrumentFunctions.cpp:InstrumentFunctionArpeggio::processNote:grow-container - first at src/core/InstrumentFunctions.cpp:375: cnphv.push_back( _n );` |
| **GREEN** | `python3 tests/rt-safety-sweep.py --check` | 0 | sweep-2-green.log: 5150 lines, 4 hits in 3 keys, all allowlisted |
| 4b: capture the product diff | `git diff -- src/core/InstrumentFunctions.cpp src/core/PresetPreviewPlayHandle.cpp include/PresetPreviewPlayHandle.h > logs/fix.patch` | 0 | fix.patch |
| 4b: revert the product change | `git apply -R logs/fix.patch` | 0 | - |
| 4b: rebuild the reverted tree | `cmake --build build --target zene InstrumentTrackRenderPathTest -j4` | 0 | build-8-reverted.log |
| 4b: **RED** | `python3 tests/rt-safety-sweep.py --check` | 1 | sweep-3-reverted.log: the same `grow-container` hit |
| 4b: **RED** through ctest | `cd build/tests && ctest -R "RtSafetySweep\|InstrumentTrackRenderPathTest" --output-on-failure` | 8 | test-6-reverted.log: `RtSafetySweep ***Failed`; `InstrumentTrackRenderPathTest Passed` (the golden holds on unfixed code) |
| 4b: re-apply the fix | `git apply logs/fix.patch` | 0 | - |
| 4b: rebuild | `cmake --build build --target zene InstrumentTrackRenderPathTest -j4` | 0 | build-9-reapplied.log |
| 4b: **GREEN** through ctest | `cd build/tests && ctest -R "RtSafetySweep\|RtSafetySelfTest\|InstrumentTrackRenderPathTest\|NotePlaybackTest" --output-on-failure` | 0 | test-7-reapplied.log: 4/4 passed |
| family, after the test rewrite | `cd build/tests && ctest -R "InstrumentTrackRenderPathTest\|NotePlaybackTest" --output-on-failure` | 0 | test-5-green.log: 2/2 passed |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | feature-check-2.log (table below) |

## Gates

`python3 .../feature_check.py <worktree>` printed (unpiped exit **0**):

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

The same table on the unmodified branch was PASS as well (`feature-check-1-baseline.log`, exit 0), so no row was
already red.

## Registration

- `src/core/InstrumentFunctions.cpp`, `src/core/PresetPreviewPlayHandle.cpp` and `include/PresetPreviewPlayHandle.h`
  are inherited: one line each was appended to `tests/upstream-modifications.txt` with the reason and the
  regressions.
- `tests/src/tracks/InstrumentTrackRenderPathTest.cpp` is fork-new (`tests/fork-sources.txt:3077`): no upstream
  entry. `tests/rt-safety-scope.txt` and `tests/upstream-modifications.txt` are under `tests/`, which the gate
  allows by construction.
- The changed header was rebuilt into every target that includes it: `zene` (Engine.cpp, FileBrowser.cpp,
  InstrumentFunctions.cpp, PresetPreviewPlayHandle.cpp) and the `peakcontrollereffect` plug-in, plus
  `InstrumentTrackRenderPathTest`. The only other mention of the class, `plugins/PeakControllerEffect/PeakControllerEffect.cpp`,
  uses the class, not the removed function (`rg nphsOfInstrumentTrack` now finds only
  `NotePlayHandle::nphsOfInstrumentTrack` and its two callers).
- No control command, undo step or journal entry is touched: this is a render-path change only, so the
  `ControlCommandsSnapshot` canary is not in play.

## Self-check

- **Verdict and evidence**: REAL, with the pre-fix allocation site (`InstrumentFunctions.cpp:365` pre-fix,
  `NotePlayHandle.cpp:593-607`, `include/NotePlayHandle.h:51-52`) and the live call site
  (`src/tracks/InstrumentTrack.cpp:776-777`) named above.
- **The regression fails without the fix and passes with it**: the 4b rows. Reverted: sweep exit **1** and
  `RtSafetySweep ***Failed` under ctest (exit 8). Re-applied: sweep exit **0**, 4/4 ctest targets pass.
- **A test that passes without the fix is not used as the regression**: the allocation-probe slot passes both
  before and after, and is documented as the runtime half only, because the probe cannot see Qt containers
  (`docs/KNOWN-LIMITATIONS.md:3228`, confirmed with the in-test control in `build-4-debug.log`). It stays as a
  guard against an `operator new` allocation appearing there later.
- **Deterministic**: no wall-clock, sleep, race or second thread. The sweep reads source text; the golden slot
  injects the first note's played frame count; skip, miss and random direction stay 0.
- **At the cause and complete**: the two functions the arpeggio called for its notes are gone from the path
  (`rg nphsOfInstrumentTrack` finds only `NotePlayHandle::nphsOfInstrumentTrack` and its two remaining callers,
  `InstrumentPlayHandle.cpp:49` and the new slot's setup assertion). Both halves of the old allocation - the
  collection and the fallback append - are gone, in all three modes.
- **No collateral damage**: no saved data or project attribute is read differently (the settings load/save code is
  untouched); no limit or check was added that could reject a caller; sort mode's keys are identical, shown by the
  golden passing on unfixed and fixed code; `NotePlaybackTest`'s 31 arpeggio/note slots still pass. Projects load
  exactly as before: `PresetPreviewPlayHandle::nphsOfInstrumentTrack` had no other caller, and
  `NotePlayHandle::nphsOfInstrumentTrack` keeps its signature and behaviour for `InstrumentPlayHandle`.
- **Realtime, undo, commands**: the new code allocates, frees, locks, waits and logs nothing; no undo step and no
  control command was added.
- **ALREADY-FIXED / FALSE-POSITIVE do not apply**: the verdict is FIXED. The pre-fix construct is in this branch's
  own source (`src/core/InstrumentFunctions.cpp:365` pre-fix, quoted in the 4b RED log line
  `src/core/InstrumentFunctions.cpp:375: cnphv.push_back( _n );`), not in an inherited contract or a by-design entry.
- **No command file changed**: this fix touches `src/core/InstrumentFunctions.cpp`,
  `src/core/PresetPreviewPlayHandle.cpp`, `include/PresetPreviewPlayHandle.h`, the two `tests/` ledgers and the
  test file - no command file and no `ControlRegistry` handler. So no sibling command (`app.*`, `arrangement.*` or
  otherwise) is affected, and the `ControlCommandsSnapshot` canary does not move.
- **Not verified here**: only two test binaries are built in this work tree (`InstrumentTrackRenderPathTest`,
  `NotePlaybackTest`), so the wider suite was not run; and RTSan builds are not available on this box, so the
  "no allocation" claim on the audio thread rests on the static sweep plus code reading, not a runtime sanitizer.

## Other findings

- `InstrumentPlayHandle::play` (`src/core/InstrumentPlayHandle.cpp:49`) has the same collection on the same audio
  thread: `NotePlayHandle::nphsOfInstrumentTrack( instrumentTrack, true )` into a QList, re-scanned in a `do/while`.
  Not fixed here: it is a different cluster's finding, and its re-scan loop (a note may be added while the loop
  runs) needs its own behaviour proof, which the two available test binaries cannot cover.
- Creating an arpeggio note still allocates on the audio thread: the `NotePlayHandle` constructor appends the child
  to its parent's `m_subNotes` (`src/core/NotePlayHandle.cpp:99`), a `QList` growth per note created. That is the
  next allocation this path makes and wants a preallocated sub-note list of its own; out of scope for a report
  about the collection.
