FIXED

# Cluster 5235: equal file timestamps discarded a newer recovery

## Verdict: REAL

`decideRecovery()` treated a recovery as stale when its mtime was *less than or equal to* the
source project's mtime, so an equal stamp was read as "the saved project already holds at least as
much". On a file system whose timestamps are coarser than the writes (whole seconds, or two), a
save and the autosave that follows it land in the same interval, and the user's newer unsaved work
was then discarded without a prompt.

Code as it was (checked out at the start of this session, before any edit):

- `src/core/ProjectRecovery.cpp:113-118` - `recoveryIsStale()` compared with
  `recovery.fileModified <= recovery.sourceProjectModified`.
- `src/core/ProjectRecovery.cpp:269-281` - `decideRecovery()` turned that into
  `RecoveryVerdict::Stale` with the detail *"is not newer than the saved project (%2 <= %3)"*, and
  the comment above it said "Equal mtimes are treated as stale for the same reason".
- `tests/src/core/ProjectRecoveryTest.cpp:151` - the tree's own test
  `aRecoveryAsNewAsTheProjectIsNotOffered()` codified that behaviour, so it was the regression
  target, not a counter-example.
- Caller: `src/core/main.cpp:1698-1703` uses only `offered()` and `detail`, so the change is
  invisible to it. `src/core/RevisionTimeline.cpp:213-230` uses only the sidecar's identity rule
  (another project), never the mtime comparison, so it has no sibling copy of the rule to fix.

## The fix

`src/core/ProjectRecovery.cpp`:

- `recoveryIsStale()` is replaced by two named predicates: `recoveryIsOlderThanTheProject()`
  (line 115, strict `<`) and `stampsCannotOrderTheWrites()` (line 126, `==`).
- `decideRecovery()` keeps `Stale` for the strictly-older case only (line 297) and, for equal
  stamps, offers the recovery with a detail that says why (line 316): *"recovering '%1', autosaved
  %2: equal to the saved project's time (%3), which cannot order the two writes"*.

`include/ProjectRecovery.h:85` and `:143-147` - the `Stale` contract and the function comment now
state the rule the code implements (strictly older is stale, equal is uncertain).

The offer is non-destructive: `src/core/main.cpp:1733-1795` opens the recovery file, never the
project file, and the prompt still offers Discard. No control command was added, no audio-thread
code is touched (this is a pure comparison called once at start-up on the main thread), and undo is
not involved.

