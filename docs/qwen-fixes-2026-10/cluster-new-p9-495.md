# Cluster new-p9-495: TripleOscillator allocates note state on the audio path

**Verdict: NEEDS-CONTEXT** — the finding is real (measured: 7 heap allocations and 7
deallocations around the first TripleOscillator note period and its cleanup), but a correct
fix cannot be made inside `plugins/TripleOscillator/TripleOscillator.cpp` alone. The six
`Oscillator` objects it builds store reference members bound to the note and the instrument,
and `~Oscillator` owns and deletes `m_subOsc`, so neither the fork's in-handle storage
(`NotePlayHandle::pluginDataStorage()`, 512 B) nor a reusable pool can hold that chain
without changing the shared `Oscillator` class. This is the same finding class as the Organic
cluster `new-p9-331`, which was ruled NEEDS-CONTEXT for the same missing note-state lifecycle.

## The finding

> First processing a note allocates six `Oscillator` objects and plugin data; note cleanup
> deletes them. This path runs during audio processing and can stall the audio thread.
> Fix: Preallocate note state off the audio thread and defer reclamation until it is safe to free.

## Evidence

| Location | Evidence |
|---|---|
| `plugins/TripleOscillator/TripleOscillator.cpp:306` | `playNoteImpl()` is the per-note render callback. |
| `plugins/TripleOscillator/TripleOscillator.cpp:319,327,338,347` | First period allocates six `Oscillator` objects (`new Oscillator`, 3 left + 3 right). |
| `plugins/TripleOscillator/TripleOscillator.cpp:364` | Allocates the `oscPtr` wrapper. |
| `plugins/TripleOscillator/TripleOscillator.cpp:386-393` | `deleteNotePluginData()` deletes both oscillator chains and the wrapper. |
| `src/core/NotePlayHandle.cpp:149-151` | `~NotePlayHandle()` calls `deleteNotePluginData()` while the note is retired. |
| `src/core/AudioEngine.cpp:283-285` | The render owner retires a finished note with `NotePlayHandleManager::release()`, which runs that destructor (`src/core/NotePlayHandle.cpp:754-756`). |
| `include/Oscillator.h:80-90` | `Oscillator`'s constructor binds `freq`, `detuning_div_samplerate`, `phase_offset` and `volume`; `~Oscillator()` does `delete m_subOsc`. |
| `include/Oscillator.h:246-250` | Those four are **reference** members (`const float& m_freq`, `m_detuning_div_samplerate`, `m_volume`, `m_ext_phaseOffset`) plus the owning `Oscillator* m_subOsc`. |
| `plugins/TripleOscillator/TripleOscillator.cpp:322,341` | `m_freq` is bound to `_n->frequency()` (`include/NotePlayHandle.h:125-128`), which is note-specific and cannot be rebound after construction. |
| `include/NotePlayHandle.h:77-80,393` | The fork already provides 512-byte in-handle storage for note state created on the audio path. |
| `plugins/Monstro/Monstro.cpp:1089-1092,1113` | The established fix pattern: `static_assert(sizeof(MonstroSynth) <= PluginDataStorageSize)` then `std::construct_at`/`std::destroy_at` in that storage. |
| `plugins/Organic/Organic.cpp:245-274` | Organic builds the identical `new Oscillator` chain; its finding (`cluster-new-p9-331`) was ruled NEEDS-CONTEXT. |

### Measured allocation (temporary probe, reverted)

A temporary probe in the existing integration test (the FreeBoy pattern at
`tests/src/plugins/PluginPortsMigrationTest.cpp:1450-1459`) counted `operator new`/`operator delete`
around the TripleOscillator render and cleanup. It printed:

```
QINFO  : PluginPortsMigrationTest::migratedPluginsPreserveBehaviour() PROBE tripleoscillator allocations=7 deallocations=7
```

i.e. exactly 6 `Oscillator` + 1 `oscPtr`, matching the source. The probe edit was reverted with
`git checkout -- tests/src/plugins/PluginPortsMigrationTest.cpp`; no test change is committed.

### Why the fix is not local

A size probe compiled with the plugin's own flags (include paths from
`build/compile_commands.json`) reported:

```
sizeof(Oscillator)=112
sizeof(oscPtr)=16
6 osc + oscPtr=688
PluginDataStorageSize=512
```

