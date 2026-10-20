FIXED

# Cluster new-p14-227: realtime allocation watch did not cover single-streamed play()

Finding (BUG 13, `tests/src/plugins/PluginPortsHarness.h`): the allocation watch in
`renderInstrumentBuffers()` ended after `inst.playNote(...)` and before the per-period
`inst.play(span)` that single-streamed instruments need, so a heap allocation made in `play()`
was never counted and the harness reported zero for that period.

## Evidence (file:line)

- Pre-fix code, `tests/src/plugins/PluginPortsHarness.h:1032-1043`: `RealtimeAllocationWatch::begin()`
  (1032), `inst.playNote(&nph, span)` (1033), `RealtimeAllocationWatch::end()` (1036), then
  `if (inst.isSingleStreamed()) { inst.play(span); }` (1040-1042). `play()` ran outside the watch.
- Post-fix code, same function: `begin()` (1033), `playNote` (1034), `if isSingleStreamed { play(span) }`
  (1036-1038), `end()` (1041). Both per-period calls are inside the watch.
- Callers of `renderInstrumentBuffers` (`PluginPortsHarness.h:1008`) go through `renderInstrumentInFreshThread`
  (`PluginPortsHarness.h:1070`). The only caller that passes a counts pointer is the migrated-instrument loop in
  `PluginPortsMigrationTest.cpp:1762-1763`, and only for `freeboy` and `tripleoscillator`. Neither is
  single-streamed, so the `play()` branch never runs for them and their counts are unchanged. The other callers
  (`PluginPortsHarness.h:1239`, `PluginPortsMigrationTest.cpp:1372, 2005, 2064, 3508`) pass no counts pointer, so
  no watch runs and behaviour is unchanged.
- Sibling watch: the note-plugin-data cleanup watch (`PluginPortsHarness.h:1056-1061`) is a separate window
  outside the per-period loop and is untouched. `handleMidiEvent` (once, before the loop) is not per-period
  and stays unwatched by design. `renderInstrumentPeriods` (`PluginPortsMigrationTest.cpp` ~406) has no watch and
  calls only `playNote`; it is not part of this finding and is unchanged.
