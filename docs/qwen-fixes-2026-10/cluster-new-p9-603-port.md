# Cluster new-p9-603-port: BitInvader allocates per note on the audio path

**Verdict: FIXED**

The defect was real on this branch and is fixed here. This is a **port** of the fix
accepted on `bugs/qwen-new-p9-603` (commits `a6c68de41` "fix(BitInvader): avoid per-note
render allocations" and `023765f70` "docs(qwen-fixes): cluster new-p9-603 BitInvader
audio-path allocation report"). That branch could not be merged into the newest line
because of a conflict in `include/NotePlayHandle.h`; the change is therefore written by
hand against the current code, not cherry-picked.

## The defect (file:line on the tree before this change)

- `plugins/BitInvader/BitInvader.cpp:255-262` (before): the first render callback of a
  note ran `nph->m_pluginData = new BSynth(...)` on the audio thread.
- `plugins/BitInvader/BitInvader.cpp:79` (before): the `BSynth` constructor ran
  `sample_shape = new float[wavetableSize]` (`wavetableSize` = 200, so 800 bytes), a
  second allocation in the same callback.
- `plugins/BitInvader/BitInvader.cpp:284-287` (before): `deleteNotePluginData` ran
  `delete`, so the note's end freed heap memory on the audio thread as well. That path is
  reached from `src/core/NotePlayHandle.cpp:160-162` (`~NotePlayHandle` ->
  `m_instrumentTrack->deleteNotePluginData(this)`) and
  `src/tracks/InstrumentTrack.cpp:775-781` (-> `m_instrument->deleteNotePluginData(n)`).
- The render runs inside the engine's realtime scope
  (`src/core/AudioEngine.cpp:919-923`, `renderNextPeriod()` opens `const RealtimeScope
  realtime;`), so all three allocations/frees were on the audio thread.

## The fix (file:line after this change)

- `plugins/BitInvader/BitInvader.h:51`: `wavetableSize` moved into the header as
  `inline constexpr int wavetableSize = 200` (the value is unchanged).
- `plugins/BitInvader/BitInvader.h:68`: `BSynth::sample_shape` is now
  `std::array<float, wavetableSize>` - the note's own wavetable copy lives inside the
  note state instead of on the heap. The `BSynth` destructor is gone (nothing to free).
- `plugins/BitInvader/BitInvader.cpp:72-91`: the constructor copies the graph into the
  inline array; no `new[]`.
- `plugins/BitInvader/BitInvader.cpp:255-264`: the first render constructs the state with
  `std::construct_at` into `NotePlayHandle::pluginDataStorage()`, guarded by
  `static_assert(sizeof(BSynth) <= NotePlayHandle::PluginDataStorageSize)` and
  `static_assert(alignof(BSynth) <= alignof(std::max_align_t))`.
- `plugins/BitInvader/BitInvader.cpp:281-284`: the note cleanup calls `std::destroy_at`,
  so the note's end frees nothing.
- `include/NotePlayHandle.h:82`: `PluginDataStorageSize` 720 -> 1024, so the 824-byte
  `BSynth` fits in the handle's inline storage. **This is the file the upstream branch
  conflicted on**: upstream raised it 512 -> 1024; the newest line had already raised it
  512 -> 720 for TripleOscillator (cluster new-p9-495, `tests/upstream-modifications.txt`
  line 2554). The port keeps the newest line's value as its base and raises it to 1024,
  so both users' `static_assert`s hold and the newer line's other user is untouched.

The design follows the pattern the newest line already uses for the same problem:
`plugins/Monstro/Monstro.cpp:1089-1092` and
`plugins/TripleOscillator/TripleOscillator.cpp:340-350` place their per-note state in
`NotePlayHandle::pluginDataStorage()` with `construct_at`/`destroy_at`.

## Evidence table

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-603-port.r1/logs`.

| Step | Exact command | Exit | Result (log) |
| --- | --- | ---: | --- |
| New test target needs a re-configure | `cmake --build build --target BitInvaderAllocationTest` | 2 | `No rule to make target 'BitInvaderAllocationTest'` (build-1.log); the lane guard refuses `cmake -S . -B build` (cmake-1.log), so the re-configure ran through the `zene` target |
| Re-configure | `cmake --build build --target zene` | 0 | `-- Generating done`, `zene` relinked (build-2.log) |
| RED build | `cmake --build build --target BitInvaderAllocationTest` | 0 | test target built against the unfixed plugin (build-3.log) |
| **RED** | `cd build/tests && ctest -R '^BitInvaderAllocationTest$' --output-on-failure` | **8** | `noteRenderMakesNoHeapAllocation()` FAILED: "period 0 made 2 heap allocations on the render path" (test-1.log) |
| GREEN build | `cmake --build build --target zene BitInvaderAllocationTest` | 0 | 92 lmmsobjs files + the plugin + the test rebuilt (build-4.log) |
| **GREEN** | `cd build/tests && ctest -R '^BitInvaderAllocationTest$' --output-on-failure` | **0** | 1/1 passed (test-2.log) |
| 4b reverse the fix | `git diff -- include/NotePlayHandle.h plugins/BitInvader/BitInvader.cpp plugins/BitInvader/BitInvader.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 | the product change is reverted, the test stays (fix.patch) |
| 4b RED build | `cmake --build build --target zene BitInvaderAllocationTest` | 0 | rebuild after the revert (build-6.log) |
| **4b RED** | `cd build/tests && ctest -R '^BitInvaderAllocationTest$' --output-on-failure` | **8** | the same assertion fails again: "period 0 made 2 heap allocations on the render path" (test-4.log) |
| 4b re-apply the fix | `git apply logs/fix.patch` | 0 | the fix is back in the tree |
| 4b GREEN build | `cmake --build build --target zene BitInvaderAllocationTest` | 0 | rebuild after the re-apply (build-7.log) |
| **4b GREEN** | `cd build/tests && ctest -R '^BitInvaderAllocationTest$' --output-on-failure` | **0** | 1/1 passed (test-5.log) |
| Plugin modules that include the changed header | `cmake --build build --target bitinvader monstro tripleoscillator freeboy gigplayer kicker lb302 organic patman sf2player sfxr sid malletsstk vibedstrings xpressive nes opulenz watsyn audiofileprocessor slicert` | 0 | every plugin module that includes `NotePlayHandle.h` rebuilt against the new header (build-8.log, build-9.log) |
| Affected-family tests | `cd build/tests && ctest -R '^(BitInvaderAllocationTest|NotePlaybackTest|MpePlaybackTest|InstrumentLayersCommandsTest|ClipTransposeTest|ArticulationPlaybackTest|InstrumentLatencyPdcTest|AudioFileProcessorPlayheadTest|MixerConcurrencyTest|NoteCurvesAcrossTest|SlideNotesTest|SlicerTPlayheadTest|SessionPlaybackRenderTest|ExprSynth|RackZoneRoutingTest|RackZoneFeedRunTest|UnmuteChaseTest|NoteEditingCommandsTest|MpeExpressionTest|MpeNoteStorageTest|MpeInputPathTest|SessionTuningTest|MidiInputTransformTest|PluginPortsMigrationTest|TripleOscillatorUserWaveTest)$' --output-on-failure` | 0 | **25/25 tests passed** (test-8.log) |
| BitInvader render is unchanged | `cd build/tests && ctest -R '^(BitInvaderAllocationTest|PluginPortsMigrationTest|TripleOscillatorUserWaveTest)$' --output-on-failure` | 0 | 3/3 passed; `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()` renders bitinvader with sha256 `4867c4e050125a7025a30c3c9071bbb1752ba8edee3ccac3d35549854ad8ece2`, identical to the frozen pre-migration reference (test-7.log, test-3.log) |
| Gate (all) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-603-port` | **0** | 8/8 rows PASS (gate-feature-check-2.log). The first run exited 1 because `tests/all-sources.txt` is derived from `git ls-files` and the new test file was not yet tracked; after `git add` the manifest reproduces byte-exactly (gate-feature-check-1.log, gate-fork-sources-1.log) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | 6 pre-existing REGRESSION lines, all in files this change does not touch (`tests/src/gui/XyPadPairTest.cpp` `dotCentreAlong`, `src/core/DawProjectReadTracks.cpp` `parseNotes`, `src/core/ControlCommandsMixerTools.cpp` `adjust`, `src/core/ControlCommandsMixerStrip.cpp` `setStrip`, `src/core/ProjectRevisions.cpp` `rotateProjectRevision`, `src/gui/ControlCommandsShortcuts.cpp` `importedOverrides`); none is in a changed file (gate-complexity-1.log). The ratchet comparison in feature_check reports "no new item in your files" -> PASS |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | 15 pre-existing REGRESSION lines, all in untouched files (`src/core/TakeLane.cpp`, `tests/src/core/*`, `tests/src/gui/ArrangerTimelineTest.cpp`); none is in a changed file, and the new test file is 205 lines (gate-filelength-1.log). The ratchet comparison reports "no new item in your files" -> PASS |
| Registration | `bash tests/fork-sources-gate.sh` | 0 | "PASS: every tracked source in scope is registered" (gate-fork-sources-1.log) |

### Environment note (not a regression of this change)

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` failed in the first
family run (test-3.log, test-6.log) with "SWH Decimator test fixture was not loaded":
the lane's build tree had never built the LADSPA fixture `decimator_1202.so`, so the
lookup failed. Building that fixture (`cmake --build build --target decimator_1202`,
exit 0, build-12.log) makes the whole family green (test-7.log, test-8.log). The same
failure is recorded as pre-existing on this train in
`docs/qwen-fixes-2026-10/cluster-new-p9-155-port.md` and
`docs/qwen-fixes-2026-10/cluster-new-p9-531.md`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  RED: `ctest -R '^BitInvaderAllocationTest$'` exit **8** ("period 0 made 2 heap
  allocations on the render path", test-1.log). Reverted-fix RED: exit **8**, the same
  assertion (test-4.log, after `git apply -R logs/fix.patch` and a rebuild,
  build-6.log). GREEN: exit **0** (test-2.log), and again exit **0** after
  `git apply logs/fix.patch` + rebuild (test-5.log, build-7.log).
- **The test is deterministic.** It counts allocations with the tree's own probe
  (`tests/src/core/AllocationProbe.h`, a replacement global `operator new` that counts
  per thread), so there is no wall-clock timing, no sleep and no race: the same render
  callback is measured three times in a row and the count must be 0 every time. The
  second case compares rendered frames of two notes (bit-exact `SampleFrame` equality),
  not timings.
- **The fix is at the cause and complete.** The cause is the per-note heap object on the
  render path; both halves are fixed: the construction
  (`plugins/BitInvader/BitInvader.cpp:255-264`, `construct_at`) and the destruction
  (`plugins/BitInvader/BitInvader.cpp:281-284`, `destroy_at`), which is the other path
  into the same state. Every caller was checked: `BSynth` is constructed only in
  `BitInvader::playNoteImpl` and destroyed only in `BitInvader::deleteNotePluginData`;
  `m_pluginData` is touched by no other code for this plugin, and
  `Instrument::deleteNotePluginData` (src/core/Instrument.cpp:102-104) is an empty base
  implementation that BitInvader overrides. The wider type the value flows into is
  `NotePlayHandle::m_pluginDataStorage`
  (`include/NotePlayHandle.h:446`), whose size is now declared once at
  `include/NotePlayHandle.h:82` and whose two other users (Monstro, TripleOscillator)
  keep their `static_assert`s. The note handle is pooled
  (`NotePlayHandleManager::acquire`, src/core/NotePlayHandle.cpp:769-790) and
  placement-newed on reuse, so `m_pluginData` is reset to `nullptr` for every new note
  and a reused handle never renders a stale state.
- **No collateral damage.** The plugin's saved settings are untouched
  (`sampleShape`, `sampleLength`, `interpolation`, `normalize` in
  `plugins/BitInvader/BitInvader.cpp:158-209`), so old projects load exactly as before,
  and `wavetableSize` keeps its value 200. The rendered output is bit-identical: the
  A/B test against the frozen pre-migration reference
  (`PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()`) reports the same
  sha256 `4867c4e050125a7025a30c3c9071bbb1752ba8edee3ccac3d35549854ad8ece2` for the
  migrated and the reference plugin (test-7.log). The only limit that was added is
  `static_assert(sizeof(BSynth) <= NotePlayHandle::PluginDataStorageSize)`, a compile-time
  check on this plugin's own state; it cannot hit any other caller.
- **Realtime / undo / commands.** On the audio thread nothing new allocates, frees,
  locks, waits, logs or emits a signal: `construct_at` and `destroy_at` write into the
  handle's own inline storage, and `BSynth` has no destructor and no owned heap memory
  any more (the class holds an `std::array` by value). `playNoteImpl` and
  `deleteNotePluginData` are the only touched functions on the audio path; neither
  touches `AudioEngine`, a mutex, a file or the logger. No control command was added,
  so the `ControlCommandsSnapshot` canary does not move, and undo is untouched (the fix
  changes no user-visible state, no model and no command).
- **Port fidelity.** The upstream fix (commit `a6c68de41`) was applied by hand: the same
  four product hunks (`include/NotePlayHandle.h`, `plugins/BitInvader/BitInvader.h`,
  `plugins/BitInvader/BitInvader.cpp`, and the new test) and the same registration
  (`tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`). The one
  deliberate difference is the constant's base value: upstream went 512 -> 1024, this
  port goes 720 -> 1024 because the newest line had already raised it to 720 for
  TripleOscillator. The regression test is the upstream test, adapted to the current
  tree's APIs (it compiles unchanged against this tree's `NotePlayHandle`,
  `InstrumentTrack` and `base64`).
- **Sibling allocations left alone.** Other instruments still allocate per note on the
  audio path (`plugins/Sfxr/Sfxr.cpp`, `plugins/Sid/SidInstrument.cpp`,
  `plugins/Sf2Player/Sf2Player.cpp`, `plugins/GigPlayer/GigPlayer.cpp` and the LADSPA
  hosts). They are separate findings with separate clusters; changing them here would
  exceed this bug's scope.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE"** - not applicable: the verdict
  is FIXED, and the code evidence for the defect and for the fix is in the two
  file:line sections above (`plugins/BitInvader/BitInvader.cpp:79`, `:262`, `:286`
  before; `:255-264`, `:281-284` after).

## Files committed

- `include/NotePlayHandle.h` - `PluginDataStorageSize` 720 -> 1024 (the file the upstream
  branch conflicted on).
- `plugins/BitInvader/BitInvader.h` - `wavetableSize` in the header, `BSynth` holds the
  wavetable copy inline, no destructor.
- `plugins/BitInvader/BitInvader.cpp` - `construct_at`/`destroy_at` into the note
  handle, no `new`/`delete` on the render path.
- `tests/src/plugins/BitInvaderAllocationTest.cpp` - the new regression (fork-NEW).
- `tests/CMakeLists.txt` - registers the test and compiles the production plugin into it.
- `tests/all-sources.txt` - the whole-tree source manifest (re-derived, reproduces).
- `tests/fork-sources.txt` - the fork-NEW registry.
- `tests/upstream-modifications.txt` - the divergence ledger entries for the inherited
  files.
- `docs/qwen-fixes-2026-10/cluster-new-p9-603-port.md` - this report.
