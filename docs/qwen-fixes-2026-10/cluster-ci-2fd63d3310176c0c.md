FIXED

# cluster ci-2fd63d3310176c0c - CI regression: build / mingw64

## Verdict and classification

**FIXED. REAL on this branch, classified test.** The defect is a test-source
portability error, not a product runtime defect and not CI infrastructure: the
Windows-only named-pipe cases named `ControlServer::ClientIdleTimeoutMs`
unqualified, and C++ cannot see a class static member through a using-directive.

The worktree is pinned at `dec3f1d45c62b26bce955875521780db173a0228`
(050/m0-train-32) and still contains the six unqualified references. The
prepared packet's `ALREADY_DONE` note describes promoted 050/m0-train-33, where
`5854c983c8` already changed them; that commit is not an ancestor of this branch,
so the defect is present here and needs its own fix and regression.

## Evidence and cause (file:line)

- `include/ControlServer.h:96` declares the value as a **class** static member:
  `static constexpr int ClientIdleTimeoutMs = 30000;` inside `ControlServer`.
- `tests/src/core/ControlShutdownHookTest.cpp:90` has `using namespace lmms;`.
  A using-directive imports names from a namespace; it never imports a class
  member, so `ClientIdleTimeoutMs` cannot resolve in the test method scope.
- At pinned `HEAD`, `tests/src/core/ControlShutdownHookTestParts1.h` used it
  unqualified at lines 149, 155, 161, 169, 196 and 209, inside the two
  `#if defined(Q_OS_WIN)` cases. POSIX preprocessing removes those cases, which
  is why only the Windows lanes see the error.
- The sibling `tests/src/core/ControlShutdownHookTestParts2.h:132,134` already
  spells `ControlServer::ClientIdleTimeoutMs`, confirming the intended contract.

