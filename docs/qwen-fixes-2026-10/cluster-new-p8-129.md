ALREADY-FIXED

# Cluster new-p8-129: layer note-off routing overwritten by another note on the same key

## Decision: ALREADY-FIXED on branch `bugs/qwen-new-p8-129`

The reported code (`include/InstrumentLayers.h:160-164`, `src/core/InstrumentLayers.cpp:347-351`) kept one
atomic mask per MIDI key, so a second note-on on a key replaced the first note's route. That code is no
longer on this branch: commit `56551280a1` ("Fix overlapping live layer routing", cluster
new-p10-178-port-port) replaced it with a bounded, ordered per-key route FIFO, and that commit is an
ancestor of the gate base `fa4a5f2e7` (`git merge-base --is-ancestor 56551280a1 fa4a5f2e78; EXIT=0`). No
product file is changed by this job.

### Where the fixed behaviour lives at branch tip

- `include/InstrumentLayers.h:189-204` - `MaxActiveLiveNotesPerKey = 128`, `struct LiveKeyRoutes`
  (`entries[128]`, `nextSequence`, `overflowNotes`) and
  `std::array<LiveKeyRoutes, NumKeys> m_liveLayers`. One ordered slot per live note-on, not one mask per key.
- `src/core/InstrumentLayers.cpp:495-510` (`liveLayersFor`) - a live note-on computes its zone mask and
  appends it with `admitLiveNoteOn` (line 509); a note-off takes the **oldest** outstanding route with
  `takeOldestLiveLayers` (line 505), so the second note-on cannot take over the first note's note-off.
- `src/core/InstrumentLayers.cpp:524-536` (`rememberLiveLayers`) - each note-on claims a free slot and stamps
  a monotonically increasing sequence; nothing is overwritten.
- `src/core/InstrumentLayers.cpp:537-557` (`takeOldestLiveLayers`) - picks the oldest active route by
  wrap-safe sequence age, so an interleaved same-key note-off goes to the note that started first.
- `src/core/InstrumentLayers.cpp:512-521` (`admitLiveNoteOn`) - at the 128-route bound the oldest route is
  stolen, receives an immediate note-off, and its later note-off is swallowed (`overflowNotes`, line 504).
- `src/core/InstrumentLayers.cpp:567-585` - layer insert/remove remap every active route mask; `clear()`
  and all-notes-off clear the routes.
- Live input reaches this code from `InstrumentTrack::processInEvent` ->
  `m_layers.forwardInEvent(...)` (`src/tracks/InstrumentTrack.cpp:523`, `src/core/InstrumentLayers.cpp:588-593`).

The pre-fix code was restored for the RED run below and shows exactly the reported defect:
`std::array<std::atomic<std::uint32_t>, NumKeys> m_liveLayers` with `live.store(layers)` on note-on and
`live.exchange(0)` on note-off.

## Regression coverage (existing, from the fix commit)

| Path | Test | What it pins |
|---|---|---|
| normal + failure | `tests/src/gui/InstrumentSetupUiTest.cpp:485` `overlappingLiveSameKeyNotesKeepTheirLayerNoteOffRoutes` | soft note on the key -> layer 1, hard note on the same key -> layer 2; the first note-off releases **layer 1 only** while layer 2 still sounds; the second releases layer 2 |
| exhaustion | `tests/src/core/RackZoneFeedRunTest.cpp:256` `liveSameKeyRouteCapacityStillForwardsTheNextZone` | 128 outstanding routes on one key, the 129th note-on is admitted by stealing the oldest, whose off stays paired |
| reconfiguration | `tests/src/gui/InstrumentSetupUiTest.cpp:531` `allNotesOffClearsQueuedLayerRoutes`, `liveNoteOffFollowsLayerAfterUndoInsert`; `tests/src/core/InstrumentLayersCommandsTest.cpp:295` `layersAddZoneRemoveUndoReload` | routes cleared by an all-notes-off, remapped by layer insert/remove and undo, and the rack round-trips save/load |
| ordering | `tests/src/core/InstrumentLayersCommandsTest.cpp:209` `liveRouteOrderingStaysFifoAcrossTheSequenceWrap` | oldest-first order survives the sequence-ring wrap (fixed sequence values injected) |
| audio thread | design | `m_liveLayers` entries are `std::atomic<std::uint32_t>` in fixed arrays; the route helpers allocate, lock, wait and log nothing. The path runs on the MIDI/GUI thread from `processInEvent`, not on the render thread |
| platform | design | plain C++20 and Qt containers/atomics already used by the file; no platform-specific call, no clock, no sleep |

All tests inject `MidiEvent`s synchronously and read `InstrumentTrack::playingNote`; no wall-clock timing,
sleep or race is involved.

## Commands run in this session

