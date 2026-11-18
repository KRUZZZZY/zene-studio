FIXED

# Cluster ci-512849fa221097db — MSVC C2492 on a `thread_local` member of exported `AudioEngine`

**Verdict: REAL, fixed.** The hosted msvc-x64 build fails at `include/AudioEngine.h(891)`:

```
error C2492: 'lmms::AudioEngine::s_changeInModelDepth':
data with thread storage duration may not have dll interface
```

## Decision: REAL

- `AudioEngine` is `class LMMS_EXPORT AudioEngine` (`include/AudioEngine.h:88`). On MSVC the
  generated `lmms_export.h` expands `LMMS_EXPORT` to `__declspec(dllexport)` (`src/CMakeLists.txt:204-208`).
- Before the fix the class declared a static `thread_local` data member at
  `include/AudioEngine.h:891`: `static thread_local int s_changeInModelDepth;`, defined at
  `src/core/AudioEngine.cpp:473`. MSVC forbids data with thread storage duration in a class that has
  a DLL interface; GCC and Clang accept it, so only the msvc-x64 job saw it. The member sits inside
  `#ifdef LMMS_TESTING`, and `lmmsobjs` is compiled with that define (`tests/CMakeLists.txt:5-7`), which
  is why the test build hits it.
- The rule was reproduced on this box with `clang-cl --target=x86_64-pc-windows-msvc`, which implements
  the same dllexport restriction. The old pattern (`class __declspec(dllexport) X { static thread_local
  int s_depth; };`) fails with `error: 'X::s_depth' cannot be thread local when declared 'dllexport'`
  (`logs/16-msvc-rule-old.log`, EXIT=1). The new shape (a file-local `thread_local` plus an out-of-line
  accessor) compiles (`logs/17-msvc-rule-new.log`, EXIT=0).
- The seam has exactly four callers, all through `AudioEngine::isChangeInModelHeld()`:
  `tests/src/core/AudioPortsModelTest.cpp:532,535`, `tests/src/core/RoutingGraphLiveTest.cpp:215`,
  `tests/src/gui/ArrangerFileDropTest.cpp:507,520` and `tests/src/gui/PinConnectorTest.cpp:149`. No
  caller reads the data member directly.

## Fix (smallest, test-only seam, no product behaviour change)

### `include/AudioEngine.h`

- `:625`: `bool isChangeInModelHeld() const noexcept;` is now a declaration, not an inline definition.
  The comment says why it is out of line.
- `:888-889`: the `static thread_local int s_changeInModelDepth;` member is gone; the exported class
  carries no thread-local data.

### `src/core/AudioEngine.cpp`

- `:471-482`: the nesting depth is now a file-local `static thread_local int s_changeInModelDepth = 0;`
  (internal linkage, no DLL interface), and `AudioEngine::isChangeInModelHeld()` is defined out of line
  next to it.
- `:1716`, `:1724`, `:1742`: `requestChangeInModel()` / `doneChangeInModel()` /
  `requestChangeInModelForTest()` increment and decrement the same depth exactly as before. The guard,
  the render path and the lock are unchanged.

No audio-thread path, command, undo step or saved-data format is touched. `isChangeInModelHeld()` is
compiled only with `LMMS_TESTING`.

## Regression test: `ExportedClassTlsCheck` (`tests/exported-class-tls-check.py`)

The test scans `include/` and `src/` headers (skipping the vendored `src/3rdparty` tree), tracks class
bodies and flags a `thread_local` declaration that ends a member statement inside a class carrying
`LMMS_EXPORT` or `__declspec(dllexport)`. A `thread_local` inside a member function body, a
`thread_local` member of a non-exported class, and comments/strings are not flagged. It first runs its
own red/green fixtures, so a scanner that cannot fail fails instead. It reads sources only: no build,
no clock, no sleeps, no threads.