The hosted mingw64 log (untrusted CI evidence, read as data only) names the same
six lines and six errors:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/dec3f1d45c62b26bce955875521780db173a0228/run-43681793608-job-132713785145.log:5273-5289`.
Native mingw64 compilation cannot be rerun on this host: `which
x86_64-w64-mingw32-g++ x86_64-w64-mingw32-gcc` exits **1** and
`/usr/x86_64-w64-mingw32` is absent. The exact name-lookup failure is therefore
also demonstrated with the host compiler (see the compile control below).

## Fix

Qualified all six references in `tests/src/core/ControlShutdownHookTestParts1.h`
as `ControlServer::ClientIdleTimeoutMs`. No product source, API, limit, save
format, control command or runtime path changes.

Completeness: `include/ControlServer.h` declares four public static constants
(`MaxRequestLineBytes:83`, `MaxQueuedReplyBytes:91`, `MaxClients:94`,
`ClientIdleTimeoutMs:96`). A scan of both part headers found every use qualified
(`MaxClients` and `MaxQueuedReplyBytes` already were). No other translation unit
includes `ControlShutdownHookTestParts1.h`/`Parts2.h`; only
`ControlShutdownHookTest.cpp` does.

## Regression test

`ControlShutdownHookTest::windowsPipeCasesNameTheIdleDeadlineThroughTheServer()`
(`tests/src/core/ControlShutdownHookTest.cpp:407`, declaration at :338). It reads
the two part headers through `QT_TESTCASE_SOURCEDIR`, strips comments while
keeping line numbers (`sourceCodeLines`, :287), and fails for every
`ClientIdleTimeoutMs` token whose immediate prefix is not `ControlServer::`. It
also requires at least six references so deleting the cases cannot make it pass
vacuously. This is the same portable source seam
`RenderSourceGuardTest::windowsBuildHasNoUnusedTestHelpers()` uses for a
Windows-only compilation property.

Test matrix:

| Path | Exercise |
|---|---|
| Normal | every token qualified -> test passes; full `ControlShutdownHookTest` green |
| Failure | the original header restored -> test fails and names all six compiled-away lines |
| Boundary | fewer than six references -> the count assertion fails, so the scan cannot pass vacuously |
| Invalid input | unreadable/missing part header -> `QVERIFY2(file.open(...))` fails with the path |
| Audio thread | n/a: test-only source scan, no product path, allocation, lock or timing |
| Supported platforms | Windows cases stay behind `Q_OS_WIN`; the scan is portable C++20/Qt and compiles on Linux and Windows |

Determinism: the test is a text scan of two fixed source files located by the
build's existing `QT_TESTCASE_SOURCEDIR` define. It uses no clock, sleep, thread,
socket, external service or shared mutable state; its result is a pure function
of the committed text.

## Exact commands, exits and logs

All logs are in `build/qwen-logs/` because the nominated log folder refuses
writes from this session (see the log-folder note). Commands were run from the
worktree root unless `cd build/tests` is shown.

| Step | Command | Exit | Log |
|---|---|---|---|
| RED build | `cmake --build build --target ControlShutdownHookTest` | 0 | `build-1-red-compile.log` |
| RED run | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` | **8** | `test-1-red.log` |
| GREEN build | `cmake --build build --target ControlShutdownHookTest` | 0 | `build-2-green-compile.log` |
| GREEN run | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` | **0** | `test-2-green.log` |
| 4b patch | `git diff HEAD^ HEAD -- tests/src/core/ControlShutdownHookTestParts1.h > build/qwen-logs/final-fix.patch` | 0 | - |
| 4b reverse | `git apply -R build/qwen-logs/final-fix.patch` | 0 | - |
| 4b RED build | `cmake --build build --target ControlShutdownHookTest` | 0 | `build-7-final-red-compile.log` |
| 4b RED run | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` | **8** | `test-8-final-red.log` |
| 4b restore | `git apply build/qwen-logs/final-fix.patch` | 0 | - |
| 4b GREEN build | `cmake --build build --target ControlShutdownHookTest` | 0 | `build-8-final-restored-compile.log` |
| 4b GREEN run | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` | **0** | `test-9-final-restored-green.log` |
| Family build | `cmake --build build --target ControlRegistryTest ControlRegistryDisplayTest ControlRegistryNestingTest ControlEventHubTest ControlResultSchemaTest` | 0 | `build-5-family.log` |
| Family run | `cd build/tests && ctest -R '^Control(ShutdownHook\|Registry\|RegistryDisplay\|RegistryNesting\|EventHub\|ResultSchema)Test$' --output-on-failure` | **0** (6/6) | `test-5-family.log` |
| Direct run | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlShutdownHookTest` | 0 (14 passed, 1 skipped) | `test-6-direct.log` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gate-1-feature-check.log` |
| Compile control (unqualified) | `g++ -std=c++20 -fsyntax-only build/qwen-logs/name-lookup-control.cpp` | **1** | `compile-control-unqualified.log` |
| Compile control (qualified) | same file with `ControlServer::` restored | **0** | `compile-control-qualified.log` |

RED failure text (`test-8-final-red.log`; the initial RED run `test-1-red.log`
named the same lines) names exactly the lines the mingw64 log named:

```
FAIL!  : ControlShutdownHookTest::windowsPipeCasesNameTheIdleDeadlineThroughTheServer()
'unqualified.isEmpty()' returned FALSE. (these idle-deadline references would fail the mingw64 build because ClientIdleTimeoutMs is a ControlServer member, not a free name:
src/core/ControlShutdownHookTestParts1.h:149
src/core/ControlShutdownHookTestParts1.h:155
src/core/ControlShutdownHookTestParts1.h:161
src/core/ControlShutdownHookTestParts1.h:169
src/core/ControlShutdownHookTestParts1.h:196
src/core/ControlShutdownHookTestParts1.h:209)
Totals: 13 passed, 1 failed, 1 skipped, 0 blacklisted, 133ms
```

GREEN test totals: the direct binary run reports `14 passed, 0 failed, 1
skipped` (13 pre-existing passes plus the new regression; the one Windows-only
case is skipped on Linux as before), and `ctest` reports `100% tests passed out
of 1`.

The host compile control reproduces the language rule independently of the
Windows headers:

```
$ g++ -std=c++20 -fsyntax-only build/qwen-logs/name-lookup-control.cpp   # unqualified
error: 'ClientIdleTimeoutMs' was not declared in this scope
EXIT=1
$ g++ -std=c++20 -fsyntax-only build/qwen-logs/name-lookup-control.cpp   # ControlServer::
EXIT=0
```

## Verification of no collateral damage

The change is confined to a test-support header and its only test source. No
product code, project format, save/load path, undo step, control command or
audio-thread path is touched. The adjacent control-test family (6/6) and the
gate check pass unchanged. The five sibling binaries were built and run, not
merely listed.

## Log-folder note

The nominated folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-2fd63d3310176c0c.r1/logs`
is `drwxr-xr-x kruzzzzy:kruzzzzy` but refuses writes:
`touch <folder>/probe.log` -> **EXIT=1**, `Permission denied`. Every build/test
log and `fix.patch` was therefore written to the worktree's ignored
`build/qwen-logs/` directory and is cited by name above.

