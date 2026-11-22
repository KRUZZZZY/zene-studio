Verdict: FIXED

new-p16-304: FIXED (REAL). The earlier FALSE-POSITIVE verdict (commit `fb79961dbc`) was wrong and is withdrawn. A signed negative rate reaches the unsigned `BasicFilters` API and wraps to `UINT32_MAX`, which changes filter output.

## Evidence

- The boundary is unsigned: `sample_rate_t` is `std::uint32_t` ([include/LmmsTypes.h:42](../../include/LmmsTypes.h:42)). A signed argument such as `-1` converts at the call site to `4294967295` before the constructor or setter runs. The old zero-only checks then accept it.
- Old constructor: [include/BasicFilters.h:295-296](../../include/BasicFilters.h:295). The `> 0 ? … : 1` substitution is only `!= 0` on an unsigned type, so a wrapped negative passed it.
- Old setter: [include/BasicFilters.h:364-373](../../include/BasicFilters.h:364). Only `sampleRate == 0` is rejected, so the wrapped rate is stored and used for coefficients.
- Fix, signed overloads that validate before conversion:
  - `toSampleRate` maps any value `> 0` to `sample_rate_t` and any nonpositive value to `0`: [include/BasicFilters.h:306-311](../../include/BasicFilters.h:306).
  - Signed constructor template, which delegates to the unsigned constructor with the mapped value: [include/BasicFilters.h:313-317](../../include/BasicFilters.h:313). A nonpositive value therefore takes the existing constructor fallback to 1 Hz.
  - Signed setter template, which delegates to the unsigned setter: [include/BasicFilters.h:357-362](../../include/BasicFilters.h:357). A nonpositive value becomes 0 and is rejected by the existing zero path, so the current rate is kept.
  - `#include <concepts>` for `std::signed_integral`: [include/BasicFilters.h:37](../../include/BasicFilters.h:37).
- Unsigned callers are unaffected. Non-template overloads win on `sample_rate_t`. Signed nonnegative callers take the template, which maps them to the same value as before. Production caller: `LOMMEffect::changeSampleRate()` ([plugins/LOMM/LOMM.cpp:76-81](../../plugins/LOMM/LOMM.cpp:76)), which passes an unsigned engine rate.

## Regression tests

`tests/src/core/BasicFiltersSampleRateTest.cpp`, two new deterministic cases (no timing, sleeps or threads):

- `negativeSampleRateSetterIsRejected` (line 71): `BasicFilters<2>` at 44100 Hz, then `setSampleRate(-1)`. Output over 16 samples must equal an untouched 44100 Hz filter.
- `negativeSampleRateConstructorFallsBack` (line 89): `BasicFilters<2>(-1)` must equal the zero-rate fallback `BasicFilters<2>(sample_rate_t{0})`. The cutoff is 1000.5 Hz. At the 1 Hz fallback rate, a whole-number cutoff gives a degenerate response (`omega` is a multiple of 2π), so the test first asserts the reference output is nonzero (line 102), which proves the stimulus can tell the paths apart.