It is registered as ctest `ExportedClassTlsCheck` (`tests/CMakeLists.txt:6283-6291`) wherever python3
exists. Before the fix it reports `include/AudioEngine.h:891`; after the fix it passes.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/ci-512849fa221097db.r1/logs/`)

| Step | Command | EXIT |
| --- | --- | --- |
| **RED** (direct, before fix) | `python3 tests/exported-class-tls-check.py . > 02-test-red-direct.log` | **1**: `include/AudioEngine.h:891: exported class declares a thread_local data member (MSVC C2492)` |
| **RED** (ctest, before fix) | `cd build/tests && ctest -R '^ExportedClassTlsCheck$' --output-on-failure > 04-ctest-red.log` | **8**: 0/1 passed, same finding |
| Build after fix | `CCACHE_TEMPDIR=$TMPDIR/ccache-ci-512849fa221097db cmake --build build --target zene AudioPortsModelTest RoutingGraphLiveTest ArrangerFileDropTest PinConnectorTest -j4 > 06-build-fix.log` | **0** |
| **GREEN** family | `ctest -R '^(ExportedClassTlsCheck\|AudioPortsModelTest\|RoutingGraphLiveTest\|ArrangerFileDropTest\|PinConnectorTest)$' --output-on-failure > 07-ctest-green-family.log` | **0**: 5/5 passed |
| 4b patch / revert | `git diff -- include/AudioEngine.h src/core/AudioEngine.cpp > logs/fix.patch && git apply -R logs/fix.patch` | DIFF **0**, REVERT **0** |
| 4b rebuild reverted | `cmake --build build --target zene -j4 > 08-build-reverted.log` | **0** |
| 4b test reverted | `ctest -R '^ExportedClassTlsCheck$' --output-on-failure > 09-ctest-reverted.log` | **8**: same finding |
| 4b reapply | `git apply logs/fix.patch` | **0** (`git apply -R --check` also 0) |
| 4b rebuild reapplied | `cmake --build build --target zene AudioPortsModelTest RoutingGraphLiveTest ArrangerFileDropTest PinConnectorTest -j4 > 10-build-reapplied.log` | **0** |
| 4b test reapplied | `ctest -R '^(ExportedClassTlsCheck\|AudioPortsModelTest\|RoutingGraphLiveTest\|ArrangerFileDropTest\|PinConnectorTest)$' --output-on-failure > 11-ctest-reapplied.log` | **0**: 5/5 passed |
| Broader AudioEngine family build | `cmake --build build --target AudioBusHandleTest AudioEngineProfilerTest RoutingGraphScheduleTest RoutingGraphTest -j4 > 14-build-family-missing.log` | **0** |
| Broader AudioEngine family test | `ctest -R 'AudioEngine\|AudioBusHandle\|RoutingGraph' --output-on-failure > 15-ctest-audioengine-family.log` | **0**: 6/6 passed |
| MSVC rule probe (old shape) | `clang-cl --target=x86_64-pc-windows-msvc /c /std:c++20 old.cpp` (`16-msvc-rule-old.log`) | **1**: `cannot be thread local when declared 'dllexport'` |
| MSVC rule probe (new shape) | `clang-cl --target=x86_64-pc-windows-msvc /c /std:c++20 new.cpp` (`17-msvc-rule-new.log`) | **0** |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 12-feature-check.log` | **0**: 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| Gates (final, report present) | same command `> 18-feature-check-final.log` | **0**: 8/8 PASS |
| Pre-submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-512849fa221097db > 20-submit-check.log` | **0**: PASS, with the Self-check questions answered below |

The reverted test ran against a rebuilt `zene` (log 08 rebuilt `lmmsobjs`, including
`src/core/AudioEngine.cpp`), not a stale binary.

## Registration

- `tests/fork-sources.txt`: `tests/exported-class-tls-check.py` and this report (product-new).
- `tests/upstream-modifications.txt`: `include/AudioEngine.h`, `src/core/AudioEngine.cpp` and
  `tests/CMakeLists.txt`, each with a reason. The file ends with a newline.

## Self-check

- **Regression test fails reverted, passes fixed (exit codes).** Reverted product files:
  `ctest -R '^ExportedClassTlsCheck$'` EXIT=**8** (`09-ctest-reverted.log`), reporting
  `include/AudioEngine.h:891`. Reapplied: EXIT=**0**, 5/5 family passed (`11-ctest-reapplied.log`).
  The original RED/GREEN was 8/0 (`04`, `07`). The 4b revert used
  `git diff -- include/AudioEngine.h src/core/AudioEngine.cpp > logs/fix.patch && git apply -R logs/fix.patch`
  (DIFF=0, REVERT=0), then rebuilt `zene` (EXIT=0, `08-build-reverted.log`), then re-applied
  (`git apply logs/fix.patch`, EXIT=0) and rebuilt (EXIT=0, `10-build-reapplied.log`).
- **Deterministic.** The test is a pure source scan over fixed inputs with inline red/green fixtures;
  it uses no wall clock, sleeps, threads, build output or environment-dependent values. The MSVC rule
  probe is a single `clang-cl` invocation on a fixed two-line probe.
- **At the cause and complete.** The exported class no longer carries thread-local data (the member
  previously at `include/AudioEngine.h:891` is removed; the accessor is declared at `:625`); the depth
  is file-local in `src/core/AudioEngine.cpp:471-482` and the three updates (`:1716`, `:1724`, `:1742`)
  are unchanged. Every reader (`AudioPortsModelTest.cpp:532,535`, `RoutingGraphLiveTest.cpp:215`,
  `ArrangerFileDropTest.cpp:507,520`, `PinConnectorTest.cpp:149`) goes through the out-of-line
  accessor. The sibling file-local `s_renderingThread` (`src/core/AudioEngine.cpp:470`) was already
  outside the class.
- **No collateral damage.** Old projects and saved data are untouched: the change moves a
  `LMMS_TESTING`-only seam and changes no serialization, command, undo or audio-thread code. No new
  limit or check was added to product code. The seam tests and the wider AudioEngine family pass
  (5/5 and 6/6, logs `07`, `11`, `15`).
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED. The old
  declaration is `include/AudioEngine.h:891` in the parent commit and the CI error is C2492 on that
  line; the reverted test reproduces it (`09-ctest-reverted.log`).
- **Sibling commands.** None of the command files behind `app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get/run/set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks` or
  `arrangement.get_state` is changed by this fix. The diff is `include/AudioEngine.h`,
  `src/core/AudioEngine.cpp`, the test registration and docs only, so every listed command keeps its
  existing behaviour.
- **Not verified here:** MSVC itself is not installed on this box. The hosted msvc-x64 job on a
  `ci/*` branch is the final confirmation; the `clang-cl` probe reproduces the same restriction.
