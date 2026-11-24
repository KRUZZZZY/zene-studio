ALREADY-FIXED

# cluster ci-2fd63d3310176c0c - CI regression: build / mingw64

## Verdict and reason

**ALREADY-FIXED on this branch.** The clustered mingw64 failure is the
name-lookup error in `tests/src/core/ControlShutdownHookTestParts1.h`: the two
Windows-only named-pipe cases used the class static member
`ControlServer::ClientIdleTimeoutMs` unqualified at lines 149, 155, 161, 169,
196 and 209. A POSIX build preprocesses those cases away, so only the Windows
lanes saw the compile error. The correction is already in this worktree, and
the branch already carries the portable source-scan regression that fails on
the old spelling and passes on the corrected spelling.

No product or test source is changed by this run. The only committed file is
this report; the existing fix and regression are re-verified in place below.

## Evidence (file:line)

- `include/ControlServer.h:96` declares the value as a class static member:
  `static constexpr int ClientIdleTimeoutMs = 30000;`.
- `tests/src/core/ControlShutdownHookTest.cpp:90` has
  `using namespace lmms;`. A using-directive imports a namespace, not a class
  member, so the unqualified spelling cannot resolve in the test function's
  scope.
- `tests/src/core/ControlShutdownHookTestParts1.h:149,155,161,169,196,209` all
  now spell `ControlServer::ClientIdleTimeoutMs`. The sibling
  `tests/src/core/ControlShutdownHookTestParts2.h:132,134` already did.
- The regression is
  `ControlShutdownHookTest::mingwPipeCasesNameTheIdleDeadlineThroughTheServer()`
  (`tests/src/core/ControlShutdownHookTest.cpp:373`, body at :483). It reads
  both part headers through `QT_TESTCASE_SOURCEDIR`, strips comments without
  losing line numbers, requires at least six references, and fails for any
  `ClientIdleTimeoutMs` token not immediately prefixed by `ControlServer::`.
  The sibling windows-arm64 scan is at `:378`, body at `:446`.

History proving the correction is already on this branch:

- `git merge-base --is-ancestor 5854c983c8 HEAD` exits **0**.
  `5854c983c8` contains the six-line correction (`git show 5854c983c8 --
  tests/src/core/ControlShutdownHookTestParts1.h`).
- `d9a26d06ad` reconciled the CI repairs and brought the regression and this
  report onto the train. `git log --oneline --follow --
  docs/qwen-fixes-2026-10/cluster-ci-2fd63d3310176c0c.md` names it.

Hosted evidence, read as data only:

- The pinned failure log
  `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/dec3f1d45c62b26bce955875521780db173a0228/run-43681793608-job-132713785145.log:5273-5289`
  reports `'ClientIdleTimeoutMs' was not declared in this scope` at the six
  lines above.
- The next hosted mingw64 run for the promoted train-33 source
  (`.../train-ci/cc29a63dde09cf28bb00498a2d2a5b124d4d614d/run-43710174450-job-132799809563.log`)
  no longer reports those errors: it builds past `ControlShutdownHookTest`
  and fails later at `tests/src/core/ExportWavWriteErrorTest.cpp:201`
  (`riffSizes` defined but not used). That is a different cluster
  (`ci-f8d87e270a05ef5e`), and its fix is also already here:
  `git merge-base --is-ancestor 9feb23eb1b HEAD` exits **0**, and the helper
  is under `#ifdef Q_OS_UNIX` in the worktree.

Answer to the packet's outstanding question, "Has a hosted mingw64 build passed
against the promoted train-33 source?": no green hosted run was observed. The
train-33 run failed later on the unrelated `riffSizes` issue above. The
`ClientIdleTimeoutMs` errors are gone from that log, and the deterministic
source scan below proves the same contract locally. Native MinGW compilation
cannot be reproduced on this host: `which x86_64-w64-mingw32-g++
x86_64-w64-mingw32-gcc` exits **2** with both tools absent
(`logs/mingw-toolchain-1.log`).

## Test matrix

