FIXED

# cluster ci-9b3cdba8549c073f - CI regression: build / linux-arm64

## Verdict

FIXED. The finding is REAL on this branch. At the pinned train commit
`dec3f1d45c62b26bce955875521780db173a0228` the hosted `linux-arm64` job (run
43681793608, job 132713785107) stopped at 59% while compiling
`tests/src/core/ScriptClockTest.cpp`:

```
ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
gmake[2]: *** [tests/CMakeFiles/ScriptClockTest.dir/build.make:99: ...] Error 1
```

The classification is **test-source portability**, not infrastructure and not product
code: the product side already has the portable adapter, and the failing translation
unit is the test.

Why this job and not the others: `linux-arm64` is the Linux lane that does **not** pass
`-DWANT_QT6=ON`. Its `CMAKE_OPTS` (`.github/workflows/build.yml:316-326`) omit it, so
`CMakeLists.txt:142` (`option(WANT_QT6 ... OFF)`) leaves the job on Qt 5.15. The other
Linux/macOS lanes configure Qt 6, where the line compiles. Ubuntu 24.04 arm64's Qt 5
`QJsonValue` declares `toInt`/`toDouble` only (`/usr/include/qt/QtCore/qjsonvalue.h:114-115`
on this box's Qt 5.15.19; the hosted job uses the same 5.15 generation), so
`QJsonValue::toInteger` - a Qt 6 member - does not exist there.

The test was added by `6bb874de58` (2026-10-15 05:39) and the tree's Qt 5 sweep
`5d0e2ca80e` (10:43, "Qt 5 paths for the Qt 6-only APIs the train-5 fixes added") updated
`AafImportTest`, `ArrangingCatchUpTest`, `ControlBounceRenderTest`, `ControlSuccessPathsTest`
and `CommandFormTest` to the adapter but missed this newer file. `d7ac01db10` then made
the adapter's Qt 5 branch match Qt 6's integer contract. The direct call survived until
the hosted build caught it.

## Cause and fix

`include/DeprecationHelper.h:104-120` already defines the sanctioned adapter:

```cpp
inline qint64 jsonInteger(const QJsonValue& value, qint64 defaultValue = 0)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	return value.toInteger(defaultValue);
#else
	... Qt 5 double -> whole, in-range qint64, else defaultValue ...
#endif
}
```

`tests/src/core/ScriptClockTest.cpp` did not use it. The fix is the smallest one at the
cause - include the adapter and route both reads through it:

```diff
 #include "ControlReversibility.h"
+#include "DeprecationHelper.h"
 #include "Engine.h"
...
-	const qint64 perId = clock.scheduleState(id).value(QStringLiteral("source_bytes")).toInteger(-1);
+	const qint64 perId = jsonInteger(clock.scheduleState(id).value(QStringLiteral("source_bytes")), -1);
...
-		inState = one.value(QStringLiteral("source_bytes")).toInteger(-1);
+		inState = jsonInteger(one.value(QStringLiteral("source_bytes")), -1);
```

On Qt 6 `jsonInteger` calls the identical `QJsonValue::toInteger(defaultValue)`, so the
test's behaviour and the value it reads are byte-for-byte unchanged; on Qt 5 the adapter
applies the same whole/in-range rule (commit `d7ac01db10`). No product behaviour, save
format, undo step, command or audio-path code is touched, and no new control command is
added.

## Completeness

Every direct `toInteger` call in first-party sources was checked:
`grep -rn toInteger --include=*.cpp --include=*.h src include tests plugins` (vendored
CarlaBase excluded) returns only the two ScriptClockTest lines and the adapter's own
guarded implementation. After the fix the only remaining occurrence is inside
`#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)` in `DeprecationHelper.h`. The adapter's
other callers (e.g. `src/core/ControlCommandsRecordTools.cpp:106`,
`src/gui/BrowserResults.cpp:104-105`, `tests/src/core/AafImportTest.cpp:527-528`) already
use it and are unaffected.

## Regression: tests/Qt5JsonIntegerCheck.py (ctest `Qt5JsonIntegerCheck`)

Two deterministic halves, no timing, sleeps, threads or injected clock:

1. **Source scan** of `src`, `include`, `tests/src` and `plugins` (vendored trees such as
   `src/3rdparty`, `CarlaBase/carla`, `Sfizz`, `rtneural` are excluded) for a
   `.toInteger(`/`->toInteger(` call outside an `#if QT_VERSION >= 6...` branch. It reads
   sources only, so it is red on this Qt 6 host where the defect compiles - the property
   the hosted Qt 6 lanes need. Self-test fixtures prove the scanner flags the unguarded
   call and does not flag `jsonInteger(...)` or a Qt 6-guarded call.
2. **Compile probe** (when pkg-config finds Qt 5): the recorded compile command of
   `tests/src/core/ScriptClockTest.cpp` from `build/compile_commands.json` is retargeted at
   the Qt 5 headers, with a Qt 5 `moc` generated stand-in for the Qt 6 moc file, and
   compiled `-fsyntax-only`. Two controls run first: Qt 5 must reject a known-bad
   `QJsonValue::toInteger` probe and accept a known-good `toInt` probe; without a Qt 5
   tree the probe reports itself skipped, never passed.

Registered in `tests/CMakeLists.txt` beside `Qt5QListApiCheck` and in
`tests/fork-sources.txt`.

## Evidence

The assigned log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-9b3cdba8549c073f.r1/logs`
refuses writes in this session (`touch` there returns EACCES although the directory is
`drwxr-xr-x` and owned by this user - only the worktree and its `build/` are writable, the
same condition the previous CI cluster recorded). Every log below is therefore in
`build/fix-logs/` inside the worktree.

| step | command | exit | log |
| --- | --- | --- | --- |
| initial target build | `cmake --build build --target ScriptClockTest -j 8` | 0 | `build-1.log` |
| hosted failure reproduced (manual Qt 5 retarget of the recorded command, before the fix) | `c++ ... Qt 5 flags ... -fsyntax-only tests/src/core/ScriptClockTest.cpp` | 1 (`toInteger` both lines) | `repro-qt5-red.log` |
| RED, regression script | `python3 tests/Qt5JsonIntegerCheck.py --root . --build-dir build` | 1 | `qt5json-red.log` |
| GREEN, regression script | same after the fix | 0 | `qt5json-green.log` |
| rebuild (Qt 6 target) | `cmake --build build --target ScriptClockTest -j 8` | 0 | `build-2-green.log` |
| behavior test | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 0 (1/1) | `ctest-scriptclock-green.log` |
| both ctests | `cd build/tests && ctest -R '^(Qt5JsonIntegerCheck\|ScriptClockTest)$' --output-on-failure` | 0 (2/2) | `ctest-both-green.log` |
| family | `cd build/tests && ctest -R '^(Qt5\|MocQt5)' --output-on-failure -j2` | 0 (11/11) | `ctest-qt5-family.log` |
| zene untouched | `cmake --build build --target zene -j 8` | 0 | `build-zene-noop.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `feature-check-4.log` |

The exact RED/GREEN proof with the fix patch (4b). This defect is in the test translation
unit itself, so there is no product file to revert; the negative control reverts the fix
hunk while leaving the regression check in place:

```
git diff -- tests/src/core/ScriptClockTest.cpp > build/fix-logs/fix.patch   # exit 0 (29 lines)
git apply -R build/fix-logs/fix.patch                                       # exit 0
cmake --build build --target ScriptClockTest -j 8                           # exit 0
cd build/tests && ctest -R '^Qt5JsonIntegerCheck$' --output-on-failure      # exit 8 (1 of 1 failed)
  ScriptClockTest.cpp:408: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
  ScriptClockTest.cpp:415: QJsonValue::toInteger is Qt 6 only; use jsonInteger()
  ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
  ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
git apply build/fix-logs/fix.patch                                          # exit 0
cmake --build build --target ScriptClockTest -j 8                           # exit 0
cd build/tests && ctest -R '^(Qt5JsonIntegerCheck|ScriptClockTest)$' \
  --output-on-failure                                                       # exit 0 (2 of 2 passed)
```

Logs: `build-3-reverted.log`, `ctest-red-reverted.log`, `build-4-restored.log`,
`ctest-green-restored.log`; the same proof was repeated after the checker's complexity
split (`build-5-reverted-final.log`, `ctest-red-reverted-final.log` exit 8;
`build-6-restored-final.log`, `ctest-green-restored-final.log` exit 0, 2/2).

Files committed: `tests/src/core/ScriptClockTest.cpp`, `tests/Qt5JsonIntegerCheck.py`,
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, this report.

## Self-check

* **Regression fails with the fix reverted and passes with it.** Reverted:
  `ctest -R '^Qt5JsonIntegerCheck$'` -> exit 8 (1/1 failed), with both source findings and
  both Qt 5 compile errors. Restored: exit 0 (2/2 passed). Logs and commands above.
* **Deterministic.** The check uses only checked-in sources and the recorded compile line;
  no wall clock, sleep, thread, random value or network. The compile probe's inputs are
  the sources plus `build/compile_commands.json`; its Qt 5 controls make a false pass
  impossible, and no Qt 5 tree is reported as skipped rather than passed.
* **Cause and completeness.** Both direct calls in this file are converted to the existing
  adapter; the only other first-party occurrence is the adapter's own Qt 6-guarded body.
  The adapter's contract is the one `d7ac01db10` established: the Qt 5 branch returns the
  default for anything not a whole, in-range integer, exactly as Qt 6's `toInteger` does.
  No sibling caller needs a change.
* **No collateral damage.** The change is confined to a test source, a new test script,
  its CMake registration and the manifest. No product code, project data, undo step,
  command schema or audio-thread path changes. Existing Qt 6 behaviour is identical
  because `jsonInteger` forwards to `toInteger` there.
* **ALREADY-FIXED / FALSE-POSITIVE?** No. The cited lines compile on Qt 6 and fail on
  Qt 5; the hosted log at
  `run-43681793608-job-132713785107.log:6541-6549` has the identical diagnostics, and the
  local Qt 5 compile reproduces them. The fix is a real portability change, not a waiver.