A third candidate, `2^32 + 44100` through a signed 64-bit setter, was dropped. Under the project's `-Werror` settings the implicit 64-bit-to-32-bit conversion at the call site fails to compile before the fix, so it cannot give a runtime RED. Positive values above `UINT32_MAX` therefore still wrap at the boundary. That is a separate input class from this finding and is not fixed here.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-304.r2/logs/`. The r1 folder named in the lane brief no longer exists, so the r2 folder holds this run.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build (tests only, no product fix yet) | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4 > 003-build-red.log 2>&1` | 0 | target built |
| RED run, fix absent | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` from `build/tests` (`006-ctest-red.log`) | 8 | 2 failed, 5 passed. Failing assertions: `negativeSampleRateSetterIsRejected` (`filter.update(...) == reference.update(...)` returned FALSE), `negativeSampleRateConstructorFallsBack` (`negative.update(...) == expected` returned FALSE) |
| Fix build | `CCACHE_DISABLE=1 cmake --build build --target zene BasicFiltersSampleRateTest -j4 > 007-build-green.log 2>&1` | 0 | built; 9 `error|warning:` matches, all in unrelated files (Qt deprecations, `ControllerView.h`, `ControlDeviceSupport.cpp`), none in `BasicFilters.h` |
| GREEN run, focused | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (`008-ctest-green.log`) | 0 | 1/1 passed |
| Dependent targets build | `cmake --build build --target DeviceDspTest PluginPortsMigrationTest partc_ref_dualfilter partc_ref_lomm EqFilterTest FilterPoolTest -j4` (`009`) | 0 | 0 `error:` lines; `libdualfilter.so` and `liblomm.so` relinked |
| Dependent family, first run | `ctest -R 'BasicFiltersSampleRateTest\|DeviceDspTest\|PluginPortsMigrationTest\|FilterPoolTest\|DualFilter\|LOMM' --output-on-failure` (`010`) | 8 | PluginPortsMigrationTest failed on `nonRealtimeLadspaEffectsAreInvalid`: `description != nullptr` ("SWH Decimator test fixture was not loaded") |
| Cause of that failure | `cmake --build build --target help` lists `decimator_1202`; the test reads `build/plugins/ladspa` (`tests/CMakeLists.txt:3801`), which held only `amp_1181.so` and `dj_eq_1901.so` | — | the fixture target was never built in this worktree. Not caused by the header |
| Fixture build | `CCACHE_DISABLE=1 cmake --build build --target decimator_1202 -j4` (`011`) | 0 | `decimator_1202.so` built |
| Dependent family, after fixture | same ctest command (`012-ctest-dependents-green.log`) | 0 | 4/4 passed: BasicFiltersSampleRateTest (1), DeviceDspTest, FilterPoolTest, PluginPortsMigrationTest |
| Step 4b: save product patch | `git diff -- include/BasicFilters.h > 013/fix.patch` | — | 48 lines |
| Step 4b: reverse product change | `git apply -R fix.patch` (`013-apply-reverse.log`) | 0 | only the test file remains modified |
| Step 4b: rebuild with product change reversed | `CCACHE_DISABLE=1 cmake --build build --target zene BasicFiltersSampleRateTest -j4` (`014`) | 0 | built |
| Step 4b: RED with product change reversed | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (`015-ctest-reverted-red.log`) | 8 | 2 failed, 5 passed; same two assertions as the first RED run |
| Step 4b: reapply product change | `git apply fix.patch` (`016`) | 0 | fix restored |
| Step 4b: rebuild with fix | `CCACHE_DISABLE=1 cmake --build build --target zene BasicFiltersSampleRateTest -j4` (`017`) | 0 | built |
| Step 4b: GREEN with fix | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (`018`) | 0 | 1/1 passed |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` (`gate-fork-sources.log`) | 0 | |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `include/BasicFilters.h` is an inherited file already listed in the upstream manifest |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 | |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| Gate: complexity ratchet (raw) | `bash tests/complexity-gate.sh --check` | 1 | 6 `REGRESSION:` lines, none naming `BasicFilters` or the test file |
| Gate: file-length ratchet (raw) | `bash tests/file-length-gate.sh --check` | 1 | 22 `REGRESSION:` lines, none naming `BasicFilters` or the test file; same 28-line count as the previous round |
| `feature_check.py` | `python3 …/feature_check.py <worktree>` | 1 | Crashes before any gate runs: `PermissionError` creating `<worktree>-feature-check/`, a folder outside the write-jail (`projects/zene-studio-program/`). The six simple gates and two ratchets were run by hand with the same commands, as recorded above |

Affected-family test count: 4 tests executed and passed (BasicFiltersSampleRateTest 1/1; DeviceDspTest, FilterPoolTest, PluginPortsMigrationTest). The `DualFilter` and `LOMM` name patterns matched no ctest entries.

Gate note: the ratchet items are red on the train by design. Their rule fails only items in files this branch changed. The 28 `REGRESSION:` lines name no changed file, so the ratchets should pass under that rule. I did not compute the base-branch baseline, so this is inferred, not observed.

## Files changed

- `include/BasicFilters.h`: signed-integral constructor and setter overloads plus `toSampleRate`, and `#include <concepts>`.
- `tests/src/core/BasicFiltersSampleRateTest.cpp`: two new slots and test bodies.
- `docs/qwen-fixes-2026-10/cluster-new-p16-304.md`: this report.

## Self-check

- Regression test FAILS with the product change reverted and PASSES with it: RED exit 8 (2 failed) at `006` and `015`; GREEN exit 0 (1/1) at `018`. The reversal was done with `git apply -R` on the saved patch, rebuilt, then reapplied.
- Deterministic: no wall-clock timing, sleeps or threads. Inputs are literal integers. The cutoff and rate values were chosen so the two paths produce different, nonzero output, and the test asserts that nonzero value rather than assuming it.
- Fix at the cause: both entry points that take an unsigned rate, the constructor and the setter, have signed siblings. Both route through the one `toSampleRate` mapping. The sub-filter propagation in the setter is unchanged. The unsigned overloads are untouched.
- No collateral damage: unsigned callers resolve to the unchanged non-template overloads. Nonnegative signed callers get the same value as before. The change adds no audio-thread work. Nothing is allocated, locked or logged.
- Residual: positive signed values above `UINT32_MAX` still wrap at the boundary. This is a separate input class from the finding, and I could not write a runtime RED for it under `-Werror`. It is not fixed here.
- The reviewer's commit `678ae7d15c` exists in the shared object store but not on this branch. I did not cherry-pick it. This fix is my own implementation: it uses a single mapping helper, and it drops the reviewer's unregistered-test concern by keeping the test in the already-registered `BasicFiltersSampleRateTest` file.
- Gates: the manual gate run is recorded above. `feature_check.py` could not run because its log folder is outside the write-jail. The submit check's gates item will therefore still fail for that reason alone.
