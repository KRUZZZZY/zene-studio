ALREADY-FIXED

# Cluster ci-b5ac4280f3e4b317-port — hosted macos-arm64 Qt 5 build: ambiguous QStringList brace assignment

## Finding

The queue record for `ci-b5ac4280f3e4b317` names CI run
[43611324689](https://github.com/KRUZZZZY/zene-studio/actions/runs/43611324689), job
`132496796743`, train `050/m0-train-31` at
`c20ef591b571e3c3dc7b108107d166497f1b34be`. Its log contains exactly one compile
error:

```
macos-arm64	Build	2026-11-14T16:13:39.0352600Z /Users/runner/work/zene-studio/zene-studio/tests/src/core/ImportTrackIoTest.cpp:344:18: error: use of overloaded operator '=' is ambiguous (with operand types 'QStringList' and 'void')
macos-arm64	Build	2026-11-14T16:13:39.0594540Z make[2]: *** [tests/CMakeFiles/ImportTrackIoTest.dir/src/core/ImportTrackIoTest.cpp.o] Error 1
```

At that revision the line was
`plan.documents = {UnparsableTrack, trackDocument(Lead)};`. Hosted Qt 5.15's
`QStringList` declares the base `QList<QString>` assignment overloads as well as
its own, so a brace list is ambiguous. Qt 6 aliases `QStringList` to
`QList<QString>` and therefore compiles the same line.

## Decision: ALREADY-FIXED on this branch

The same source site is already written in a portable form:

```
tests/src/core/ImportTrackIoTest.cpp:414-418
  414		control::transplant::TrackPlan plan;
  415		plan.documents.clear();
  416		plan.documents.append(UnparsableTrack);
  417		plan.documents.append(trackDocument(Lead));
  418		plan.need = {0, 1};
```

`git blame` attributes lines 415-417 to the consolidated train commit
`14e637c600`, and that commit is an ancestor of this branch's HEAD
(`git merge-base --is-ancestor 14e637c600 HEAD` → exit 0). The original branch
commit `7bc7a2f63d` is not a direct ancestor (exit 1), but `14e637c600` is the
consolidation that carried its fix onto this line: `git show --stat 14e637c600 --
tests/src/core/ImportTrackIoTest.cpp` shows the same file changed 3 insertions /
1 deletion, replacing the brace assignment with `clear()` plus two `append()`
calls. The behaviour and the resulting document list are identical.

The shipped regression is present and registered:

- `tests/Qt5QStringListBraceCheck.py` — the self-testing, whole-tree scanner. It
  flags the ambiguous assignment, accepts the portable spellings, and compiles
  the failing and fixed shapes against an installed Qt 5.
- `tests/CMakeLists.txt:6627-6632` — registers CTest
  `Qt5QStringListBraceCheck`; `ctest -N` reports it as test **#840**.
- `tests/fork-sources.txt:934` — registers the scanner (the original report is
  at line 937).

## Test matrix

| Path | Case | Expected | Result |
|---|---|---|---|
| Normal build/runtime | `ImportTrackIoTest` suite on the current branch | builds and passes | `build-1-green.log` exit 0; `test-1-green.log` and `test-3-green-final.log` exit 0 (test #387 passed) |
| Failure/defect | re-introduce the historical brace assignment at the same site | scanner fails and names the line | `test-2-red.log` exit 8: `tests/src/core/ImportTrackIoTest.cpp:415: ... is ambiguous on Qt 5` |
| Exhaustion/reconfiguration | N/A: this is a compile-time overload-resolution defect, not a runtime resource path | — | no product runtime path touched |
| Audio thread | N/A: no render/process code changed | no allocation, lock, wait or I/O added | unchanged |
| Supported platform, Qt 5 | Qt 5.15.19 is installed here; scanner compile probe | Qt 5 rejects the brace form and accepts `QStringList{...}` | `scan-1-tree.log` and both green CTests print the probe line |
| Hosted macOS arm64 | not reproducible on this Linux host | CI is the final verifier | no Darwin toolchain here; the defect and fix are Qt-5 overload resolution, proven locally against Qt 5.15.19 |

## Commands and exits (this session)

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-b5ac4280f3e4b317-port.r2/logs`.

```
$ python3 tests/Qt5QStringListBraceCheck.py > .../logs/scan-1-tree.log 2>&1; echo EXIT=$?
EXIT=0
  self-test: the brace assignment is flagged and every portable form is not
  probe: Qt 5 rejects the brace assignment and accepts QStringList{...}
  PASS: no ambiguous QStringList brace assignment in the first-party tree

$ cd build/tests && ctest -N -R Qt5QStringListBraceCheck > .../logs/ctest-list-1.log 2>&1; echo EXIT=$?
EXIT=0
  Test #840: Qt5QStringListBraceCheck

$ cmake --build build --target ImportTrackIoTest -j4 > .../logs/build-1-green.log 2>&1; echo EXIT=$?
EXIT=0

$ cd build/tests && ctest -R '^(Qt5QStringListBraceCheck|ImportTrackIoTest)$' --output-on-failure > .../logs/test-1-green.log 2>&1; echo EXIT=$?
EXIT=0
  100% tests passed out of 2

# Negative control: reintroduce the historical line at the same site, run the
# registered test, then restore.
$ ctest -R '^Qt5QStringListBraceCheck$' --output-on-failure > .../logs/test-2-red.log 2>&1; echo EXIT=$?
EXIT=8
  tests/src/core/ImportTrackIoTest.cpp:415: `plan.documents = {...}` is ambiguous on Qt 5; name the type: `plan.documents = QStringList{...}`
  FAIL: 1 ambiguous QStringList brace assignment(s)

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

Procedure step 4b's product-patch reversal is not applicable to an
ALREADY-FIXED closure: there is no product hunk to reverse, and `git diff --`
for product files is empty. The equivalent negative control is recorded above:
re-introducing the historical defect into the test source made the registered
test fail (exit 8), and restoring the branch's source made it pass (exit 0).

## Self-check

- **Regression test RED/GREEN:** the fix is already on the branch, so I reverted
  the *defect* at the same site instead of a product hunk. With the historical
  `plan.documents = {...}` line restored, CTest `Qt5QStringListBraceCheck` exited
  **8** and printed `ImportTrackIoTest.cpp:415: ... is ambiguous on Qt 5`
  (`test-2-red.log`). Restoring the branch lines 415-417 made the same CTest,
  together with `ImportTrackIoTest`, pass **2/2** with exit **0**
  (`test-3-green-final.log`). `git diff --exit-code` after restoring exited 0.
- **Deterministic:** the test is a fixed-string source scan plus a compiler
  verdict on two fixed sources. It uses no wall clock, sleeps, threads, random
  input or timing. The scanner first runs its own red/green fixtures
  (`self-test: the brace assignment is flagged...`) so a scanner that cannot
  fail is itself caught.
- **Fix at the cause and complete:** the cause is the ambiguous Qt 5
  `QStringList` assignment. The whole first-party tree is scanned by resolving
  each brace assignment's target to its declared type; both green runs report
  `PASS: no ambiguous QStringList brace assignment in the first-party tree`.
  The only historical occurrence is the line the negative control reintroduced.
  No product runtime caller or command family is involved.
- **No collateral damage:** the change is confined to a test setup statement; it
  builds the same `TrackPlan` and asserts the same behaviour. No old project or
  saved data path, no limit/check and no product code was changed. The adjacent
  `ImportTrackIoTest` suite passes (test #387).
- **ALREADY-FIXED evidence:** `tests/src/core/ImportTrackIoTest.cpp:415-417`
  holds the portable form, blamed to ancestor commit `14e637c600`; the CI error
  site is gone. `tests/Qt5QStringListBraceCheck.py` (registered at
  `tests/CMakeLists.txt:6627`) passes on the branch and fails when the old line
  is put back.
- **Sibling commands:** this lane changes only this report; it adds no command
  id, command handler, schema entry, reversibility class, undo step or
  audio-thread code. The command files listed by `submit_check.py` are train
  changes between its `FIX-BASE` (`050/m0-train-35`) and this branch's train-52
  base, not edits from this port lane, so no sibling command is affected.

## Other findings

None. The hosted macOS arm64 job cannot be rerun on this Linux host (no Darwin
toolchain); the Qt 5.15.19 headers present here reproduce the compiler's
overload-resolution contract, which is what the regression test exercises.
