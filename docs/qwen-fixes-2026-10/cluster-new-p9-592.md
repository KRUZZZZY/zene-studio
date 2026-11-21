# cluster new-p9-592 — `BasicFilters` reads an uninitialised filter type

Verdict: **FIXED**

## Finding

`include/BasicFilters.h` (finding line 285, historical): the constructor did not initialise
`m_type`, but `calcFilterCoeffs()` and `update()` switch on it. A filter constructed and used
before `setFilterType()` therefore read an indeterminate enum, which is undefined behaviour.

## Evidence (before the fix)

- Constructor, initialiser list did not set `m_type`: `include/BasicFilters.h:294-300` (pre-fix).
- Reads of `m_type` before any `setFilterType()`: `calcFilterCoeffs` (first read `BasicFilters.h:728`
  pre-fix) and `update()` (`switch( m_type )` at `BasicFilters.h:360`).
- Compiler confirmation on the unfixed header: `-Werror=uninitialized` at `BasicFilters.h:728`
  (`build-2-red.log`, first attempt with an inline-visible test). This is why the test calls the
  calc/update path through an out-of-line helper (see below).

## Caller audit (is it reachable in production today?)

Every production caller calls `setFilterType()` before any use, so the bug is latent:

- `src/core/InstrumentSoundShaping.cpp:142-144` — `make_unique<BasicFilters<>>` then `setFilterType` on each note.
- `plugins/DualFilter/DualFilter.cpp:85-93` — `setFilterType` on first run (`m_filter1changed` is initialised `true`, `:64-65`), before `calcFilterCoeffs` `:154` and `update` `:159`.
- `plugins/LOMM/LOMM.cpp:68` — `setFilterType(AllPass)` in the constructor.
- `tests/src/core/DeviceDspTest.cpp:492-493`, `tests/src/plugins/PluginPortsMigrationTest.cpp:1697-1698`,
  `tests/src/core/BasicFiltersSampleRateTest.cpp:44-59` — all set the type first.

No caller changes behaviour with the fix, because the default is overwritten before any read.

## Fix

- `include/BasicFilters.h:295` — `m_type( FilterType::LowPass ),` as the first initialiser.
- `include/BasicFilters.h:924` — comment `// LowPass until setFilterType() is called` on the member.
- `tests/upstream-modifications.txt` — registration line for the inherited header change
  (`BasicFilters.h`, cluster new-p9-592). The no-upstream gate accepts it (`g-no-upstream.log:2543`).

LowPass was chosen because it is enumerator 0, the type `BasicFiltersSampleRateTest` already
uses, and it is the value a fresh filter behaves as in the existing tests.

## Regression test

`tests/src/core/BasicFiltersSampleRateTest.cpp` — new slot `unsetTypeDefaultsToLowpass()`
(`:105`), with an out-of-line helper `impulseResponse()` (`:96`) using the same
`__attribute__((noinline))` idiom as `PluginPortsMigrationTest.cpp:689`.

- Storage is `alignas` byte array; every 4th byte set to `0xFF`, so each int-sized word reads
  `0x000000FF` (matches no `FilterType` enumerator). Placement-new into it simulates stale memory.
- Helper: `calcFilterCoeffs(1000, 1)` then 16 `update()` samples (impulse input).
- Reference: a normally constructed filter with `setFilterType(LowPass)`, same helper.
- Asserts each output is finite, equals the reference exactly, and the impulse response is non-zero.

Why `calcFilterCoeffs` and not only `update()`: before the fix, `update()` on an unconfigured
biquad returns 0 whether the type is garbage or LowPass (zero coefficients). The distinguishing
read is in `calcFilterCoeffs`, which switches on `m_type` and sets the LowPass coefficients only
when the type is LowPass. The test therefore exercises the same bad read through the public API.

Determinism: no clock, sleep, thread or random input. The "injected" state is the 0xFF/0x00
byte pattern written into the storage.

## Commands and exit codes

| # | Command (run in worktree, CCACHE_DISABLE=1 for the cmake builds) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target BasicFiltersSampleRateTest -j4` (first try, ccache temp dir not writable) | 2 | `build-1-red.log` |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4` (inline test; `-Werror=uninitialized` at `BasicFilters.h:728`) | 2 | `build-2-red.log` |
| 3 | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4` (noinline helper, unfixed header) | 0 | `build-3-red.log` |
| 4 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (**RED**, unfixed) | 8 | `test-1-red.log` |
| 5 | `CCACHE_DISABLE=1 cmake --build build -j4 --target zene bitcrush crossovereq dualfilter eq lomm multitapecho slewdistortion BasicFiltersSampleRateTest DeviceDspTest NoteCurvesAcrossTest MultitapEchoBoundsTest PluginPortsMigrationTest` (fixed header) | 0 | `build-4-green.log` |
| 6 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (**GREEN**, fixed) | 0 | `test-2-green.log` |
| 7 | `git diff -- include/BasicFilters.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | `fix.patch` |
| 8 | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4` (reverted) | 0 | `build-5-revert.log` |
| 9 | `ctest -R '^BasicFiltersSampleRateTest$'` (**RED again**, reverted) | 8 | `test-3-revert.log` |
| 10 | `git apply logs/fix.patch` | 0 | — |
| 11 | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4` (reapplied) | 0 | `build-6-reapply.log` |
| 12 | `ctest -R '^BasicFiltersSampleRateTest$'` (**GREEN**, reapplied) | 0 | `test-4-reapply.log` |

