ALREADY-FIXED

# cluster-ci-e58078851126eac9-port — Port: CI regression: build / linux-x86_64

ci-e58078851126eac9-port: ALREADY-FIXED

## What the original cluster was

The hosted Qt 5.15 `build / linux-x86_64` job at
`dec3f1d45c62b26bce955875521780db173a0228` stopped compiling
`tests/src/core/ScriptClockTest.cpp` because it read a JSON number with
`QJsonValue::toInteger()`, a member Qt 6 added:

```
tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
```

The portable read is `jsonInteger(value, defaultValue)` in
`include/DeprecationHelper.h:104-120`: Qt 6 keeps `toInteger(defaultValue)` and
Qt 5 does the equivalent checked whole-number conversion of the value's double.

## Evidence that this branch already has the fix

- `tests/src/core/ScriptClockTest.cpp:409` and `:416` now read both
  `source_bytes` values with `jsonInteger(..., -1)`. There is no first-party
  `QJsonValue::toInteger` call left; `tests/Qt5QJsonToIntegerCheck.py` scans
  4308 first-party C/C++ files and reports 0 findings.
- The original repair is commit `c416d82c3c` on `bugs/qwen-ci-e58078851126eac9`.
  It reached this line in `d9a26d06ad` ("Reconciled thirteen overlapping CI
  repairs..."), which is an ancestor of HEAD (`git merge-base --is-ancestor
  d9a26d06ad HEAD` exits 0). `git diff d9a26d06ad HEAD` for
  `tests/Qt5QJsonToIntegerCheck.py`, `tests/src/core/ScriptClockTest.cpp` and
  `include/DeprecationHelper.h` is empty, so the merged repair is byte-identical
  to what this branch still carries.
- Both regression checks are registered:
  `tests/CMakeLists.txt:9770-9773` registers `Qt5QJsonToIntegerCheck` (the
  source scan) and `tests/CMakeLists.txt:9778-9781` registers
  `Qt5QJsonValueIntegerCheck` (the real translation unit recompiled against
  Qt 5). `tests/Qt5QJsonToIntegerCheck.py:1-27` names this exact cluster and
  records the historical `build / linux-x86_64` error.

No product or test source changed in this port. The only files added or changed
by this job are this report and its `tests/fork-sources.txt` registration.

## Regression proof

The existing `Qt5QJsonToIntegerCheck` scan and the existing
`Qt5QJsonValueIntegerCheck` Qt 5 compile probe were run in both states. To make
the RED state, the two `jsonInteger(...)` reads in
`tests/src/core/ScriptClockTest.cpp` were temporarily changed back to the
historical `.toInteger(-1)` spelling; `bug-reintroduced.patch` captures that
21-line diff. `git apply -R bug-reintroduced.patch` restored the branch fix.

| step | command | EXIT | log |
|---|---|---|---|
| branch build | `cmake --build build --target zene -j4` | 0 | `build-1.log` |
| scanner self-test | `python3 tests/Qt5QJsonToIntegerCheck.py --self-test` | 0 | `test-1-selftest.log` |
| GREEN scan | `python3 tests/Qt5QJsonToIntegerCheck.py --root .` | 0 | `test-2-green-scan.log` |
| GREEN ctest scan | `cd build/tests && ctest -R '^Qt5QJsonToIntegerCheck$' --output-on-failure` | 0 | `test-3-green-ctest.log` |
| GREEN Qt 5 real TU | `python3 tests/Qt5QJsonValueIntegerCheck.py --root . --build-dir build` | 0 | `test-5-qt5-real-tu-green.log` |
| GREEN ScriptClock build | `cmake --build build --target ScriptClockTest -j4` | 0 | `build-2-scriptclock.log` |
| GREEN ScriptClock ctest | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 0 | `test-4-green-scriptclock-ctest.log` |
| RED ctest scan | same `ctest -R '^Qt5QJsonToIntegerCheck$'` after reinserting the bug | 8 | `test-6-red-ctest-scan.log` |
| RED Qt 5 real TU | same `Qt5QJsonValueIntegerCheck.py` after reinserting the bug | 1 | `test-7-red-qt5-real-tu.log` |
| RED scan | same `Qt5QJsonToIntegerCheck.py --root .` after reinserting the bug | 1 | `test-8-red-scan.log` |
| restore | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/ci-e58078851126eac9-port.r2/logs/bug-reintroduced.patch` | 0 | `bug-reintroduced.patch` |
| GREEN scan, restored | `python3 tests/Qt5QJsonToIntegerCheck.py --root .` | 0 | `test-9-green-scan-restored.log` |
| GREEN Qt 5 real TU, restored | `python3 tests/Qt5QJsonValueIntegerCheck.py --root . --build-dir build` | 0 | `test-10-green-qt5-real-tu-restored.log` |
| GREEN ScriptClock rebuild | `cmake --build build --target ScriptClockTest -j4` | 0 | `build-3-scriptclock-restored.log` |
| GREEN ctest family | `cd build/tests && ctest -R '^(ScriptClockTest|Qt5QJsonToIntegerCheck)$' --output-on-failure` | 0 | `test-11-green-ctest-family.log` |

The RED Qt 5 probe is the actual hosted failure:

```
tests/src/core/ScriptClockTest.cpp:409:92: error: ‘class QJsonValue’ has no member named ‘toInteger’
tests/src/core/ScriptClockTest.cpp:416:69: error: ‘class QJsonValue’ has no member named ‘toInteger’
```

With the branch fix restored, the same probe prints
`PASS: tests/src/core/ScriptClockTest.cpp compiles against Qt 5 and reads JSON
integers through jsonInteger` (EXIT 0).

## Test matrix

| Path | Covered by |
|---|---|
| Normal path | Qt 5.15.19 compile probe of the real `ScriptClockTest.cpp`, EXIT 0 (`test-5`, `test-10`); Qt 6 `ScriptClockTest` ctest, 1/1 (`test-4`, `test-11`) |
| Failure path | Reinserted historical `.toInteger(-1)`: Qt 5 probe EXIT 1 (`test-7`); source scan EXIT 1 (`test-8`); ctest EXIT 8 (`test-6`) |
| Exhaustion / reconfiguration | Not applicable: the change reads one JSON value; there is no queue, cap, persistent state or reconfiguration path |
| Audio-thread constraints | Not applicable: no `processImpl`/`playImpl`/mixer/render code changed; this port changes no product code |
| Supported-platform build | Qt 5.15.19 (the hosted job's compiler input) compiles the real TU; the configured Qt 6 build also compiles and runs it |
| Old projects / saved data | Not applicable: no serialization, schema, command or product behaviour changed |

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** RED:
  `Qt5QJsonValueIntegerCheck.py` EXIT **1** (`test-7-red-qt5-real-tu.log`) and
  `Qt5QJsonToIntegerCheck.py` EXIT **1** (`test-8-red-scan.log`), plus ctest EXIT
  **8** (`test-6-red-ctest-scan.log`), after the historical `.toInteger(-1)`
  spelling was put back. GREEN: EXIT **0** for both probes (`test-9`,
  `test-10`) and ctest EXIT **0** for the `ScriptClockTest` +
  `Qt5QJsonToIntegerCheck` family (`test-11`). `git apply -R` of the captured
  bug patch restored the tree; `git status --porcelain` then showed no source
  change.
- **Deterministic.** The source scan is a pure read of 4308 repository files
  with fixed strings and no sleeps, clocks, threads or network. The Qt 5 probe
  runs `c++ -fsyntax-only` on the real translation unit with the recorded
  compile flags retargeted at the installed Qt 5.15.19 headers; it has no
  timing or race input. The self-test fixtures are two fixed strings.
- **The fix is at the cause and complete.** The scanner reports 0 first-party
  `QJsonValue::toInteger` calls over all 4308 first-party sources. The only
  remaining occurrence is the guarded Qt 6 branch of the adapter at
  `include/DeprecationHelper.h:109`, which the scanner intentionally exempts.
  The two original call sites are both converted (`ScriptClockTest.cpp:409` and
  `:416`); there is no other first-party caller to update.
- **No collateral damage.** No product code, save format, schema, command or
  limit changed in this port. On Qt 6 `jsonInteger` calls `toInteger(default)`,
  so the value and the `-1` sentinel are exactly as before; on Qt 5 it is the
  existing checked-double adapter already used by other tests and product code.
- **ALREADY-FIXED evidence.** `d9a26d06ad` is an ancestor of HEAD and its
  `ScriptClockTest.cpp` / `Qt5QJsonToIntegerCheck.py` content is unchanged at
  HEAD; current lines 409 and 416 use `jsonInteger`. See
  `logs/evidence-1-already-fixed.log`.
- **Sibling commands.** This port changes no command file (`git diff` for this
  job touches only this report and `tests/fork-sources.txt`), so `app.about`,
  `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`,
  `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks` and `arrangement.get_state` are unaffected. The
  broader `050/m0-train-35..HEAD` baseline contains earlier train changes, not
  this cluster's repair.
- **Registration.** The new report is listed in `tests/fork-sources.txt`; the
  existing scanner and Qt 5 probe were already registered in
  `tests/CMakeLists.txt` and `tests/fork-sources.txt` by the original repair.
- **Realtime, undo and commands.** No audio path, undo action or control
  command is touched; the diff is documentation and a registration list entry.

## Other findings

None.