| Path | Exercise |
|---|---|
| Normal | all references qualified -> both source scans pass; full family `EXIT=0` with a short `TMPDIR` |
| Failure | one reference unqualified -> the scan fails and names `src/core/ControlShutdownHookTestParts1.h:149` |
| Boundary | fewer than six references -> the count assertion fails, so the scan cannot pass vacuously |
| Invalid input | unreadable or missing part header -> `QVERIFY2(file.open(...))` fails with the path |
| Audio thread | n/a: a test-only source scan, no product path, allocation, lock, wait or log |
| Supported platforms | Windows cases remain under `Q_OS_WIN`; the scan is portable C++20/Qt and runs on POSIX and Windows |

## Commands and exit codes (all logs in the nominated log folder)

```
cmake --build build --target ControlShutdownHookTest -j4 > .../logs/build-1.log 2>&1
EXIT=0

cd build/tests
ctest -R '^ControlShutdownHookTest$' --output-on-failure > .../logs/test-1.log 2>&1
EXIT=8
```

`test-1.log` is the environment's long `TMPDIR`: its socket paths (110-117
bytes) exceed the product's 107-byte AF_UNIX path limit. Both source scans
still PASS there; the failure is unrelated to this cluster. Re-run with a short
temporary directory:

```
TMPDIR=/tmp ctest -R '^ControlShutdownHookTest$' --output-on-failure > .../logs/test-2.log 2>&1
EXIT=0
```

RED/GREEN of the regression itself, without touching product code: one
qualifier at `ControlShutdownHookTestParts1.h:149` was temporarily changed back
to the original unqualified spelling with `apply_patch`, the test was run, and
the spelling was restored with `apply_patch`:

```
TMPDIR=/tmp ctest -R '^ControlShutdownHookTest$' --output-on-failure > .../logs/test-3-red.log 2>&1
EXIT=8
# FAIL! mingwPipeCases... and windowsPipeCases... name
# src/core/ControlShutdownHookTestParts1.h:149

TMPDIR=/tmp ctest -R '^ControlShutdownHookTest$' --output-on-failure > .../logs/test-4-green.log 2>&1
EXIT=0
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
  /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-2fd63d3310176c0c-port \
  > .../logs/gates-1.log 2>&1
EXIT=0
RESULT: PASS   (fork-sources, all-sources-reproduce, no-upstream-regression,
                check-namespace, no-tautology, doc-figures, complexity,
                file-length all PASS)
```

## No collateral damage

Only this report changes. No product source, API, limit, save format, undo
step, control command or audio-thread path is touched, so old projects and
saved data load exactly as before. The correction covers all six references;
`Parts2.h` already conformed; no other translation unit includes the two part
headers.

## Other findings

- The family's AF_UNIX cases fail when `TMPDIR` is the long per-run directory:
  the socket path exceeds `ControlServer`'s 107-byte limit. This predates and is
  unrelated to this cluster; the tests pass under `TMPDIR=/tmp`. Not changed
  here.
- The train-33 hosted mingw64 run failed after this defect on
  `ExportWavWriteErrorTest.cpp:201` (`riffSizes` unused). That is cluster
  `ci-f8d87e270a05ef5e`, already fixed by `9feb23eb1b` on this branch.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** The
  fix is a test-support header, so the proof temporarily restored one original
  spelling: `test-3-red.log` **EXIT=8**, naming
  `src/core/ControlShutdownHookTestParts1.h:149`; after restore,
  `test-4-green.log` **EXIT=0**. The working tree was clean before the commit
  (`git status --porcelain` empty).
- **The test is deterministic.** It opens two fixed source files through the
  build's `QT_TESTCASE_SOURCEDIR` define and scans their text. There is no
  clock, sleep, thread, socket or shared mutable state.
- **The fix is at the cause and complete.** All six compile errors were the
  same identifier and all six are qualified; `Parts2.h:132,134` already was;
  only `ControlShutdownHookTest.cpp` includes the part headers.
- **No collateral damage.** No product source, save/load path, undo step,
  real-time path or command is changed by this run.
- **ALREADY-FIXED evidence.** `include/ControlServer.h:96` (the class member),
  `tests/src/core/ControlShutdownHookTestParts1.h:149,155,161,169,196,209`
  (qualified references) and `tests/src/core/ControlShutdownHookTest.cpp:483`
  (the passing regression). The fix commit `5854c983c8` is an ancestor of
  `HEAD` (`merge-base --is-ancestor` EXIT=0).
