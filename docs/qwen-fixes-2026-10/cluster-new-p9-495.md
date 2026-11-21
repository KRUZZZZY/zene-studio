# Cluster new-p9-495: TripleOscillator allocates note state on the audio path

**Verdict: FIXED** — the 2026-10-05 finding "TripleOscillator allocates and frees memory in its
note-processing path". This supersedes the round-1 NEEDS-CONTEXT (`95c2ce165`), which Luna's QA
review (`new-p9-495.r1.md`) disputed. Its "not local" argument is withdrawn: the note state now
lives in the note's own inline storage, which needs no pool, capacity limit or re-arm API.

Measured before the fix: 7 `operator new` and 7 `operator delete` calls on the audio thread
across the first note period and its cleanup (`allocations=7, deallocations=7`).
Measured after the fix: 0 and 0.

## The finding

> First processing a note allocates six `Oscillator` objects and plugin data; note cleanup
> deletes them. This path runs during audio processing and can stall the audio thread.

## Fix

- `plugins/TripleOscillator/TripleOscillator.cpp` (anonymous namespace): `TripleOscillatorNoteState`
  holds the left and right chains as `std::array<std::optional<Oscillator>, NUM_OF_OSCILLATORS>`.
  `playNoteImpl` places it with `std::construct_at` into `_n->pluginDataStorage()` and emplaces each
  oscillator in place. `deleteNotePluginData` runs `std::destroy_at` on the state. Nothing is
  `new`ed or `delete`d.
- Sub-oscillator links are non-owning: each `emplace` passes `false` for `ownsSubOsc`.
- `include/Oscillator.h`, `src/core/Oscillator.cpp`: new constructor parameter
  `bool ownsSubOsc = true`, stored in `m_ownsSubOsc`. The destructor deletes `m_subOsc` only when the
  flag is set. The default keeps every existing caller unchanged (Organic and the rest still own
  their sub-oscillator).
- `include/NotePlayHandle.h:77`: `PluginDataStorageSize` 512 -> 720. A `static_assert` in
  `TripleOscillator.cpp` checks size and alignment against the storage.
- `plugins/TripleOscillator/TripleOscillator.h`: removed the `oscPtr` wrapper and the
  `class Oscillator;` forward declaration that only it used.
- Unchanged on purpose: `tests/reference/TripleOscillator/*`, the frozen pre-migration copy that the
  migration test compares against.

### Measured sizes

Compiled with the TripleOscillator compile flags from `build/compile_commands.json`
(`-fsyntax-only`, probe in `/tmp/szprobe-new-p9-495.cpp`, diagnostics show the values):

| Type | Size |
| --- | ---: |
| `sizeof(Oscillator)` (the new bool sits in existing padding) | 112 B |
| `sizeof(TripleOscillatorNoteState)` (6 x `optional<Oscillator>` at 120 B) | 720 B |
| `NotePlayHandle::PluginDataStorageSize` | 720 B |

### Deviation from the brief

The brief named 688 B (6 x 112 + 16) and raw `construct_at` for each oscillator. The fix uses
`std::optional` per oscillator instead. `optional` adds an engaged flag (8 B each after alignment),
so the state is 720 B, not 688 B. Each oscillator is still built in place with no heap, and the state
itself is `construct_at`'d into the handle. Output is identical (see the checksum below).

## Why the in-handle route works

The round-1 blockers, checked one by one:

1. **Size.** Fixed by `PluginDataStorageSize` = 720 (above).
2. **Ownership.** Fixed by the opt-in `ownsSubOsc = false`. Each owner no longer deletes a sub that
   lives inside the same buffer. The default is unchanged.
3. **Capacity contract.** Does not apply. Note state is constructed per note inside that note's
   handle. `NotePlayHandleManager` pools whole handles (`src/core/NotePlayHandle.cpp:718-766`), each
   with its own storage, so no separate pool or exhaustion policy is needed.
4. **Re-arm.** Does not apply. Each note builds its chain fresh; nothing is re-armed.

Monstro uses the same pattern (`plugins/Monstro/Monstro.cpp:1089-1092,1113`).

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp`, `migratedPluginsPreserveBehaviour`: the existing
FreeBoy realtime check now also covers `tripleoscillator`. The harness
(`tests/src/plugins/PluginPortsHarness.h:1032-1061`) counts allocations on the render thread around
each `playNote` period and around `deleteNotePluginData`. The check asserts zero of each. The same
test still compares the rendered checksum against the reference, so output is checked too.

Notes on the test:

- The brief named `ctest -R '^TripleOscillator$'` and an Organic family. Neither is registered in
  this build (593 tests, none matching). The reference copies are built only as migration MODULEs.
  The registered test that exercises TripleOscillator is `PluginPortsMigrationTest`, which also
  contains `organicDistortionLeavesPreOffsetFramesUnchanged`; it passed.
- `QVERIFY2` returns from `migratedPluginsPreserveBehaviour` on failure, so in RED the later tests
  that read the migrated set also fail (`comparisonIsSensitive`, `slice5`-`slice8`). That cascade is
  how the existing test is built, not a new failure.

## Correction to the Organic citation

The round-1 report cited `plugins/Organic/Organic.cpp:245-274`. Current lines: `new oscPtr` at 233,
`new Oscillator` at 245, 253, 264, 273, deletes at 313-318 (`rg` on this tree). Organic keeps the
owning default and is not changed by this fix.

## Verification record

Every line below was run in this worktree. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-495.r2/logs`. `ctest` ran from
`build/tests`. Exit codes are unpiped.

