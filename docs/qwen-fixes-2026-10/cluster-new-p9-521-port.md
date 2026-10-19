# Cluster new-p9-521 (port): Watsyn allocates note buffers on the audio path

Verdict: **FIXED**

## Finding

`plugins/Watsyn/Watsyn.cpp:76` (the line the patrol quoted) allocated two
period-sized `SampleFrame` arrays in the `WatsynObject` constructor, and
`WatsynInstrument::playNoteImpl()` created that object with `new` on every
note start, so a note that begins while the engine renders allocates on the
audio thread. The finding is **REAL** on this branch: the pre-fix tree still
had `m_abuf = new SampleFrame[_frames]; m_bbuf = new SampleFrame[_frames];`
and `auto w = new WatsynObject(...)` in the note path.

This is the port of the fix accepted on `bugs/qwen-new-p9-521`
(`9d7097dc3`, `a6241c22a`). The accepted branch could not be merged into this
line because its regression included `../core/AllocationProbe.h` while
`tests/src/plugins/PluginPortsMigrationTest.cpp` already defines its own
replacement `operator new` (line 105) - linking the shared probe
(`zene_allocation_probe`, `tests/CMakeLists.txt:1321`) would define
`operator new` twice and the link would fail with a duplicate symbol.

**The port keeps ONE probe.** The regression counts with the replacement
`operator new` that `PluginPortsMigrationTest.cpp` already defines, which
feeds `partc::RealtimeAllocationWatch` (`tests/src/plugins/PluginPortsHarness.h:75`).
`AllocationProbe.h` is not included and `zene_allocation_probe` is not linked,
so the test binary keeps exactly one replacement allocation function.

## Change

- `plugins/Watsyn/Watsyn.h:77-117`: `WatsynObject` owns no period-sized
  buffer any more. It is a default-constructible voice with a lock-free
  claim (`std::atomic_flag m_inUse`, line 101), `initialize()` (line 88) and a
  `renderOutput(std::span<SampleFrame>)` that writes into the note's own
  buffer (line 94).
- `plugins/Watsyn/Watsyn.cpp:291-305`: a module-wide pool of 256 preallocated
  voices (`std::array<WatsynObject, 256> watsynVoices`) and
  `acquireWatsynVoice()`, which claims a free voice with an atomic flag - no
  mutex, no allocation. The pool lives in the module's BSS
  (`nm -C build/plugins/libwatsyn.so` shows `watsynVoices` at 28,836,608 bytes
  of zero-initialised BSS), so nothing is allocated when a note starts.
- `plugins/Watsyn/Watsyn.cpp:405-432` (`playNoteImpl`): the first period of a
  note claims a voice, initialises it and renders into
  `out.data() + offset`. A note that finds every voice busy is silenced for
  that period and tries again on the next one - the pool is the only new
  limit, and it is per loaded module.
- `plugins/Watsyn/Watsyn.cpp:106-281` (`renderOutput`): the A/B mixing that
  used to happen in `playNoteImpl` over two scratch buffers now happens per
  frame inside the render loop, so two notes that sound at the same time never
  share a render buffer. The arithmetic is unchanged (same `qBound` mix
  envelope, same `( A * amix ) + ( B * bmix )`), which the frozen reference
  module proves: the migrated Watsyn still hashes to the reference sha256
  `6fb8637ff7eb347e...` (`test-12.log:139`).
- `plugins/Watsyn/Watsyn.cpp:435-444` (`deleteNotePluginData`): the voice goes
  back to the pool instead of being deleted, and `m_pluginData` is cleared so
  a second cleanup cannot release a voice another note has claimed (the same
  contract `plugins/SlicerT/SlicerT.cpp:172` uses for its slot pool).
- `tests/src/plugins/PluginPortsMigrationTest.cpp:1914-2023`: the regression
  `watsynOverlappingNotesAreIndependentAndAllocationFree`.

## Regression

`PluginPortsMigrationTest::watsynOverlappingNotesAreIndependentAndAllocationFree`
renders eight notes one after another (reference), then starts the same eight
notes together on eight worker threads and compares every rendered sample with
that note's reference, while each worker counts the allocations its own thread
made. No timing is involved: the workers meet at a `std::barrier` and the
assertions are on allocation counts and sample values.

## Evidence

All commands were run from the worktree root, except the `ctest` ones, which
were run from `build/tests`. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-521-port.r1/logs`.

| Run | Command | Exit | Log |
| --- | --- | ---: | --- |
| Build the regression | `cmake --build build --target PluginPortsMigrationTest -j12` | 0 | `build-1.log` |
| RED on the original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-1.log` |
| Build the change | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-2.log` |
| GREEN with the change | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 -> see note | `test-2.log` |
| Save the product patch | `git diff -- plugins/Watsyn/Watsyn.cpp plugins/Watsyn/Watsyn.h > .../logs/fix.patch` | 0 | `fix.patch` |
| Reverse the product patch | `git apply -R .../logs/fix.patch` | 0 | - |
| Rebuild the reverted product | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-10.log` |
| RED with the product reverted | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-11-red-reversed.log` |
| Restore the product patch | `git apply .../logs/fix.patch` | 0 | - |
| Rebuild the restored product | `cmake --build build --target zene watsyn PluginPortsMigrationTest -j12` | 0 | `build-11.log` |
| GREEN with the product restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-12-green-restored.log` |
| The regression alone | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest watsynOverlappingNotesAreIndependentAndAllocationFree` (cwd `build/tests`) | 0 | `test-5-single-slot.log` |
| Gate (all gates) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-1-feature-check.log` |

