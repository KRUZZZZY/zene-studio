# Cluster new-p16-16

Verdict: **FIXED**

Luna's finding "Failed handoff can leave a newly created empty folder" is REAL on this branch. `project.handoff` created the destination folder before it wrote the bundle, and every failure after that point (the session save, the media collection, the manifest) left the folder behind. The fix tracks the folders this call created and takes them back when the hand off fails, while a folder that was there before is left alone.

Round 2 (the review's blocking finding) is also fixed: the tracking now follows the file system's own resolution of the destination path - a symlink and a `..` in the middle of it - instead of the lexically cleaned path, so the folder the hand off really created is the one that is taken back.

## The finding and the evidence

| Claim | Evidence (file:line, before the fix) |
| --- | --- |
| The destination is created before anything is written | `src/core/ControlCommandsHandoff.cpp:169` `if (!QDir().mkpath(dir)) { ... }` ran before the session copy, the media folder and the manifest were written. |
| A save failure leaves the folder behind | `:187-190` - `if (!song->saveProjectFile(file)) { return ControlResult::failure(...); }` returns without touching the folder `mkpath` had just made. |
| A media-collection failure removes the partial files but not the folder | `:195-199` - `collectProjectMediaInto(...)` fails, `discardPartialBundle(file)` (`:158-163`) removes the session copy and the `media` folder, and the destination folder itself stays. |
| A manifest failure leaves the folder behind too | `:201-205` - same shape: `discardPartialBundle(file)` then a refusal, with the destination folder left. |
| Nothing removed the destination folder | `discardPartialBundle` (`:158-163`) only ever removed the session copy and the `media` folder beside it; `grep -n mkpath src/core/ControlCommandsHandoff.cpp` found exactly one `mkpath` (`:169`) and no `rmdir` anywhere in the file. |

The finding's line 169 was the `mkpath` and is still the `mkpath`; the code around it moved when the body was split into `writeBundle()`.

## Round 2 - the review's blocking finding

The review was right, and the fix it caught is real:

> `foldersToCreate()` records the lexically cleaned path, while `mkpath()` creates the symlink-resolved path.

Round 1's tracker built its list from `QDir::cleanPath(path)`, which is a *lexical* operation: `cleanPath("base/link/../made")` is `base/made`. The creation, on the other hand, is done by the file system, which resolves the symlink and the `..` before it creates anything: `mkpath("base/link/../made")` creates `base/target/made` when `link` points at `target/child`. The rollback therefore removed a folder that was never made, and the one that was made stayed behind.

Measured on this machine (Qt 6.12.0, a standalone program built against the same Qt the product uses - `/tmp/qtprobe/probe3.cpp`, compile and run EXIT=0):

| Call | Result |
| --- | --- |
| `QDir::cleanPath(base + "/link/../made2")` | `base/made2` - the lexical path |
| `QDir().mkpath(base + "/link/../made2")` | creates `base/target/made2` - the resolved path |
| `QFileInfo(dest).canonicalFilePath()` | `base/target/made2` |
| `QSaveFile(dest + "/y.mmpz").open(...)` | fails with "No such file or directory" - Qt's QSaveFile puts its temporary file in the *lexically* cleaned directory, which does not exist |

That last row is why a hand off into such a path always fails at the save (`DataFile::writeFile()` writes through a `QSaveFile`, `src/core/DataFile.cpp:723`), and it is why the leak was observable: the destination folder was created and then abandoned.

The round-2 fix replaces the lexical walk with a walk over the path's **components**:

- `missingFolders()` (`src/core/ControlCommandsHandoff.cpp:171-184`) - the prefixes of the path, one component at a time, that are not there yet. The prefixes are the positions a creation can land in, because the file system resolves each component as it walks: `base/link/../made` gives `base`, `base/link`, `base/link/..` and `base/link/../made`, and the folder that is really made is the resolution of the last one. A `.` component is skipped (it names the folder that is already there), and the list stops being lexical: nothing is cleaned up before the file system has had its say.
- `removeCreatedFolders()` (`:188-195`) - unchanged in shape: it takes the list back innermost first with `QDir::rmdir()`, which removes a folder only while it is empty, and it skips a symlink so the rollback can never follow a link out of the destination. Because the prefixes are the *raw* path, `rmdir("base/link/../made")` resolves to the same folder the creation made (`base/target/made`) and removes it.

The destination-path semantics are unchanged: the hand off still creates and writes where the file system resolves the path. Only the bookkeeping of what has to be taken back changed.

## The fix

`src/core/ControlCommandsHandoff.cpp`:

- `missingFolders()` (`:171-184`) - the folders the destination path needs and does not have yet (see above).
- `removeCreatedFolders()` (`:188-195`) - takes those folders back, innermost first, and only while they are empty.
- `writeBundle()` (`:200-242`) - the body of the old `handoff()`, unchanged: the session's own file, the taken slots, the media slot, the save, the media collection and the manifest are all exactly as they were.
- `handoff()` (`:244-262`) - validates the argument, works out which folders this call is about to create, creates them, and hands the bundle to `writeBundle()`. If the bundle could not be written, the folders this call created are taken back; a successful hand off keeps them.

The contract is "a hand off folder is all of a bundle or none of it" - the same sentence `discardPartialBundle()` already carried, extended from the files inside the folder to the folder itself.

## The regression tests

`tests/src/core/LibraryProjectsTest.cpp` (the file that already holds the hand-off tests) gained three slots and the helpers they need:

- `aFailedHandOffTakesItsNewFolderBack()` (`:562`) - the hand off targets a folder that does not exist (`new-folder/deeper`, so the folder on the way there is created too), the session cannot be saved, the hand off is refused, and the folders it made are gone.
- `aFailedHandOffKeepsTheFolderItDidNotCreate()` (`:580`) - the same refusal into a folder that was there before: the folder stays.
- `aFailedHandOffTakesBackTheFolderItReallyCreated()` (`:598`) - the round-2 case: `base/link` is a symlink to `base/target/child`, the destination is `base/link/../made`, and the folder the file system really creates is `base/target/made`. The test proves where the file system puts such a path (the same `QDir::mkpath()` call the hand off makes, on a `probe` name), forces the save refusal, and then asserts that `base/target/made` is gone while `base/target/child`, `base` and the symlink `base/link` are all still there.
- Helpers: `writeText()` (`:75`), `songProject()` (`:90`) and `loadPartially()` (`:112`).

The failure is injected, not timed: the session is loaded with a section skipped, so `Song::saveProjectFile()` refuses to write a document that would be missing that section (`src/core/Song.cpp:2264-2272`, the partial-load policy). That is the real refusal a user hits, and it happens after the destination folder was created. No sleep, no clock, no race.

The three new slots are the last ones in the class, because the partial load replaces the session's contents: every other slot in this file still sees the session it was written for.

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-16.r1/logs`.

### Round 1

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
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) | **0** | `RESULT: PASS` |

### Round 2 (this round)

| Step | Command | Exit | Result |
| --- | --- | --- | --- |
| platform facts | standalone Qt 6.12 program `/tmp/qtprobe/probe3.cpp`: `cleanPath`, `mkpath`, `canonicalFilePath`, `QSaveFile::open` | 0 | the lexical path and the created path differ; `mkpath` creates `base/target/made2` |
| first test draft | `cmake --build build --target LibraryProjectsTest` (build-8.log) + `ctest -R LibraryProjectsTest` (test-8.log) | 8 | the draft's *successful* hand off into the same path shape cannot be written at all (QSaveFile puts its temporary file in the lexically cleaned directory), so the draft asserted a hand off that this path never allows. The draft was replaced by a probe of the resolution plus the refusal case. |
| fix in place | `cmake --build build --target LibraryProjectsTest zene` (build-9.log) | 0 | built |
| GREEN | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-9.log) | **0** | 1/1 passed |
| 4b revert | `git diff -- src/core/ControlCommandsHandoff.cpp > .../logs/fix2.patch && git apply -R .../logs/fix2.patch` | 0 | the round-2 product change reverted, the test file untouched |
| 4b RED | `cmake --build build --target LibraryProjectsTest` (build-11.log) | 0 | built |
| 4b RED | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure` (test-11.log) | **8** | `FAIL! aFailedHandOffTakesBackTheFolderItReallyCreated() '!QFileInfo::exists(.../target/made)' returned FALSE. (the failed hand off left the folder it really created behind)` - the leak the review reported |
| 4b restore | `git apply .../logs/fix2.patch` | 0 | fix back in the tree |
| 4b GREEN | `cmake --build build --target LibraryProjectsTest` (build-12.log) + `ctest -R LibraryProjectsTest` (test-12.log) | 0 / **0** | 1/1 passed |
| family | `ctest -R "LibraryProjectsTest\|PartialLoadTest\|ControlProjectArchiveTest\|ControlSuccessPathsTest\|ControlRegistryTest\|ProjectContainerTest" --output-on-failure` (test-13.log) | **0** | 6 of 6 tests passed |
| final fix | `cmake --build build --target LibraryProjectsTest zene` (build-14.log) + the family ctest (test-14.log) | 0 / **0** | 6 of 6 passed |
| final 4b revert | `git diff -- src/core/ControlCommandsHandoff.cpp > .../logs/fix3.patch && git apply -R .../logs/fix3.patch` | 0 | the whole round-2 product change reverted |
| final 4b RED | `cmake --build build --target LibraryProjectsTest` (build-15.log) + `ctest -R LibraryProjectsTest` (test-15.log) | 0 / **8** | the new test fails without the change - 20 passed, 1 failed |
| final 4b restore | `git apply .../logs/fix3.patch` | 0 | fix back in the tree |
| final 4b GREEN | `cmake --build build --target LibraryProjectsTest zene` (build-16.log) + `ctest -R LibraryProjectsTest` (test-16.log) | 0 / **0** | 1/1 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-3.log) | **0** | `RESULT: PASS` - every row PASS |

