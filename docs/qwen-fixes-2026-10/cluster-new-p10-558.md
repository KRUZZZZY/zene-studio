FIXED

# Cluster new-p10-558 - huge state writes mis-sized; finite bounds with an infinite range

Branch `bugs/qwen-new-p10-558`, cut from `050/m0-train-24` (`2a7fd9f9c7`). One line per bug:

new-p10-558: FIXED
new-p10-547: ALREADY-FIXED
new-p16-319: FIXED

Logs: `build/qwen-logs/new-p10-558/` in the worktree (see Self-check item 1 for why they are not in the
prescribed run folder - the harness sandbox denies every write outside the worktree, `$TMPDIR` and its own
home, including `~/.local/share/zene-bunny-fix/qwen/runs/new-p10-558.r1/logs/` and the worktree's parent
directory; `00-sandbox-probe.log` is the probe and its exit codes). Gate logs are the same folder; the
unmodified-spec gate scripts were run through a copy whose two output paths were redirected (see
`gate-runner/feature_check.patch`), no gate limit or baseline was touched.

## new-p10-558: REAL - `plugins/ClapEffect/ClapHostParams.cpp:246-250` at the base commit (before the fix)

`saveState()` handed `uint64_t size` straight to `QByteArray::append(const char*, qsizetype)`. The narrowing
cast is implementation-defined for a count above `INT64_MAX`; on Qt 6 a negative length makes `append()` copy
`strlen(buffer)` bytes (`/usr/include/qt6/QtCore/qbytearray.h:306-311` call the `QByteArrayView(const char*)`
constructor), and the callback still returned the original `size`. Measured on this machine (Qt 6.12.0):

