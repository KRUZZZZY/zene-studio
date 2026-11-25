ALREADY-FIXED

# cluster ci-2fd63d3310176c0c-port - port check: CI regression: build / mingw64

## Verdict and reason

**ALREADY-FIXED on this branch.** The clustered mingw64 failure is the
name-lookup error in `tests/src/core/ControlShutdownHookTestParts1.h`: the two
Windows-only named-pipe cases used the class static member
`ControlServer::ClientIdleTimeoutMs` unqualified at lines 149, 155, 161, 169,
196 and 209. A POSIX build preprocesses those cases away, so only the Windows
lanes saw the compile error. The correction is already in this worktree, and
this round adds the required test-only probe to
`tests/train-51-portability-check.py` (CTest `Train51Portability`) so this
lane's own regression proves the contract red/green.

## Evidence (file:line)

- `include/ControlServer.h:96` declares the value as a class static member:
  `static constexpr int ClientIdleTimeoutMs = 30000;`. A using-directive
  imports a namespace, not a class member.
- `tests/src/core/ControlShutdownHookTest.cpp:90` has `using namespace lmms;`,
  so the unqualified spelling cannot resolve in the test function's scope.
- `tests/src/core/ControlShutdownHookTestParts1.h:149,155,161,169,196,209` all
  now spell `ControlServer::ClientIdleTimeoutMs`. The sibling
  `tests/src/core/ControlShutdownHookTestParts2.h:132,134` already did.
- `tests/train-51-portability-check.py:208-247` is the new
  `shutdown_timeout_probe`: it reads both part headers, requires at least six
  `ClientIdleTimeoutMs` references, fails for any token not immediately
  prefixed by `ControlServer::`, then compiles two fixtures under
  `using namespace lmms` - the qualified name must compile and the bare name
  must fail.
- `tests/train-51-portability-check.py:264` runs the probe from `main()`.
- `tests/CMakeLists.txt:6560-6563` registers the script as CTest
  `Train51Portability`.

History proving the correction is already on this branch:

- `git merge-base --is-ancestor 5854c983c8 HEAD` exits **0**; that commit
  contains the six-line qualifier correction.
- `d9a26d06ad` reconciled the CI repairs and brought the regression and the
  original report onto the train.

## Test matrix

| Path | Exercise |
|---|---|
| Normal | all references qualified -> both source scans and both fixture compiles pass; CTest `Train51Portability` EXIT=0 |
| Failure | one reference unqualified -> the scan fails and names `src/core/ControlShutdownHookTestParts1.h:149` |
| Boundary | fewer than six references -> the count assertion fails, so the scan cannot pass vacuously |
| Invalid input | missing/unreadable part header -> `Path.read_text` raises `OSError`, which `main()` reports and exits 1 |
| Audio thread | n/a: a test-only source scan and compiler fixture, no product path, allocation, lock, wait or log |
| Supported platforms | the scan and fixtures are portable C++20/Python; the Windows named-pipe cases stay under `Q_OS_WIN` |

## Commands and exit codes (this session; logs in the nominated folder)

RED: one qualifier at `ControlShutdownHookTestParts1.h:149` was temporarily
changed back to the bare spelling with `apply_patch`, then:

```
python3 tests/train-51-portability-check.py --source-root . > /home/kruzzzzy/.config/kads/zene/state/runs/ci-2fd63d3310176c0c-port.r3/logs/train51-red-2.log 2>&1
EXIT=1
# train51 portability check: ClientIdleTimeoutMs is a ControlServer member;
# unqualified references: tests/src/core/ControlShutdownHookTestParts1.h:149
```

The spelling was restored with `apply_patch`; `git diff --stat --
tests/src/core/ControlShutdownHookTestParts1.h` then produced no output.

GREEN, with the branch code restored:

```
python3 tests/train-51-portability-check.py --source-root . > /home/kruzzzzy/.config/kads/zene/state/runs/ci-2fd63d3310176c0c-port.r3/logs/train51-green-3.log 2>&1
EXIT=0
# PASS shutdown-timeout-qualified: compiled
# PASS shutdown-timeout-bare: failed as expected
```

CTest, run from `build/tests`:

```
TMPDIR=/tmp ctest -R '^Train51Portability$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/ci-2fd63d3310176c0c-port.r3/logs/ctest-train51-1.log 2>&1
EXIT=0   (1/1 passed)

TMPDIR=/tmp ctest -R '^(ControlShutdownHookTest|Train51Portability)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/ci-2fd63d3310176c0c-port.r3/logs/ctest-family-2.log 2>&1
EXIT=0   (2/2 passed)
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-2fd63d3310176c0c-port > /home/kruzzzzy/.config/kads/zene/state/runs/ci-2fd63d3310176c0c-port.r3/logs/gates-1.log 2>&1
EXIT=0
RESULT: PASS   (fork-sources, all-sources-reproduce, no-upstream-regression,
                check-namespace, no-tautology, doc-figures, complexity,
                file-length all PASS)
```

## No collateral damage

This round changes only `tests/train-51-portability-check.py` and this report.
No product source, API, save format, undo step, control command or audio-thread
path is touched, so old projects and saved data load exactly as before. The
probe scans the same two part headers the C++ regression reads and does not
reject any legitimate caller: only the `ClientIdleTimeoutMs` token is checked,
and the branch has exactly the expected eight qualified references.

## Other findings

- None observed this session beyond the finding above. `ControlShutdownHookTest`
  was run with `TMPDIR=/tmp`, the documented short-path workaround; the family
  passed 2/2.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** The
  product correction predates this run, so the red seam is the same source
  seam the CTest uses: one qualifier was temporarily changed to the original
  bare spelling. `train51-red-2.log` **EXIT=1**, naming
  `src/core/ControlShutdownHookTestParts1.h:149`; after restoring the
  qualifier, `train51-green-3.log` **EXIT=0**. The fixture controls also prove
  the compiler half: `shutdown-timeout-qualified` compiles and
  `shutdown-timeout-bare` fails as expected.
- **The test is deterministic.** It reads two fixed source files and compiles
  two fixed fixtures. There is no clock, sleep, thread, socket or shared
  mutable state.
- **The fix is at the cause and complete.** All six compile errors were the
  same identifier and all six are qualified; `Parts2.h:132,134` already was;
  only `ControlShutdownHookTest.cpp` includes the two part headers. The new
  probe scans both files and counts the references, so a deleted case cannot
  make the check pass vacuously.
- **No collateral damage.** Only the test script and this report are changed
  in this round. No product source, save/load path, undo step, real-time path
  or command is changed.
- **ALREADY-FIXED evidence.** `include/ControlServer.h:96` (the class member),
  `tests/src/core/ControlShutdownHookTestParts1.h:149,155,161,169,196,209`
  (qualified references), `tests/src/core/ControlShutdownHookTestParts2.h:132,134`
  and `tests/src/core/ControlShutdownHookTest.cpp:483` (the passing C++
  regression). The fix commit `5854c983c8` is an ancestor of `HEAD`
  (`merge-base --is-ancestor` EXIT=0).
- **Sibling-command ASK.** The `app.*` and `arrangement.*` ids listed by
  `submit_check.py` come from other lanes' commits in the train range
  `050/m0-train-35..HEAD`, not from this run. Before this commit
  `git status --porcelain` showed only `M
  tests/train-51-portability-check.py`; this commit adds only that file and
  this report. No command file, registry or reversibility table is changed
  here, so no sibling command is affected.