The failing assertion, verbatim from `test-11-red-reversed.log:163-164`:

```text
QINFO  : ... Watsyn overlap max|delta|=0 worker note allocations=3,3,3,3,3,3,3,3
FAIL!  : ... 'allocationFree' returned FALSE. (Watsyn note start allocated on a rendering worker)
```

Three allocations per note start: the `WatsynObject` plus its two period
buffers. With the fix (`test-12-green-restored.log:163`): `worker note
allocations=0,0,0,0,0,0,0,0`, and the family run is `100% tests passed out of
1` (exit 0).

Note on the earlier runs (`test-1.log`, `test-2.log`): the family also
contains `nonRealtimeLadspaEffectsAreInvalid`, which needs the SWH Decimator
LADSPA plugin. It was missing from this container (only the mda basics are in
`/usr/lib/ladspa`), so that slot failed in every run until the vendored
fixture was built (`cmake --build build --target decimator_1202 -j8`, exit 0,
`build-9-decimator.log`). Proof that it is environmental and not caused by
this change: running that slot alone, without the Watsyn regression, fails on
its own (`test-1b-ladspa-only.log`, exit 1). Every proof run quoted above was
repeated after the fixture was built, so the RED run has exactly one failing
slot - the new regression - and the GREEN run has none.

The affected CTest family is **1/1** registered test target
(`PluginPortsMigrationTest`), 45 slots passed, 2 skipped in the GREEN run.
`ctest -N` shows no other registered test loads the Watsyn module
(`rg -l watsyn tests/` names only `PluginPortsMigrationTest.cpp`,
`PluginPortsMigrationReference.cpp`, `PluginPortsHarness.h`).

## Self-check

- **Does the regression FAIL with the product change reverted and PASS with it
  (exit codes)?** Yes. Reverted: `ctest -R '^PluginPortsMigrationTest$'` exits
  **8**, the new slot fails with `worker note allocations=3,3,3,3,3,3,3,3`
  (`test-11-red-reversed.log`). Restored: the same command exits **0**,
  `100% tests passed out of 1` (`test-12-green-restored.log`). The
  reverse-patch cycle itself: `git diff` exit 0, `git apply -R` exit 0,
  `git apply` exit 0.
- **Is the test deterministic (no wall-clock timing, sleeps or races)?** Yes.
  The eight workers meet at a `std::barrier` (`<barrier>`), so they start
  together; no sleep, no clock, no timeout is used. The two assertions are on
  values, not on timing: the per-thread allocation count (a counter in the
  replacement `operator new`) and the sample values (each worker compares its
  16 periods against the same note rendered alone). Both hold identically on
  every run: the RED run reported `3,3,3,3,3,3,3,3` and the GREEN run
  `0,0,0,0,0,0,0,0`, with `max|delta|=0` in both.
- **Is the fix at the cause and complete?** Yes. The cause was the per-note
  heap buffers, and they are gone from the note path: the note start now
  claims a preallocated voice (`Watsyn.cpp:405-432`) and the render writes
  into the note's own span (`Watsyn.cpp:106-281`). Callers and siblings
  checked: `deleteNotePluginData` (`Watsyn.cpp:435`) is the only other place
  that touches the note's plugin data, and it returns the voice to the pool;
  it is reached from `NotePlayHandle::~NotePlayHandle`
  (`src/core/NotePlayHandle.cpp:149-152`) through
  `InstrumentTrack::deleteNotePluginData` (`src/tracks/InstrumentTrack.cpp:728`),
  and `NotePlayHandleManager::release` runs that destructor
  (`src/core/NotePlayHandle.cpp:753-759`), so every finished note returns its
  voice. The track destructor silences the notes before it deletes the
  instrument (`src/tracks/InstrumentTrack.cpp:222-229`), so no voice is left
  pointing at a dead instrument. The sibling that shares the same pattern,
  `plugins/SlicerT/SlicerT.cpp:79-187`, uses the same claim/release contract
  and is left untouched. No other code referenced the removed members
  (`rg 'm_abuf|m_bbuf|abuf\(|bbuf\(|m_fpp' plugins/Watsyn/` is empty; only the
  frozen reference module `tests/reference/Watsyn/` still has them, and it must
  not change).
- **No collateral damage?** Old projects and saved data are untouched: no
  setting, no file format and no serialised state changed - the change is
  inside the render path only. The rendered output is unchanged: the migrated
  module still produces the frozen reference's sha256
  `6fb8637ff7eb347ed436850ecddb23318e55d4e83fef070d2d8d2143bf0d465a`
  (`test-12-green-restored.log:139`), so saved songs sound exactly as before.
  The one new limit is the pool: 256 simultaneous Watsyn notes per loaded
  module. It cannot hit a legitimate caller in a way that changes existing
  behaviour for fewer notes, and a note that finds the pool full is silenced
  for one period and retries, rather than dropping out. The pool is
  zero-initialised BSS in the module (`size build/plugins/libwatsyn.so`: bss
  28,854,312 bytes), allocated when the module is loaded, not per note.
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
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-521-port.md`
