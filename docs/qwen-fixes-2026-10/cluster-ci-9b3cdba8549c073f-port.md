ALREADY-FIXED

# Cluster ci-9b3cdba8549c073f-port - CI regression: build / linux-arm64

## Verdict

ALREADY-FIXED for the product defect, with a branch-local regression test added in this branch.
The finding was real: `linux-arm64` is the Linux lane that does not pass `-DWANT_QT6=ON`
(`.github/workflows/build.yml:316-325`), so it compiles against Qt 5.15 because
`CMakeLists.txt:142` defaults `WANT_QT6` to `OFF`. `tests/src/core/ScriptClockTest.cpp` called
`QJsonValue::toInteger`, a Qt 6-only member, and Qt 5 rejected it:

```
tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
tests/src/core/ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
```

At this branch's HEAD both reads already use the sanctioned adapter:
`tests/src/core/ScriptClockTest.cpp:48` includes `DeprecationHelper.h`, and `:409` and `:416`
call `jsonInteger(...)`. The adapter at `include/DeprecationHelper.h:104-120` forwards to
`QJsonValue::toInteger` on Qt 6 and reproduces the same whole/in-range-`qint64` contract on
Qt 5. The product code is therefore not changed by this branch.

## Branch-local regression test

`tests/Qt5JsonIntegerCheck.py` existed at the merge base but was unchanged by this branch, so
the previous attempt had no test file this branch added or changed. It is now extended with a
port-specific assertion over `ScriptClockTest::scheduleStateReportsUtf8Bytes()`: both
`source_bytes` reads in that function must be arguments of `jsonInteger()`. The self-test gains
a matching fixture pair: a function-shaped read that calls `.toInteger()` directly must be
flagged, while the `jsonInteger()` form must not be. The existing CTest `Qt5JsonIntegerCheck`
(`tests/CMakeLists.txt:9774-9777`) runs the checker unchanged. The checker and this report are
already registered in `tests/fork-sources.txt:872,880`.

The already-present `jsonInteger` product code in `ScriptClockTest.cpp` is unchanged.

## Reproduction and RED/GREEN evidence

Qt 5.15.19 is installed locally (`pkg-config --modversion Qt5Core` -> 5.15.19) and
`build/compile_commands.json` is present, so both checker halves run.

| Step | Command | Exit | Log |
|---|---|---:|---|
| Baseline GREEN, unchanged checker | `python3 tests/Qt5JsonIntegerCheck.py --root . --build-dir build` | 0 | `checker-baseline-0.log` |
| Self-test with the new fixture pair | `python3 tests/Qt5JsonIntegerCheck.py --self-test` | 0 | `checker-selftest-3.log` |
| Save only the historical hunk | `git show 0a7d9ced99 -- tests/src/core/ScriptClockTest.cpp > logs/revert-fix.patch` | 0 | `revert-fix.patch` |
| Reverse-check | `git apply -R --check logs/revert-fix.patch` | 0 | `revert-check.log` |
| Reintroduce the defect | `git apply -R logs/revert-fix.patch` | 0 | `revert-apply-2.log` |
| RED checker | `python3 tests/Qt5JsonIntegerCheck.py --root . --build-dir build` | **1** | `checker-red-2.log` |
| RED CTest | `cd build/tests && ctest -R '^Qt5JsonIntegerCheck$' --output-on-failure` | **8** | `ctest-red-2.log` |
| Restore the fix | `git apply logs/revert-fix.patch` | 0 | `restore-apply-2.log` |
| GREEN CTest | `cd build/tests && ctest -R '^Qt5JsonIntegerCheck$' --output-on-failure` | **0** | `ctest-green-2.log` |
| Rebuild behaviour test | `cmake --build build --target ScriptClockTest -j4` | 0 | `build-scriptclock-1.log` |
| Qt 5 JSON family plus behaviour test | `cd build/tests && ctest -R '^(Qt5JsonIntegerCheck|ScriptClockTest|Qt5QJsonToIntegerCheck|Qt5QJsonValueIntegerCheck|Qt5JsonValueIntegerCheck)$' --output-on-failure -j2 --timeout 900` | 0 (5/5) | `test-family-2.log` |