| # | Step | Exact command | EXIT | Result / log |
| --- | --- | --- | ---: | --- |
| 1 | Build with the regression edit, first try | `cmake --build build --target PluginPortsMigrationTest -j2 > build-1-red.log 2>&1` | 2 | ccache could not create its temp file under `/run/user/1000/ccache-tmp` (sandbox denied). Not a code error. |
| 2 | Same build, ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-495 cmake --build build --target PluginPortsMigrationTest -j2 > build-2-red.log 2>&1` | 0 | Built on the parent product code (`95c2ce165`) |
| **RED** | Migration family, product code = `95c2ce165` | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-1-red.log 2>&1` | **8** | `FAIL! : migratedPluginsPreserveBehaviour() 'realtimeCounts.allocations == 0 && realtimeCounts.deallocations == 0' returned FALSE. (tripleoscillator render allocations=7, deallocations=7)`; `Totals: 38 passed, 7 failed, 2 skipped` (the 7 include cascades) |
| 3 | Fix build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-495 cmake --build build --target zene PluginPortsMigrationTest -j2 > build-3-fix.log 2>&1` | 0 | Rebuilt every includer of `NotePlayHandle.h` / `Oscillator.h` |
| **GREEN** | Migration family, with fix | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-2-green.log 2>&1` | **8** | `Totals: 44 passed, 1 failed, 2 skipped`; `migratedPluginsPreserveBehaviour` passes; `tripleoscillator` sha256 `7b06600c...` migrated = reference. The one failure is `nonRealtimeLadspaEffectsAreInvalid` (SWH Decimator LADSPA fixture not installed on this box; not TripleOscillator) |
| 4 | Reverse-apply proof: save product patch | `git diff -- include/Oscillator.h src/core/Oscillator.cpp include/NotePlayHandle.h plugins/TripleOscillator/TripleOscillator.h plugins/TripleOscillator/TripleOscillator.cpp > fix.patch` (220 lines) | 0 | |
| 5 | Revert product code | `git apply -R fix.patch` | 0 | `git status`: only the test file modified |
| 6 | Rebuild on reverted code | `cmake --build build --target zene PluginPortsMigrationTest -j2` | 0 | `build-4-reverse.log` |
| 7 | Run on reverted code | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-3-reverse-red.log 2>&1` | **8** | Same failure: `(tripleoscillator render allocations=7, deallocations=7)`; `Totals: 38 passed, 7 failed, 2 skipped`. The test catches the old code. |
| 8 | Re-apply fix | `git apply fix.patch` | 0 | |
| 9 | Rebuild with fix | `cmake --build build --target zene PluginPortsMigrationTest -j2` | 0 | `build-5-regreen.log` |
| 10 | Run with fix | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > test-4-regreen.log 2>&1` | **8** | `Totals: 44 passed, 1 failed, 2 skipped` (same as GREEN) |
| 11 | Complexity ratchet | `bash tests/complexity-gate.sh --check > gate-3-complexity-final.log 2>&1` | 1 | 6 `REGRESSION` lines, all in files this fix does not touch |
| 12 | File-length ratchet | `bash tests/file-length-gate.sh --check > gate-4-filelength-final.log 2>&1` | 1 | 14 `REGRESSION` lines, all in files this fix does not touch |
| 13 | Upstream ledger gate (run after the fix commit `bede58057`) | `bash tests/no-upstream-regression-gate.sh > gate-5-upstream-ledger.log 2>&1` | 0 | `PASS: every change to upstream-inherited code since 01148947ea4d... is declared` |

Exit 8 is the same before and after the fix, because the unrelated LADSPA-fixture failure keeps the
binary's exit at 8. The RED/GREEN signal is the `FAIL!` line and the `Totals` line.

Family count: `ctest -R '^PluginPortsMigrationTest$'` runs one ctest entry with 47 test functions
(44 pass, 1 fail, 2 skip). `ctest -N` lists no `TripleOscillator` or `Organic` entry in this build.

### Gate results

- `bash tests/complexity-gate.sh --check` -> EXIT=1. Regressions reported in `XyPadPairTest.cpp`,
  `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`,
  `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`. None are in the files changed here.
  `TripleOscillator.cpp` has no `REGRESSION` line and no entry in `complexity-baseline.tsv`, so its
  functions are within the CCN target (the CCN values themselves were not printed).
- `bash tests/file-length-gate.sh --check` -> EXIT=1. Regressions reported in 14 test and source
  files not touched here (for example `CollabTransportTest.cpp`, `TakeLane.cpp`). None are in the
  files changed here.
- Both gates fail with the same counts the QA review recorded at the parent commit (6 and 14). The
  baselines were not edited or re-anchored.

## Files committed

- `include/Oscillator.h`
- `src/core/Oscillator.cpp`
- `include/NotePlayHandle.h`
- `plugins/TripleOscillator/TripleOscillator.h`
- `plugins/TripleOscillator/TripleOscillator.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt` (5 ledger entries with reasons; the changed upstream files were
  not all listed before)
- `docs/qwen-fixes-2026-10/cluster-new-p9-495.md` (this report)

No new files were created, so `tests/fork-sources.txt` needed no entry.

## Not verified and residual risk

- Only `zene` and `PluginPortsMigrationTest` were rebuilt and run. Other test binaries that include
  `NotePlayHandle.h` or `Oscillator.h` were not rebuilt in this pass, so they have not been run
  against the change.
- If a note holds the last reference to a sample buffer, `m_userWave`'s `shared_ptr` release can
  free it on the audio thread when the note is destroyed. That exists today and is not changed here.
  Not measured.
- `NotePlayHandleManager::extend` uses `malloc` when the handle pool runs out. Whether `acquire` can
  reach that on the audio thread was not checked and is out of scope.
