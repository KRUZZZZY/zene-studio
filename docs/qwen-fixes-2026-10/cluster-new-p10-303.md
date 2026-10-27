Verdict: FIXED

new-p10-303: FIXED

Finding: `readCacheFile()` read the whole plugin-scan cache with `file.readAll()` and no size limit, so a corrupt or foreign cache of any size was loaded into memory before the parser rejected it.

Evidence (reproduced before the fix, then fixed):

* Original read: `src/core/PluginScanCache.cpp:161` (`out = QJsonDocument::fromJson(file.readAll(), &parseError);`), with no size check before it. The finding's line 148 is this read in the pre-patch tree.
* Fix, read side: a 64 MiB limit (`s_maxCacheFileBytes`, `src/core/PluginScanCache.cpp:52-55`) is checked with `file.size()` before the read at `src/core/PluginScanCache.cpp:153-158`. Over the limit, it warns and returns false, so the scan falls back to a full scan, as the corrupt-file contract already does.
* Fix, write side: `save()` serialises once into `bytes` (`src/core/PluginScanCache.cpp:270`), refuses anything over the same limit (`:272-276`), and writes those bytes through `QSaveFile` (`:286`). A cache that `load()` would refuse is never left on disk.
* The only reader is `PluginScanCache::load()` through `readCacheFile()`. The writers are `PluginFactory.cpp:283`, `PluginFactoryBackgroundScan.cpp:377` and `ControlCommandsPluginScanEdit.cpp:191,248`; the two command sites check the return value and the other two ignore it, so a refused save is visible or harmless.
* Size evidence for the limit: the owner's largest real cache, `/home/kruzzzzy/Documents/Zene Studio/plugin-scan-cache.json`, is 6,735,262 bytes for 10,196 file records and 1 quarantine entry (about 660 bytes per record). 64 MiB is about 100,000 records, roughly 10x that cache.

Regression tests (`tests/src/core/PluginScanCacheTest.cpp`, registered already in `tests/fork-sources.txt`; no new files):

* `testOversizedCacheDegradesToFullScan` (`:490`): a valid cache padded with trailing whitespace to exactly 64 MiB loads (1 record), and one byte more is refused (0 records, 0 quarantine). Then a normal `save()` replaces the refused file and it loads again. The assertion that fails before the fix is at `PluginScanCacheTest.cpp:515`.
* `testOversizedCacheIsNotWritten` (`:529`): a record whose JSON exceeds 64 MiB is refused by `save()`, and no file is left behind; a normal cache then saves and loads. The assertion that fails before the fix is at `PluginScanCacheTest.cpp:539`.
* Deterministic: no timing, sleeps or threads. The limit is pinned as a literal in the test, so no seam is needed and the test does not depend on the implementation.

Test matrix:

* Normal path: a valid cache loads; a normal save loads back; a cache at exactly the limit loads.
* Failure path: one byte over the limit is refused on load and on save; a refused file is replaced by the next save; the corrupt-file test (`testCorruptCacheDegradesToFullScan`) still passes.
* Audio thread: none. The header says the cache runs on the GUI thread only (`include/PluginScanCache.h`, "Where it runs"). The diff adds no allocation, lock or log to any audio path.
* Platform: uses only `QFile::size`, `QByteArray`, `QString(n, QChar)` and `qWarning`, all available in Qt 5.15 and Qt 6. No platform-specific code.

Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-303.r1/logs/`). Builds used `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p10-303` because the default ccache temp directory failed with "Permission denied" in this sandbox (`build-1-red.log`, EXIT=2, aborted before any test ran).

| Evidence | Command | Result |
| --- | --- | --- |
| RED build | `cmake --build build --target PluginScanCacheTest -j4 > build-2-red.log 2>&1` (with CCACHE_TEMPDIR) | EXIT=0 |
| RED test, before the fix | `cd build/tests && ctest -R '^PluginScanCacheTest$' --output-on-failure > test-1-red.log 2>&1` | EXIT=8; `FAIL!` at `PluginScanCacheTest.cpp:515` and `:539`; Totals 18 passed, 2 failed |
| GREEN build | `cmake --build build --target PluginScanCacheTest -j4 > build-3-green.log 2>&1` | EXIT=0 |
| GREEN test | `ctest -R '^PluginScanCacheTest$' --output-on-failure > test-2-green.log 2>&1` | EXIT=0; 1/1 ctest entry |
| GREEN, QTest count | `ctest -R '^PluginScanCacheTest$' -V > test-6b-reapplied-verbose.log 2>&1` | Totals 20 passed, 0 failed |
| Family build | `cmake --build build --target MissingDevicesRound2Test zene -j4 > build-4-family.log 2>&1` | EXIT=0 |
| Family test | `ctest -R '^(PluginScanCacheTest\|MissingDevicesRound2Test)$' -V > test-4-family-verbose.log 2>&1` | EXIT=0; PluginScanCacheTest 20 passed, MissingDevicesRound2Test 5 passed; 25 functions, 0 failed |
| Step 4b: save the product diff | `git diff -- src/core/PluginScanCache.cpp > logs/fix.patch` | EXIT=0 |
| Step 4b: revert the product fix | `git apply -R logs/fix.patch` | EXIT=0 |
| Step 4b: rebuild reverted | `cmake --build build --target PluginScanCacheTest -j4 > build-5-reverted.log 2>&1` | EXIT=0 |
| Step 4b: RED with the fix reverted | `ctest -R '^PluginScanCacheTest$' --output-on-failure > test-5-reverted-red.log 2>&1` (from build/tests) | EXIT=8; the same two `FAIL!` lines; Totals 18 passed, 2 failed |
| Step 4b: re-apply the fix | `git apply logs/fix.patch` | EXIT=0 |
| Step 4b: rebuild re-applied | `cmake --build build --target PluginScanCacheTest -j4 > build-6-reapplied.log 2>&1` | EXIT=0 |
| Step 4b: GREEN with the fix re-applied | `ctest -R '^PluginScanCacheTest$' --output-on-failure > test-6-reapplied-green.log 2>&1` | EXIT=0 |
| Final zene build | `cmake --build build --target zene -j4 > build-7-zene-final.log 2>&1` | EXIT=0 |
| Gate, wrapper | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gate-feature-check-1.log 2>&1` | EXIT=1; not a gate verdict: `PermissionError` creating the sibling log directory `<worktree>-feature-check`, which is outside the write-jail |
| Gate, fork-sources | `bash tests/fork-sources-gate.sh` (from the worktree) | EXIT=0 (also re-run after the doc edit, EXIT=0) |
| Gate, all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | EXIT=0 |
| Gate, no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | EXIT=0 |
| Gate, check-namespace | `tests/scripted/check-namespace` | EXIT=0 |
| Gate, no-tautology | `bash tests/no-tautology-gate.sh` | EXIT=0 |
| Gate, doc-figures | `python3 tests/doc-figures-gate.py` (re-run after the doc edit) | EXIT=0 |
| Gate, complexity (ratchet) | `bash tests/complexity-gate.sh --check` | raw EXIT=1 (red on the train, as the brief says); 7 regression lines, **0 new** against the cached base listing `gate-baselines/7641e9493d…json` (key comparison the wrapper uses) |
| Gate, file-length (ratchet) | `bash tests/file-length-gate.sh --check` | raw EXIT=1 (red on the train); 22 regression lines, **0 new** against the cached base listing (the baseline has 21 keys; the extra line is a duplicate key) |