Family test count: 6 ctest tests, 6 passed, 0 failed (test-14.log).

## Files changed

- `src/core/ControlCommandsHandoff.cpp` - the fix.
- `tests/src/core/LibraryProjectsTest.cpp` - the regression tests and their helpers.
- `docs/qwen-fixes-2026-10/cluster-new-p16-16.md` - this report.

No new file, so nothing to register in `tests/fork-sources.txt`; both changed files are already registered there (fork-NEW files, `src/core/ControlCommandsHandoff.cpp` and `tests/src/core/LibraryProjectsTest.cpp`), and neither is an upstream-inherited file, so `tests/upstream-modifications.txt` is untouched.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Round 1: `git apply -R logs/fix.patch` (exit 0) -> `cmake --build build --target LibraryProjectsTest` (exit 0) -> `ctest -R LibraryProjectsTest --output-on-failure` (exit **8**). Round 2: `git apply -R logs/fix3.patch` (exit 0) -> build (exit 0) -> the same ctest (exit **8**, `aFailedHandOffTakesBackTheFolderItReallyCreated` fails on `!QFileInfo::exists(.../target/made)`). Restored: `git apply logs/fix3.patch` (exit 0) -> `cmake --build build --target LibraryProjectsTest zene` (exit 0) -> the same ctest (exit **0**). The patches used are `git diff -- src/core/ControlCommandsHandoff.cpp` only - the product file, not the test.
- **The test is deterministic.** The failure is injected by loading the session with a section skipped (`loadPartially()`, `tests/src/core/LibraryProjectsTest.cpp:112`), which makes `Song::saveProjectFile()` refuse (`src/core/Song.cpp:2264-2272`). The symlink is created by the test itself, and the file system's resolution of it is measured by the test, not assumed. No wall-clock, no sleep, no polling, no concurrency.
- **The fix is at the cause and complete.** The cause is the folder `mkpath` creates before the bundle is written; the fix is in the one function that creates it (`handoff()`, `src/core/ControlCommandsHandoff.cpp:244-262`), and every failure after the `mkpath` - the session's own file, the taken copy/manifest/temporary slots, the media slot, the save, the media collection, the manifest write, and the `mkpath` itself - now goes through it. The tracking follows the path's components, so it names the folder the file system creates and not a lexically cleaned one. `project.handoff` has one entry point: the control command; the GUI menu item (`src/gui/shell/ShellCommandMenus.cpp:507`) drives that same command, so there is no second path to keep consistent.
- **Siblings.** `project.collect_media` shares the media-copy helpers (`src/core/ControlCommandsCollectMedia.cpp`, `collectProjectMediaInto()`) but has no destination folder of its own to take back - it writes into the project's own folder, which is never removed. The other `project.*` commands (36 of them) do not create a destination folder, so none of them is affected. No new command id was added, so the command snapshot and the canary are untouched.
- **No collateral damage.** A destination that existed before the hand off is never on the removal list (the prefixes that are already there are not listed) - `aFailedHandOffKeepsTheFolderItDidNotCreate()` proves it, and the pre-existing tests `aFailedHandOffLeavesNoBundleBehind`, `aHandOffNeverWritesIntoOrRemovesAnExistingMediaFolder`, `aHandOffNeverRemovesWhatAMediaLinkPointsAt`, `aHandOffNeverOverwritesTheDestinationFolder`, `aHandOffNeverReplacesADanglingLink` and `aHandOffNeverReplacesTheSaveTemporary` still pass. `QDir::rmdir()` refuses a folder that is not empty, so a folder that holds something is never removed, and a symlink on the path is skipped, so the rollback never follows a link. Old projects and saved data are untouched: no file format, no save path and no project state was changed.
- **Realtime, undo and commands.** The change is in a control command that runs on the caller's thread, not on the audio thread: no allocation, no lock, no wait and no logging was added to any render path, and no audio-path file was touched. `project.handoff` stays `mutating = false` and records no undo step, exactly as before - the folder it now removes is one it created itself in the same call, not a project change. No new control command was added.
- **Verdict.** **FIXED**, not ALREADY-FIXED and not a FALSE-POSITIVE: the code above shows the `mkpath` at `src/core/ControlCommandsHandoff.cpp:169` (before the fix) with no `rmdir` anywhere in the file, and the RED run failed on exactly that behaviour. The last question of the pre-submit list ("if your verdict is ALREADY-FIXED or FALSE-POSITIVE, cite the code") therefore does not apply; the evidence for the REAL verdict is the evidence table above.
- **Portability.** The fix uses only `QDir`, `QFileInfo` and `QStringList` from the Qt Core module; no Qt-6-only API was introduced (the first attempt used a static `QDir::rmdir`, which does not exist in Qt 6 - it was replaced by the member call before the fix was ever committed; `Qt::SkipEmptyParts` is available in Qt 5.14 and later). No `windows.h` macro names and no `std::filesystem` are used. The path walk stops at the root and at a path that cannot be its own parent, so a Windows drive root (`C:/`) ends the walk as it should.
