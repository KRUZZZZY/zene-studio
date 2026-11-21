Verdict: FIXED

new-p12-2: A malformed config can be overwritten with defaults

Evidence: `ConfigManager::loadConfigFile()` read a config that does not parse with only a dialog (GUI, not unattended). Without a GUI or in an unattended run it showed nothing, and the flag that would stop the shutdown save did not exist. The destructor at [ConfigManager.cpp:106-109](src/core/ConfigManager.cpp:106) calls `saveConfigFile()`, which opened the file with `QSaveFile` and wrote the in-memory defaults over it. The fix:

- [ConfigManager.h:306-308](include/ConfigManager.h:306) adds `bool m_configUnreadable = false;`, set only when the file was opened and `setContent` failed.
- [ConfigManager.cpp:441](src/core/ConfigManager.cpp:441) clears the mark on every `loadConfigFile()` call, so a later readable load is not stuck with it.
- [ConfigManager.cpp:556-575](src/core/ConfigManager.cpp:556) sets the mark on a parse failure. The existing dialog is kept for GUI runs; every other run emits `qWarning` ("Error while parsing configuration file ...; settings not loaded, file left unchanged").
- [ConfigManager.cpp:662-667](src/core/ConfigManager.cpp:662) returns before the `QDomDocument` is built and written, so the file is not truncated. The in-memory `setValue` calls above it still run, so in-memory state is unchanged.

All save paths go through that one guard: the destructor ([ConfigManager.cpp:108](src/core/ConfigManager.cpp:108)), `addRecentlyOpenedProject` ([:348](src/core/ConfigManager.cpp:348)), `addFavoriteItem` ([:355](src/core/ConfigManager.cpp:355)) and `removeFavoriteItem` ([:362](src/core/ConfigManager.cpp:362)). The only product loader is `main.cpp:1299`, plus the tests that call `loadConfigFile` on fresh files.

Tradeoff, stated plainly: while the config file stays malformed, its settings are not loaded and nothing is saved to it. The file is preserved, and the warning says so, but the user has to repair or remove it before settings persist again. The finding asked for exactly this (mark and preserve, do not overwrite).

Regression tests, in `tests/src/core/ConfigMigrationTest.cpp` (the existing home of config-preservation tests, next to `failedConfigWritePreservesExistingFile`):

