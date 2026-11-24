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

No product or test source changed in this port: the fix is inherited, and this
job only adds its own regression check (round 3, below), the registrations for
it, and this report.

## Round 3: the port-local regression check

Round 2 was sent back because its proof had no `test`/`regression-test`
artifact naming a test file the branch adds or changes. This round adds one,
for exactly this cluster, with both halves of the defect asserted in one
deterministic process:

- `tests/ScriptClockQt5JsonIntegerPortCheck.py` (new, 222 lines):
  - **both sites**: reads `tests/src/core/ScriptClockTest.cpp` and requires
    exactly the two `source_bytes` reads (line 409 `scheduleState(id)` and line
    416 `stateJson()`), each through `jsonInteger(value, -1)`. A reverted site, a
    half-reverted pair, a missing site and a changed default are all findings.
  - **the hosted compiler input**: recompiles the real
    `tests/src/core/ScriptClockTest.cpp` `-fsyntax-only` through this build's
    `build/compile_commands.json` entry, retargeted at the Qt 5.15 headers with
    the Qt 5 moc standing in for the Qt 6 autogen output. It drives the existing
    probe in `tests/Qt5QJsonValueIntegerCheck.py` (imported, so the two cannot
    drift) and its Qt 5 bad/good fixtures (Qt 5 must reject `toInteger` and
    accept `jsonInteger`).
  - `--self-test` feeds the site scan the reverted, half-reverted and
    wrong-default shapes (each must be a finding) and the portable pair (must be
    clean).
  - exit 0 = clean, 1 = finding, 2 = usage, 77 = skipped (no Qt 5 development
    tree or moc; CTest `SKIP_RETURN_CODE 77`). Both halves always run, so a
    missed site cannot hide the compile result.
- Registration: `tests/CMakeLists.txt` (new `add_test` next to the other Qt 5
  JSON checks, with `TIMEOUT 300` and `SKIP_RETURN_CODE 77`),
  `tests/fork-sources.txt` (the new file) and `tests/upstream-modifications.txt`
  (the `tests/CMakeLists.txt` registration line).

| step | command | EXIT | log |
|---|---|---|---|
| branch build + reconfigure | `cmake --build build --target zene -j4` | 0 | `build-1.log` |
| self-test (fixtures) | `python3 tests/ScriptClockQt5JsonIntegerPortCheck.py --self-test` | 0 | `test-1-selftest.log` |
| GREEN direct | `python3 tests/ScriptClockQt5JsonIntegerPortCheck.py --root . --build-dir build` | 0 | `test-2-direct-green.log` |
| GREEN ctest | `cd build/tests && ctest -R '^ScriptClockQt5JsonIntegerPortCheck$' --output-on-failure` | 0 | `test-3-ctest-green.log` |
| RED ctest (bug put back) | same ctest, after both sites returned to `.toInteger(-1)` | 8 | `test-5-ctest-red.log` |
| RED Qt 5 probe only | `python3 tests/Qt5QJsonValueIntegerCheck.py --root . --build-dir build` on the reverted tree | 1 | `test-6-qt5-probe-red.log` |
| RED direct, both halves | `python3 tests/ScriptClockQt5JsonIntegerPortCheck.py --root . --build-dir build` on the reverted tree | 1 | `test-7-direct-red-both-halves.log` |
| restore | `git apply -R $LOGS/bug-reintroduced.patch` | 0 | `bug-reintroduced.patch` |
| GREEN direct, restored | `python3 tests/ScriptClockQt5JsonIntegerPortCheck.py --root . --build-dir build` | 0 | `test-8-direct-green-restored.log` |
| GREEN ctest family | `cd build/tests && ctest -R '^(ScriptClockQt5JsonIntegerPortCheck\|ScriptClockTest\|Qt5QJsonToIntegerCheck\|Qt5QJsonValueIntegerCheck\|Qt5JsonIntegerCheck\|Qt5JsonValueIntegerCheck)$' --output-on-failure` | 0 (6/6) | `test-9-ctest-family-green.log` |

`bug-reintroduced.patch` is the RED state: both `jsonInteger(...)` reads put back
to the historical `.toInteger(-1)` (2 lines changed). `git diff --stat --
tests/src/core/ScriptClockTest.cpp` was empty after `git apply -R`.

The RED run shows both halves of the same hosted failure in one process:

```
tests/src/core/ScriptClockTest.cpp: line 409: reads source_bytes through QJsonValue::toInteger, which Qt 5.15 does not declare: ...toInteger(-1);
tests/src/core/ScriptClockTest.cpp: line 416: reads source_bytes through QJsonValue::toInteger, which Qt 5.15 does not declare: ...toInteger(-1);
tests/src/core/ScriptClockTest.cpp:409:92: error: ‘class QJsonValue’ has no member named ‘toInteger’
tests/src/core/ScriptClockTest.cpp:416:69: error: ‘class QJsonValue’ has no member named ‘toInteger’
```

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
| Normal path | `ScriptClockQt5JsonIntegerPortCheck` on the real source and TU, EXIT 0 (`test-2`, `test-3`, `test-8`, `test-9`); Qt 5.15.19 compile probe of the real `ScriptClockTest.cpp`, EXIT 0 (`test-5`, `test-10`); Qt 6 `ScriptClockTest` ctest, 1/1 (`test-4`, `test-11`) |
| Failure path | Reinserted historical `.toInteger(-1)` at both sites: new check EXIT 1 (`test-7`) and ctest EXIT 8 (`test-5`); Qt 5 probe EXIT 1 (`test-7` round 2); source scan EXIT 1 (`test-8` round 2) |
| Partial repair | `--self-test` half-reverted fixture must be a finding; the port check fails if either site leaves `jsonInteger(value, -1)` (`test-1-selftest.log`, EXIT 0 on the fixtures themselves) |
| Exhaustion / reconfiguration | Not applicable: the change reads one JSON value; there is no queue, cap, persistent state or reconfiguration path |
| Audio-thread constraints | Not applicable: no `processImpl`/`playImpl`/mixer/render code changed; this port changes no product code |
| Supported-platform build | Qt 5.15.19 (the hosted job's compiler input) compiles the real TU; the configured Qt 6 build also compiles and runs it |
| Old projects / saved data | Not applicable: no serialization, schema, command or product behaviour changed |

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it (round 3,
  `tests/ScriptClockQt5JsonIntegerPortCheck.py`).** RED: direct EXIT **1**
  (`test-7-direct-red-both-halves.log`) and ctest EXIT **8**
  (`test-5-ctest-red.log`) after both sites were returned to `.toInteger(-1)`
  (`bug-reintroduced.patch`). GREEN: direct EXIT **0**
  (`test-8-direct-green-restored.log`) and ctest EXIT **0**
  (`test-3-ctest-green.log`, `test-9-ctest-family-green.log`, 6/6) after
  `git apply -R`. Same test, same command, opposite exit codes.
- **The regression test FAILS with the fix reverted and PASSES with it (round 2,
  the inherited checks).** RED:
  `Qt5QJsonValueIntegerCheck.py` EXIT **1** (`test-7-red-qt5-real-tu.log`) and
  `Qt5QJsonToIntegerCheck.py` EXIT **1** (`test-8-red-scan.log`), plus ctest EXIT
  **8** (`test-6-red-ctest-scan.log`), after the historical `.toInteger(-1)`
  spelling was put back. GREEN: EXIT **0** for both probes (`test-9`,
  `test-10`) and ctest EXIT **0** for the `ScriptClockTest` +
  `Qt5QJsonToIntegerCheck` family (`test-11`). `git apply -R` of the captured
  bug patch restored the tree; `git status --porcelain` then showed no source
  change.
- **Deterministic.** The new check has no sleeps, clocks, threads, network or
  temporary state that can come out differently twice: it reads
  `tests/src/core/ScriptClockTest.cpp`, matches fixed strings, and runs
  `c++ -fsyntax-only` on the real translation unit with the flags recorded in
  `build/compile_commands.json` retargeted at the installed Qt 5.15 headers. Its
  fixtures are fixed strings written into a `TMPDIR` temporary directory. The
  round-2 instruments are the same: the source scan is a pure read of 4308
  repository files with fixed strings, and the Qt 5 probe is a compile.
- **The fix is at the cause and complete.** The scanner reports 0 first-party
  `QJsonValue::toInteger` calls over all 4308 first-party sources. The only
  remaining occurrence is the guarded Qt 6 branch of the adapter at
  `include/DeprecationHelper.h:109`, which the scanner intentionally exempts.
  The two original call sites are both converted (`ScriptClockTest.cpp:409` and
  `:416`), and the new check asserts both of them by name and by line, so a
  repair that converts only one is red: `--self-test` proves that with the
  half-reverted fixture, and the RED run above shows both sites flagged. There
  is no other first-party caller to update.
- **No collateral damage.** No product code, save format, schema, command or
  limit changed in this port. On Qt 6 `jsonInteger` calls `toInteger(default)`,
  so the value and the `-1` sentinel are exactly as before; on Qt 5 it is the
  existing checked-double adapter already used by other tests and product code.
- **ALREADY-FIXED evidence.** `d9a26d06ad` is an ancestor of HEAD and its
  `ScriptClockTest.cpp` / `Qt5QJsonToIntegerCheck.py` content is unchanged at
  HEAD; current lines 409 and 416 use `jsonInteger`. See
  `logs/evidence-1-already-fixed.log`.
- **Sibling commands.** This port changes no command file (`git diff` for this
  job touches only this report, the new check, `tests/fork-sources.txt`,
  `tests/upstream-modifications.txt` and `tests/CMakeLists.txt`), so `app.about`,
  `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`,
  `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks` and `arrangement.get_state` are unaffected. The
  broader `050/m0-train-35..HEAD` baseline contains earlier train changes, not
  this cluster's repair.
- **Registration.** The new check is listed in `tests/fork-sources.txt`, its
  `add_test` is in `tests/CMakeLists.txt` (recorded in
  `tests/upstream-modifications.txt`), and the file is named after its CTest so
  `ctest -R '^ScriptClockQt5JsonIntegerPortCheck$'` finds it
  (`test-3-ctest-green.log`). The existing scanner and Qt 5 probe were already
  registered by the original repair.
- **Realtime, undo and commands.** No audio path, undo action or control command
  is touched; the diff is one Python check, its registrations and this report.
  No allocation, lock, wait or log is added anywhere that runs per period.

## Round 3 outcome against the send-back

The send-back said the proof had no `test`/`regression-test` artifact naming a
test file this branch adds or changes. It now does:
`ScriptClockQt5JsonIntegerPortCheck.py` is added by this branch, registered, run
green directly (EXIT 0) and through CTest (EXIT 0), and red with the historical
spelling restored (EXIT 1 direct, EXIT 8 through CTest). See
`logs/proof.json`.

## Other findings

None.