All commands ran in
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-129`; logs are new files in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-129.r1/logs`.

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| baseline build | `cmake --build build --target RackZoneFeedRunTest InstrumentSetupUiTest InstrumentLayersCommandsTest -j4 > .../build-1-baseline.log 2>&1; echo EXIT=$?` | 0 | Built the three test binaries at HEAD |
| baseline test (GREEN) | `ctest -R '^(RackZoneFeedRunTest\|InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' --output-on-failure > .../test-1-baseline.log 2>&1; echo EXIT=$?` (from `build/tests`) | 0 | 3/3 passed at branch tip |
| product patch | `git diff 56551280a1^ HEAD -- include/InstrumentLayers.h src/core/InstrumentLayers.cpp > .../fix.patch; echo EXIT=$?` | 0 | 273-line patch: the fix commit's product change |
| reverse it | `git apply -R .../fix.patch; echo EXIT=$?` | 0 | Header back to `std::array<std::atomic<std::uint32_t>, NumKeys> m_liveLayers`; `git diff --stat 56551280a1^ -- <both files>` empty |
| RED build | `cmake --build build --target RackZoneFeedRunTest InstrumentSetupUiTest InstrumentLayersCommandsTest -j4 > .../build-2-prefix-red.log 2>&1; echo EXIT=$?` | 0 | Built the pre-fix product with all regressions retained |
| RED test | `ctest -R '^(RackZoneFeedRunTest\|InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' --output-on-failure > .../test-2-prefix-red.log 2>&1; echo EXIT=$?` | **8** | `FAIL! : InstrumentSetupUiTest::overlappingLiveSameKeyNotesKeepTheirLayerNoteOffRoutes() 'firstOffReleasedSoftOnly' returned FALSE.` - the reported bug. Also `RackZoneFeedRunTest::liveSameKeyRouteCapacityStillForwardsTheNextZone() 'hardSurvivedStolenRouteOffs' returned FALSE` and `InstrumentLayersCommandsTest::liveRouteOrderingStaysFifoAcrossTheSequenceWrap() Compared values are not the same` |
| restore | `git apply .../fix.patch; echo EXIT=$?` | 0 | Product files back to HEAD; `git diff --stat HEAD -- <both files>` empty, working tree clean |
| GREEN build | `cmake --build build --target RackZoneFeedRunTest InstrumentSetupUiTest InstrumentLayersCommandsTest -j4 > .../build-3-restored-green.log 2>&1; echo EXIT=$?` | 0 | Rebuilt the fixed product |
| GREEN test | `ctest -R '^(RackZoneFeedRunTest\|InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' --output-on-failure > .../test-3-restored-green.log 2>&1; echo EXIT=$?` | **0** | 3/3 CTest executables passed |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gates-1-feature-check.log 2>&1; echo EXIT=$?` | 0 | `PASS fork-sources`, `PASS all-sources-reproduce`, `PASS no-upstream-regression`, `PASS check-namespace`, `PASS no-tautology`, `PASS doc-figures`, `PASS complexity`, `PASS file-length`; `RESULT: PASS` |

## Self-check

- **Verdict evidence:** ALREADY-FIXED. Code references above
  (`include/InstrumentLayers.h:189-204`, `src/core/InstrumentLayers.cpp:495-510,524-557`) show per-note
  ordered routes; the reported one-mask code is absent at HEAD. Fix commit `56551280a1` is an ancestor of the
  gate base (exit 0 above). No product file is modified by this job, so saved projects, undo and commands are
  untouched.
- **The regression test FAILS with the product change reverted and PASSES with it:** yes, seen here.
  Reverting the product patch rebuilt the pre-fix tree and `ctest` exited 8 with
  `firstOffReleasedSoftOnly returned FALSE`; after restoring it, `ctest` exited 0. Logs `test-2-prefix-red.log`
  and `test-3-restored-green.log`.
- **Deterministic:** the tests inject fixed `MidiEvent`s (key 118, velocities 40/100) and read
  `playingNote(key)` synchronously; the wrap test seeds fixed sequence values. No clock, sleep, timer, thread
  or wall-clock dependence.
- **Cause and completeness:** `forwardInEvent` is the only caller of `liveLayersFor`; note-on admission,
  note-off consumption, key pressure (`activeLiveLayers`), channel-wide messages, the 128-route bound,
  insert/remove remapping and `clear()`/all-notes-off were each checked and are covered by the tests listed
  above. The base instrument keeps its own accepted/skipped counters (`m_baseAccepted`/`m_baseSkipped`),
  unchanged by the fix and covered by `overlappingSameKeyBaseNotesKeepTheirNoteOffs`.
- **No collateral damage:** routes are runtime-only state; no serialization, command, undo step or limit
  changed in this job. The 128-route bound applies only to live layer routing for one key and admits the next
  note by stealing the oldest, so it does not reject a legitimate later note.
- **Realtime:** the route helpers use fixed atomic arrays and stack locals; no allocation, free, mutex, wait,
  logging or Qt call is added (nothing is added at all - ALREADY-FIXED).
- **Registration and gates:** no new product file and no new test file, so `tests/fork-sources.txt` and
  `tests/upstream-modifications.txt` are unchanged; `feature_check.py` is PASS with exit 0.
- **GUI evidence:** this job changes no `src/gui` file, so a screenshot/recording is not applicable. The
  scenario test lives in `tests/src/gui/InstrumentSetupUiTest.cpp` but was not modified here.
- **Sibling commands (`app.*`, `arrangement.*`, ...):** not applicable to this cluster. The branch is cut
  from `fa4a5f2e7` and already carries 430 files of earlier accepted work from the train, so the mechanical
  check lists command files that came in by merge. This cluster's own commit,
  `git show --stat HEAD` -> `docs/qwen-fixes-2026-10/cluster-new-p8-129.md | 105 +++`, touches no command,
  registry or reversibility file, so no sibling command's behaviour changes. The command-side surface of
  live layer routing is nothing at all: routes are internal runtime state, no `rack.*` command reads or
  writes them.
- **Registration:** the fix commit `56551280a1` already updated `tests/upstream-modifications.txt` for the
  inherited `src/core/InstrumentLayers.cpp`; this job adds no file there (report only).

## Other findings

- `InstrumentTrack::m_notes[key]` still holds a single note handle per key for the track's own instrument, so
  the base instrument cannot sound two overlapping notes of the same key (MIDI note-offs carry no instance
  id). That is pre-existing upstream behaviour, outside this finding, and is covered by
  `InstrumentLayersCommandsTest::overlappingSameKeyBaseNotesKeepTheirNoteOffs`; not changed here.