RED output (unfixed, both runs):
`FAIL! : BasicFiltersSampleRateTest::unsetTypeDefaultsToLowpass() 'actual[i] == expected[i]' returned FALSE.`
at `tests/src/core/BasicFiltersSampleRateTest.cpp:131`, `Totals: 5 passed, 1 failed`.

GREEN output (fixed): `100% tests passed, 0 tests failed out of 1`, `Test #18: BasicFiltersSampleRateTest ... Passed`.

Family run (fixed; binaries built in step 5, header unchanged since the last reapply):

| # | Command | EXIT | Log |
|---|---|---|---|
| 13 | `cd build/tests && ctest -R 'BasicFilters\|DeviceDspTest\|MultitapEchoBounds\|NoteCurvesAcross\|PluginPortsMigration'` | 8 | `test-5-family.log` |

Result: 5 ctest tests executed; 4 passed; 1 failed (`PluginPortsMigrationTest`, one slot):
`FAIL! : PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid() 'description != nullptr'
(SWH Decimator test fixture was not loaded)`, `PluginPortsMigrationTest.cpp:1616`.
The fixture `decimator_1202.so` is not installed here (`/usr/lib/ladspa` has amp, delay, filter, noise, sine
only; `find /usr/lib /usr/local/lib /usr/lib64 -name decimator_1202.so` finds nothing). This is an
environment gap, not a `BasicFilters` path. I did not run this slot on the unfixed header as a
separate baseline, so the claim "pre-existing" is based on the missing fixture file only.
The other 87 `PluginPortsMigrationTest` slots passed (88 passed, 1 failed, 2 skipped).

## Gates

`feature_check.py` could not run here: it writes its per-gate logs to
`<worktree>/../<name>-feature-check/` (`feature_check.py:70-71`), which is outside the writable
area, so it stops with `PermissionError` before any gate runs (`gate-1-feature-check.log`, EXIT=1).
I ran the same gate commands it lists, from the worktree, logging to the LOG FOLDER:

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates are red on the train, as the brief says. Their REGRESSION lines
(`g-complexity.log:96-101`, `g-file-length.log:25-47`) name other files; none name `BasicFilters.h`,
`BasicFiltersSampleRateTest.cpp` or `upstream-modifications.txt`, and I added no function over
CCN 10 and no new file over 500 lines in the files I changed. Under feature_check's rule (fail only
on NEW items in files the branch changed) they pass. The exit 1 is only the raw script exit.

## Files committed

- `include/BasicFilters.h`
- `tests/src/core/BasicFiltersSampleRateTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-592.md`

No new files were added, so `tests/fork-sources.txt` and `tests/all-sources.txt` need no change
(`BasicFiltersSampleRateTest.cpp` is already registered at `tests/all-sources.txt:2663`).

## Self-check

- **Fails with the change reverted, passes with it:** yes. Step 4 (RED, fixed header absent) EXIT 8;
  step 9 (RED after `git apply -R`) EXIT 8; step 12 (GREEN after `git apply`) EXIT 0. The failing
  assertion is the same in both RED runs.
- **Deterministic:** yes. No clock, sleep, thread or randomness. The only injected state is the
  0x00/0xFF byte pattern in the placement-new storage, which sets `m_type` to the non-enumerator
  `0x000000FF` before construction. The noinline helper keeps the read out of compile-time analysis.
- **At the cause and complete:** the cause is the constructor's missing initialiser. The sibling
  readers (`calcFilterCoeffs`, `update`) and the subfilter (`m_subFilter` is created through the same
  constructor and gets `setFilterType` on the double-filter path, `BasicFilters.h:287-291` pre-fix, `289-293` after) are all
  covered by the one change. Other ctor members (`m_r`, `m_p`, `m_k`, RC/formant/SV coefficients) are
  only read by the type branch that writes them first, so they are not part of this bug.
- **No collateral damage:** every production caller sets the type before any read (caller audit
  above), so their behaviour is unchanged. No save or project format is touched. No limit or check is added.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the defect was present on this branch
  (`git show HEAD:include/BasicFilters.h` has no initialiser). The bug is latent in production,
  which is recorded above.

## Open items (not caused by this change)

1. `feature_check.py` cannot write its logs outside the write-jail; gates were run by hand (above).
2. `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` needs `decimator_1202.so`, which is
   not installed on this machine.
3. ccache is unusable in this sandbox (`ccache: error: failed to create temporary file ... Permission
   denied`), so the cmake builds used `CCACHE_DISABLE=1`. This changes only the compile cache, not the output.
4. Build freshness: after the last reapply the header is newer than the other objects (zene, plugins, the
   other tests) that were compiled with the fixed header in step 5. Their binaries reflect the fixed code;
   the next build will recompile them.
