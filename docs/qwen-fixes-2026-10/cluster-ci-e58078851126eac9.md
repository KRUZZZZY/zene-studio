FIXED - cluster ci-e58078851126eac9: ScriptClockTest used QJsonValue::toInteger, a Qt 6-only member, and stopped the hosted Qt 5 build

Verdict: FIXED (REAL). The reported line is exactly the defect on this branch: the
Qt 5.15 build of `tests/src/core/ScriptClockTest.cpp` fails at the two
`toInteger(-1)` reads, and the hosted `build / linux-x86_64` job is a Qt 5 job.
The product code is not wrong; the test translation unit called an API that only
Qt 6 declares. The fix is the portable adapter the tree already carries.

## The reported failure

Hosted CI, train 050/m0-train-32, commit
`dec3f1d45c62b26bce955875521780db173a0228` (this checkout's HEAD), job
`run-43681793608-job-132713785008`, step Build:

```
tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
  408 | const qint64 perId = clock.scheduleState(id).value(QStringLiteral("source_bytes")).toInteger(-1);
tests/src/core/ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
  415 |     inState = one.value(QStringLiteral("source_bytes")).toInteger(-1);
gmake[2]: *** [tests/CMakeFiles/ScriptClockTest.dir/build.make:99: .../ScriptClockTest.cpp.o] Error 1
```

The job is Qt 5: `.github/workflows/build.yml:158` sources
`/opt/qt5*/bin/qt5*-env.sh` and passes no `-DWANT_QT6=ON` (the Qt 6 jobs pass it
explicitly, e.g. `quality-gates.yml:157`). `CMakeLists.txt:373` still declares a
Qt 5.15 minimum, so Qt 5 is a supported build configuration.

The two reads at `tests/src/core/ScriptClockTest.cpp:408,415` are the only
first-party `toInteger` call sites (`rg -n 'toInteger'`; the other hits are the
guarded adapter and vendored Carla/JUCE). `QJsonValue::toInteger` was added in
Qt 6. The repository's own portable read is
`include/DeprecationHelper.h:104-123`:

```cpp
inline qint64 jsonInteger(const QJsonValue& value, qint64 defaultValue = 0)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	return value.toInteger(defaultValue);
#else
	// checked whole-number double conversion
...
```

That adapter is already used by product code (`src/gui/BrowserResults.cpp:104`,
`src/core/ControlCommandsRecordTools.cpp:106`) and by the control-suite tests
(`ControlSuccessPathsTest.cpp:308`, `ArrangingCatchUpTest.cpp:153`, ...). The
test was simply the one new caller that used the raw Qt 6 member.

## Reproduction on Qt 5

A separate Qt 5 build tree was configured inside the worktree (`build-qt5`,
git-ignored by `/build*/`) with the box's Qt 5.15.19 and the distro Qt5Svg
package extracted under the local dependency root (this box's system Qt 5 has no
Svg module):

```
/usr/bin/cmake -S . -B build-qt5 -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTARGET_UARCH=none -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON \
  -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_QT6=OFF \
  -DQt5Svg_DIR=/home/kruzzzzy/.local/zene-deps/qt5svg-root/usr/lib/cmake/Qt5Svg
```

Exit 0; the configure log says `Experimental Qt6 support : Disabled`. With the
fix reversed, building the target reproduces the CI error byte-for-byte:

```
/usr/bin/cmake --build build-qt5 --target ScriptClockTest -j8
...
../../tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member named 'toInteger'
../../tests/src/core/ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member named 'toInteger'
make[3]: *** [tests/CMakeFiles/ScriptClockTest.dir/build.make:99: .../ScriptClockTest.cpp.o] Error 1
```

Exit 2 (`build-5-qt5-red-reversed.log`). With the fix restored the same target
builds (exit 0, `build-6-qt5-green-restored.log`) and
`ctest -R '^ScriptClockTest$'` passes 1/1 (`test-10-qt5-green-restored-ctest.log`).
The Qt 5 build compiles all 1114 link objects of the target, so this is the real
job's compiler input, not a probe.

## The fix (2 lines + one include)

- `tests/src/core/ScriptClockTest.cpp`: include `DeprecationHelper.h`; read both
  `source_bytes` values with `jsonInteger(value, -1)` instead of
  `value.toInteger(-1)`. The assertions are unchanged (`-1` still means "not
  present"), and on Qt 6 `jsonInteger` is exactly `toInteger(default)`, so the
  Qt 6 behaviour is identical.

No product source changes: the product already compiles on both toolchains. The
fix is at the cause (the test's Qt-6-only API), not at a symptom.

## The regression test

`tests/Qt5QJsonToIntegerCheck.py` (new, registered as ctest
`Qt5QJsonToIntegerCheck` in `tests/CMakeLists.txt`): scans every first-party
C/C++ source under `src/`, `include/`, `plugins/` and `tests/src/` for
`QJsonValue::toInteger` and names the `jsonInteger` replacement. It is red on
the Qt 6 hosts too, where the failing line compiles, so a reintroduction is
caught before the Qt 5 job. The one exempt site is the adapter itself
(`include/DeprecationHelper.h`), whose Qt 6 branch is the legitimate guarded
call. A built-in fixture proves the scanner can fail: a `toInteger` fixture must
be flagged, a `jsonInteger` fixture must not.

The existing `ScriptClockTest::scheduleStateReportsUtf8Bytes` slot is the
behavioural test; it was only blocked from compiling on Qt 5. It is unchanged.

## Evidence

The requested LOG FOLDER is not writable by this session's file jail:
`echo probe > .../ci-e58078851126eac9.r1/logs/probe.log` fails with
`bash: ... Permission denied` and exit 1 (`test-0-logdir-probe.log`; the
`write` tool returns `EACCES` in the same directory). The logs below are
therefore in the on-disk scratch directory
`/home/kruzzzzy/.cache/zene-tmp/zene-ci-e58078851126eac9-logs/`, which is
`$TMPDIR/zene-ci-e58078851126eac9-logs`. Every command was run with its exit
code measured directly (`cmd > log 2>&1; echo EXIT=$?`).

| # | Stage | Exact command | Exit | Log |
|---:|---|---|---:|---|
| 1 | Qt 5 configure | `/usr/bin/cmake -S . -B build-qt5 ... -DWANT_QT6=OFF -DQt5Svg_DIR=...` | 0 | `build-1-qt5-configure.log` |
| 2 | Scanner self-test | `python3 tests/Qt5QJsonToIntegerCheck.py --self-test` | 0 | `test-1-selftest.log` |
| 3 | **RED** scan, pre-fix | `python3 tests/Qt5QJsonToIntegerCheck.py --root .` | **1** (2 findings at 408/415) | `test-2-red-scan.log` |
| 4 | GREEN scan, post-fix | same | 0 (4262 files, no finding) | `test-3-green-scan.log` |
| 5 | ctest GREEN, post-fix | `QT_QPA_PLATFORM=offscreen ctest -R '^Qt5QJsonToIntegerCheck$' --output-on-failure` (build/tests) | 0 (1/1) | `test-4-ctest-green-scan.log` |
| 6 | **RED** ctest, fix reversed | same command after `git apply -R fix.patch` | **8** (0/1 failed) | `test-5-ctest-red-scan.log` |
| 7 | GREEN ctest, restored | same command after `git apply fix.patch` | 0 (1/1) | `test-6-ctest-green-restored.log` |
| 8 | Qt 6 rebuild | `/usr/bin/cmake --build build --target ScriptClockTest -j8` | 0 | `build-4-qt6-test.log` |
| 9 | Qt 6 family | `QT_QPA_PLATFORM=offscreen ctest -R '^(ScriptClockTest\|Qt5QJsonToIntegerCheck)$'` | 0 (2/2) | `test-7-qt6-ctest.log` |
| 10 | Qt 5 build, fixed | `/usr/bin/cmake --build build-qt5 --target ScriptClockTest -j8` | 0 | `build-2-qt5-green-fixed.log` |
| 11 | Qt 5 ctest, fixed | `QT_QPA_PLATFORM=offscreen ctest -R '^ScriptClockTest$'` (build-qt5/tests) | 0 (1/1) | `test-8-qt5-green-ctest.log` |
| 12 | **RED** Qt 5 build, fix reversed | `git apply -R fix.patch && cmake --build build-qt5 --target ScriptClockTest -j8` after deleting the binary/object | **2** (the CI error) | `build-5-qt5-red-reversed.log` |
| 13 | **RED** Qt 5 ctest | same ctest command (binary absent) | **8** (`Not Run`) | `test-9-qt5-red-ctest.log` |
| 14 | GREEN Qt 5 build, restored | `git apply fix.patch && cmake --build build-qt5 --target ScriptClockTest -j8` | 0 | `build-6-qt5-green-restored.log` |
| 15 | GREEN Qt 5 ctest, restored | same ctest command | 0 (1/1) | `test-10-qt5-green-restored-ctest.log` |
| 16 | Qt 6 reconfigure | `/usr/bin/cmake -S . -B build` | 0 | `build-3-qt6-reconfigure.log` |
| 17 | Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (all PASS) | `gate-2-feature-check-post-commit.log` |
| 18 | Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-e58078851126eac9` | 0 | `gate-3-submit-check.log` |

`fix.patch` is the two-hunk diff of `tests/src/core/ScriptClockTest.cpp` (the fixed
translation unit) only; the regression-test script and its registration are not
in the reversal patch, so the reversed run leaves the ctest registered and red.

## Test matrix

| Path | Covered by |
|---|---|
| Normal read (slot passes on Qt 6) | Qt6 `ScriptClockTest`, exit 0, 2/2 family |
| Normal read on the supported Qt 5 build | Qt5 build exit 0 + ctest 1/1 |
| Failure face (the exact CI breakage) | Qt5 build with fix reversed, exit 2; scanner scan exit 1; ctest exit 8 |
| Exhaustion / reconfiguration | not applicable: a JSON value read, no queue, cap or state |
| Audio thread | not applicable: the diff touches one QtTest file and a Python source scan; no `processImpl`/`playImpl`/mixer/render path |
| Supported-platform build | Qt 5.15.19 (this box, the hosted job's compiler input) and Qt 6.12 (this box) both build and pass |
| Saved data / old projects | not applicable: no serialization, schema or product code changed |

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** RED:
  `git apply -R fix.patch` then Qt 5 build exit **2** (`build-5-qt5-red-reversed.log`)
  and ctest exit **8** (`test-9-qt5-red-ctest.log`, `Not Run`); the scanner alone
  exits **1** (`test-2-red-scan.log`) and its ctest exits **8**
  (`test-5-ctest-red-scan.log`). GREEN: `git apply fix.patch` then Qt 5 build exit
  **0** and ctest exit **0** (`test-10-qt5-green-restored-ctest.log`); scanner
  exit **0** and ctest exit **0**. The binary and test object were deleted before
  the RED build, so no stale binary could launder the result.
- **Deterministic, no wall-clock timing, sleeps or races.** The new test is a
  source scan; its only inputs are the repository files, and its self-test writes
  two fixed strings to a temporary directory. No clocks, threads or sleeps. The
  Qt 5 ctest exercises a fixed schedule id and fixed byte strings.
- **The fix is at the cause and complete.** Every first-party `toInteger` call
  was re-grepped; after the fix the only first-party occurrence is the guarded
  adapter at `include/DeprecationHelper.h:109`, and the scanner reports 0 over
  4262 files. The two reads in `ScriptClockTest` are the only affected callers.
  The Qt 6 path is bit-for-bit the same (`jsonInteger` calls `toInteger` there),
  and the Qt 5 path is the adapter's checked `double` conversion, which the other
  control tests already use.
- **No collateral damage.** No product source, save format, command, schema or
  limit changed. The test's assertions and the `-1` sentinel are unchanged, so
  old projects and saved data are untouched. The scanner only reads sources; it
  refuses nothing at runtime.
- **Realtime / undo / commands.** Nothing on an audio path changed; the diff is a
  QtTest source and a static check. No new command id, so
  `ControlCommandsSnapshot` is not moved and undo semantics are untouched.
- **Registration.** `tests/Qt5QJsonToIntegerCheck.py` is registered in
  `tests/fork-sources.txt` and as ctest `Qt5QJsonToIntegerCheck` in
  `tests/CMakeLists.txt`; the inherited `tests/CMakeLists.txt` change is recorded
  in `tests/upstream-modifications.txt`. `ScriptClockTest.cpp` was already a
  fork-NEW source in `tests/fork-sources.txt`; no registration change was needed
  for it.
- **The ALREADY-FIXED / FALSE-POSITIVE question does not apply.** The verdict is
  FIXED: the failure reproduces on Qt 5 at `ScriptClockTest.cpp:408,415` and
  disappears with the fix, as the RED/GREEN builds and ctests above show.
- **Portability.** Qt 5.15.19 and Qt 6.12 both build the target and pass the
  test. No platform-specific API was added; the Python check is plain
  `argparse`/`os`/`re`/`tempfile`, so it runs wherever python3 does (the same
  rule as the existing Qt 5 checks).