## Independent QA

An independent review agent inspected the pinned `HEAD` source, the diff, the
four evidence logs and the regression, and re-ran the test. Its observed
evidence and verdict are recorded in the Self-check below. It did not modify
the tree.

## Files committed

- `tests/src/core/ControlShutdownHookTestParts1.h` - qualify the six Windows-only idle-deadline references.
- `tests/src/core/ControlShutdownHookTest.cpp` - deterministic portable source-scan regression.
- `docs/qwen-fixes-2026-10/cluster-ci-2fd63d3310176c0c.md` - this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it.** Reverted header: `git apply -R build/qwen-logs/final-fix.patch` EXIT=0
  -> rebuild EXIT=0 -> `ctest -R '^ControlShutdownHookTest$'` **EXIT=8**
  (`test-8-final-red.log`, 13 passed / 1 failed / 1 skipped, naming the six
  original lines). Restored: `git apply build/qwen-logs/final-fix.patch` EXIT=0
  -> rebuild EXIT=0 -> the same ctest **EXIT=0**
  (`test-9-final-restored-green.log`). The reverted patch is the six-line header
  hunk; the regression test stays in place in both runs, so it is the test that
  changes verdict. The comment stripper was simplified to a sentinel lookahead
  (CCN 9) after the first proof; the final version was re-proved here.

- **The test is deterministic: no wall-clock timing, sleeps or races.** It opens
  two fixed files under the build's `QT_TESTCASE_SOURCEDIR` define and scans
  their text. There is no clock, sleep, thread, timeout, socket or shared
  mutable state; the only seam is the source path the build already supplies.

- **The fix is at the cause and complete.** All six compile errors are the same
  identifier and all six are qualified. `Parts2.h:132,134` already had the
  qualified spelling. Every use of the four `ControlServer` static constants in
  both part headers is qualified, and no other translation unit includes those
  headers. No caller or sibling command is involved.

- **No collateral damage.** Only two test files change. No product source, API,
  limit, project/save format, undo step, real-time path or control command is
  touched; old projects and saved data load exactly as before. The adjacent
  `Control(Registry|RegistryDisplay|RegistryNesting|EventHub|ResultSchema)`
  family passes 6/6 and `feature_check.py` exits 0.

- **Verdict is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE item does not apply.**
  For the record, the pinned branch is not the promoted train-33 tree:
  `git merge-base --is-ancestor 5854c983c8 HEAD` exits **1** (the commit exists
  on `050/m0-train-33`, not here), and the six unqualified lines are present at
  `HEAD` as shown above. ALREADY-FIXED on train-33 therefore does not close this
  pinned branch's defect.

- **Independent QA.** The reviewer confirmed (a) the six unqualified references
  at pinned `HEAD`, (b) `ClientIdleTimeoutMs` is a `ControlServer` static
  member, (c) the fix covers all six and the sibling `Parts2.h` was already
  qualified, (d) the regression is non-vacuous, deterministic and names the six
  original lines, and (e) re-running `ctest -R '^ControlShutdownHookTest$'` from
  `build/tests` passed; see the evidence table above for the recorded commands
  and exits.
