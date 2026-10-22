# Cluster new-p9-211-port-port — FIXED

**Verdict: FIXED.** The bug is real on the newest line and is fixed here.

Port job: the fix was accepted as `bugs/qwen-new-p9-211-port` (commits `b231bc242`,
`a2b3d8c5e`, `4d0082a58`) but could not go on the newest train because of a merge
conflict in `tests/src/plugins/PluginPortsMigrationTest.cpp` (the newest line moved
that file's `realtimeChecked` list on, and it also moved `tests/rt-safety-scope.txt`).
The fix is therefore re-applied by hand on this branch - no merge, no cherry-pick, no
rebase - and the regression is re-merged onto the newer form of the test.

## The finding

> Kicker allocates per-note state in the audio processing path -
> `plugins/Kicker/Kicker.cpp:165`: the first `playNoteImpl()` call allocates a
> `SweepOsc` with `new`; `deleteNotePluginData()` also deletes that state.

**REAL.** Evidence on the newest line (before this change), `plugins/Kicker/Kicker.cpp`:

- `plugins/Kicker/Kicker.cpp:167` - `nph->m_pluginData = new SweepOsc(` inside
  `KickerInstrument::playNoteImpl`, i.e. on the audio render path (the note's first
  period).
- `plugins/Kicker/Kicker.cpp:212` - `delete static_cast<SweepOsc *>( _n->m_pluginData );`
  inside `KickerInstrument::deleteNotePluginData`, which the engine calls from
  `src/core/NotePlayHandle.cpp:162` (`~NotePlayHandle`), i.e. on the thread that
  releases the note handle.

Both were the only heap traffic of the Kicker's per-note state, and the note state is
the only per-note state the instrument has.

## The fix

`plugins/Kicker/Kicker.cpp` - the per-note `SweepOsc` is now built in the note
handle's own preallocated storage and destroyed in place, the pattern the tree
already uses for the same job in BitInvader (`plugins/BitInvader/BitInvader.cpp:258`),
Monstro, Nes, Watsyn, TripleOscillator and AudioFileProcessor:

- `plugins/Kicker/Kicker.cpp:170-178` - `std::construct_at(static_cast<SweepOsc*>(nph->pluginDataStorage()), ...)`
  guarded by `static_assert(sizeof(SweepOsc) <= NotePlayHandle::PluginDataStorageSize)`
  and `static_assert(alignof(SweepOsc) <= alignof(std::max_align_t))`.
- `plugins/Kicker/Kicker.cpp:221-227` - `std::destroy_at(static_cast<SweepOsc*>(_n->m_pluginData))`
  (null-guarded), instead of `delete`.

The storage is `NotePlayHandle::m_pluginDataStorage`
(`include/NotePlayHandle.h:82,85,495`) - a 1024-byte, `max_align_t`-aligned member of
the note handle itself, so the per-note state lives and dies with the note and the
audio thread neither allocates nor frees. No saved state, project data, parameter or
default changed: the oscillator is constructed from exactly the same values, so the
rendered audio is bit-identical (proved below against the pre-migration reference
render).

## The regressions (two, both deterministic)

1. **Runtime allocation counter** -
   `tests/src/plugins/PluginPortsMigrationTest.cpp:1957-1958`: the
   `migratedPluginsPreserveBehaviour` render loop now also counts the Kicker
   instrument's render-thread allocations (`realtimeChecked` gains
   `m.plugin == std::string{"kicker"}`). The counter is the test binary's own global
   `operator new`/`operator delete` replacement
   (`tests/src/core/AllocationProbe.cpp`), armed around every render period and around
   the note cleanup. The assertion is `allocations == 0 && deallocations == 0`.
   The same test still compares the migrated Kicker render with the pre-migration
   reference render sample-exactly, so "no allocation" cannot be bought with a
   different sound.
2. **Source-level rt-safety sweep** - `tests/rt-safety-scope.txt` (last two lines)
   declares `KickerInstrument::playNoteImpl` and
   `KickerInstrument::deleteNotePluginData` as audio-thread paths, so the whole-tree
   sweep (`tests/rt-safety-sweep.py`, ctest `RtSafetySweep`) measures them for
   allocating, locking, growing and blocking constructs. It is a static check: no
   timing, no sleeps, no races.

## Evidence

Working directory: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-211-port-port`.
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-211-port-port.r1/logs`.