- `malformedConfigIsReportedAndNotOverwritten` ([:212](tests/src/core/ConfigMigrationTest.cpp:212)): malformed bytes, load, save, file bytes unchanged; expects the parse warning.
- `readableReloadClearsTheUnreadableMark` ([:230](tests/src/core/ConfigMigrationTest.cpp:230)): malformed load, then readable load, then save; the readable file is written again (`<?xml` prefix).
- `armSwitchDoesNotOverwriteAMalformedConfig` in `tests/src/gui/MidiReconnectActionTest.cpp` (listed in the finding's test family): a malformed `zene.xml` is loaded, the Edit > MIDI Controller Reconnect item is toggled twice, and the file bytes must be unchanged. The arm switch is a real save path: `midi.reconnect_arm` calls `setMidiReconnectPersistedEnabled()` ([src/core/midi/MidiReconnect.cpp:306-311](src/core/midi/MidiReconnect.cpp:306)), which ends in `saveConfigFile()`. Without the fix that save overwrites the file. Round 2 added this test because the loop's negative control ran only `MidiReconnectActionTest` and it passed with the fix reverted.

Determinism: no timers, sleeps, threads or wall clock. Inputs are fixed byte strings written into a `QTemporaryDir`. The warning is asserted with `QTest::ignoreMessage` and a regex on the message text. No test seam was needed.

Files committed (round 1, `fef4e79eb`): `include/ConfigManager.h`, `src/core/ConfigManager.cpp`, `tests/src/core/ConfigMigrationTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-2.md`. Round 2 adds `tests/src/gui/MidiReconnectActionTest.cpp` and this report's round-2 section. No new file, so the registries were not edited by me.

Registry note: a later commit on the branch, `6df29227e` ("derive source registries from tracked files", author "Zene workflow"), was not made by me. It appends one line to `tests/upstream-modifications.txt` for `tests/src/core/ConfigMigrationTest.cpp` whose text is `# A malformed config can be overwritten with defaults`, which is not the usual path-and-reason form. I left it as committed; it needs review by whoever owns that tooling.

### Round 2: negative control and the listed test

The loop's negative control reverted the product change and ran only `MidiReconnectActionTest`, which passed (`negctl/test-reverted.log`). That test did not reach the changed code. Round 2 adds `armSwitchDoesNotOverwriteAMalformedConfig` to it, so that the listed family now fails without the fix. The fix commit is `fef4e79eb`; the product patch is `logs/fix.patch` (`git diff fef4e79eb^ fef4e79eb`, 71 lines). Round-2 logs are in `new-p12-2.r2/logs/`.

| Step | Command | Result |
| --- | --- | --- |
| Save patch | `git diff fef4e79eb^ fef4e79eb -- include/ConfigManager.h src/core/ConfigManager.cpp > fix.patch` | EXIT=0 |
| Revert product | `git apply -R fix.patch` | EXIT=0; only the test files differ from HEAD |
| Build, fix reverted | `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene MidiReconnectActionTest ConfigMigrationTest -j4 > 01-build-red.log 2>&1` | Log shows all three targets built |
| **RED**, fix reverted | `ctest -R '^(MidiReconnectActionTest\|ConfigMigrationTest)$' --output-on-failure > 03-test-red-exit.log 2>&1` (from `build/tests`) | EXIT=8, 2/2 tests failed. `MidiReconnectActionTest::armSwitchDoesNotOverwriteAMalformedConfig`: "Compared values are not the same"; `ConfigMigrationTest::malformedConfigIsReportedAndNotOverwritten` and `readableReloadClearsTheUnreadableMark` fail as in round 1 |
| Reapply fix | `git apply fix.patch` | EXIT=0; product files match HEAD again |
| Build, fix restored | `... cmake --build build --target zene MidiReconnectActionTest ConfigMigrationTest -j4 > 04-build-green.log 2>&1` | Log shows all three targets built |
| **GREEN**, fix restored | `ctest -R '^(MidiReconnectActionTest\|ConfigMigrationTest)$' --output-on-failure > 06-test-green-exit.log 2>&1` (from `build/tests`) | EXIT=0, 2/2 tests passed. Verbose run (`05-test-green.log`): `ConfigMigrationTest` 11 passed, 0 failed; `MidiReconnectActionTest` 5 passed, 0 failed, including `armSwitchDoesNotOverwriteAMalformedConfig` |

Gates, round 2 (run by hand from the worktree, logs in `new-p12-2.r2/logs/gate-*.log`): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity 1, file-length 1. The two ratchet exits are the same train-wide items as round 1. None is in `ConfigManager.h/.cpp`, `ConfigMigrationTest.cpp` or `MidiReconnectActionTest.cpp`. `feature_check.py` exits 1 with `PermissionError` on `…-feature-check/fork-sources.log` (the write-jail again), so its table is still not produced.

Build note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied`. ccache's configured temp directory (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is outside the write-jail. Every later build used `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`, which sits in the allowed build-cache area. No compiler flags or global config were changed.

### Evidence table

All paths are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-2.r1/logs/`. Test runs were from `build/tests` with `QT_QPA_PLATFORM=offscreen`.

| Step | Command | Result |
| --- | --- | --- |
| Build, first attempt (not a code error) | `cmake --build build --target ConfigMigrationTest -j4 > build-1.log 2>&1` | EXIT=2 (ccache temp-dir permission, see build note) |
| Build with writable ccache temp dir (test code only, pre-fix) | `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ConfigMigrationTest -j4 > build-2.log 2>&1` | EXIT=0 |
| **RED** (pre-fix product, new tests) | `ctest -R '^ConfigMigrationTest$' --output-on-failure > test-red-1.log 2>&1` | EXIT=8. `malformedConfigIsReportedAndNotOverwritten`: "Compared values are not the same"; `readableReloadClearsTheUnreadableMark`: "Not all expected messages were received". Totals: 9 passed, 2 failed |
| Build with fix | `env CCACHE_TEMPDIR=... cmake --build build --target zene ConfigMigrationTest -j4 > build-3-green.log 2>&1` | EXIT=0 (background task status 0; log ends "Built target ConfigMigrationTest") |
| **GREEN** | `ctest -R '^ConfigMigrationTest$' --output-on-failure > test-green-1.log 2>&1` | EXIT=0, 1/1 passed |
| Verbose totals, GREEN | `ctest -R '^ConfigMigrationTest$' -V > test-4b-green-verbose.log 2>&1` | EXIT=0; Totals 11 passed, 0 failed; both new tests PASS |
| 4b: save the patch | `git diff -- include/ConfigManager.h src/core/ConfigManager.cpp > fix.patch` | EXIT=0 |
| 4b: revert product fix | `git apply -R fix.patch` | EXIT=0 (`git status`: only the test file modified) |
| 4b: rebuild without fix | `env CCACHE_TEMPDIR=... cmake --build build --target zene ConfigMigrationTest -j4 > build-4b-red.log 2>&1` | Log shows "Built target ConfigMigrationTest"; the wrapper's exit code was not written to a file, so the build's exit is reported as the background task status (0) |
| 4b: **RED** without fix | `ctest -R '^ConfigMigrationTest$' --output-on-failure` (test-4b-red.log, then re-run for exit code: test-4b-red-exit.log) | EXIT=8, same two failing assertions as the first RED |
| 4b: reapply fix | `git apply fix.patch` | EXIT=0; `git diff` of the two product files is byte-identical to fix.patch (`cmp` passed) |
| 4b: rebuild with fix | `env CCACHE_TEMPDIR=... cmake --build build --target zene ConfigMigrationTest -j4 > build-4b-green.log 2>&1` | Background task status 0; log shows "Built target ConfigMigrationTest" |
| 4b: **GREEN** | `ctest -R '^ConfigMigrationTest$' --output-on-failure > test-4b-green.log 2>&1` | EXIT=0 |
| Affected test from the finding | `env CCACHE_TEMPDIR=... cmake --build build --target MidiReconnectActionTest -j4 > build-5-midi.log 2>&1`, then `ctest -R '^MidiReconnectActionTest$' --output-on-failure > test-5-midi.log 2>&1` | Build status 0; ctest EXIT=0, 1/1 passed |
| Gates (feature_check.py, the intended run) | `python3 .../feature_check.py <worktree> > gate-1.log 2>&1` | EXIT=1 with `PermissionError`: the script creates `<worktree>-feature-check/` outside the worktree, which the write-jail blocks. It was not edited. See gate table below |

Affected family: `ctest -R '^ConfigMigrationTest$'` is the family this regression lives in and it ran 1 CTest test (11 QTest assertions). `MidiReconnectActionTest` is the other existing test named in the finding; it ran 1 CTest test, passed.

### Gates, run by hand

`feature_check.py` could not run (above), so I ran each gate command it lists, from the worktree, with output in `gate-<name>.log`:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | Train-wide items (e.g. `WasmSandbox::loadModuleBytes`, `XyPadPairTest::dotCentreAlong`, `DawProjectReadTracks`). None is in `ConfigManager.h/.cpp` or `ConfigMigrationTest.cpp`. The run also reports "baseline not written", since there is no cached baseline for base `639e1d23e` |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | Train-wide items (`Vst3Host.cpp`, `WasmSandbox.cpp`, many new test files). None is in the three changed files; the largest changed file, `ConfigManager.cpp`, is not flagged |

Under feature_check's rule (complexity and file-length fail only on items new in changed files), both ratchet rows would PASS for this branch. The raw exit codes above are the train-wide state, recorded as-is. The official table still needs a run from a place the jail allows, because the script writes its logs and baseline cache outside the worktree.

## Self-check

1. **Regression test FAILS with the product change reverted and PASSES with it.** Round 1 (`ConfigMigrationTest`): RED `ctest` EXIT=8 (`test-red-1.log`, `test-4b-red-exit.log`), two failing assertions named above; GREEN `ctest` EXIT=0 (`test-green-1.log`, `test-4b-green.log`), 11/11 QTest assertions. Round 2 (`MidiReconnectActionTest`, the listed family the loop checks): RED with the fix reverted, `ctest` EXIT=8 (`03-test-red-exit.log`), `armSwitchDoesNotOverwriteAMalformedConfig` fails; GREEN with the fix restored, `ctest` EXIT=0 (`06-test-green-exit.log`), 2/2 CTest tests, 16 QTest assertions. Reverting and reapplying used `git apply -R` and `git apply` of `fix.patch`, each EXIT=0.
2. **Deterministic.** No wall-clock timing, sleeps, threads or races. Inputs are fixed bytes in a `QTemporaryDir`. The only observable is the warning, asserted with `QTest::ignoreMessage` and a regex. No test seam was needed.
3. **Fix is at the cause; callers and siblings checked.** Every save path runs through `saveConfigFile()` (see callers above), so one guard covers the destructor and the three setters. The only product loader is `main.cpp:1299`; the other `loadConfigFile` callers are tests that load fresh files. The flag is reset on every load (`ConfigManager.cpp:441`), so a later readable load is not affected.
4. **No collateral damage.** A readable file, a missing file, or a file that cannot be opened (not a parse failure) never sets the mark, so saves behave as before. The only change for existing data: a file that already fails to parse is no longer overwritten at shutdown. That is the bug. The tradeoff (settings persist again only after the file is repaired) is stated above. The 0-byte case counts as a parse failure too; it is only reachable through external truncation, since the save path uses `QSaveFile` (atomic).
5. **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so no "already handled" citation is needed. The code at the cause is cited above.