Two independent blockers stop a local fix:

1. **Size.** The note state is 688 B, which does not fit the 512-byte in-handle storage. Enlarging
   `PluginDataStorageSize` is possible but is not the hard part.
2. **Ownership.** Even with more storage, the chain cannot be lived in a pool or an in-handle
   buffer. `~Oscillator()` unconditionally `delete`s `m_subOsc`, and the six objects are a chain
   (`oscs_l[i]` owns `oscs_l[i+1]`). Destroying the head calls `operator delete` on pointers into
   the buffer/pool — undefined behavior; destroying children first double-frees. `m_subOsc` is
   private with no detach/reset API. Re-arming a pooled object is also impossible: `m_freq` and the
   detuning/phase/volume members are **references** fixed at construction, and `m_freq` references
   the per-note `_n->m_frequency`.

   Removing the chain from Oscillator, or giving it a non-owning/reset form, changes
   `include/Oscillator.h` and `src/core/Oscillator.cpp`, which are shared with Organic
   (`plugins/Organic/Organic.cpp:245-274`) — the same finding that is already NEEDS-CONTEXT — and
   with every other `Oscillator` user. That is a cross-plugin design decision, not a
   one-plugin bug fix.

3. **No capacity contract.** A fixed pool needs a capacity and an exhaustion policy. Note handles
   are allocated and extended without bound (`src/core/NotePlayHandle.cpp:749-766`,
   `include/NotePlayHandle.h` `PluginDataStorageSize` is per-handle rather than per-instrument), so
   choosing a limit and a silent-refusal/steal rule changes current polyphony behavior. The Organic
   report records the same missing decision.

Monstro's inline-storage fix does not transfer: `MonstroSynth` is self-contained, owns no
sub-objects, and fits in 512 B. TripleOscillator's state is self-referential and 688 B.

## Verification record

No product or test source was changed, so there is no RED/GREEN product proof and no revert-fix
proof to claim (matching the Organic NEEDS-CONTEXT report). All build/test/gate logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-495.r1/logs`. Build and gate commands
ran from the worktree root; `ctest` ran from `build/tests`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Affected-family build | `cmake --build build --target PluginPortsMigrationTest` | 0 | `build-3-restore.log` |
| Affected family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | 44 passed, 1 failed, 2 skipped; the only failure is `nonRealtimeLadspaEffectsAreInvalid()` — the external SWH Decimator LADSPA fixture is absent on this box and is unrelated to TripleOscillator. `test-2-baseline.log` |
| Allocation probe (temporary) | `cmake --build build --target PluginPortsMigrationTest`; `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0; 8 | `PROBE tripleoscillator allocations=7 deallocations=7` (`test-1-probe.log`); probe reverted |
| Size probe | plugin-flag compile of `/tmp/opencode/szprobe.cpp` | 0 | 112 B/osc, 688 B total, storage 512 B |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `gate-1-complexity.log` — pre-existing: fails on `main`-tree files, not on this lane (see below) |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `gate-2-file-length.log` — pre-existing (see below) |
| Complexity, same HEAD, sibling worktree | `git rev-parse HEAD`=`ded355440871a86cc907cdc2d6465f2d3d597a6b`; `bash tests/complexity-gate.sh --check` | 1 | `FAIL: complexity ratchet regressed (check mode: baseline not written)` |
| File-length, same HEAD, sibling worktree | `bash tests/file-length-gate.sh --check` | 1 | `FAIL: file-length ratchet regressed (check mode: baseline not written)` |

### Ratchet state is pre-existing at this HEAD

Both ratchets are red before any change in this lane (`git status` was clean). The
`tests/complexity-baseline.tsv` (66 entries) and `tests/file-length-baseline.tsv` (5 entries) were
last updated at commits older than this branch tip; the measured set now has 68 over-target
functions and 19 files over 500 code lines. The identical failures reproduce in a sibling worktree
checked out at the same commit `ded355440871a86cc907cdc2d6465f2d3d597a6b`
(`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-107-port`),
so they are a branch-wide manifest/baseline mismatch, not caused by this lane, which changes no
product or test source. Per the gate contract these baselines must not be re-anchored to hide
regressions, so none was touched.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-495.md` (this report only)
