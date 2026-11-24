ALREADY-FIXED

Cluster `review-02492a165` — "Revision rotation can still lose old destination on rename failure"
(`src/core/ProjectRevisions.cpp`). The rotation no longer removes the destination before the
staged rename: the replacement is one replacing `rename(2)`/`MoveFileEx`, and the regression the
finding asks for (fail the rename *after* valid staging, not staging creation) already exists and
was proven RED against the pre-fix shape in this session.

## Evidence from this branch

The destination is never deleted before the move. `copyRevision()` stages the copy into a
uniquely named sibling and then replaces the destination in one step:

- `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-02492a165/src/core/ProjectRevisions.cpp:124`
  — `const QString staged = stageCopy(from, to + QStringLiteral(".rotate-XXXXXX"));`
  (the staging helper is `stageCopy()` at `ProjectRevisions.cpp:85`, a `QTemporaryFile` with
  `setAutoRemove(false)`; a failed write removes only that staging file).
- `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-02492a165/src/core/ProjectRevisions.cpp:126`
  — `if (!replaceWithStaged(staged, to))` — the only write into the destination.
- `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-02492a165/src/core/ProjectRevisions.cpp:65`
  — `replaceWithStaged()`; POSIX path is `std::rename()` at
  `ProjectRevisions.cpp:78`, Windows path is `std::filesystem::rename()` (wide paths) at
  `ProjectRevisions.cpp:72`. Both replace an existing file in one step, so a failure leaves `to`
  byte-identical. There is no `QFile::remove(to)` (or any destination deletion) anywhere in
  `copyRevision()`; the only `QFile::remove()` in it removes `staged`.

Both rotation callers go through that one function, so both are covered:
`shiftRevisionsUp()` at `ProjectRevisions.cpp:275` (the rev(n-1) -> rev(n) shift, call at
`ProjectRevisions.cpp:284`) and the `rev0` store at `ProjectRevisions.cpp:353`. The restore
replacement (`replaceProjectWithStaged()`, `ProjectRevisions.cpp:465`) uses the same
`replaceWithStaged()`.

The removal the finding describes was in the pre-fix code and was repaired by an earlier rework on
this branch: commit `6dbeb12f43` "fix(rework 02492a165): a rotation removed the slot before its
replacement could fail". It is an ancestor of this worktree's `HEAD`
(`git merge-base --is-ancestor 6dbeb12f43 HEAD` -> EXIT=0), and its diff is what introduced
`replaceWithStaged()` plus the test seam `setRevisionReplacementFailureForTest()`
(`ProjectRevisions.cpp:60`, declaration `include/ProjectRevisions.h:105`).