Why the wrapper row is replaced: `feature_check.py` must create its log directory next to the worktree, which the write-jail forbids. I ran the same gate commands it runs, from the worktree, with logs in the LOG FOLDER, and applied its ratchet rule by hand (a regression fails only when its key is not in the cached baseline and it lives in a changed file). Neither ratchet gate has a new item. I did not edit the wrapper, the gate scripts, any limit or any baseline.

Files committed for this verdict:

* `src/core/PluginScanCache.cpp`
* `tests/src/core/PluginScanCacheTest.cpp`
* `docs/PLUGIN-SCAN-CACHE.md` (one sentence in the failure-tolerance paragraph, stating the 64 MiB limit)
* `docs/qwen-fixes-2026-10/cluster-new-p10-303.md` (this report)

Residual risk, for the owner:

* A valid cache over 64 MiB, which `save()` now never writes, is refused on load and its quarantine list goes with it. A plugin quarantined in that file would then be scanned again. This is the same consequence a corrupt file has today. The limit is a judgement (about 10x the largest real cache); it is one constant at `src/core/PluginScanCache.cpp:55`, and the test pins the value.
* At about 100,000 records the cache stops persisting, so each start does a full scan with a warning. That is the intended degradation.

## Self-check

* **Each new test fails with the product change reverted, then passes restored (exit codes).** Yes. Reverted: `test-5-reverted-red.log`, EXIT=8, the same two `FAIL!` lines as before the fix. Restored: `test-6-reapplied-green.log`, EXIT=0, 20 passed. Both tests: `testOversizedCacheDegradesToFullScan` and `testOversizedCacheIsNotWritten`.
* **Deterministic, no wall-clock timing, sleeps or races (the seam used).** Yes. No timing and no threads. The limit is pinned as the literal `64LL * 1024 * 1024` in the test, and the sizes are built from that literal. The padding is exact, so the boundary is exact.
* **Fix at the cause and complete; every caller and sibling checked.** Yes. The one reader is `load()` through `readCacheFile()`. The writers are `PluginFactory.cpp:283`, `PluginFactoryBackgroundScan.cpp:377`, `ControlCommandsPluginScanEdit.cpp:191,248`. The write side got the same limit so that a cache that cannot be read back is never written. The diff touches no command-registration file, so the sibling-command list does not apply.
* **No collateral damage; old projects and saved data load as before; the limit does not hit legitimate callers.** Yes, with the residual above. The largest real cache is 6,735,262 bytes (10,196 records), far under 64 MiB. Existing tests pass: `PluginScanCacheTest` 20/20 and `MissingDevicesRound2Test` 5/5 (`test-4-family-verbose.log`).
* **Realtime, undo and commands.** Nothing on the audio thread. `PluginScanCache` is documented as GUI-thread only (`include/PluginScanCache.h`, "Where it runs"). No command was added. No undo step is involved.
* **Portability (Qt 5.15 / MSVC / macOS).** Only long-standing Qt APIs (`QFile::size`, `QByteArray`, `QString(n, QChar)`, `qWarning`). No platform-specific code.
* **Registration.** No new file, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. The fork-sources gate passes (EXIT=0).
* **Verdict FIXED, with the cause and the file:line evidence above.** Yes.
* **The ratchet gates and wrapper.** Not run through `feature_check.py` (write-jail); the same commands were run directly and the ratchet rule was applied by hand (0 new items for either gate). The wrapper's own gate row is therefore unverified in this lane.
