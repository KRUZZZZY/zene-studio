# Cluster new-p16-16

Verdict: **FIXED**

Luna's finding "Failed handoff can leave a newly created empty folder" is REAL on this branch. `project.handoff` created the destination folder before it wrote the bundle, and every failure after that point (the session save, the media collection, the manifest) left the folder behind. The fix tracks the folders this call created and takes them back when the hand off fails, while a folder that was there before is left alone.

## The finding and the evidence

| Claim | Evidence (file:line, before the fix) |
| --- | --- |
| The destination is created before anything is written | `src/core/ControlCommandsHandoff.cpp:169` `if (!QDir().mkpath(dir)) { ... }` ran before the session copy, the media folder and the manifest were written. |
| A save failure leaves the folder behind | `:187-190` - `if (!song->saveProjectFile(file)) { return ControlResult::failure(...); }` returns without touching the folder `mkpath` had just made. |
| A media-collection failure removes the partial files but not the folder | `:195-199` - `collectProjectMediaInto(...)` fails, `discardPartialBundle(file)` (`:158-163`) removes the session copy and the `media` folder, and the destination folder itself stays. |
| A manifest failure leaves the folder behind too | `:201-205` - same shape: `discardPartialBundle(file)` then a refusal, with the destination folder left. |
| Nothing removed the destination folder | `discardPartialBundle` (`:158-163`) only ever removed the session copy and the `media` folder beside it; `grep -n mkpath src/core/ControlCommandsHandoff.cpp` found exactly one `mkpath` (`:169`) and no `rmdir` anywhere in the file. |

The finding's line 169 was the `mkpath` and is still the `mkpath`; the code around it moved when the body was split into `writeBundle()`.

## The fix

`src/core/ControlCommandsHandoff.cpp`:

- `foldersToCreate()` (`:165-181`) - the folders the destination path needs and does not have yet, outermost first. It stops at the first folder that already exists, so a destination that was there before is never on the list.
- `removeCreatedFolders()` (`:183-192`) - takes those folders back, innermost first, with `QDir::rmdir()`, which removes a folder only while it is empty. A symlink on the path is skipped, so the rollback can never follow a link out of the destination.
- `writeBundle()` (`:194-239`) - the body of the old `handoff()`, unchanged: the session's own file, the taken slots, the media slot, the save, the media collection and the manifest are all exactly as they were.
- `handoff()` (`:241-256`) - validates the argument, works out which folders this call is about to create, creates them, and hands the bundle to `writeBundle()`. If the bundle could not be written, the folders this call created are taken back; a successful hand off keeps them.

The contract is "a hand off folder is all of a bundle or none of it" - the same sentence `discardPartialBundle()` already carried, extended from the files inside the folder to the folder itself.

## The regression test

`tests/src/core/LibraryProjectsTest.cpp` (the file that already holds the hand-off tests) gained two slots and the helpers they need:

- `aFailedHandOffTakesItsNewFolderBack()` (`:562`) - the hand off targets a folder that does not exist (`new-folder/deeper`, so the folder on the way there is created too), the session cannot be saved, the hand off is refused, and the folders it made are gone.
- `aFailedHandOffKeepsTheFolderItDidNotCreate()` (`:580`) - the same refusal into a folder that was there before: the folder stays.
- Helpers: `writeText()` (`:75`), `songProject()` (`:90`) and `loadPartially()` (`:112`).

The failure is injected, not timed: the session is loaded with a section skipped, so `Song::saveProjectFile()` refuses to write a document that would be missing that section (`src/core/Song.cpp:2264-2272`, the partial-load policy). That is the real refusal a user hits, and it happens after the destination folder was created. No sleep, no clock, no race.

