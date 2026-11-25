ALREADY-FIXED

# Cluster ci-b5ac4280f3e4b317-port — hosted macos-arm64 Qt 5 build: ambiguous QStringList brace assignment

## Finding

The cluster record is `Port: CI regression: build / macos-arm64`. The prior
round's report on this branch quoted the hosted CI compile error:

```
tests/src/core/ImportTrackIoTest.cpp:344:18: error: use of overloaded operator '=' is ambiguous
  (with operand types 'QStringList' and 'void')
```

At that revision the line was
`plan.documents = {UnparsableTrack, trackDocument(Lead)};`. Qt 5.15's
`QStringList` has assignment overloads for both `QStringList` and its
`QList<QString>` base, so a brace list is ambiguous. Qt 6 aliases
`QStringList` to `QList<QString>` and compiles it.

## Decision: ALREADY-FIXED on this branch, with a branch-owned regression test

The call site is already portable:

```
tests/src/core/ImportTrackIoTest.cpp:414-418
  414		control::transplant::TrackPlan plan;
  415		plan.documents.clear();
  416		plan.documents.append(UnparsableTrack);
  417		plan.documents.append(trackDocument(Lead));
  418		plan.need = {0, 1};
```

`git blame -L 414,418` attributes 415-417 to `14e637c600`; `git merge-base
--is-ancestor 14e637c600 HEAD` exits 0, so the clear-plus-two-appends form is
already on this branch. This is the fix the contract requires; it is not a
product-code change made by this port lane.

This port branch now owns the regression test. `tests/Qt5QStringListBraceCheck.py`
was extended to pin the target's exact forms:

- `FIXTURE_GREEN` lines 149-155 contain the exact
  `plan.documents.clear()` plus two `plan.documents.append(...)` calls, so the
  target's accepted shape is exercised by the scanner.
- `FIXTURE_GREEN_CLEAR_APPEND` (lines 163-168) and `self_test()` (lines
  412-417) fail the scanner if the green fixture silently drops that form.
- `FIXTURE_RED` line 128 and `FIXTURE_RED_LINES` line 161 pin the exact
  historical `plan.documents = {UnparsableTrack, trackDocument(Lead)};` shape
  as a finding at line 10 of the fixture.

It is registered in `tests/CMakeLists.txt:6627-6632` (CTest
`Qt5QStringListBraceCheck`) and `tests/fork-sources.txt:934`. `ctest -N` from
`build/tests` reports `#840: Qt5QStringListBraceCheck` and `#387:
ImportTrackIoTest`.

## Test matrix

| Path | Case | Expected | Result |
|---|---|---|---|
| Normal build/runtime | `ImportTrackIoTest` on the branch | builds and passes | `build-1-green.log` exit 0; `test-1-green.log` / `test-3-green-final.log` exit 0 |
| Failure/defect | historical ambiguous assignment restored at the target site | scanner fails and names line 415 | `scan-2-red.log` exit 1; `test-2-red.log` (ctest) exit 8 |
| Exhaustion/reconfiguration | N/A: compile-time overload resolution, no runtime resource path | — | no product runtime path touched |
| Audio thread | N/A: no process/render code changed; test-only change | no allocation, lock, wait or I/O added | unchanged |
| Supported platform, Qt 5 | Qt 5.15.19 installed here; scanner compile probe | Qt 5 rejects the brace form, accepts `QStringList{...}` | probe line in `scan-1-green.log` and both green CTests |
| Hosted macOS arm64 | not reproducible on this Linux host | CI is the final verifier | no Darwin toolchain here; the defect is Qt 5 overload resolution, reproduced against local Qt 5.15.19 |

## Commands and exits (this session)

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-b5ac4280f3e4b317-port.r3/logs`.

```
$ python3 tests/Qt5QStringListBraceCheck.py > .../logs/scan-1-green.log 2>&1; echo EXIT=$?
EXIT=0
  self-test: the brace assignment is flagged and every portable form is not
  probe: Qt 5 rejects the brace assignment and accepts QStringList{...}
  PASS: no ambiguous QStringList brace assignment in the first-party tree

$ cmake --build build --target ImportTrackIoTest -j4 > .../logs/build-1-green.log 2>&1; echo EXIT=$?
EXIT=0
  [100%] Built target ImportTrackIoTest

