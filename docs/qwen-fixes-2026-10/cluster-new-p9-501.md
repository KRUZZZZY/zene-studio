# Cluster new-p9-501 — Vibed allocates while starting a note

**Verdict: NEEDS-CONTEXT.** The defect is real. The fix changes audible behaviour and
needs an owner decision (pool size, oversize-note policy, sample-rate changes), so no
production code was changed and no RED/GREEN regression was produced.

## Finding

Luna's patrol reported that `Vibed::playNoteImpl` allocates heap memory on the audio path
when a note starts (`plugins/Vibed/Vibed.cpp:204-230`). Each enabled string then allocates
its delay lines and impulse buffer.

## Evidence (file:line, this branch `bugs/qwen-new-p9-501` at `ded355440`)

| Allocation | Location |
| --- | --- |
| `new StringContainer{...}` per note, on the audio path | `plugins/Vibed/Vibed.cpp:208-209` (guard `if (!n->m_pluginData)` at `:206`) |
| `std::array<VibratingString, s_stringCount>` built inside the container | `plugins/Vibed/Vibed.cpp:95` |
| Per-string `m_outsamp` | `plugins/Vibed/VibratingString.cpp:44` |
| Per-string impulse buffer, length `stringLength` | `plugins/Vibed/VibratingString.cpp:53` (and `:58` for state=true) |
| Two delay lines per string, each with `make_unique` DelayLine and data | `plugins/Vibed/VibratingString.cpp:71-77`, called from `:60-61` |
| `stringLength = m_oversample * sampleRate / pitch + 1`, scaled by detune | `plugins/Vibed/VibratingString.cpp:46-48` |

The audio-thread context is confirmed:

- `AudioEngine::renderNextPeriod()` opens `const RealtimeScope realtime;` at
  `src/core/AudioEngine.cpp:918`. Everything under it is "realtime code" for RTSan.
- `NotePlayHandle::play` calls `m_instrumentTrack->playNote(this, buffer)` at
  `src/core/NotePlayHandle.cpp:307`.
- `InstrumentTrack::playNote` calls `m_instrument->playNote(n, workingBuffer)` at
  `src/tracks/InstrumentTrack.cpp:700`.

So `Vibed::playNoteImpl` runs inside that realtime scope.

The size of the allocations depends on the note's pitch and the sample rate, so a
fixed-size pool cannot cover them as written.

## Precedent found

`include/NotePlayHandle.h:74-80` provides in-handle storage for plugin note state
(`PluginDataStorageSize = 512`, `pluginDataStorage()`). `plugins/Monstro/Monstro.cpp:1088-1093`
uses it with `std::construct_at` and a `static_assert` on size. Monstro's note state fits
that storage. Vibed's `StringContainer` holds nine `VibratingString` objects. I estimate
that is near or over 512 bytes, but I did not measure it. The in-handle storage would
remove only the container allocation. The delay-line and impulse buffers would still be
heap-allocated, and those are the bulk of the cost.

## Why this is NEEDS-CONTEXT and not a fix

A behaviour-preserving fix needs all of the following, and the contract for them is not in
the repository:

1. **Bounded capacity.** The buffer size is `oversample * sampleRate / pitch`. For very low
   notes, that is unbounded. A preallocated arena needs a maximum frequency or length.
2. **Oversize-note policy.** When a note exceeds the arena, the options are silence, a
   clamped pitch, or a dropped string. Each one changes audible output. The owner has to
   choose.
3. **Sample-rate changes.** Buffers sized at one sample rate would need reallocating when
   the device rate changes. That must happen off the audio thread.
4. **Pool exhaustion.** More simultaneous notes than the pool holds need a defined outcome.

The finding's own suggested fix, "prepare note state outside the audio callback", has no
hook here. Note creation is on the audio thread too, because MIDI events are processed
there.

## Regression status

- **No RED/GREEN run.** No product code was changed, so there is no fix to prove. Writing
  an allocation-probe test would mean building a plugin-level harness; it was not done in
  this pass.
- The finding's cited test, `tests/reference/Vibed/Vibed.cpp` (`ctest -R '^Vibed$'`), does
  not map to a ctest entry. `tests/reference/` is frozen reference source, not a registered
  test. Vibed has no ctest entry (`ctest -N` lists 593 tests; none match "vibed").
- Existing coverage that touches Vibed's plugin surface is `PluginPortsMigrationTest` and
  `PluginLogoResourceTest`. Neither asserts zero allocations during `playNote`.
- The allocation probe (`tests/src/core/AllocationProbe.h`) is available, and its users are
  listed in the test tree. It would be the right tool for a RED allocation count on
  `playNoteImpl` once the contract is decided.

## Commands run (exact, with EXIT codes)

Commands are run from the worktree root unless noted. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-501.r1/logs/`.

| Command | EXIT |
| --- | --- |
| `git status --short` / `git branch --show-current` | 0 (clean; `bugs/qwen-new-p9-501`) |
| `cd build/tests && ctest -N > .../ctest-N-1.log 2>&1` | 0 (Total Tests: 593) |
| `bash tests/complexity-gate.sh --check > .../gate-complexity-1.log 2>&1` | **1** |
| `bash tests/file-length-gate.sh --check > .../gate-filelength-1.log 2>&1` | **1** |

### Gate failures (pre-existing on the base commit; not caused by this report)

No product file was changed, so both failures are present on `ded355440` and are not
caused by this work. I did not edit any baseline or limit.

- **Complexity** (`gate-complexity-1.log`): "FAIL: complexity ratchet regressed". The
  regressions are `dotCentreAlong` (CCN 16, `tests/src/gui/XyPadPairTest.cpp:76`),
  `parseNotes` (CCN 13, `src/core/DawProjectReadTracks.cpp:94`), `adjust` (CCN 12,
  `src/core/ControlCommandsMixerTools.cpp:215`), `setStrip` (CCN 11,
  `src/core/ControlCommandsMixerStrip.cpp:250`), `rotateProjectRevision` (CCN 11,
  `src/core/ProjectRevisions.cpp:151`), and `importedOverrides` (CCN 11,
  `src/gui/ControlCommandsShortcuts.cpp:168`).
- **File length** (`gate-filelength-1.log`): "FAIL: file-length ratchet regressed". The
  regressions include `src/core/SmfInterchangeReader.cpp` (504), `src/core/TakeLane.cpp`
  (520), and several `tests/src/core/*Test.cpp` files at 511-752 lines.

Neither gate touches `plugins/Vibed/`. Both need an owner or base-branch decision, not a
change from this cluster.

## What the owner needs to decide

1. Whether Vibed should keep its per-note allocation or move to bounded in-handle storage.
   If bounded, choose the maximum frequency or buffer length.
2. What an oversize or over-pool note should do: silence, a clamped pitch, or a dropped
   string.
3. Whether a sample-rate change should rebuild the arena off the audio thread, and where
   that hook lives.

Once those are chosen, the next step is an allocation-probe RED on `playNoteImpl` in a new
test, then the fix, GREEN, and the gates.
