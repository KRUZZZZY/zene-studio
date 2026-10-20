FIXED

# Cluster new-p9-521 (port, round 2): Watsyn allocates note buffers on the audio path

## Verdict

**FIXED.** The finding was real on this branch: `plugins/Watsyn/Watsyn.cpp:76`
(the line the patrol quoted) allocated two period-sized `SampleFrame` arrays in
the `WatsynObject` constructor, and `WatsynInstrument::playNoteImpl()` created
that object with `new` on every note start, so a note that begins while the
engine renders allocated on the audio thread.

This is the port of the fix accepted on `bugs/qwen-new-p9-521`
(`9d7097dc3`, `a6241c22a`). The accepted branch could not be merged into this
line because its regression included `../core/AllocationProbe.h` while
`tests/src/plugins/PluginPortsMigrationTest.cpp` already defines its own
replacement `operator new` (line 105) - linking the shared probe
(`zene_allocation_probe`, `tests/CMakeLists.txt:1321`) would define
`operator new` twice and the link would fail with a duplicate symbol.

**The port keeps ONE probe.** The regressions count with the replacement
`operator new` that `PluginPortsMigrationTest.cpp` already defines, which
feeds `partc::RealtimeAllocationWatch` (`tests/src/plugins/PluginPortsHarness.h:75`).
`AllocationProbe.h` is not included and `zene_allocation_probe` is not linked,
so the test binary keeps exactly one replacement allocation function.

## Round 2: the voice limit is gone

Round 1 of this port kept the per-note state in a module-wide pool of 256
preallocated voices and silenced a note that found the pool full. The review
was right that this changes playback: the note manager grows its cache as
needed and has no 256-note cap (`src/core/NotePlayHandle.cpp:734`,
`:762`), while the silenced note's play time still advances
(`src/core/NotePlayHandle.cpp:366`) - a supported workload lost a period or a
whole short note. **Round 2 has no limit at all.**

### The design

- **The per-note state lives in the note handle's own storage.**
  `NotePlayHandle` carries 512 bytes of inline plugin storage
  (`include/NotePlayHandle.h:77-80`), the same place Monstro's per-note synth
  lives (`plugins/Monstro/Monstro.cpp:1089-1092`). `WatsynVoice`
  (`plugins/Watsyn/Watsyn.h:105-133`) is 72 bytes and is constructed in place
  with `std::construct_at` (`plugins/Watsyn/Watsyn.cpp:455-478`). There is no
  pool, no slot counter and therefore no cap: how many Watsyn notes may sound
  at the same time is the note manager's limit.
- **Each note renders into its own buffer.** The A/B mix that used to run
  over two shared scratch buffers now happens per frame inside the render
  loop (`plugins/Watsyn/Watsyn.cpp:101-276`), so two notes that sound at the
  same time never share a render buffer. The arithmetic is unchanged (same
  `qBound` mix envelope, same `( A * amix ) + ( B * bmix )`).
