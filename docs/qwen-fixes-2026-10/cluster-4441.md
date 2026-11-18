# Cluster 4441 — User log text can misclassify a script error as a budget failure

**Verdict: FIXED**

## Finding

`src/core/ScriptClock.cpp:331` (historical): `errorKindFor()` ran on the error *after*
script-controlled log lines were folded in. A script that logs
`instruction budget exceeded` and then fails for an unrelated reason was classified
`budget` and incremented `budget_exceeded`.

## Evidence

- `src/core/ScriptClock.cpp:355` (pre-fix): `entry.lastErrorKind = errorKindFor(entry.lastError);`
  classified the **folded** text (`foldLogs(error, logs)`), so user log lines could masquerade
  as an engine failure kind.
- Fix: classify the raw `runString` error before folding logs —
  `entry.lastErrorKind = errorKindFor(error);` (folded text still stored in `lastError` for display).
- Regression test: `tests/src/core/ScriptClockTest.cpp` —
  `aLogLineCannotMasqueradeAsABudgetFailure()` schedules a script that prints
  `instruction budget exceeded` then errors `unrelated boom`; asserts
  `errors == 1`, `budget_exceeded == 0`, `last_error_kind == "script"`, and the folded
  `last_error` contains both texts.

## Commands and exit codes

| Step | Command | EXIT |
|---|---|---|
| Build (RED) | `cmake --build build --target ScriptClockTest > $LOGS/build-1.log 2>&1; echo EXIT=$?` | (binary proven by RED run) |
| RED run | `cd build/tests && ctest -R ScriptClockTest --output-on-failure > $LOGS/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Build (GREEN) | `cmake --build build --target zene ScriptClockTest > $LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN run | `cd build/tests && ctest -R ScriptClockTest --output-on-failure > $LOGS/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4441.r1/logs`.

RED failure (test-1.log): `FAIL! : ScriptClockTest::aLogLineCannotMasqueradeAsABudgetFailure() Compared values are not the same` — `budget_exceeded` expected 0, got 1 (the misclassification).

Family: `ctest -R ScriptClockTest` runs 1 test (the ScriptClockTest binary); GREEN: `100% tests passed, 0 tests failed out of 1`.

## Files committed

- `src/core/ScriptClock.cpp` (one-line fix + 2 comment lines)
- `tests/src/core/ScriptClockTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-4441.md` (this report)

No new product files; `ScriptClock.cpp`/`.h` already registered in `tests/fork-sources.txt`.
