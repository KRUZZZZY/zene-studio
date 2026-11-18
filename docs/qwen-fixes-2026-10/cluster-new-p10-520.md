# cluster new-p10-520 — zero sample rate poisons BasicFilters coefficients

Verdict: **FIXED**

## Finding

`include/BasicFilters.h:330` — `setSampleRate(0)` sets `m_sampleRatio = 1.f / m_sampleRate`
to infinity; every coefficient in `calcFilterCoeffs` (RC, formant, Moog, SV, biquad paths)
multiplies by `m_sampleRatio`, so outputs become non-finite. The constructor
(`include/BasicFilters.h:285-288`, `m_sampleRatio( 1.0f / m_sampleRate )`) has the same
issue for a zero rate.

## Fix

`include/BasicFilters.h`:

- `setSampleRate` (was line 330): reject a zero rate before computing its reciprocal —
  early `return` keeps the previous rate and ratio intact.
- Constructor (was line 287): clamp a non-positive rate to 1 before the reciprocal, so
  `m_sampleRatio` is always finite. Positive rates are untouched — behaviour-preserving
  otherwise.

## Regression

New test `tests/src/core/BasicFiltersSampleRateTest.cpp` (registered in `tests/CMakeLists.txt`
LMMS_TESTS, `tests/fork-sources.txt`; `include/BasicFilters.h` registered in
`tests/upstream-modifications.txt`). It builds `BasicFilters<2>(0)` and a
`setSampleRate(0)`-touched 44100 filter, runs `calcFilterCoeffs` + `update`, and asserts
every output is finite and the rejected filter matches an untouched reference sample-exactly.

Note: the finding's suggested families `^DualFilter$` / `^DualFilterControls$` are not
registered ctests in this build tree (`tests/reference/DualFilter/*` are frozen
partc_ref_* reference MODULEs, not test executables), so a dedicated test was added.

## Evidence table

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-520.r1/logs`.

| Step | Command (from) | EXIT |
|---|---|---|
| Build test target (original code) | `cmake --build build --target BasicFiltersSampleRateTest` (worktree root) | 0 (build-7.log) |
| RED on original code | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` (build/tests) | **8** (test-2.log: `1 tests failed out of 1`, `2 passed, 1 failed` assertions) |
| Fix patch | `git diff -- include/BasicFilters.h > logs/fix.patch` (842 bytes) | 0 |
| Reverse proof | `git apply -R logs/fix.patch`, rebuild (build-9.log EXIT=0), `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` | **8** (test-6.log) |
| Re-apply | `git apply logs/fix.patch`, rebuild (build-10.log EXIT=0), `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure` | **0** (test-7.log: `100% tests passed, 0 tests failed out of 1`) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **0** (gate-1.log) |
| File-length gate | `bash tests/file-length-gate.sh --check` | **0** (gate-2.log: `PASS (check mode: no regressions; baseline not written)`) |

Affected-family test count: 1 test executed per run (`1 tests failed out of 1` RED,
`0 tests failed out of 1` GREEN — never zero).

## Files committed

- `include/BasicFilters.h` (the fix)
- `tests/src/core/BasicFiltersSampleRateTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (LMMS_TESTS registration)
- `tests/fork-sources.txt` (fork-NEW test entry)
- `tests/upstream-modifications.txt` (header change reason)
- `docs/qwen-fixes-2026-10/cluster-new-p10-520.md` (this report)
