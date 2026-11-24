ALREADY-FIXED

# Cluster ci-9b3cdba8549c073f-port - CI regression: build / linux-arm64

## Verdict

ALREADY-FIXED on this branch. The finding was real: the `linux-arm64` job is the Linux lane that
does not pass `-DWANT_QT6=ON` (`.github/workflows/build.yml:316-325`), so its build uses Qt 5.15
because `CMakeLists.txt:142` defaults `WANT_QT6` to `OFF`. The failing translation unit,
`tests/src/core/ScriptClockTest.cpp`, called `QJsonValue::toInteger`, a Qt 6-only member, and Qt 5
refused it with "class QJsonValue has no member named toInteger".

At this branch's HEAD (`9913d36d53`) both reads already use the sanctioned compatibility adapter:
`tests/src/core/ScriptClockTest.cpp:47-48` includes `DeprecationHelper.h`, and `:409` and `:416`
call `jsonInteger(...)`. The adapter at `include/DeprecationHelper.h:104-120` forwards to
`QJsonValue::toInteger` on Qt 6 and applies the same whole/in-range-`qint64` contract on Qt 5. The
regression is registered as CTest `Qt5JsonIntegerCheck` (`tests/CMakeLists.txt:9774-9777`), the
checker is `tests/Qt5JsonIntegerCheck.py`, and both the checker and the original report are
registered in `tests/fork-sources.txt:872,880`.

The train carried the repair over by reconciliation. `d9a26d06ad` is an ancestor of HEAD; it lists
`ci-repair-ci-9b3cdba8549c073f-0a7d9ced9980` (`bf74b7c952ac`) in
`docs/qwen-fixes-2026-10/sol-train-34-replay-reconciliation.md:9`, and the preservation paragraph at
`:25` states that ScriptClockTest uses the existing `jsonInteger` adapter with the same sentinel,
cleanup and UTF-8 assertions. The original `0a7d9ced99` commit is not itself an ancestor, but its
exact change is present at HEAD; the current two reads are the reconciled content.

## Reproduction and RED/GREEN evidence

Qt 5.15.19 is installed locally, so the checker's source scan and Qt 5 compile probe both run:

| Step | Command | Exit | Log |
|---|---|---:|---|
| Qt 5 toolchain | `pkg-config --modversion Qt5Core` | 0 | `env-qt5.log` (5.15.19) |
| CTest registered | `ctest -N -R '^Qt5JsonIntegerCheck$'` from `build/tests` | 0 | `ctest-list-1.log` (Test #1027) |
| Save the historical fix hunk | `git show 0a7d9ced99 -- tests/src/core/ScriptClockTest.cpp > logs/revert-fix.patch` | 0 | `proof-4b.log` |
| Reverse-check | `git apply -R --check logs/revert-fix.patch` | 0 | `proof-4b.log` |
| Reintroduce the defect | `git apply -R logs/revert-fix.patch` | 0 | `proof-4b.log` |
| RED without the fix | `cd build/tests && ctest -R '^Qt5JsonIntegerCheck$' --output-on-failure` | **8** | `proof-4b.log` |
| Restore the fix | `git apply logs/revert-fix.patch` | 0 | `proof-4b.log` |
| GREEN with the fix | `cd build/tests && ctest -R '^Qt5JsonIntegerCheck$' --output-on-failure` | **0** | `proof-4b.log` |
| Build the behaviour test | `cmake --build build --target ScriptClockTest -j4` | 0 | `build-scriptclock-1.log` |
| Behaviour plus checker family | `cd build/tests && ctest -R '^(Qt5JsonIntegerCheck|ScriptClockTest|Qt5QJsonToIntegerCheck|Qt5QJsonValueIntegerCheck|Qt5JsonValueIntegerCheck)$' --output-on-failure -j2 --timeout 900` | 0 (5/5) | `test-family-1.log` |

The RED run printed the two source findings and the actual Qt 5 compiler errors:

```
tests/src/core/ScriptClockTest.cpp:408: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
tests/src/core/ScriptClockTest.cpp:415: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
tests/src/core/ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
FAIL: 3 Qt-5-incompatible QJsonValue::toInteger use(s)
```

## Completeness

Both first-party reads in `scheduleStateReportsUtf8Bytes` are converted, including the per-id read
and the `stateJson` scan; no direct `toInteger` call remains in first-party code outside the
adapter's own `QT_VERSION >= 6` branch. The sibling Qt 5 JSON checks
(`Qt5QJsonToIntegerCheck`, `Qt5QJsonValueIntegerCheck`, `Qt5JsonValueIntegerCheck`) and the
behaviour test all pass in the same family run. There is no product, save-format, undo, command or
audio-path change: the repair is confined to a test source, its checker, its registration and the
report.

## Self-check

* **Regression test fails with the fix reverted and passes restored.** With the historical fix
  reversed, `Qt5JsonIntegerCheck` exits 8 and prints both Qt 5 compile errors; restored, it exits 0.
  Commands, codes and logs are in `proof-4b.log`.
* **Deterministic.** The checker reads checked-in sources and compiles the recorded translation unit
  against Qt 5; there is no wall clock, sleep, thread, random value or network input. Its own
  self-test fixtures prove an unguarded `.toInteger()` is flagged while `jsonInteger(...)` and a
  Qt 6-guarded call are not.
* **Cause and complete.** Both reads use the adapter that already exists for this exact Qt 5
  contract; the adapter's other callers are unchanged and the direct-call source scan is clean.
* **No collateral damage.** Only a test source, its checker, registration and this report are in
  scope for the cluster. No saved project data, limit or runtime behaviour changes.
* **ALREADY-FIXED evidence.** `tests/src/core/ScriptClockTest.cpp:47-48,409,416` and
  `include/DeprecationHelper.h:104-120` show the fixed code path; `tests/CMakeLists.txt:9774-9777`
  registers the regression. The reconciled commit `d9a26d06ad` is an ancestor of HEAD, and
  `docs/qwen-fixes-2026-10/sol-train-34-replay-reconciliation.md:9,25` records the preservation.
* **Sibling-command ask.** This cluster changes no `ControlCommands*` or
  `ControlReversibilityTable*` file; any such files in the pre-submit diff range come from other
  lanes in the train, not from this repair, and none is affected by a test-only Qt 5 conversion.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 0
(`feature-check-1.log`):

```
feature-check kads-ci-9b3cdba8549c073f-port (base fa4a5f2e7, 654 changed files; logs in .../logs/feature-check-1)
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

The completion check after the final commit is recorded below.

## Completion check

The pre-commit `submit_check.py` run had one expected FAIL, "working tree clean", because the report
and its registration were not yet committed; every other row passed and all ASK items are answered
in `## Self-check` above (`submit-check-1.log`). After commit `55f349bdbe`, the clean-tree run
exited 0 (`submit-check-2.log`):

```
PRE-SUBMIT CHECK for ci-9b3cdba8549c073f-port
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-ci-9b3cdba8549c073f-port.md
  PASS  tests added or changed - tests/Qt5QStringListSizeConstructorCheck.py, tests/clang-template-keyword-check.py, tests/control-audio-test-tone.py
  PASS  new test files registered in tests/CMakeLists.txt
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

The ASK items printed by that run are answered in `## Self-check`. A final re-run after this report
update is recorded as `submit-check-3.log`.

## Other findings

None.