The RED run shows all three layers: the generic scan, the new port-specific assertion, and the
Qt 5 compile probe of the recorded translation unit.

```
tests/src/core/ScriptClockTest.cpp:408: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
tests/src/core/ScriptClockTest.cpp:415: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
tests/src/core/ScriptClockTest.cpp:408: source_bytes is read without jsonInteger()
tests/src/core/ScriptClockTest.cpp:415: source_bytes is read without jsonInteger()
.../ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
.../ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
FAIL: 5 Qt-5-incompatible QJsonValue::toInteger use(s)
```

GREEN CTest reports `100% tests passed out of 1`; the family run reports `100% tests passed
out of 5`.

## Completeness

Both first-party reads in `scheduleStateReportsUtf8Bytes` are converted, including the per-id
read and the `stateJson` scan. The only other consumer of the value is the adapter's own
`QT_VERSION >= 6` branch. The sibling Qt 5 JSON checks and the behaviour test pass in the same
family run. This branch changes no product code, save format, undo, command or audio path.

## Self-check

- Regression test fails when reverted and passes restored. With only the historical hunk
  reversed, `Qt5JsonIntegerCheck` exits 1 (CTest exits 8) and prints both Qt 5 compiler
  errors; restored, it exits 0. Logs: `checker-red-2.log`, `ctest-red-2.log`,
  `ctest-green-2.log`.
- Deterministic. The checker reads checked-in sources and compiles the recorded translation
  unit against Qt 5; there is no wall clock, sleep, thread, random value or network input. Its
  new fixture pair proves a direct `.toInteger()` read is flagged and `jsonInteger()` is not.
- Cause and complete. Both reads use the existing adapter for this exact Qt 5 contract; the
  adapter's other callers are unchanged and the direct-call source scan is clean.
- No collateral damage. Only a test script and this report change in this round. No saved
  project data, limit or runtime behaviour changes; the new assertion only constrains the two
  named `source_bytes` reads and does not reject other callers.
- ALREADY-FIXED evidence. `tests/src/core/ScriptClockTest.cpp:48,409,416` and
  `include/DeprecationHelper.h:104-120` show the fixed code path;
  `tests/CMakeLists.txt:9774-9777` registers the checker. The original `0a7d9ced99` commit is
  not an ancestor of HEAD, but its exact change is present at HEAD (the train's replay
  reconciliation); the RED step confirms this by reversing that hunk on top of HEAD.
- Sibling commands. This round changes no `ControlCommands*`, `ControlReversibilityTable*` or
  `ControlRegistry*` file. The sibling list printed by `submit_check.py` comes from train
  changes in the wide `base..HEAD` range, not from this test-only round, and none of those
  commands is affected.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 0
(`feature-check-2.log`):

```
feature-check kads-ci-9b3cdba8549c073f-port (base fa4a5f2e7, 655 changed files; logs in .../logs/feature-check-2)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Completion check

Before commit, `submit_check.py` reported one expected FAIL, "working tree clean", because the
checker and this report were not yet committed; every other row passed (`submit-check-1.log`):

```
PRE-SUBMIT CHECK for ci-9b3cdba8549c073f-port
  PASS  commits on the branch
  FAIL  working tree clean - M tests/Qt5JsonIntegerCheck.py
  PASS  report docs/qwen-fixes-2026-10/cluster-ci-9b3cdba8549c073f-port.md
  PASS  tests added or changed - ...
  PASS  new test files registered in tests/CMakeLists.txt
  PASS  gates (feature_check.py)
RESULT: FAIL - 1 item(s) to fix before submitting
```

After commit `f703a42689`, the clean-tree run exited 0 (`submit-check-2.log`):

```
PRE-SUBMIT CHECK for ci-9b3cdba8549c073f-port
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-ci-9b3cdba8549c073f-port.md
  PASS  tests added or changed - tests/Qt5JsonIntegerCheck.py, tests/Qt5QStringListSizeConstructorCheck.py, tests/clang-template-keyword-check.py
  PASS  new test files registered in tests/CMakeLists.txt
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

The six ASK items printed by that run are answered in `## Self-check` above.

## Other findings

None.
