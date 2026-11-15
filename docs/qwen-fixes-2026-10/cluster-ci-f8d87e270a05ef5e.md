FIXED

## Evidence and decision

The finding is **REAL**, and it is a **test-source portability defect**, not a product
behaviour defect and not infrastructure flakiness. On the pinned commit
`cc29a63dde09cf28bb00498a2d2a5b124d4d614d`, `tests/src/core/ExportWavWriteErrorTest.cpp`
defined the free function `riffSizes(const QString&)` at line 201 outside any platform
guard, while its only call site (line 334 before the fix) lives inside the
`#ifdef Q_OS_UNIX` test block at line 310. A non-template free function in an anonymous
namespace has internal linkage, so GCC reports it as
`-Wunused-function` when `Q_OS_UNIX` is undefined; the project's `-Werror`
(`cmake/modules/ErrorFlags.cmake:44`) turns that warning into the hosted MinGW64 build
failure quoted in the finding.

The regression is reproduced deterministically on this Linux host by compiling the real
translation unit in its non-Unix shape: the source's own headers are pre-included, then
`Q_OS_UNIX` is `#undef`-ed before the source body, the meta-object is regenerated with
`moc -UQ_OS_UNIX`, and the recorded compile command is run with
`-Werror=unused-function`. Before the fix this fails with the exact hosted diagnostic,
at the exact line:

```
tests/src/core/ExportWavWriteErrorTest.cpp:201:11: error: '{anonymous}::RiffSizes {anonymous}::riffSizes(const QString&)' defined but not used [-Werror=unused-function]
  201 | RiffSizes riffSizes(const QString& path)
```

The smallest correct fix moves `RiffSizes` and `riffSizes` inside the existing
`#ifdef Q_OS_UNIX` block (`tests/src/core/ExportWavWriteErrorTest.cpp:190-266`). The
Unix-only assertions, tolerances and helper bodies are unchanged. The probe now compiles
clean, which also proves no other unguarded free function remains in this translation unit.

The regression is a new `wav-write-error` mode in the existing hosted-compiler probe
`tests/train-41-portability-check.py:203`, registered as CTest `Train41-wav-write-error`
in `tests/CMakeLists.txt:9319`. It is deterministic: it rebuilds the real source with the
build's own `compile_commands.json` flags and injects only the platform preprocessor
state; there is no clock, sleep or race.

## Evidence table

`LOG` below is the required log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-f8d87e270a05ef5e.r1/logs`.
This session's file sandbox refused every write there (`touch .../logs/probe.tmp` →
`Permission denied`, exit 1), so the numbered logs listed here were written to
`<worktree>/build/qwen-logs/`; the exact command and exit code are unchanged, and only
the log destination differs.

| Check | Exact command | Result |
|---|---|---|
| Reproduce on the exact commit (probe, pre-fix) | `python3 tests/train-41-portability-check.py --source-root "$PWD" --build-dir "$PWD/build" --qt-major 6 --moc /usr/lib/qt6/moc --mode wav-write-error > "$PWD/build/qwen-logs/001-red-portability-probe.log" 2>&1` | EXIT=1; `riffSizes` "defined but not used" at `ExportWavWriteErrorTest.cpp:201`, identical to the hosted MinGW log |
| Probe after the fix | same command with `002-green-portability-probe.log` | EXIT=0; real TU compiles under simulated non-Unix with `-Werror=unused-function` |
| Build the affected target | `cmake --build build --target ExportWavWriteErrorTest > "$PWD/build/qwen-logs/003-build-target.log" 2>&1` | EXIT=0; target linked |
| Affected ctest family, fixed | from `build/tests`: `ctest -R 'ExportWavWriteError\|Train41-wav-write-error' --output-on-failure > "$PWD/build/qwen-logs/004-ctest-family.log" 2>&1` | EXIT=0; 2/2 tests passed (`ExportWavWriteErrorTest`, `Train41-wav-write-error`) |
| Whole portability family | from `build/tests`: `ctest -R '^Train41' --output-on-failure > "$PWD/build/qwen-logs/013-ctest-train41-family-rebuilt.log" 2>&1` | EXIT=0; 7/7 tests passed. The first attempt (`011`) failed only in `Train41-darwin-tu` because `CrashReporterArmTest.moc` was absent from the partially built tree; `cmake --build build --target CrashReporterArmTest` (`012`) EXIT=0, then the rerun (`013`) is green. |
| Reverse-patch RED (step 4b) | `git diff -- tests/src/core/ExportWavWriteErrorTest.cpp > "$PWD/build/qwen-logs/fix.patch"; git apply -R "$PWD/build/qwen-logs/fix.patch"` then `ctest -R 'Train41-wav-write-error' --output-on-failure > .../005-ctest-without-fix.log 2>&1` (from `build/tests`) | Reverse EXIT=0; ctest EXIT=8, 0% passed, the same line-201 `-Werror=unused-function` failure |
| Restored GREEN | `git apply "$PWD/build/qwen-logs/fix.patch"`; `cmake --build build --target ExportWavWriteErrorTest > .../006-rebuild-with-fix.log 2>&1`; then `ctest -R 'ExportWavWriteError\|Train41-wav-write-error' --output-on-failure > .../007-ctest-with-fix.log 2>&1` (from `build/tests`) | Apply EXIT=0; rebuild EXIT=0; ctest EXIT=0, 2/2 tests passed |
| Quality gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD" > "$PWD/build/qwen-logs/008-feature-check.log" 2>&1` | EXIT=0; all 8 gates PASS; complexity and file-length report "no new item in your files" |

