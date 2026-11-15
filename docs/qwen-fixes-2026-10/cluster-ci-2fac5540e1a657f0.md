FIXED

# cluster ci-2fac5540e1a657f0 — macOS arm64 build: ScriptClockTest used Qt 6-only `QJsonValue::toInteger`

## Verdict and classification

**REAL — test portability defect (not product runtime, not infrastructure).**

The hosted `macos-arm64` build at the pinned train-32 commit `dec3f1d45c62b26bce955875521780db173a0228`
failed while compiling `tests/src/core/ScriptClockTest.cpp`:

```
error: no member named 'toInteger' in 'QJsonValue'
```

`QJsonValue::toInteger` is Qt 6 only. `CMakeLists.txt:142` keeps `WANT_QT6` OFF by default and
`CMakeLists.txt:363-373` makes Qt 5.15 the supported default, so the test source has to compile with Qt 5
headers. The project already documents the API split at `include/DeprecationHelper.h:104` and provides the
`jsonInteger()` adapter at `include/DeprecationHelper.h:106`.

The checkout under repair is the exact pinned base (`dec3f1d45c`, branch `bugs/qwen-ci-2fac5540e1a657f0`);
it still had the two failing reads at the historical lines 408 and 415, so the failure reproduced here rather
than being accepted as already fixed. The local lane build tree is Qt 6 (`WANT_QT6=ON`), where `toInteger`
exists; that is why a plain rebuild of the binary did not expose the defect. The Qt 5.15.19 headers are
installed (`pkg-config Qt5Core`, `qmake-qt5` 5.15.19), and compiling the two extracted reads against them
produced the exact hosted diagnostic (see `test-1-red-direct.log`).

## Fix

`tests/src/core/ScriptClockTest.cpp:408-417`: both `source_bytes` reads now use
`static_cast<qint64>(...toDouble(-1))`, preserving the existing test assertions (the UTF-8 byte-count check
still requires 13 UTF-16 units / 14 bytes). The value is produced by
`src/core/ScriptClock.cpp:355,469` as `entry.source.toUtf8().size()`, bounded by
`SourceByteCap = 65536` (`include/ScriptClock.h:164`), so it is an exact whole number well inside double's
integer range. No product code, save format, command, undo or audio path changes.

All siblings were audited: after the fix, `grep -rn "\.toInteger(" src include tests` finds no
`QJsonValue::toInteger` site outside the `jsonInteger()` adapter; the other two hits are unrelated Carla JUCE
and `BigInteger` methods.

## Regression test

New `tests/Qt5JsonValueIntegerCheck.py`:

* extracts the **actual** two `source_bytes` statements from `tests/src/core/ScriptClockTest.cpp` and
  syntax-checks them against the installed Qt 5 headers (`pkg-config --cflags Qt5Core`, `c++ -fsyntax-only`);
* has a negative control that compiles a direct `QJsonValue::toInteger` call against the same headers and
  fails unless the compiler rejects it with a diagnostic naming `toInteger` — this proves the probe really is
  using a Qt 5 header rather than passing vacuously;
* is deterministic: no wall-clock timing, sleeps, races or injected delays; it compiles source text and checks
  compiler exit status plus the diagnostic token.

Registered as ctest `Qt5JsonValueIntegerCheck` at `tests/CMakeLists.txt:6267-6281`, only where
`pkg-config --exists Qt5Core` is true. That is the configuration the defect lives in; a Qt 6-only host cannot
compile the guarded contract. New/changed test registrations:
`tests/fork-sources.txt:3936-3937`, `tests/upstream-modifications.txt:5967`.

Test matrix:

| Path | Evidence |
|---|---|
| normal | both real `source_bytes` reads compile with Qt 5.15 (`test-2-green-direct.log`, ctest test 798) |
| negative/failure | direct `toInteger` is rejected by Qt 5 and the diagnostic names `toInteger` |
| Qt 6 build | `ScriptClockTest` still builds and its 13-unit/14-byte assertions pass (ctest test 405) |
| boundary | byte counts are whole, in range and capped at 65536 (`SourceByteCap`), so the double read is exact |
| portability | Qt 5.15 default and optional Qt 6 both kept; no runtime or platform branch added |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ci-2fac5540e1a657f0.r1-logs/`.
The instruction's canonical log folder (`.../qwen/runs/ci-2fac5540e1a657f0.r1/logs`) returned `EACCES` even
for `touch` (owner/mode were correct; the sandbox denies `$HOME` outside the worktree), so the same numbered
files were written to the writable run-log store the harness exposes.

| # | Command | Exit |
|---|---|---|
| 1 | `python3 tests/Qt5JsonValueIntegerCheck.py --source-root .` (before fix) | **1** (RED: `no member named 'toInteger'`) |
| 2 | `python3 tests/Qt5JsonValueIntegerCheck.py --source-root .` (after fix) | **0** (GREEN) |
| 3 | `cmake --build build --target ScriptClockTest -j"$(nproc)"` (after fix) | **0** |
| 4 | `cd build/tests && ctest -R "Qt5JsonValueIntegerCheck\|ScriptClockTest" --output-on-failure` | **0**, 2/2 passed |
| 5 | `git diff -- tests/src/core/ScriptClockTest.cpp > .../fix.patch` | **0** |
| 6 | `git apply -R .../fix.patch` (revert the fix) | **0** |
| 7 | `cmake --build build --target ScriptClockTest -j"$(nproc)"` (reverted) | **0** |
| 8 | `cd build/tests && ctest -R Qt5JsonValueIntegerCheck --output-on-failure` (reverted) | **8**, 0/1 passed — RED |
| 9 | `git apply .../fix.patch` (restore) | **0** |
| 10 | `cmake --build build --target ScriptClockTest -j"$(nproc)"` (restored) | **0** |
| 11 | `cd build/tests && ctest -R "Qt5JsonValueIntegerCheck\|ScriptClockTest" --output-on-failure` | **0**, 2/2 passed |
| 12 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0**, RESULT: PASS |

The reverse/restore proof uses `tests/src/core/ScriptClockTest.cpp` because the defect is in the test
translation unit itself; there is no product file hunk to reverse. The regression probe is an independent
source-derived compile of whatever those lines contain, so it fails on the reverted tree and passes on the
fixed tree exactly as rows 8 and 11 show.

Gate table (`gates-1.log`):

```
PASS  fork-sources
PASS  all-sources-reproduce
PASS  no-upstream-regression
PASS  check-namespace
PASS  no-tautology
PASS  doc-figures
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

## No collateral damage

* `src/core/ScriptClock.cpp` still serialises `source_bytes` as the same `toUtf8().size()` value; only the
  test-side read changes.
* Save/load, project compatibility, undo/command identity and the A16 contract are untouched.
* No audio-thread code is touched; the change is in tests only.
* The Qt 6 build of the same target and the unchanged `ScriptClockTest` assertions pass.

## Independent QA

An independent, read-only QA pass (separate agent, no worktree edits, no cmake/build) verified seven checks,
all PASS:

1. The working tree's `scheduleStateReportsUtf8Bytes` has `toDouble` on both `source_bytes` reads and no
   `toInteger` call; its only `toInteger` hit is the explanatory comment.
2. `git show HEAD:tests/src/core/ScriptClockTest.cpp` on the pinned `dec3f1d45c` still has `.toInteger(-1)`
   at lines 408 and 415, so the base really was broken.
3. RED on the pinned source: the independent copy of the HEAD file in a scratch root made
   `python3 tests/Qt5JsonValueIntegerCheck.py --source-root "$S"` exit **1** with
   `'class QJsonValue' has no member named 'toInteger'` on both extracted statements.
4. GREEN on the working tree: the same probe exited **0** three times.
5. Independence of the probe: QA mutated one read in a scratch copy (`toDouble` -> `toInteger`) and the probe
   then failed quoting the mutated line, proving it compiles the real extracted statements rather than a
   hardcoded fixture. The negative control also proved the resolved headers are Qt 5, not Qt 6.
6. Completeness: `grep -rn "\.toInteger(" src include tests` leaves only the Qt6-guarded
   `DeprecationHelper.h:109` adapter and the probe's negative-control source string.
7. Scratch was deleted; the worktree was not modified by QA.

Residual, non-blocking observations: registration follows the repository's existing Qt5 probe convention
(`pkg-config --exists Qt5Core`); a Qt5 toolchain installed without a pkg-config module would not register the
test, exactly as the existing `Train39-qt5` / `Train41-eq` checks behave. The probe resolves `c++` from PATH,
so it is a header/API contract probe rather than a second product build.

## Self-check

* **Regression fails with the fix reverted and passes with it:** yes. Reverted: build EXIT 0, ctest EXIT 8
  (0/1). Restored: build EXIT 0, ctest EXIT 0 (2/2). Rows 5-11 above.
* **Deterministic:** yes. The probe compiles extracted source text against `pkg-config` Qt5 flags and checks
  the compiler exit status and the `toInteger` diagnostic token; there is no timing, sleep, race or network.
* **Cause and completeness:** yes. Both failing call sites are fixed, and all `.toInteger(` uses in
  `src/`, `include/` and `tests/` were audited; no `QJsonValue::toInteger` caller remains outside the
  documented Qt 5/6 adapter.
* **No collateral damage:** yes, as summarised above; the test's byte-count assertions and the product
  producer are unchanged.
* **Verdict evidence:** this is FIXED, not ALREADY-FIXED/FALSE-POSITIVE; the pinned base still had the
  calls at `tests/src/core/ScriptClockTest.cpp:408,415` (HEAD `dec3f1d45c`), and the fixed tree has
  `toDouble` at 410 and 417.
