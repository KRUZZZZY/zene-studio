# Cluster 2862 — NaN meter reference passes the range check

Verdict: **FIXED**

## The bug

`meter.set_reference` (`src/core/ControlCommandsMixerTools.cpp:339`) range-checked
`dbfs` with `dbfs < -30.0 || dbfs > 0.0`. Both comparisons are false for NaN, so a
nonfinite `dbfs` passed the check and was stored as the meter reference
(`setMeterReferenceDbfs`, `src/core/ControlCommandsMeterFile.cpp:135`, writes it to
the config verbatim). The schema gate does not catch it: the command's `dbfs`
property is `numberProperty()` (`src/core/ControlVocabulary.cpp:80-83`, just
`{"type":"number"}`), and `ControlSchema.cpp:49` accepts any `isDouble()` value —
NaN is a double.

## The fix

`src/core/ControlCommandsMixerTools.cpp:339` — reject nonfinite values before the
range check:

```cpp
if (!std::isfinite(dbfs) || dbfs < -30.0 || dbfs > 0.0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'dbfs' is -30..0")); }
```

`<cmath>` was already included (`src/core/ControlCommandsMixerTools.cpp:36`). No
header changed; no new files — both touched files were already registered in
`tests/fork-sources.txt` (`src/core/ControlCommandsMixerTools.cpp` line 1189,
`tests/src/core/MixerToolsTest.cpp` line 1967).

## Regression test

`tests/src/core/MixerToolsTest.cpp` — new case `aNonfiniteMeterReferenceIsRefused()`:
invokes `meter.set_reference` with `std::numeric_limits<double>::quiet_NaN()` and
requires `ControlErrorKind::InvalidArgs` plus the reference staying at the -18
default. RED before the fix (the command succeeded, `errorKind` 0 vs expected 3,
and the reference was clobbered); GREEN after.

## Commands run (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2862.r1/logs)

From the worktree root unless noted:

```
cmake --build build --target MixerToolsTest > $LOGS/build-1.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R MixerToolsTest --output-on-failure > $LOGS/test-1.log 2>&1; echo EXIT=$?
EXIT=8   # RED: test-1.log shows "FAIL! : MixerToolsTest::aNonfiniteMeterReferenceIsRefused()"
         #   actual errorKind 0, expected ControlErrorKind::InvalidArgs (3);
         #   "Totals: 18 passed, 1 failed" — only the new case failed

cmake --build build --target MixerToolsTest > $LOGS/build-2.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R MixerToolsTest --output-on-failure > $LOGS/test-2.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN: test-2.log shows "100% tests passed, 0 tests failed out of 1"

cmake --build build --target zene > $LOGS/build-3.log 2>&1; echo EXIT=$?
EXIT=0

bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1; echo COMPLEXITY_EXIT=$?
COMPLEXITY_EXIT=0
bash tests/file-length-gate.sh --check >> $LOGS/gate-1.log 2>&1; echo LENGTH_EXIT=$?
LENGTH_EXIT=0
```