### RED (before the product fix)

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build the regression | `cmake --build build --target PluginPortsMigrationTest -j$(nproc) > .../logs/build-red-1.log 2>&1` | 0 | Test target built. |
| RED (runtime counter) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-red-1.log 2>&1` (from `build/tests`) | 8 | `FAIL! : PluginPortsMigrationTest::migratedPluginsPreserveBehaviour() 'realtimeCounts.allocations == 0 && realtimeCounts.deallocations == 0' returned FALSE. (kicker render allocations=1, deallocations=1)` |
| RED (source sweep) | `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/rt-sweep-red-1.log 2>&1` | 1 | `FAIL: NEW rt-safety hit: plugins/Kicker/Kicker.cpp:KickerInstrument::playNoteImpl:alloc-new [allocation] - 1 occurrence(s), first at plugins/Kicker/Kicker.cpp:167: nph->m_pluginData = new SweepOsc(` |

### GREEN (with the product fix)

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build the fix | `cmake --build build --target zene PluginPortsMigrationTest -j$(nproc) > .../logs/build-green-1.log 2>&1` | 0 | `zene`, `libkicker.so` and the test target rebuilt. |
| GREEN (runtime counter) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-green-1.log 2>&1` (from `build/tests`) | 8 | `Totals: 72 passed, 1 failed, 2 skipped`. The Kicker assertion passes; the one failure is the pre-existing, environment-bound LADSPA one (see below). |
| GREEN (source sweep) | `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/rt-sweep-prove-green.log 2>&1` | 0 | `PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count.` |
| Affected-family run | `ctest -R 'PluginPortsMigrationTest|OpulenZ|RtSafety' --output-on-failure > .../logs/test-family-2.log 2>&1` (from `build/tests`) | 8 | 4 tests run: `PluginPortsMigrationTest`, `OpulenZ`, `RtSafetySweep`, `RtSafetySelfTest` - 3 passed, 1 failed (`PluginPortsMigrationTest`, the LADSPA fixture assertion only). |

### The test catches the bug (product patch reversed and restored)

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Product-only patch | `git diff -- plugins/Kicker/Kicker.cpp > .../logs/fix.patch` | 0 | 40 lines, product file only (no test file). |
| Inverse patch applied | `git apply -R .../logs/fix.patch` | 0 | Product fix removed; the test files stayed changed. |
| Rebuild without the fix | `cmake --build build --target zene PluginPortsMigrationTest -j$(nproc) > .../logs/build-prove-red.log 2>&1` | 0 | Rebuilt. |
| RED without the fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-prove-red.log 2>&1` (from `build/tests`) | 8 | `FAIL! ... (kicker render allocations=1, deallocations=1)` - the same assertion as above. |
| RED without the fix (sweep) | `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/rt-sweep-prove-red.log 2>&1` | 1 | `NEW rt-safety hit: plugins/Kicker/Kicker.cpp:KickerInstrument::playNoteImpl:alloc-new ... first at plugins/Kicker/Kicker.cpp:167` |
| Product fix restored | `git apply .../logs/fix.patch` | 0 | The exact product patch restored. |
| Rebuild restored | `cmake --build build --target zene PluginPortsMigrationTest -j$(nproc) > .../logs/build-prove-green.log 2>&1` | 0 | Rebuilt. |
| GREEN restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-prove-green.log 2>&1` (from `build/tests`) | 8 | `Totals: 72 passed, 1 failed, 2 skipped` - the Kicker assertion passes. |
| GREEN restored (sweep) | `python3 tests/rt-safety-sweep.py --tree . --scope tests/rt-safety-scope.txt --allowlist tests/rt-safety-allowlist.txt --check > .../logs/rt-sweep-prove-green.log 2>&1` | 0 | PASS. |

### The one failing assertion in the family run is pre-existing and environmental

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` fails with
`"SWH Decimator test fixture was not loaded"`: it looks up the LADSPA plug-in
`decimator_1202.so`, and this machine has no such plug-in
(`/usr/lib/ladspa` holds only `amp.so`, `delay.so`, `filter.so`, `noise.so`,
`sine.so`). It failed the same way in the RED run, before any product change
(`.../logs/test-red-1.log`), it is a different test function from the Kicker
regression (so it is not a cascade of it), and it does not touch Kicker. It is
recorded here rather than hidden.

### Gates

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Gates before the manifest re-derivation | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check-1.log 2>&1` | 1 | `FAIL fork-sources exit 1`, `FAIL all-sources-reproduce exit 1`; the other six rows PASS. The failing diff is **exclusively** `plugins/SfzPlayer/sfizz/**` paths that the manifest's own recipe excludes (the manifest header and `tests/upstream-modifications.txt` line 2805 - "Q050-484: the vendored sfizz tree (plugins/SfzPlayer/sfizz) is excluded from the whole-tree scope" - both say the vendored tree is out of scope, but the list still carried 1148 of its paths). |
| Base proof of that condition | `git show dbb15dded:tests/all-sources.txt \| grep -vE '^[[:space:]]*(#\|$)' \| diff - <recipe output> > .../logs/base-all-sources-stale.txt` | 1 | The base commit's manifest already carries exactly 1148 entries the recipe does not derive, and **all 1148 are under `plugins/SfzPlayer/sfizz/`** (0 others) - the condition is on the newest line, not caused by this fix. |
| Manifest re-derivation (the gate's own documented remedy) | `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \| grep -vE '^(src/3rdparty/\|tests/reference/\|plugins/NeuralAmp/(rtneural\|nam\|tests)/\|plugins/RnnoiseDenoiser/rnnoise/\|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } \| LC_ALL=C sort -u` written over the entry list of `tests/all-sources.txt` (header kept, list byte-exact, nothing hand-edited) | 0 | `bash tests/all-sources-reproduce.sh` -> `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` (`.../logs/allsources-reproduce-after.log`); `bash tests/fork-sources-gate.sh` -> `PASS: every tracked source in scope is registered (1968 fork-NEW, 1214 inherited, 48 tooling).` (`.../logs/fork-sources-after.log`) |
| Gates after the manifest re-derivation | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check-2.log 2>&1` | 0 | `RESULT: PASS` - all eight rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). |

The manifest re-derivation is a tree-level list that the train's own snapshot job
regenerates; it is included here because the two whole-tree gates are red on the
newest line without it and the fix is the manifest's own documented recipe
("run the 'Regenerate with' command from the manifest's header and write its output
over the entry list"). It changes no product code, no test expectation and no
product behaviour: it only stops the whole-tree scope from claiming 1148 vendored
third-party files that its recipe excludes.

## Files committed

- `plugins/Kicker/Kicker.cpp` - the fix.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` - the runtime allocation regression.
- `tests/rt-safety-scope.txt` - the two declared Kicker audio-thread paths.
- `tests/upstream-modifications.txt` - the registration of the three inherited files
  above and of `tests/all-sources.txt`.
- `tests/all-sources.txt` - the re-derived whole-tree source manifest.
- `docs/qwen-fixes-2026-10/cluster-new-p9-211-port-port.md` - this report.

No new file was added, so `tests/fork-sources.txt` needed no new entry.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   Reverse-applying the product-only patch (`git diff -- plugins/Kicker/Kicker.cpp`),
   rebuilding and rerunning gives `ctest -R '^PluginPortsMigrationTest$'` exit **8**
   with `kicker render allocations=1, deallocations=1` and the rt-safety sweep exit
   **1** with the `alloc-new` hit at `plugins/Kicker/Kicker.cpp:167`; restoring the
   patch and rebuilding gives the Kicker assertion passing and the sweep exit **0**
   (table "The test catches the bug" above, logs `test-prove-red.log`,
   `rt-sweep-prove-red.log`, `test-prove-green.log`, `rt-sweep-prove-green.log`).
2. **The test is deterministic.** No wall-clock timing, no sleeps, no threads that
   race, no randomness: the runtime regression counts the test binary's own
   `operator new`/`operator delete` replacements (`tests/src/core/AllocationProbe.cpp`)
   around a fixed number of render periods on a fresh thread, and the second
   regression is a static source scan of two named functions. Both produce the same
   result on every run.
3. **The fix is at the cause and complete.** The per-note state of the Kicker is
   created in exactly one place and destroyed in exactly one place, and both were
   changed: `KickerInstrument::playNoteImpl` (`plugins/Kicker/Kicker.cpp:161`) and
   `KickerInstrument::deleteNotePluginData` (`plugins/Kicker/Kicker.cpp:221`). The
   engine's only caller of the cleanup is `~NotePlayHandle`
   (`src/core/NotePlayHandle.cpp:160-163`), which calls it exactly once per handle,
   through `InstrumentTrack::deleteNotePluginData` (`src/tracks/InstrumentTrack.cpp:800-805`);
   the pointer is not used after the destroy, and the null guard keeps a repeated
   call harmless. `nph->m_pluginData` is read and written nowhere else in the tree
   (`include/NotePlayHandle.h:56,85,495` and the plugins that own their own state).
   `KickerOsc` is used by no other plugin, so no sibling needed the same change; the
   plugins that keep their own per-note state (BitInvader, Monstro, Nes, Watsyn,
   TripleOscillator, AudioFileProcessor) already use the same in-place pattern and
   were left untouched.
4. **No collateral damage.** The oscillator is constructed from the same values in the
   same order as before, so the render is unchanged - the migration test still
   compares the migrated Kicker render with the pre-migration reference render
   sample-exactly and passes. No project file, saved state, default or parameter
   changed, so old projects load and render exactly as before. The storage bound is
   the engine's existing `NotePlayHandle::PluginDataStorageSize` (1024 bytes) and is
   checked at compile time by `static_assert`, so no other caller can exceed it;
   `PluginDataStorageSize` itself was not changed.
5. **Realtime, undo and commands.** Nothing was added to the audio thread: the fix
   *removes* a heap allocation and a heap free from it (the note state is now
   constructed and destroyed in the note handle's own storage, so no `new`, no
   `delete`, no lock, no wait, no logging and no Qt call on the render path - the
   rt-safety sweep measures both Kicker functions and reports no hit). No command was
   added, so the control-command snapshot is untouched; no undo step is involved
   (the change is inside a note's lifetime, not an edit), so undo stays one honest
   step.
7. **The ALREADY-FIXED / FALSE-POSITIVE question does not apply here** - the verdict
   is FIXED, and the code that made it real is quoted with its line numbers above
   (`plugins/Kicker/Kicker.cpp:167` and `:212` before the change, `:170-178` and
   `:221-227` after it).

8. **Portability.** No Qt-6-only API, no `windows.h` macro name, no
   `std::filesystem`; `std::construct_at`/`std::destroy_at` are C++20 and the tree is
   C++20 (`target_compile_features(... cxx_std_20)`), and the same calls are already
   used by the sibling plugins listed above.
