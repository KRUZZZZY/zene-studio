# Cluster new-p9-509 — overlapping same-pitch notes share a VST3 note ID

**Verdict: FIXED.** This supersedes the earlier FALSE-POSITIVE (`f05127033`), which Luna disputed in
`new-p9-509.r1.md`. The earlier argument was wrong: the track's note-off before a retrigger ends the
note, but the released voice stays addressable by its note ID during its release tail. The retrigger
reused that ID, so the two voices collided for note-off and note-expression events.

Branch `bugs/qwen-new-p9-509`, base `f05127033`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-509.r2/logs`.

## The finding

Luna's patrol: "Two overlapping same-pitch notes on one channel share a VST3 note ID, so an expression
update or note-off can hit the wrong voice." The cited `Vst3Instrument.cpp:297` is `reprepare()` now, so the
line number is stale. The ID is derived in `plugins/Vst3Effect/Vst3MidiEvent.cpp` (`noteIdOf`, before this fix).

## Root cause (before the fix)

- `noteIdOf` returned `channel * 128 + key` for every note event, so ON(A), OFF(A) and ON(B) on channel 3,
  key 60 all carried `444`. Nothing per note existed.
- Track-level order is preserved (cut, then retrigger), so the host never sounds two notes on one key. The
  collision is between the released voice (still addressable by ID) and the new one.

## The fix

Each note-on now starts a new ID from a counter. Note-off, poly pressure and note expression name the ID
their key last started. The slot survives the note-off, so an expression in the release tail still reaches
the released voice. The table is consumer-side (audio thread, inside the drain), so there is no cross-thread
state. The ID is resolved in the sorted order the plug-in receives the events.

| File | Change |
| --- | --- |
| `plugins/Vst3Effect/Vst3MidiQueue.h:66-98` | New `NoteIds`: fixed `std::array<int32, 16*128>` (channel and key) plus a counter that wraps after 2^31-1 and never yields -1. |
| `plugins/Vst3Effect/Vst3MidiQueue.h:194-195, 210, 226` | `MidiQueue::noteIds()` accessor; `reset()` clears the table; member `m_noteIds`. |
| `plugins/Vst3Effect/Vst3MidiEvent.cpp:64-83` | `NoteIds::resolve`: note-on (velocity > 0) starts a new ID; note-off, velocity-0 note-on, pressure and expression read the current ID; other types and no-note-id plug-ins get -1. |
| `plugins/Vst3Effect/Vst3MidiEvent.cpp:85`, `:105-142` | `midiToVst3Event` takes the resolved `noteId` instead of computing `noteIdOf`. An expression with no ID is not carried (returns false). |
| `plugins/Vst3Effect/Vst3MidiEvent.cpp:194` | The drain resolves each note event's ID, in sorted order, just before conversion. |
| `plugins/Vst3Effect/Vst3MidiEvent.h:58-65, 79-80` | Declaration and doc updated together (the old doc said both note events carry -1, which was already wrong). |
| `tests/src/plugins/Vst3HostTest.cpp` (+50) | New `testRetriggerGetsANewNoteId` (slot and definition). |

Realtime: `resolve` is an array read and write plus an integer counter. It allocates nothing, takes no lock
and logs nothing. The table is fixed-size and lives inside the queue object the drain already holds.

### Behaviour changes (deliberate, for review)

- Note IDs are now counter values (0, 1, 2, ...), not `channel * 128 + key`. Nothing in tree reads the old
  values; no existing test asserts them.
- A note-off, pressure or expression for a key that never started a note now carries -1 (match by key, as a
  host with no IDs does), not `channel * 128 + key`.
- A note expression for a key with no sounding note is dropped. Before, it was sent with an ID that named no
  voice, so the effect was the same.

### Not changed

- CLAP (`plugins/ClapEffect/ClapHostNotes.cpp:210,255`) keeps `channel * 128 + key`. It has the same retrigger
  issue in principle, but the finding is about VST3, so it needs its own cluster and owner decision.

## RED / GREEN

The regression is a drain-level test in the existing `Vst3HostTest`. It needs no plug-in and uses the
existing drain signature, so the same test compiles against the original code and the fixed code. The
sequence: ON(A)@0, OFF(A)@100, EXPR@150 (release tail), ON(B)@200 (retrigger), EXPR@300. It asserts the
note-off and tail expression name A, the retrigger and the later expression name B, and A != B.

Build note: the first build failed with `ccache: failed to create temporary file for /run/user/1000/...:
Permission denied` (environment; sandbox). Every later build set `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-509`.

| Step | Command (cwd = worktree, or `build/tests` for ctest) | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target Vst3HostTest -j2` (`build-red-1.log`) | 2 | ccache temp-dir permission error (environment, not code) |
| Build, RED | same, with `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-509` (`build-red-2.log`) | 0 | built on the original product code |
| **RED run** | `ctest -R '^Vst3HostTest$' --output-on-failure` (`test-red-1.log`) | **8** | `FAIL! testRetriggerGetsANewNoteId() 'voiceA != voiceB' returned FALSE (the retrigger reused the released voice's note id)`; Totals 17 passed, 1 failed |
| Fix applied; build | `cmake --build build --target zene -j2` (`build-green-zene-1.log`) | 0 | |
| Fix applied; build | `cmake --build build --target Vst3HostTest -j2` (`build-green-test-1.log`) | 0 | |
| **GREEN run** | `ctest -R '^Vst3HostTest$' --output-on-failure` (`test-green-1.log`) | **0** | 100% passed, 1/1 |
| Proof: save diff | `git diff -- plugins/Vst3Effect/Vst3MidiEvent.cpp plugins/Vst3Effect/Vst3MidiEvent.h plugins/Vst3Effect/Vst3MidiQueue.h > logs/fix.patch` | 0 | product files only |
| Proof: revert | `git apply -R logs/fix.patch` | 0 | |
| Proof: build zene / test on reverted code | `cmake --build build --target zene -j2` / `... --target Vst3HostTest -j2` | 0 / 0 | |
| **Proof: RED on revert** | `ctest -R '^Vst3HostTest$' --output-on-failure` (`test-revert-1.log`) | **8** | same `voiceA != voiceB` failure; 17 passed, 1 failed |
| Proof: reapply | `git apply logs/fix.patch` | 0 | |
| Proof: build zene / test | `cmake --build build --target zene -j2` / `... --target Vst3HostTest -j2` | 0 / 0 | |
| **Proof: GREEN on reapply** | `ctest -R '^Vst3HostTest$' -V` (`test-reapply-1.log`) | **0** | `PASS testRetriggerGetsANewNoteId()`; Totals 18 passed, 0 failed |