`docs/AUTOSAVE-RECOVERY.md:135` and `:253-260` documented the old rule ("Equal timestamps are
treated as stale on purpose") and were updated with the rule as it now is.

## Regression test

`tests/src/core/ProjectRecoveryTest.cpp`:

- `aRecoveryAsNewAsTheProjectIsOfferedBecauseEqualStampsCannotOrderTheWrites()` (line 151) -
  replaces `aRecoveryAsNewAsTheProjectIsNotOffered()`; hand-built `RecoveryInfo` with equal
  timestamps must be offered, and the reason must name the equality.
- `readingRealFilesOffersARecoveryWithTheSameTimeAsTheProject()` (line 168) - the same case through
  the real file system in a `QTemporaryDir`: both files written and stamped with the same instant
  via `QFile::setFileTime` before the decision is taken.

The strictly-older negative control (`aRecoveryOlderThanTheProjectIsNotOffered`,
`readingRealFilesRefusesAStaleRecovery`) and the identity control
(`aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne`) are unchanged, so the test cannot pass
by offering everything.

## Test matrix (checked before editing)

| Path | Case | Test |
|---|---|---|
| normal | recovery newer than the project | `freshRecoveryOfTheProjectBeingOpenedIsOffered` |
| normal | launch naming no project | `aLaunchThatNamesNoProjectIsOfferedItsRecovery` |
| normal | untitled project | `aProjectWithNoFilenameYetIsOffered` |
| failure (the bug) | equal mtimes | the two new tests above |
| failure control | recovery strictly older | `aRecoveryOlderThanTheProjectIsNotOffered`, `readingRealFilesRefusesAStaleRecovery` |
| identity control | recovery of another project | `aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne`, `readingRealFilesRefusesARecoveryOfAnotherProject` |
| exhaustion / limit | sidecar over `MaxRecoveryIdentityBytes` | `anOversizedIdentitySidecarIsReadAsAbsent` |
| reconfiguration | project file gone, no sidecar, empty file, absent file | `aRecoveryWhoseProjectFileIsGoneIsStillOffered`, `aRecoveryFileWithNoSidecarIsReadAndOffered`, `anEmptyRecoveryFileIsNotOffered`, `readingAnAbsentRecoveryFileSaysSo` |
| audio thread | none: `decideRecovery()`/`readRecoveryInfo()` run once at start-up on the main thread, no allocation, lock, wait or log added | n/a (no realtime path touched) |
| platform | pure `QDateTime`/`QFileInfo` comparisons; the test injects the stamps, so it does not depend on the file system's timestamp resolution. Built and run here on Linux x86_64, Qt 6, gcc | `cmake --build build --target ProjectRecoveryTest` |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5235.r1/logs`.

| Step | Command (from the worktree) | Exit |
|---|---|---|
| baseline build | `cmake --build build --target ProjectRecoveryTest -j4 > .../logs/build-1-baseline.log 2>&1` | 0 |
| baseline test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ProjectRecovery --output-on-failure > .../logs/test-1-baseline.log 2>&1` | 0 (1/1 ctest passed) |
| RED build (new test, product code unchanged) | `cmake --build build --target ProjectRecoveryTest -j4 > .../logs/build-2-red.log 2>&1` | 0 |
| RED test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ProjectRecovery --output-on-failure > .../logs/test-2-red.log 2>&1` | **8** - `Totals: 28 passed, 2 failed` (the 28 pre-existing tests still pass); both new tests `FAIL!`, detail `Stale: recovery of 'Track.mmp' is not newer than the saved project (… <= …)` |
| GREEN build | `cmake --build build --target ProjectRecoveryTest -j4 > .../logs/build-4-green.log 2>&1` | 0 |
| GREEN test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ProjectRecovery --output-on-failure > .../logs/test-4-green.log 2>&1` | 0 (`100% tests passed out of 1`) |
| GREEN totals | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectRecoveryTest > .../logs/test-5-green-direct.log 2>&1` | 0 - `Totals: 30 passed, 0 failed` |
| product rebuild after the header edit | `cmake --build build --target zene -j4 > .../logs/build-11-zene-final.log 2>&1` | 0 |
| family build | `cmake --build build --target RevisionTimelineTest ProjectRevisionRestoreStagingTest DataFileContainerRecoveryTest -j4 > .../logs/build-7-family.log 2>&1` | 0 |
| family test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ProjectRecoveryTest\|RevisionTimelineTest\|ProjectRevisionRestoreStagingTest\|DataFileContainerRecoveryTest" --output-on-failure > .../logs/test-8-family.log 2>&1` | 0 (4/4 passed) |
| 4b diff | `git diff -- src/core/ProjectRecovery.cpp include/ProjectRecovery.h > .../logs/fix.patch` | 0 (111 lines) |
| 4b revert | `git apply -R .../logs/fix.patch` | 0 |
| 4b build without the fix | `cmake --build build --target ProjectRecoveryTest -j4 > .../logs/build-9-without-fix.log 2>&1` | 0 |
| 4b test without the fix | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ProjectRecoveryTest --output-on-failure > .../logs/test-9-without-fix.log 2>&1` | **8** - `Totals: 28 passed, 2 failed` |
| 4b restore | `git apply .../logs/fix.patch` | 0 |
| 4b build with the fix | `cmake --build build --target ProjectRecoveryTest -j4 > .../logs/build-10-with-fix.log 2>&1` | 0 |
| 4b test with the fix | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ProjectRecoveryTest --output-on-failure > .../logs/test-10-with-fix.log 2>&1` | 0 (1/1 passed, no `FAIL!` line) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1-feature-check.log 2>&1` | 0 - 8 rows PASS, `RESULT: PASS` |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Without the fix (`git apply -R .../fix.patch`, rebuilt): `test-9-without-fix.log`, EXIT=8,
  `Totals: 28 passed, 2 failed`, both new tests `FAIL!`. With the fix restored and rebuilt:
  `test-10-with-fix.log`, EXIT=0, no `FAIL!`. The product files are
  `src/core/ProjectRecovery.cpp` and `include/ProjectRecovery.h`; `fix.patch` contains only those
  two files, never the test.
- **The test is deterministic.** No sleeps, no wall clock, no threads. One case injects
  `QDateTime` values straight into `RecoveryInfo` (`at(11, 11)` for both files); the other injects
  them through `QFile::setFileTime()` on an open `QFile` in a `QTemporaryDir` and then reads them
  back with `readRecoveryInfo()`. Both cases fix the two stamps to the same value, so the file
  system's own timestamp resolution cannot change the outcome.
- **The fix is at the cause and complete.** The cause is the `<=` in the freshness predicate; the
  predicate is now split so that only a strict ordering marks `Stale`. Callers checked:
  `src/core/main.cpp:1698-1703` (uses `offered()`/`detail`, unchanged contract) and
  `src/gui/MainWindow.cpp:1876-1901` (`autoSave()` writes the sidecar; it never decides). Sibling
  checked: `src/core/RevisionTimeline.cpp:213-230` re-reads the same sidecar but applies only the
  identity rule, so it carries no copy of the mtime comparison. Header comment and the
  `RecoveryVerdict::Stale` contract were updated with the code, and every target including the
  header was rebuilt (`zene` EXIT=0, `build-11-zene-final.log`).
- **No collateral damage.** Old projects and saved data are untouched: the decision only chooses
  whether the start-up prompt appears, and no file is written or deleted by it. A recovery that is
  strictly older is still refused (`aRecoveryOlderThanTheProjectIsNotOffered`,
  `readingRealFilesRefusesAStaleRecovery`), a recovery of another project is still refused
  (`aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne`), an upstream recovery with no
  sidecar is still offered, and the oversized-sidecar limit is unchanged. Nothing was added that a
  legitimate caller could exceed - the change widens nothing but the equal-stamp case, and only in
  the "offer" direction, which opens the recovery file rather than overwriting the project.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED.
- **Sibling commands in the command files I changed:** I changed no command file. The listed
  `app.*` and `arrangement.*` commands live in the control-surface files, which this fix does not
  touch, and no control command was added, so `ControlCommandsSnapshot` is unaffected.

## Other findings

- None observed inside this worktree. (The pre-submit check's "tests added or changed" row lists
  `tests/control-audio-test-tone.py` and friends from other lanes on this shared branch; they are
  not part of this fix.)