## Test matrix

| Path | What is exercised | Result |
|---|---|---|
| Ordinary non-Unix build | The real `ExportWavWriteErrorTest.cpp` with `Q_OS_UNIX` undefined, the build's recorded flags and `-Werror=unused-function` | GREEN after fix (EXIT=0); RED before fix (EXIT=1) |
| Ordinary Unix build | `cmake --build build --target ExportWavWriteErrorTest`, then its CTest | GREEN (EXIT=0); 1/1 passed |
| Boundary / invalid input / failure paths | Existing Unix cases still run unchanged: successful export, stale-header rewrite refusal, read-only channel-mask patch, short write, failed finalisation, OGG/MP3 tail flush, `RenderManager` propagation | GREEN in `ExportWavWriteErrorTest` |
| Audio-thread constraints | No product or audio-path file touched; the change is confined to a host-side test translation unit and a compile probe | No new allocation/lock/log on any audio path |
| Supported platforms | MinGW64 (`Q_OS_UNIX` undefined) must compile the helper-free TU; Linux (`Q_OS_UNIX` defined) still compiles and runs the Unix cases | Covered by the probe and the real target |
| Save/load, undo, compatibility | No command, model, serializer or undo path changed | Unchanged |

## Self-check

- **Regression FAILS with the fix reverted and PASSES with it:** `005-ctest-without-fix.log`
  (EXIT=8, line-201 `-Werror=unused-function`) versus `007-ctest-with-fix.log`
  (EXIT=0, 2/2 passed). The pre-fix direct probe `001` also fails at EXIT=1, and the
  post-fix probe `002` passes at EXIT=0.
- **Deterministic:** The probe injects platform preprocessor state (`#undef Q_OS_UNIX`,
  `moc -UQ_OS_UNIX`) and rebuilds the real TU each run; there is no wall-clock timing,
  sleep or race. The observed diagnostic and line are identical to the hosted log.
- **Fix at the cause and complete:** The cause is the unguarded internal-linkage helper.
  `RiffSizes` and its only reader moved inside the same `#ifdef Q_OS_UNIX` block that owns
  the only caller; the sibling `FileSizeLimit` was already guarded. A clean compile of the
  whole TU with `-Werror=unused-function` proves no other unguarded free function remains.
- **No collateral damage:** The Unix test bodies, assertions, tolerances and thresholds are
  untouched; only the preprocessor guard moved. No product runtime, project format, save
  path, undo step, command or realtime path changed. The whole `Train41` portability family
  still passes (7/7, `013-ctest-train41-family-rebuilt.log`, EXIT=0) after building the
  `CrashReporterArmTest` dependency the probe needs.
- **Verdict evidence:** REAL/FIXED because the pre-fix source at line 201 defines a helper
  whose sole call is inside `#ifdef Q_OS_UNIX` at line 310, and the reverse-patch probe
  reproduces the exact hosted failure.

## Files committed

- `tests/src/core/ExportWavWriteErrorTest.cpp` — guard the Unix-only RIFF helper.
- `tests/train-41-portability-check.py` — add the `wav-write-error` non-Unix compile probe.
- `tests/CMakeLists.txt` — register `Train41-wav-write-error`.
- `docs/qwen-fixes-2026-10/cluster-ci-f8d87e270a05ef5e.md` — this report.