The two new slots are the last ones in the class, because the partial load replaces the session's contents: every other slot in this file still sees the session it was written for.

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-16.r1/logs`.

| Step | Command | Exit | Result |
| --- | --- | --- | --- |
| RED (no fix) | `cmake --build build --target LibraryProjectsTest` (build-1.log) | 0 | built |
| RED (no fix) | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-1.log) | **8** | `FAIL! LibraryProjectsTest::aFailedHandOffTakesItsNewFolderBack() '!QFileInfo::exists(dir)' returned FALSE. (the failed hand off left the folder it created behind)` - 19 passed, 1 failed |
| fix attempt 1 | `cmake --build build --target LibraryProjectsTest` (build-2.log) | 2 | did not compile: `QDir::rmdir` is a member in Qt 6, there is no static one. Corrected to the member call `QDir().rmdir(path)` |
| GREEN | `cmake --build build --target LibraryProjectsTest` (build-3.log) | 0 | built |
| GREEN | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-3.log) | **0** | 1/1 passed |
| 4b revert | `git diff -- src/core/ControlCommandsHandoff.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | product fix reverted, the test file untouched |
| 4b RED | `cmake --build build --target LibraryProjectsTest` (build-4.log) | 0 | built |
| 4b RED | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-4.log) | **8** | the same assertion fails again - the test really needs the fix |
| 4b restore | `git apply .../logs/fix.patch` | 0 | fix back in the tree |
| 4b GREEN | `cmake --build build --target LibraryProjectsTest zene` (build-5.log) | 0 | test target and the `zene` binary built |
| 4b GREEN | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-5.log) | **0** | 1/1 passed |
| family | `ctest -R "LibraryProjectsTest\|PartialLoadTest\|ControlProjectArchiveTest\|ControlSuccessPathsTest\|ControlRegistryTest\|ProjectContainerTest" --output-on-failure` (test-6.log) | **0** | 6 of 6 tests passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log, before the report) | **0** | `RESULT: PASS` - every row PASS |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-2.log, with this report) | **0** | `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length: every row PASS |

Family test count: 6 ctest tests, 6 passed, 0 failed (test-6.log).

## Files changed

- `src/core/ControlCommandsHandoff.cpp` - the fix.
- `tests/src/core/LibraryProjectsTest.cpp` - the regression test and its helpers.
- `docs/qwen-fixes-2026-10/cluster-new-p16-16.md` - this report.

No new file, so nothing to register in `tests/fork-sources.txt`; both changed files are already registered there (fork-NEW files, `src/core/ControlCommandsHandoff.cpp` and `tests/src/core/LibraryProjectsTest.cpp`), and neither is an upstream-inherited file, so `tests/upstream-modifications.txt` is untouched.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: `git apply -R logs/fix.patch` (exit 0) -> `cmake --build build --target LibraryProjectsTest` (exit 0) -> `ctest -R LibraryProjectsTest --output-on-failure` (exit **8**, `aFailedHandOffTakesItsNewFolderBack` fails on `!QFileInfo::exists(dir)`). Restored: `git apply logs/fix.patch` (exit 0) -> the same build (exit 0) -> the same ctest (exit **0**). The patch used is `git diff -- src/core/ControlCommandsHandoff.cpp` only - the product file, not the test.
- **The test is deterministic.** The failure is injected by loading the session with a section skipped (`loadPartially()`, `tests/src/core/LibraryProjectsTest.cpp:112`), which makes `Song::saveProjectFile()` refuse (`src/core/Song.cpp:2264-2272`). No wall-clock, no sleep, no polling, no concurrency.
- **The fix is at the cause and complete.** The cause is the folder `mkpath` creates before the bundle is written; the fix is in the one function that creates it (`handoff()`, `src/core/ControlCommandsHandoff.cpp:241-256`), and every failure after the `mkpath` - the session's own file, the taken copy/manifest/temporary slots, the media slot, the save, the media collection, the manifest write, and the `mkpath` itself - now goes through it. `project.handoff` has one entry point: the control command; the GUI menu item (`src/gui/shell/ShellCommandMenus.cpp:507`) drives that same command, so there is no second path to keep consistent.
- **Siblings.** `project.collect_media` shares the media-copy helpers (`src/core/ControlCommandsCollectMedia.cpp`, `collectProjectMediaInto()`) but has no destination folder of its own to take back - it writes into the project's own folder, which is never removed. The other `project.*` commands (36 of them) do not create a destination folder, so none of them is affected. No new command id was added, so the command snapshot and the canary are untouched.
- **No collateral damage.** A destination that existed before the hand off is never on the removal list (`foldersToCreate()` stops at the first existing folder) - `aFailedHandOffKeepsTheFolderItDidNotCreate()` proves it, and the pre-existing tests `aFailedHandOffLeavesNoBundleBehind`, `aHandOffNeverWritesIntoOrRemovesAnExistingMediaFolder`, `aHandOffNeverRemovesWhatAMediaLinkPointsAt`, `aHandOffNeverOverwritesTheDestinationFolder`, `aHandOffNeverReplacesADanglingLink` and `aHandOffNeverReplacesTheSaveTemporary` still pass. `QDir::rmdir()` refuses a folder that is not empty, so a folder that holds something is never removed. Old projects and saved data are untouched: no file format, no save path and no project state was changed.
- **Realtime, undo and commands.** The change is in a control command that runs on the caller's thread, not on the audio thread: no allocation, no lock, no wait and no logging was added to any render path, and no audio-path file was touched. `project.handoff` stays `mutating = false` and records no undo step, exactly as before - the folder it now removes is one it created itself in the same call, not a project change. No new control command was added.
- **Verdict.** **FIXED**, not ALREADY-FIXED and not a FALSE-POSITIVE: the code above shows the `mkpath` at `src/core/ControlCommandsHandoff.cpp:169` (before the fix) with no `rmdir` anywhere in the file, and the RED run failed on exactly that behaviour. The last question of the pre-submit list ("if your verdict is ALREADY-FIXED or FALSE-POSITIVE, cite the code") therefore does not apply; the evidence for the REAL verdict is the evidence table above.
- **Portability.** The fix uses only `QDir`, `QFileInfo` and `QStringList` from the Qt Core module; no Qt-6-only API was introduced (the first attempt used a static `QDir::rmdir`, which does not exist in Qt 6 - it was replaced by the member call before the fix was ever committed). No `windows.h` macro names and no `std::filesystem` are used. The path walk stops at the root and at a path that cannot be its own parent, so a Windows drive root (`C:/`) ends the walk as it should.