- No product code changed. The `begin`/`end` calls only run in test code.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp`: new probe `AllocatingSingleStreamInstrument` (~line 383;
`Instrument::Flag::IsSingleStreamed`, `play(SampleFrame*)` does `std::make_unique<float[]>(64)` each call), and
slot `realtimeWatchCoversSingleStreamedPlay` (~line 3197). It renders 64 periods (`partc::Buffers`) through
`renderInstrumentBuffers(..., &counts)` and asserts exactly one counted allocation per period. Deterministic: no clock,
sleep or thread race. The count comes from the binary's global `operator new` hook, which is thread-local and
runs on the test thread. Expected count 64; the broken harness gives 0.

## Commands and exit codes (LOG FOLDER: `~/.local/share/zene-bunny-fix/qwen/runs/new-p14-227.r1/logs`)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| build (no ccache workaround) | `cmake --build build --target PluginPortsMigrationTest -j4` | 2 | build-1-red.log (ccache could not write `/run/user/1000/ccache-tmp`; environment, not code) |
| build (ccache temp in /tmp) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-227 cmake --build build --target PluginPortsMigrationTest -j4` | 0 | build-2-red.log |
| **RED** (test added, harness unfixed) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | test-1-red.log: `FAIL! realtimeWatchCoversSingleStreamedPlay() 'counts.allocations == Buffers' ... (play() allocations counted: 0, expected one per period (64))`; Totals 56 passed, 2 failed, 2 skipped |
| fix applied, rebuild both includers | `CCACHE_TEMPDIR=... cmake --build build --target PluginPortsMigrationTest PluginPortsMigrationReference -j4` | 0 | build-3-green.log |
| **GREEN** | same ctest | 8 | test-2-green.log: `PASS realtimeWatchCoversSingleStreamedPlay()`; Totals 57 passed, 1 failed (env, see below), 2 skipped |
| 4b save patch | `git diff -- tests/src/plugins/PluginPortsHarness.h > logs/fix.patch` | 0 | |
| 4b revert | `git apply -R logs/fix.patch` | 0 | |
| 4b rebuild | `CCACHE_TEMPDIR=... cmake --build build --target PluginPortsMigrationTest -j4` | 0 | build-4-reverted.log |
| 4b **must FAIL** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | test-3-reverted.log: `FAIL! realtimeWatchCoversSingleStreamedPlay() ... (play() allocations counted: 0, expected one per period (64))`; Totals 56 passed, 2 failed, 2 skipped |
| 4b re-apply | `git apply logs/fix.patch` | 0 | |
| 4b rebuild | `CCACHE_TEMPDIR=... cmake --build build --target PluginPortsMigrationTest -j4` | 0 | build-5-reapplied.log |
| 4b **must PASS** | same ctest | 8 | test-4-reapplied.log: `PASS realtimeWatchCoversSingleStreamedPlay()`; Totals 57 passed, 1 failed (env), 2 skipped |
| Reference family | `cd build/tests && ctest -R '^PluginPortsMigrationReference$'` | 0 | test-5-reference.log: "No tests were found!!!". `PluginPortsMigrationReference` is an executable the test spawns as a child process, not a ctest entry (`ctest -N` lists only `PluginPortsMigrationTest`, #496), so the family count is 1 ctest entry |
| Affected family count | `PluginPortsMigrationTest` (1 ctest entry, 60 test slots) | | 57 passed / 1 failed (env) / 2 skipped on the final run. `migratedPluginsPreserveBehaviour` PASS, the freeboy/tripleoscillator realtime checks stay 0 |
| Gate (feature_check.py) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | gate-feature-check-1.log: `PermissionError` creating `<worktree>/../zene-qwen-new-p14-227-feature-check` (outside the write-jail). Not modified. |
| Gate (fork-sources) | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources-2.log |
| Gate (all-sources-reproduce) | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce-2.log |
| Gate (no-upstream-regression) | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression-2.log |
| Gate (check-namespace) | `tests/scripted/check-namespace` | 0 | gate-check-namespace-2.log |
| Gate (no-tautology) | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology-2.log |
| Gate (doc-figures) | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures-2.log |
| Ratchet (complexity) | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity-2.log: 6 REGRESSION lines, none in `PluginPortsHarness.h` or `PluginPortsMigrationTest.cpp` (grep, no hits) |
| Ratchet (file-length) | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length-2.log: 15 REGRESSION lines, none in my files |

The two ratchets fail only on items in other files, as the brief says they do on the train. I did not change any gate or baseline.

## Environment notes

- ccache could not write to `/run/user/1000/ccache-tmp`, so the first build failed. The rebuilds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-227`.
- `nonRealtimeLadspaEffectsAreInvalid` fails at `PluginPortsMigrationTest.cpp:1123` ("SWH Decimator test fixture was not loaded"). `find build -name 'decimator*'` finds no module, so `decimator_1202.so` is not built in this tree. It fails identically in every run here (test-1-red, test-2-green, test-3-reverted, test-4-reapplied). I did not run it against a pristine HEAD binary.
- `feature_check.py` cannot create its log folder inside this write-jail, so it could not produce its PASS/FAIL table. I ran the same eight commands it lists, from the worktree, with the exit codes above.

## Registration

- `tests/upstream-modifications.txt`: two new lines, one for `tests/src/plugins/PluginPortsHarness.h` and one for `tests/src/plugins/PluginPortsMigrationTest.cpp`, both with the cluster reason. No new files were added, so `fork-sources.txt` is unchanged.

## Self-check

1. **RED/GREEN:** the regression fails with the harness unfixed (test-1-red.log, ctest exit 8, `allocations counted: 0`) and passes with the fix (test-2-green.log, PASS). Reverting the fix via the saved patch fails again (test-3-reverted.log), and re-applying it passes (test-4-reapplied.log). Exit codes are in the table above.
2. **Deterministic:** no wall-clock, sleep or race. The seam is the binary's existing `operator new` hook plus `RealtimeAllocationWatch` (thread-local counters), and a synthetic instrument whose `play()` makes exactly one `new[]` per call. The test runs on the calling thread and expects exactly `partc::Buffers` (64) counts.
3. **Fix at the cause and complete:** the watch now spans `playNote` and `play` in the one place that runs per period (`PluginPortsHarness.h:1033-1041`). The callers and siblings are listed under Evidence and all stay consistent: the cleanup watch and the once-per-note `handleMidiEvent` are not per-period and are unchanged.
4. **No collateral:** the change is test-only and changes counts only for single-streamed instruments (the only branch that calls `play`). `freeboy` and `tripleoscillator` stay at 0, and `migratedPluginsPreserveBehaviour` still passes (test-4-reapplied.log:157). No limit or check was added, and no saved-project or product code path changed.
5. **Verdict:** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the "already fixed" citation does not apply. The pre-fix code is cited above.

**Not checked:** I did not measure whether the real OpulenZ `play()` allocates, and I did not add it to the realtime-checked list, because that would widen the test's scope. I did not run the full ctest suite, only the `PluginPortsMigrationTest` family. The `feature_check.py` PASS table was not produced (write-jail), so the gate evidence above is per-command, not the combined table.