| `size` passed | bytes appended | callback returned | `save()` |
| --- | --- | --- | --- |
| `SIZE_MAX` | 4 (`strlen`) | `SIZE_MAX` as int64 = -1 | true (plug-in's answer) |
| `SIZE_MAX - 8` | 5 (`strlen`) | -9 | true |

So the project stores a truncated state blob while `saveState()` reports a complete save. Fix
(`ClapHostParams.cpp:249-317` on HEAD): a `StateStream` sink remembers a refused write; the callback checks the
requested size against what the blob can still hold (`QByteArray::maxSize()` on Qt 6, `INT_MAX` on Qt 5,
whose `append()` takes an `int`), refuses with `-1` when it does not fit (or when a non-empty write has a null
buffer), rolls the blob back if `append()` throws `std::bad_alloc`, and `saveState()` returns false and clears
the blob when a write was refused - a plug-in may ignore the stream answer and return true anyway
(`clap/stream.h`: `-1` on write error). This is the only caller of the stream; the sibling VST3 state path
(`plugins/Vst3Effect/Vst3Host.cpp:465`) writes through its own `MemoryStream` and is untouched.
Old data is unaffected: a representable state appends exactly as before (`testStateRoundTrip` still compares
a byte-identical second save, 128 bytes).

## new-p10-547: ALREADY-FIXED - `plugins/ClapEffect/ClapHostParams.cpp:55-79` at the base commit

`sanitizedRange()` already runs over every descriptor `descriptorOf()` builds from a plug-in's `get_info()`
(`ClapHostParams.cpp:79`, the only construction path - `scanParams()`:113 and `refreshParamInfo()`:146 both go
through `readParams()`), and orders `min`/`max`, replaces non-finite bounds with 0..1, and clamps the default
into the result. `setParamPlain()` then calls `sanitizedValue()` -> `std::clamp` on ordered, finite bounds.
Shipped by commit `1e0399d5df` (the same finding, Luna cluster 3745) with
`ClapHostTest::testAnInvalidParamRangeIsNormalized`.

Proven here by experiment, not assumption: reverting just that function to its pre-fix form (verbatim copy of
`min_value`/`max_value`, file saved as `547-pre-fix-ClapHostParams.cpp`) and rebuilding made that test fail:
`ctest -R '^ClapHostTest$'` -> EXIT=8, `'gain.minValue <= gain.maxValue' returned FALSE. (the inverted range
survived: 1 .. 0)`; `testANonFiniteParamValueNeverReachesThePlugin` also failed. Restored with
`git checkout --` -> EXIT=0, 40 passed.

## new-p16-319: REAL - `plugins/ClapEffect/ClapHostParams.cpp:57-66` at the base commit (before the fix)

The guard checked the two ENDPOINTS, not their SPAN. `-DBL_MAX .. DBL_MAX` passes both `isfinite()` tests, is
stored ordered, and `max - min` overflows to `+inf`; `normalize()` (`ClapHostInternals.h:72-77`) then divides
by it, and `setParamNormalized()` (`ClapHostParams.cpp:245-246`) computes `min + normalized * inf`. For
`normalized = 0.5` that is `min + inf = +inf`, clamped to `DBL_MAX`, and `normalize()` returns
`(DBL_MAX - -DBL_MAX) / inf = inf / inf = NaN`; ordinary values all read 0 instead of spreading. Fix
(`ClapHostParams.cpp:51-83` on HEAD): after ordering, a span that is not finite falls back to the same harmless 0..1 a
non-finite endpoint gets; a finite span (`0 .. DBL_MAX`) keeps the plug-in's endpoints exactly. This is the
single descriptor sanitizer, so every consumer (`normalize()` in the getters, `setParamNormalized()`,
`ClapParameter.cpp`'s step and tolerance, the FloatModel range) sees the valid range.

## Evidence

Commands run from the worktree root unless the row says `build/tests`; all logs are in
`build/qwen-logs/new-p10-558/`.

| Step | Command | Exit | Observed |
| --- | --- | --- | --- |
| Baseline (before any edit) | `cmake --build build --target ClapHostTest clap-test-gain`; `cd build/tests && ctest -R '^ClapHostTest$'` | 0; 0 | 36 passed, 0 failed [build-baseline.log, test-baseline.log] |
| 547 experiment RED | pre-fix `sanitizedRange`, rebuild ClapHostTest+fixture, `ctest -R '^ClapHostTest$'` | 0; **8** | inverted range survived 1..0 [547-revert-build.log, 547-revert-test.log] |
| 547 experiment restored | `git checkout -- plugins/ClapEffect/ClapHostParams.cpp` | 0 | tree back to HEAD |
| 319 RED | fixture `CLAP_TEST_GAIN_WIDE_RANGE` + `testAFiniteRangeWithAnInfiniteSpanIsRejected`, rebuild, `ctest -R '^ClapHostTest$'` | 0; **8** | `std::isfinite(gain.maxValue - gain.minValue)` FALSE, span -1.79769e+308 .. 1.79769e+308; 38 passed 1 failed [319-red-build.log, 319-red-test.log] |
| 319 GREEN | same build, same ctest | 0; 0 | 39 passed, 0 failed [319-green-build.log, 319-green-test.log] |
| 319 4b revert | `git diff -- plugins/ClapEffect/ClapHostParams.cpp > 319-fix.patch && git apply -R 319-fix.patch`; rebuild; `ctest -R '^ClapHostTest$'` | 0; 0; **8** | same assertion FALSE [319-red2-build.log, 319-red2-test.log] |
| 319 4b restore | `git apply 319-fix.patch`; rebuild; same ctest | 0; 0; 0 | 39 passed [319-green2-build.log, 319-green2-test.log] |
| 558 RED | fixture `CLAP_TEST_GAIN_HUGE_STATE` + `testAnUnrepresentableStateWriteIsAFailedSave`, rebuild, `ctest -R '^ClapHostTest$'` | 0; **8** | `!plugin.saveState(&state)` FALSE ("reported as a complete save"); 39 passed 1 failed [558-red-build2.log, 558-red-test2.log] |
| 558 GREEN | rebuild, same ctest | 0; 0 | 40 passed, 0 failed [558-green-build.log, 558-green-test.log] |
| 558 4b revert | `git diff -- plugins/ClapEffect/ClapHostParams.cpp > 558-fix.patch` (taken against the 319 commit, so it holds only the 558 hunks); `git apply -R 558-fix.patch`; rebuild; `ctest -R '^ClapHostTest$'` | 0; 0; **8** | `state.isEmpty()` FALSE, "a failed save left a 5-byte blob behind"; 39 passed 1 failed [558-red5-build.log, 558-red5-test.log] |
| 558 4b restore | `git apply 558-fix.patch`; rebuild; same ctest | 0; 0; 0 | 40 passed [558-green6-build.log, 558-green6-test.log] |
| Family | `cmake --build build --target zene ClapHostTest ClapEffectIntegrationTest ClapBusMapTest ClapLoaderErrorTest ClapTestInstrumentUnitTest ClapInstrumentViewTest`; `ctest -R '^(ClapHostTest\|ClapEffectIntegrationTest\|ClapBusMapTest\|ClapLoaderErrorTest\|ClapTestInstrumentStateTest\|ClapTestInstrumentEventCountTest\|ClapInstrumentViewTest)$'` | 0; **0** | 7/7 tests passed; ClapHostTest 40, ClapEffectIntegrationTest 19, ClapInstrumentViewTest 4 [10-family-build.log, 11-family-test.log] |
| Gates | `python3 build/qwen-logs/new-p10-558/gate-runner/feature_check.py <worktree>` | **0** | 8/8 PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length "no new item in your files" [12-feature-check-wip.log] |

Files committed (explicit paths): `plugins/ClapEffect/ClapHostParams.cpp`,
`tests/data/clap-test-plugin/clap-test-gain.c`, `tests/src/plugins/ClapHostTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-new-p10-558.md`.

## Self-check

1. **Logs outside the prescribed folder.** The prescribed LOG FOLDER is
   `~/.local/share/zene-bunny-fix/qwen/runs/new-p10-558.r1/logs/`; every write there (and to the worktree's
   parent, where `feature_check.py` writes `<wt>-feature-check/`) is refused by the harness sandbox:
   `touch .../logs/probe.log` -> `Permission denied`, EXIT=1, the same for `$HOME`, `.config`, `.local/state`,
   `/var/tmp` and the build-pool lock (`00-sandbox-probe.log`). Writable: the worktree, `$TMPDIR` and
   `~/.local/share/zene-bunny-fix/dsh/home`. Logs therefore live in the git-ignored
   `build/qwen-logs/new-p10-558/`, and `feature_check.py` / `submit_check.py` run from a copy under
   `gate-runner/` with only their two output paths redirected (`gate-runner/feature_check.patch`); the gate
   commands they invoke, and their baselines/cache, are untouched.
2. **Each test fails with the product change reverted.** New-p16-319: RED EXIT=8 -> restore -> GREEN EXIT=0 ->
   revert EXIT=8 -> `git apply` EXIT=0 -> GREEN EXIT=0. New-p10-558: RED EXIT=8 -> GREEN EXIT=0 -> revert
   EXIT=8 (the failing assertion is the truncated blob: `state.isEmpty()` FALSE, 5 bytes left) -> `git apply`
   EXIT=0 -> GREEN EXIT=0. New-p10-547 is ALREADY-FIXED; its existing regression was proven to catch the bug
   by reverting the shipped sanitizer (EXIT=8) and restoring it (EXIT=0).
3. **Deterministic.** No timing, sleeps or races: the tests inject a plug-in-reported size and a plug-in-reported
   range through two new `getenv()` switches in the in-tree fixture (`CLAP_TEST_GAIN_HUGE_STATE`,
   `CLAP_TEST_GAIN_WIDE_RANGE` / `CLAP_TEST_GAIN_HUGE_RANGE`), and assert on `saveState()`'s return value, the
   blob's length, the stored bounds and the normalized round trip. `cleanup()` clears every fixture switch so a
   failed `QVERIFY2` cannot leak one into the next case (Q050-490 convention).
4. **Cause and completeness.** 558: the only caller of `clap_ostream_t::write` in the tree is
   `saveState()`; a failed/refused write is both reported (`-1`) and remembered, so a plug-in that ignores the
   answer cannot fake success, and the blob is cleared because `ClapEffectControls::saveSettings()`
   (`plugins/ClapEffect/ClapEffectControls.cpp:78-86`) stores any non-empty `QByteArray` without checking the
   bool (the instrument caller does check it). `loadState()`'s reader is a different callback (`clap_istream_t`,
   `uint64_t` -> clamped by `available` and checked for null implicitly by `memcpy`) and is unchanged. 319: the
   one descriptor sanitizer feeds all range consumers; sibling `ClapParameter.cpp:46` and `:155` only read the
   now-guaranteed-finite span.
5. **No collateral damage.** A representable state writes byte-identically (`testStateRoundTrip` still gets a
   byte-identical second save and the same 128-byte layout); a finite `0 .. DBL_MAX` range keeps the plug-in's
   endpoints and normalizes 0 and `DBL_MAX` to 0 and 1 (asserted in the 319 test); every old descriptor
   (0..1, ordered, finite spans) takes the same path as before. No new control command, no audio-thread code
   touched (saveState/loadState run on the main thread; the callback does no locking or logging), no undo
   change.
6. **Realtime / undo / commands.** Untouched: the diff is the parameter/state path and its tests. No
   allocation, mutex, wait, I/O or signal was added to `process()`/`renderPeriod()`; `QByteArray::append` in
   `saveState()` is the pre-existing main-thread save path.
7. **Registration and gates.** `plugins/ClapEffect/ClapHostParams.cpp` is fork-NEW
   (`tests/fork-sources.txt:1249`); `tests/src/plugins/ClapHostTest.cpp` and
   `tests/data/clap-test-plugin/clap-test-gain.c` are inherited files already declared in
   `tests/upstream-modifications.txt`; no new file, no CMake change. 8/8 gates PASS with the unmodified
   limits/baselines (`12-feature-check-wip.log`, EXIT=0).
8. **Old projects.** `ClapEffectControls::loadSettings()` and `ClapInstrument::loadSettings()` read the same
   `<state>` element as before; only a save that could not be represented now fails instead of writing a
   truncated blob, so previously saved projects load exactly as before.