$ cd build/tests && ctest -R '^(Qt5QStringListBraceCheck|ImportTrackIoTest)$' --output-on-failure > .../logs/test-1-green.log 2>&1; echo EXIT=$?
EXIT=0
  1/2 Test #387: ImportTrackIoTest ... Passed
  2/2 Test #840: Qt5QStringListBraceCheck ... Passed
  100% tests passed out of 2

# RED control: replace lines 415-417 with the historical
# `plan.documents = {UnparsableTrack, trackDocument(Lead)};` at the same site.
$ python3 tests/Qt5QStringListBraceCheck.py > .../logs/scan-2-red.log 2>&1; echo EXIT=$?
EXIT=1
  tests/src/core/ImportTrackIoTest.cpp:415: `plan.documents = {...}` is ambiguous on Qt 5; name the type: `plan.documents = QStringList{...}`
  FAIL: 1 ambiguous QStringList brace assignment(s)

$ cd build/tests && ctest -R '^Qt5QStringListBraceCheck$' --output-on-failure > .../logs/test-2-red.log 2>&1; echo EXIT=$?
EXIT=8
  0% tests passed, 1 tests failed out of 1
  The following tests FAILED:
    840 - Qt5QStringListBraceCheck (Failed)

# Restore the branch's clear-plus-two-appends form.
$ git diff --exit-code -- tests/src/core/ImportTrackIoTest.cpp; echo EXIT=$?
EXIT=0

$ cmake --build build --target ImportTrackIoTest -j4 > .../logs/build-2-restored-green.log 2>&1; echo EXIT=$?
EXIT=0

$ cd build/tests && ctest -R '^(Qt5QStringListBraceCheck|ImportTrackIoTest)$' --output-on-failure > .../logs/test-3-green-final.log 2>&1; echo EXIT=$?
EXIT=0
  100% tests passed out of 2

$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
  RESULT: PASS
```

## Self-check

- **Regression test RED/GREEN:** the product fix is already on this branch, so
  the "without the fix" state is the historical ambiguous assignment at the
  same site. With lines 415-417 replaced by
  `plan.documents = {UnparsableTrack, trackDocument(Lead)};`, the scanner
  exited **1** and CTest `Qt5QStringListBraceCheck` exited **8**
  (`scan-2-red.log`, `test-2-red.log`), naming
  `tests/src/core/ImportTrackIoTest.cpp:415`. Restoring the branch's
  clear-plus-two-appends form (`git diff --exit-code` exit 0) made the same
  scanner exit **0** and CTest pass **2/2** with exit **0**
  (`scan-1-green.log`, `test-1-green.log`, `test-3-green-final.log`).
- **Deterministic:** the check is a fixed-string source scan plus a compiler
  verdict on fixed fixtures. It uses no wall clock, sleeps, threads, random
  input or timing; the fixture self-test runs before every scan so a scanner
  that cannot fail fails itself.
- **Fix at the cause and complete:** the cause is the ambiguous Qt 5
  `QStringList` assignment. The scanner resolves every brace assignment's
  target to its declared type across the whole first-party tree; the green run
  reports `PASS: no ambiguous QStringList brace assignment in the first-party
  tree`. No product runtime caller or command family is involved, so there is
  no sibling command to update.
- **No collateral damage:** the change is confined to a test fixture and its
  self-test. It builds/asserts the same `TrackPlan` behaviour, changes no
  product code or saved-data path, and the adjacent `ImportTrackIoTest` suite
  passes (#387) before and after the RED control.
- **ALREADY-FIXED evidence:** `tests/src/core/ImportTrackIoTest.cpp:415-417`
  holds the portable clear/append form, blamed to ancestor commit
  `14e637c600` (`git merge-base --is-ancestor 14e637c600 HEAD` exit 0). Putting
  the historical line back makes the registered scanner fail (exit 8), so the
  current code is what keeps CI green.
- **Sibling commands:** this lane changes no command file. `git show --stat
  c91c76262` lists exactly two files, `tests/Qt5QStringListBraceCheck.py` and
  this report; `git show --name-only c91c76262 -- 'tools/mcp-zene-control/*'
  'src/core/Control*'` is empty. It adds no command id, handler, schema entry,
  reversibility class or undo step. The `app.*` and `arrangement.*` commands
  named by `submit_check.py` are train changes between its `FIX-BASE` and this
  branch's base, not edits from this port lane, so no sibling is affected.

## Other findings

The hosted macOS arm64 job cannot be rerun on this Linux host (no Darwin
toolchain). The defect is Qt 5 overload resolution, and the installed Qt
5.15.19 reproduces the compiler contract the scanner and probe exercise.
