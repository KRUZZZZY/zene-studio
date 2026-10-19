# Cluster new-p12-151-port

Verdict: **FIXED** (port of the accepted fix `6ff3e5e1b` from `bugs/qwen-new-p12-151`)

## What the port had to reconcile

The accepted fix on `bugs/qwen-new-p12-151` ("fix: avoid NES audio-thread note-state
allocation", commit `6ff3e5e1b`) could not be merged into the newest line (`050/m0-train-5`)
because of a merge conflict in `include/NotePlayHandle.h`: that commit added its own
`m_pluginDataStorage` member to the note handle.

On the newest line the same facility already exists, added by the accepted sibling fix
`db1a301fd` ("fix(monstro): remove note-start allocation", cluster `new-p9-295`):

- `include/NotePlayHandle.h:77` — `static constexpr std::size_t PluginDataStorageSize = 512;`
- `include/NotePlayHandle.h:80` — `void* pluginDataStorage() noexcept`
- `include/NotePlayHandle.h:393` — `alignas(std::max_align_t) std::byte m_pluginDataStorage[PluginDataStorageSize];`

So this port changes **no header at all**: the NES plugin reuses the storage the newest line
already provides, which is precisely what makes the port conflict-free. `include/NotePlayHandle.h`
is therefore not part of this commit.

## The bug

`plugins/Nes/Nes.cpp` created the per-note NES state with `new` inside
`NesInstrument::playNoteImpl()`, the function the audio render path calls for every note:

```cpp
if (!n->m_pluginData)
{
    auto nes = new NesObject(this, Engine::audioEngine()->outputSampleRate(), n);
    n->m_pluginData = nes;
}
```

The first note of every NES voice therefore entered the heap allocator from the render path,
where an allocation can stall the audio thread.

## The fix

- `plugins/Nes/Nes.cpp:572-576` — the note state is placement-constructed in the note handle's
  inline storage with `std::construct_at(n->pluginDataStorage(), ...)`. Two `static_assert`s
  pin the contract: `sizeof(NesObject) <= NotePlayHandle::PluginDataStorageSize` and
  `alignof(NesObject) <= alignof(std::max_align_t)`.
- `plugins/Nes/Nes.cpp:589-593` — `NesInstrument::deleteNotePluginData()` destroys the state in
  place with `std::destroy_at()` and clears `m_pluginData`, replacing the `delete` of a heap
  object. The null check keeps the old `delete nullptr` no-op semantics.
- `plugins/Nes/Nes.cpp:27-28` — `<cstddef>` and `<memory>` are included for
  `std::max_align_t`, `std::construct_at` and `std::destroy_at`.

No behaviour changes: the same state is created, with the same arguments, at the same point of
the render path, and it is destroyed at the same point of the note's life. The only difference
is *where* the bytes come from. `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()`
renders the NES instrument against its frozen reference and reports the identical digest
(`sha256=b46e3fc15a452a87bc22f1368248d8d2ce970b95ec269a0c59e8047edc1987fe` before and after).

## The regression

`tests/src/core/NotePlaybackTest.cpp:376` — `nesNoteStartDoesNotAllocate()` loads the real NES
module, acquires a pooled note handle whose `m_pluginData` is null, and counts heap allocations
across `Instrument::playNote()`. The test executable overrides the global `operator new`
(`tests/src/core/NotePlaybackTest.cpp:278-293`), so an allocation inside the loaded plugin module
is observed. The case asserts zero allocations.

RED (original code): 1 allocation. GREEN (fixed code): 0 allocations.

## Verification evidence

All log files are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-151-port.r1/logs`.

| Stage | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target nes NotePlaybackTest -j12 > .../0001-red-build.log 2>&1` (worktree) | 0 | Built the plugin and the regression target. |
| RED run, first attempt | `ctest -R '^NotePlaybackTest$' --output-on-failure > .../0002-red-test.log 2>&1` (`build/tests`) | 8 | 22 passed, 2 failed. `nesNoteStartDoesNotAllocate` failed with **actual allocations = 1**; the other failure, `monstroNoteStartDoesNotAllocate`, was an environment gap (the monstro module was not built in this build tree, so `loadInstrument` returned the dummy instrument). |
| Build the missing modules | `cmake --build build --target monstro tripleoscillator -j12 > .../0003-build-extra-plugins.log 2>&1` (worktree) | 0 | `libmonstro.so` and `libtripleoscillator.so` are now in the test plugin directory. |
| RED run, clean | `ctest -R '^NotePlaybackTest$' --output-on-failure > .../0004-red-test2.log 2>&1` (`build/tests`) | 8 | 23 passed, 1 failed. `nesNoteStartDoesNotAllocate`: **actual allocations = 1**, expected 0. |
| GREEN build | `cmake --build build --target nes NotePlaybackTest -j12 > .../0005-green-build.log 2>&1` (worktree) | 0 | Rebuilt the plugin and the test. |
| GREEN run | `ctest -R '^NotePlaybackTest$' --output-on-failure > .../0006-green-test.log 2>&1` (`build/tests`) | 0 | 1/1 ctest passed. |
| GREEN case count | `ctest -R '^NotePlaybackTest$' -V > .../0007-green-verbose.log 2>&1` (`build/tests`) | 0 | 1 registered ctest, **24 Qt cases passed, 0 failed, 0 skipped**. |
| Save the production patch | `git diff -- plugins/Nes/Nes.cpp > .../fix.patch` (worktree) | 0 | 43 lines, product file only. |
| Reverse the production patch | `git apply -R .../fix.patch` (worktree) | 0 | Reverted only `plugins/Nes/Nes.cpp`. |
| Rebuild without the fix | `cmake --build build --target nes NotePlaybackTest -j12 > .../0008-proof-red-build.log 2>&1` (worktree) | 0 | Rebuilt the affected targets. |
| Inverse RED | `ctest -R '^NotePlaybackTest$' --output-on-failure > .../0009-proof-red-test.log 2>&1` (`build/tests`) | 8 | 23 passed, 1 failed: **actual allocations = 1**. The test fails without the fix. |
| Restore the production patch | `git apply .../fix.patch` (worktree) | 0 | Restored the exact saved patch. |
| Rebuild with the fix | `cmake --build build --target nes NotePlaybackTest -j12 > .../0010-proof-green-build.log 2>&1` (worktree) | 0 | Rebuilt the affected targets. |
| Inverse GREEN | `ctest -R '^NotePlaybackTest$' --output-on-failure > .../0011-proof-green-test.log 2>&1` (`build/tests`) | 0 | 1/1 ctest passed. |
| Second proof round, both families | `git apply -R .../fix.patch` → `cmake --build build --target nes PluginPortsMigrationTest NotePlaybackTest -j12` → `ctest -R '^(NotePlaybackTest\|PluginPortsMigrationTest)$' --output-on-failure` (`build/tests`), logs `0014`/`0015` | 8 | `nesNoteStartDoesNotAllocate` failed with 1 allocation; `PluginPortsMigrationTest` failed only on its pre-existing LADSPA fixture gap. |
| Second proof round, restore | `git apply .../fix.patch` → rebuild → `ctest -R '^(NotePlaybackTest\|PluginPortsMigrationTest)$' --output-on-failure` (`build/tests`), logs `0016`/`0017` | 8 | `NotePlaybackTest` **passed**; `PluginPortsMigrationTest` still failed only on the pre-existing LADSPA fixture gap. |
| Behaviour-preservation family | `cmake --build build --target PluginPortsMigrationTest -j12 > .../0012-build-pluginports.log 2>&1` (worktree) | 0 | Built the family. |
| Behaviour-preservation run | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../0013-test-pluginports.log 2>&1` (`build/tests`) | 8 | 44 passed, 1 failed, 2 skipped. The only failure is `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded") — a pre-existing environment gap, identical with and without this fix. The NES reference render keeps the digest `sha256=b46e3fc15a452a87bc22f1368248d8d2ce970b95ec269a0c59e8047edc1987fe`, and `nesNoiseFrequencyControlsRetuneHeldNotes` and `nesCoarseDetuneRetunesHeldNote` pass. |
| Host build | `cmake --build build --target zene -j12 > .../0022-build-zene.log 2>&1` (worktree) | 0 | The application target is unaffected and still builds. |
| Final build | `cmake --build build --target nes NotePlaybackTest PluginPortsMigrationTest -j12 > .../0023-final-build2.log 2>&1` (worktree) | 0 | Rebuilt every affected target. |
| Final GREEN | `ctest -R '^NotePlaybackTest$' --output-on-failure -V > .../0024-final-test-noteplayback.log 2>&1` (`build/tests`) | 0 | 1 registered ctest, 24 Qt cases passed, 0 failed, 0 skipped. |
| Complexity gate (after the change) | `bash tests/complexity-gate.sh --check > .../0025-gate-complexity-final.log 2>&1` (worktree) | 1 | Red **before and after** this change — see the gate note below. |
| File-length gate (after the change) | `bash tests/file-length-gate.sh --check > .../0026-gate-file-length-final.log 2>&1` (worktree) | 1 | Red **before and after** this change — see the gate note below. |
| Complexity gate (pristine base) | `bash tests/complexity-gate.sh --check > .../0020-gate-complexity-base.log 2>&1` (worktree) | 1 | Measured with this commit's changes reversed: the regression list is **byte-identical** to the list after the change. |
| File-length gate (pristine base) | `bash tests/file-length-gate.sh --check > .../0021-gate-file-length-base.log 2>&1` (worktree) | 1 | Measured with this commit's changes reversed: the same 14 files are reported, `tests/src/core/NotePlaybackTest.cpp (624)` before and `(646)` after — that file was already over the limit and already reported before this change. |

### Gate note (honest status)

Both ratchet gates are **red on the pristine base commit of this branch** (`ded355440`), before
any edit of this port:

- `tests/complexity-gate.sh --check` exits 1 with six pre-existing "new function over target"
  entries (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`,
  `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`).
  The list after this change is identical (log diff in `0025` vs `0020`): this change adds no
  function over the CCN target.
- `tests/file-length-gate.sh --check` exits 1 with fourteen fork-NEW files over 500 lines that
  are missing from the baseline, `tests/src/core/NotePlaybackTest.cpp` among them. The verdict
  (exit 1, same fourteen files) is the same before and after this change; the only difference is
  the reported length of that one file, 624 → 646, which was already over the limit and already
  reported before this change. The test lives beside its sibling regression
  (`monstroNoteStartDoesNotAllocate`, accepted fix `db1a301fd`) in the same file, which is where
  the accepted NES fix `6ff3e5e1b` also placed its regression.

No gate limit and no baseline was re-anchored, and no gate script was touched: the only gate
state change would have been to move an already-failing file further from its limit, which a
separate file would have avoided at the cost of a new test executable and a second inherited
file change. The pre-existing gate failures are unrelated to this bug and are left for the
branch owner.

## Files committed

- `plugins/Nes/Nes.cpp` — the note state is constructed in the note handle's inline storage and
  destroyed in place; no heap allocation on the render path.
- `tests/src/core/NotePlaybackTest.cpp` — the regression `nesNoteStartDoesNotAllocate`.
- `tests/upstream-modifications.txt` — the inherited file `plugins/Nes/Nes.cpp` is declared with
  its reason.
- `docs/qwen-fixes-2026-10/cluster-new-p12-151-port.md` — this report.
- `tests/fork-sources.txt` — this report registered as a fork-NEW file.