## Affected family (VST3)

Built: `vst3effect vst3instrument fake_host_vst3effect fake_host_vst3instrument zene` (EXIT 0, log
`build-family-mods-1.log`), then `Vst3BusMapTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3HostTest
Vst3InstrumentFixtureProbe Vst3InstrumentIntegrationTest Vst3InstrumentTest` (EXIT 0, log
`build-family-tests-1.log`). Run: `ctest -R 'Vst3' -V` from `build/tests`, EXIT **0**, `100% tests passed out of 7`
(`test-family-1.log`):

| Test | Executed |
| --- | --- |
| Vst3BusMapTest | 10 QTest functions passed |
| Vst3HostTest | 18 passed (incl. the new one) |
| Vst3ChunkProbeTest | 8 passed |
| Vst3EffectIntegrationTest | 7 passed |
| Vst3InstrumentFixtureProbe | 20 `[PASS]` checks, 0 `[FAIL]` (custom checker, no QTest totals) |
| Vst3InstrumentTest | 16 passed |
| Vst3InstrumentIntegrationTest | 15 passed |

No test in the family had zero tests executed.

## Gates (red before this change, and red after it, with the same regressions)

Both gates ran in `--check` mode after the final edit. Pre-change runs (`gate-*-pre-1.log`) and post-change runs
(`gate-*-post-1.log`) were compared line by line.

| Gate | Command | EXIT | Regressions before | Regressions after |
| --- | --- | --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check` | **1** | 6 | 6 (identical set) |
| File length | `bash tests/file-length-gate.sh --check` | **1** | 14 | 14 (identical set) |

- Complexity (none in the VST3 files touched): `XyPadPairTest.cpp dotCentreAlong` (CCN 16),
  `DawProjectReadTracks.cpp parseNotes` (13), `ControlCommandsMixerTools.cpp adjust` (12),
  `ControlCommandsMixerStrip.cpp setStrip` (11), `ProjectRevisions.cpp rotateProjectRevision` (11),
  `ControlCommandsShortcuts.cpp importedOverrides` (11).
- File length (new files over 500 code lines): `src/core/SmfInterchangeReader.cpp` (504),
  `src/core/TakeLane.cpp` (520), `tests/src/core/CollabTransportTest.cpp` (541),
  `tests/src/core/ControlAutomationScriptTest.cpp` (521), `tests/src/core/ControlDeviceCatalogueTest.cpp` (567),
  `tests/src/core/ControlEditCommandsTest.cpp` (511), `tests/src/core/CustomAccessTest.cpp` (556),
  `tests/src/core/DeviceDspTest.cpp` (752), `tests/src/core/HardwareIoTest.cpp` (556),
  `tests/src/core/NotePlaybackTest.cpp` (624), `tests/src/core/RackMacrosTest.cpp` (535),
  `tests/src/core/RevisionTimelineTest.cpp` (600), `tests/src/core/VideoTrackTest.cpp` (589),
  `tests/src/gui/ArrangerTimelineTest.cpp` (689).

The file-length count in the earlier report said 13. The correct number is **14**. Luna's count was right.
None of the 14 files is in this change, and `Vst3HostTest.cpp` is not on the list. These failures belong to
other clusters. Neither gate was re-anchored or had its baseline changed. Clearing them needs an owner decision
(split the files or re-anchor), so I did not do it here.

## Commit

Files committed (explicit paths only): `plugins/Vst3Effect/Vst3MidiEvent.cpp`, `plugins/Vst3Effect/Vst3MidiEvent.h`,
`plugins/Vst3Effect/Vst3MidiQueue.h`, `tests/src/plugins/Vst3HostTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-new-p9-509.md`. The fixed files are all existing fork files already registered in
`tests/fork-sources.txt`, and no file is new, so no registration change was needed.