The required regression — rename failure *after* a valid staged copy — exists at
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-02492a165/tests/src/core/RevisionTimelineTestParts1.h:381`
(`RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheSlot`, declared at
`tests/src/core/RevisionTimelineTest.cpp:239`). It writes `rev1` and `rev2`, fails only the
replacement step, and asserts `rev2` still exists with its exact bytes, that the rotation reports
a refusal, that nothing was kept, and that no `.rotate-*` staging file is left. Its sibling
`aFailedShiftKeepsTheBackupAndReportsARefusal` (`RevisionTimelineTestParts1.h:300`) covers the
staging-creation failure the finding says is *not* enough on its own.

## RED/GREEN proof that the existing regression bites

I reintroduced the exact pre-fix shape in a scratch patch (`QFile::remove(to);` immediately before
`replaceWithStaged()` in `copyRevision()`, saved as
`/home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/bug-reintroduced.patch`),
rebuilt, and ran the slot: it FAILED with the finding's symptom. I then reversed the patch,
rebuilt, and the test and its CTest family passed. The worktree is clean of that experiment
(`git status --short` printed nothing, `git diff --stat` printed nothing).

| Evidence | Command | Result |
| --- | --- | --- |
| Build the regression target | `cmake --build build --target RevisionTimelineTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/build-1.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Full suite on branch code (normal/list/compare/restore paths) | from `build/tests`: `ctest -R "^RevisionTimelineTest$" --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/test-2-ctest-RevisionTimelineTest.log 2>&1; echo EXIT=$?` | EXIT=0, 1/1 passed |
| RED: pre-fix non-atomic replacement reintroduced | `git diff -- src/core/ProjectRevisions.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/bug-reintroduced.patch; echo EXIT=$?` | EXIT=0 |
| RED build | `cmake --build build --target RevisionTimelineTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/build-2-bug-reintroduced.log 2>&1; echo EXIT=$?` | EXIT=0 |
| RED run of `aFailedReplacementAfterStagingKeepsTheSlot` | from `build/tests`: `./RevisionTimelineTest aFailedReplacementAfterStagingKeepsTheSlot > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/test-3-red-slot-with-bug.log 2>&1; echo EXIT=$?` | EXIT=1 — `FAIL! : 'QFileInfo::exists(rev2)' returned FALSE. (the failed replacement emptied the slot it would have replaced)` at `tests/src/core/RevisionTimelineTestParts1.h(395)` |
| Undo the experiment | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/bug-reintroduced.patch; echo EXIT=$?` | EXIT=0; `git status --short` empty afterwards |
| GREEN rebuild | `cmake --build build --target RevisionTimelineTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/build-3-restored.log 2>&1; echo EXIT=$?` | EXIT=0 |
| GREEN run of the CTest family | from `build/tests`: `ctest -R "RevisionTimelineTest\|ProjectRevisionRestoreStagingTest" --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/test-5-green-ctest.log 2>&1; echo EXIT=$?` | EXIT=0, 2/2 passed |
| Earlier family attempt that named the sibling before it was built | same ctest line, log `test-4-green-ctest.log` | EXIT=8 — `ProjectRevisionRestoreStagingTest` Not Run (executable not built yet); built it (`build-4-sibling-test.log`, EXIT=0) and re-ran: `test-5`, EXIT=0 |
| Fix ancestry | `git merge-base --is-ancestor 6dbeb12f43 HEAD; echo ANCESTOR_EXIT=$?` | EXIT=0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-02492a165 > /home/kruzzzzy/.config/kads/zene/state/runs/review-02492a165.r1/logs/gate-1-feature_check.log 2>&1; echo EXIT=$?` | EXIT=0; `RESULT: PASS`, all eight rows PASS |

## Test matrix

- Normal path: the whole `RevisionTimelineTest` suite (list, compare, restore, reversible
  rotation) — `test-2-ctest-RevisionTimelineTest.log`, EXIT=0.
- Failure path: rename fails after a valid staged copy — `aFailedReplacementAfterStagingKeepsTheSlot`;
  PASS on branch code (`test-2`), FAIL with the pre-fix shape (`test-3`, EXIT=1). Staging-creation
  failure — `aFailedShiftKeepsTheBackupAndReportsARefusal`; PASS in `test-1`/`test-2`.
- Exhaustion: the per-revision cap paths (`anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves`,
  `aRevisionExactlyAtTheCapStillRestores`, `anEmptyPendingStageIsRefusedAndTheLiveProjectKept`) in
  `ProjectRevisionRestoreStagingTest` — PASS, `test-5-green-ctest.log`, EXIT=0.
- Reconfiguration: none. `ProjectRevisionPolicy::Keep` (3) and `MaxRevisionBytes` are compile-time
  constants (`include/ProjectRevisions.h:56` and `include/ProjectRevisions.h:57`), so there is no
  runtime reconfiguration path to test.
- Audio-thread constraints: the file is pure filesystem work reached from control-command handlers
  (`ControlCommandsProjectFiles.cpp:276`) and `RevisionTimeline.cpp:464`; nothing here runs on the
  audio thread. No product change was made, so nothing new allocates, locks, waits or logs there.
- Supported platforms: the POSIX branch (`std::rename`, `ProjectRevisions.cpp:78`) is what this
  Linux box exercises. The Windows branch (`std::filesystem::rename` over wide paths,
  `ProjectRevisions.cpp:72`) and the macOS deployment-target path are existing code I did not
  touch and cannot execute here — not verified in this session.

## Verdict rationale

The finding describes the code as of before commit `6dbeb12f43`. On this branch the destination is
replaced in one atomic step, a failure after valid staging leaves it byte-identical, and the exact
regression the finding demands is present and demonstrably RED against the pre-fix shape and GREEN
against the branch. No product change was needed; this report is the only file committed.

## Other findings

Running a hand-picked subset of `RevisionTimelineTest` slots in an order other than their
declaration order (`test-1-slot-runs.log`, EXIT=1) makes `aFailedReplaceKeepsTheLiveProject` fail
because its `.restore` staging file from the earlier slot persists in the case-level temp
directory. The full suite in declaration order passes (`test-2`, EXIT=0), so this is an ordering
artifact of the ad-hoc subset, not a product defect; not changed here.

## Self-check

Each pre-submit `?` item, answered with this session's evidence:

- **The regression test FAILS with the product change reverted and PASSES with it.**
  - Reverted (non-atomic pre-fix shape reintroduced): `./RevisionTimelineTest
    aFailedReplacementAfterStagingKeepsTheSlot` -> **EXIT=1**,
    `FAIL! : 'QFileInfo::exists(rev2)' returned FALSE. (the failed replacement emptied the slot it
    would have replaced)` at `tests/src/core/RevisionTimelineTestParts1.h(395)`, log
    `test-3-red-slot-with-bug.log`.
  - With the branch code: same binary after `git apply -R bug-reintroduced.patch` and a rebuild ->
    `ctest -R "RevisionTimelineTest|ProjectRevisionRestoreStagingTest"` -> **EXIT=0**, 2/2 passed,
    log `test-5-green-ctest.log`.
  - The reverted-run patch, its build and the reverse-apply are `bug-reintroduced.patch`
    (EXIT=0), `build-2-bug-reintroduced.log` (EXIT=0), `git apply -R` (EXIT=0),
    `build-3-restored.log` (EXIT=0).
- **The test is deterministic.** No wall clock, sleep or race. The failure is injected by the
  existing test seam `setRevisionReplacementFailureForTest(true)`
  (`include/ProjectRevisions.h:105`, flag `g_failReplacementForTest` at
  `ProjectRevisions.cpp:60`) which makes only the replacement step of `copyRevision()` fail; the
  staged copy itself is a real, complete file. The fixture values are literal documents
  (`documentWith(6)`, `documentWith(9)`) compared byte for byte, and the slot asserts the exact
  retained-count integer `0`.
- **The fix is at the cause and complete.** `copyRevision()` is the single write path into a
  revision slot; every caller and sibling goes through it: `shiftRevisionsUp()`
  (`ProjectRevisions.cpp:284`), the `rev0` store (`ProjectRevisions.cpp:353`),
  `restoreProjectRevision()` (`ProjectRevisions.cpp:447`, via
  `replaceProjectWithStaged()` at `ProjectRevisions.cpp:465`) and the timeline restore
  (`src/core/RevisionTimeline.cpp:464`, `RevisionTimeline.cpp:472`). All of them obtain the
  destination-preserving replacement from `replaceWithStaged()` (`ProjectRevisions.cpp:65`), so
  none of them can still delete a destination before a rename. The rotation's refusal path is
  consistent: `shiftRevisionsUp()` and `rotateProjectRevision()` set `refused` and their callers
  (`ControlCommandsProjectFiles.cpp:276`, `RevisionTimeline.cpp:464`) report the refusal instead of
  installing bytes. No product file was changed for this verdict, so nothing else can drift.
- **No collateral damage.** No product file changed (`git diff --stat` empty before the report
  commit), so project documents, `keep-3` retention, the 8 MiB per-revision cap and every restore
  path behave exactly as they did. The existing cap checks (`ProjectRevisions.cpp` size guards) are
  untouched, so no caller that legitimately exceeds a limit is newly rejected.
- **ALREADY-FIXED evidence (file:line).** `ProjectRevisions.cpp:124` stages into a unique sibling,
  `ProjectRevisions.cpp:126` replaces through `replaceWithStaged()` (`ProjectRevisions.cpp:65`),
  whose POSIX step is `std::rename()` at `ProjectRevisions.cpp:78` and whose Windows step is
  `std::filesystem::rename()` at `ProjectRevisions.cpp:72`; there is no destination removal in
  `copyRevision()`. The rework that introduced this is commit `6dbeb12f43`, an ancestor of `HEAD`
  (`git merge-base --is-ancestor 6dbeb12f43 HEAD` -> EXIT=0), and its regression is
  `RevisionTimelineTestParts1.h:381` (declared `RevisionTimelineTest.cpp:239`).
- **Sibling commands in command files changed: none.** This verdict changes no command file and no
  command handler, so `app.*`, `arrangement.*` (and the rest of the listed sibling commands) are
  untouched and unaffected; the only rotation caller in the command layer,
  `ControlCommandsProjectFiles.cpp:276`, is unmodified.
- **Registration and gates.** No new product file, so `tests/fork-sources.txt` and
  `tests/upstream-modifications.txt` need no entry; `feature_check.py` -> EXIT=0, all eight rows
  PASS (`gate-1-feature_check.log`).
- **`git status`** was clean after the experiment and before the report commit, and clean after it
  (`submit-1.log`, one commit `a776babb19`).