- **The wavetable a note renders with is a snapshot the note pins.**
  `WatsynWave` (`plugins/Watsyn/Watsyn.h:87-101`) is an immutable copy of the
  four wavetables. The GUI thread publishes a new one when a graph changes
  (`plugins/Watsyn/Watsyn.cpp:289-323`, called from `updateWaveA1` and its
  three siblings at `:666`), and a note pins the snapshot that was current
  when it started (`claimWave`, `plugins/Watsyn/Watsyn.cpp:325-336`). A
  sounding note therefore keeps the wave it started with - the promise the
  upstream code kept with its per-note copy ("copy wavegraphs to the synth
  object to prevent race conditions") - while notes that start while the same
  snapshot is current share one buffer instead of one buffer each.
- **The snapshot's lifetime is exact.** The audio thread announces its claim
  (`m_waveClaimers`), pins the snapshot and leaves the window again; the GUI
  thread only recycles a retired snapshot when no note is inside that window
  and no note holds it (`recycleWaves`, `plugins/Watsyn/Watsyn.cpp:338-357`).
  Every allocation and every free of a snapshot happens on the GUI thread; the
  audio thread only does atomic increments.

Nothing on the audio path allocates, frees, locks, waits or logs:
`playNoteImpl`, `claimWave`, `renderOutput` and `deleteNotePluginData` are now
declared in `tests/rt-safety-scope.txt`, and the rt-safety sweep measures them
(0 allocation hits).

## Regressions

All three live in `tests/src/plugins/PluginPortsMigrationTest.cpp`:

| Slot | Line | What it proves |
| --- | ---: | --- |
| `watsynOverlappingNotesAreIndependentAndAllocationFree` | 1916 | Eight notes started together on eight workers stay independent of each other, and no worker allocates. |
| `watsynServesEveryAdmittedNoteWithoutAllocating` | 2038 | 320 notes - more than the 256 the round-1 pool had - all sound at the same time, every one of them renders its own audio, and no worker allocates. |
| `watsynKeepsTheWaveANoteStartedWith` | 2157 | A sounding note keeps the wave it started with, and the next note renders the wave that was just edited. |

The first two count with the replacement `operator new` the test file already
defines. No wall-clock timing, sleeps or races are involved: the workers meet
at a `std::barrier`, and the assertions are on allocation counts and sample
values.

## Evidence

All commands were run from the worktree root, except the `ctest` ones, which
were run from `build/tests`. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-521-port.r1/logs`.

| Run | Command | Exit | Log |
| --- | --- | ---: | --- |
| Build the change | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-25.log` |
| GREEN with the change | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-26-green-restored.log` |
| The whole suite, unfiltered | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest` (cwd `build/tests`) | 0 | `test-24-full.log`; 48 passed, 0 failed, 2 skipped |
| Save the product patch | `git diff -- plugins/Watsyn/Watsyn.cpp plugins/Watsyn/Watsyn.h > .../logs/fix.patch` | 0 | `fix.patch` |
| Reverse the product patch | `git apply -R .../logs/fix.patch` | 0 | - |
| Rebuild the reverted product | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-24.log` |
| RED with the product reverted | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-25-red-reversed.log` |
| Restore the product patch | `git apply .../logs/fix.patch` | 0 | - |
| Rebuild the restored product | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-25.log` |
| GREEN with the product restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-26-green-restored.log` |
| rt-safety sweep | `python3 tests/rt-safety-sweep.py --check` | 0 | `gate-6-rt-safety.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `gate-3-complexity.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `gate-4-file-length.log` |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-7-feature-check.log` |

### The three RED states, each measured

1. **Round 1's voice pool** (the product patch reversed, `test-25-red-reversed.log:165-166`):
   `Watsyn 320 simultaneous notes: max|delta|=1 silent notes=64` and
   `FAIL! ... 'silentNotes == 0' returned FALSE. (Watsyn silenced a note it
   could have played)`. Exactly 320 - 256 = 64 notes were dropped: the
   review's blocker, caught by the new regression.
2. **The original code** (the round-1 patch reversed as well,
   `test-18-red-original.log:6-10`): `worker allocations=120,120,120,120,120,120,120,120`
   for the 320 notes (40 notes per worker, three allocations each: the
   `WatsynObject` plus its two period buffers) and `3,3,3,3,3,3,3,3` for the
   eight concurrent notes - both slots FAIL with
   `Watsyn note start allocated on a rendering worker`.
3. **GREEN with the fix** (`test-26-green-restored.log`): the family is
   `100% tests passed out of 1`; the measured values are
   `Watsyn 320 simultaneous notes: max|delta|=0 silent notes=0 worker allocations=0,0,0,0,0,0,0,0`
   (`test-16-new-slots.log`) and
   `Watsyn wave peaks: first period=0.999987 second period=1 note after the edit=0`
   (`test-22-wave.log`).

The affected CTest family is **1/1** registered test target
(`PluginPortsMigrationTest`). `ctest -N` shows no other registered test loads
the Watsyn module (`rg -l watsyn tests/` names only `PluginPortsMigrationTest.cpp`,
`PluginPortsMigrationReference.cpp`, `PluginPortsHarness.h`).

### The two ratchet gates

`bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh
--check` both exit **1** on this tree, and the reviewer saw the same. The
reported regressions are in files outside this commit and outside this
cluster - `src/core/ControlCommandsMixerTools.cpp`, `src/core/ProjectRevisions.cpp`,
`src/gui/ControlCommandsShortcuts.cpp`, `tests/src/gui/XyPadPairTest.cpp`,
`src/core/DawProjectReadTracks.cpp`, `src/core/SmfInterchangeReader.cpp`,
`src/core/TakeLane.cpp`, `tests/src/core/*.cpp` - and neither gate lists a
file this commit changes (`gate-3-complexity.log`, `gate-4-file-length.log`).
The gate that compares the branch with its base commit,
`feature_check.py`, reports `PASS complexity no new item in your files` and
`PASS file-length no new item in your files` and exits **0**
(`gate-7-feature-check.log`). No gate limit or baseline was changed.

## Self-check

- **Does the regression FAIL with the product change reverted and PASS with it
  (exit codes)?** Yes. Reverted: `ctest -R '^PluginPortsMigrationTest$'` exits
  **8** and the new slot fails with `silent notes=64`
  (`test-25-red-reversed.log`). Restored: the same command exits **0**,
  `100% tests passed out of 1` (`test-26-green-restored.log`). Against the
  original (pre-fix) code both allocation slots fail (`test-18-red-original.log`,
  exit 2 for the binary, 8 for ctest). The reverse-patch cycle itself:
  `git diff` exit 0, `git apply -R` exit 0, `git apply` exit 0.
- **Is the test deterministic (no wall-clock timing, sleeps or races)?** Yes.
  The workers meet at a `std::barrier` (`<barrier>`), so they start together;
  no sleep, no clock, no timeout is used. The assertions are on values, not on
  timing: the per-thread allocation count (a counter in the replacement
  `operator new`), the number of silent notes and the sample values (each
  note compared with the same note rendered alone). Every run reported the
  same numbers: `silent notes=0`, `max|delta|=0`, `allocations=0,...,0`.
- **Is the fix at the cause and complete?** Yes. The cause was the per-note
  heap buffers, and they are gone from the note path: the note start
  constructs its state in the note handle's storage
  (`Watsyn.cpp:455-478`) and the render writes into the note's own span
  (`Watsyn.cpp:101-276`). Callers and siblings checked: `deleteNotePluginData`
  (`Watsyn.cpp:481-491`) is the only other place that touches the note's
  plugin data, and it destroys the voice and gives the snapshot back; it is
  reached from `NotePlayHandle::~NotePlayHandle`
  (`src/core/NotePlayHandle.cpp:149-152`) through
  `InstrumentTrack::deleteNotePluginData` (`src/tracks/InstrumentTrack.cpp:728`),
  and `NotePlayHandleManager::release` runs that destructor
  (`src/core/NotePlayHandle.cpp:753-759`), so every finished note returns its
  snapshot. The track destructor silences the notes before it deletes the
  instrument (`src/tracks/InstrumentTrack.cpp:222-229`), so no snapshot is
  left pointing at a dead instrument and the instrument's destructor
  (`Watsyn.cpp:279-287`) can free them. The sibling with the same shape,
  `plugins/SlicerT/SlicerT.cpp:79-187`, keeps its own bounded slot pool and is
  left untouched. No other code referenced the removed members
  (`rg 'm_abuf|m_bbuf|abuf\(|bbuf\(|m_fpp|WatsynObject' plugins/Watsyn/` is
  empty; only the frozen reference module `tests/reference/Watsyn/` still has
  them, and it must not change).
- **No collateral damage?** Old projects and saved data are untouched: no
  setting, no file format and no serialised state changed - the change is
  inside the render path only, and `saveSettings`/`loadSettings` are
  untouched. The rendered output is unchanged: the migrated module still
  produces the frozen reference's sha256
  `6fb8637ff7eb347ed436850ecddb23318e55d4e83fef070d2d8d2143bf0d465a`
  (`test-21-migration.log`), so saved songs sound exactly as before. The one
  new limit is gone: there is no voice limit any more, and the note manager's
  own limit is the only one. The wave a note renders with is unchanged too -
  `watsynKeepsTheWaveANoteStartedWith` shows a sounding note keeps the wave
  it started with (peak 1.0 in the second period) while the next note renders
  the edited wave (peak 0). The snapshot buffers are allocated and freed by
  the GUI thread only; the audio thread does atomic increments.
- **Verdict evidence (ALREADY-FIXED / FALSE-POSITIVE)?** Not applicable - the
  verdict is FIXED. For the record, the pre-fix tree really did allocate:
  `plugins/Watsyn/Watsyn.cpp:76-77` (`m_abuf = new SampleFrame[_frames]; m_bbuf
  = new SampleFrame[_frames];`) and `:348` (`auto w = new WatsynObject(...)`
  in `playNoteImpl`), and the RED run measured three allocations per note
  start on the rendering worker.

## Files committed

- `plugins/Watsyn/Watsyn.cpp`
- `plugins/Watsyn/Watsyn.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-521-port.md`
