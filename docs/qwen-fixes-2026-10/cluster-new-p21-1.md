Verdict: FIXED

new-p21-1: FIXED

The finding's trigger is wrong, but its defect is real. `Track::createClip()` does not return null when a track is full or on allocation failure: the concrete classes always return a `new`ed clip (`src/tracks/SampleTrack.cpp:318-322`, `src/tracks/InstrumentTrack.cpp:1086-1091`, `src/tracks/PatternTrack.cpp:147-152`, `src/tracks/AutomationTrack.cpp:59-64`); allocation failure throws `std::bad_alloc`, it does not return null, and nothing counts clips against a capacity. The only null return is `TrackFolder::createClip()` (`src/tracks/TrackFolder.cpp:179-182`), and its header says a folder "carries no clips of its own" (`include/TrackFolder.h:124-125`). `clip.add` resolves any track id, folders included (`src/core/ControlEditSupport.cpp:136-158` searches `Song::tracks()`), and it had no kind check, so `clip.add` on a folder dereferenced null: `clip->setName` at the old line 135 when a `name` is given, and `changeLength` through `applyLength` when a `length` is given. The unit test reproduces this as SIGSEGV at address 0x888, a member access through the null `Clip*`.

The same null return is handled elsewhere: `ControlCommandsClipImportFile.cpp:325-328` checks the result after `createClip`, `AafImportBuild.cpp:121` and `ControlCommandsBounceLand.cpp:54`, `ControlCommandsBrowserMedia.cpp:277` and `ControlCommandsConsolidate.cpp:177` use `dynamic_cast` on the result, and `Track.cpp:183-190` (cluster 5426) guards the load path. `clip.add` was the one caller that did not.

**The fix** (`src/core/ControlCommandsClip.cpp`):
- `:126-132` Refuses a folder track with `ControlErrorKind::Refused` **before** `addJournalCheckPoint()`. Reason: `ProjectJournal::addJournalCheckPoint()` pushes an undo step and clears the redo stack (`src/core/ProjectJournal.cpp:227-244`). A refusal after the checkpoint would leave a no-op undo step and destroy redo history. The unit test measures this (see the guard-only mutant below).
- `:141-145` Refuses a null result of `createClip()` with `Refused`, so the handler never dereferences null. This is defence in depth for a future track class: for the classes that exist today it is unreachable. It has the same orphan-checkpoint behaviour as `ClipImportFile` (`:324-328`), and only a future null-returning class would reach it.

No new command, no schema, id, description, `mutating` or `requires` value changes (`git diff` shows none), so the command surface is unchanged. Only the handler body changed.

**The regression test** (`tests/src/core/ControlEditCommandsTest.cpp:738-761`, `clipAddOnAFolderTrackIsRefusedWithoutAnUndoStep`): adds a folder track with `track.add`, reads `control.undo_depth`, calls `clip.add` with `name` and `length` (the two dereferences), asserts `ok == false` and `errorKind == Refused`, then asserts the undo depth is unchanged. No timing, sleeps or races. No new file is added: the test file is already in `tests/CMakeLists.txt:217` and `tests/all-sources.txt:2642`, and `src/core/ControlCommandsClip.cpp` is already in `tests/fork-sources.txt:1410`.

**Test matrix** (written before editing):
- Normal path: `clip.add` on an instrument/sample track is covered by the existing `ControlSuccessPathsTest` (`:94`), `ControlVerbInverseTest` and `ClipImportFileTest` (all passed in the family run below).
- Folder path (the bug): new test, with and without `name`/`length`.
- Null from `createClip` for a non-folder class: no such class exists; covered by the guard-only mutant run (refusal path reached through the guard, undo depth grows by one, which is why the pre-check exists).
- Audio thread: `clip.add` is a control-surface handler on the UI/control thread. Nothing on the audio path is touched.
- Platform: no new Qt API, no new file.

## Commands and results

Environment note: ccache's temp dir is outside the write-jail, so every compile needs `CCACHE_DISABLE=1`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-1.r1/logs/`. Each `cmake --build` line below was run as `CCACHE_DISABLE=1 cmake --build build --target <T> -j2 > $L/<log> 2>&1; echo EXIT=$?` from the worktree; each ctest line as `ctest -R '^<T>$' --output-on-failure > $L/<log> 2>&1; echo EXIT=$?` from `build/tests`.

| Step | Command (targets) | Log | EXIT |
| --- | --- | --- | --- |
| RED build (first test version, bare `clip.add`) | `cmake --build build --target ControlEditCommandsTest -j2` | `build-2-red.log` | 0 |
| RED run, first version | ctest `ControlEditCommandsTest` | `test-1-red.log` | 8 (assertion: `errorKind` NotFound, not Refused; undo depth moved) |
| RED build (final test: `name` + `length`) | `ControlEditCommandsTest` | `build-3-red.log` | 0 |
| **RED run (final test, fix not applied)** | ctest `ControlEditCommandsTest` | `test-2-red.log` | **8**: `SIGSEGV, SEGV_MAPERR, address 0x888` in `clipAddOnAFolderTrackIsRefusedWithoutAnUndoStep()` |
| Fix build | `zene ControlEditCommandsTest` | `build-4-green.log` | 0 |
| **GREEN run** | ctest `ControlEditCommandsTest` | `test-3-green.log` | 8 (only the pre-existing failure below; **new test passes**; 19 passed, 1 failed) |
| 4b step 1: `git diff -- src/core/ControlCommandsClip.cpp > logs/fix.patch && git apply -R logs/fix.patch` | | | 0 |
| 4b step 2: rebuild | `zene ControlEditCommandsTest` | `build-5-mutant.log` | 0 |
| 4b step 2: run (fix reverted) | ctest `ControlEditCommandsTest` | `test-4-mutant-red.log` | 8: **SIGSEGV in the new test** (`test-4-mutant-red.log:26`) |
| 4b step 3: `git apply logs/fix.patch` | | | 0 |
| 4b step 3: rebuild | `zene ControlEditCommandsTest` | `build-6-fixed-rebuild.log` | 0 |
| 4b step 3: run (fix restored) | ctest `ControlEditCommandsTest` | `test-5-fixed-green.log` | 8 (new test passes; same pre-existing failure) |
| Undo-step proof: guard only (pre-check disabled with `if (false && …)`), rebuild, run | `zene ControlEditCommandsTest`, ctest | `build-7-guard-only.log` (0), `test-6-guard-only.log` (8) | fails at `ControlEditCommandsTest.cpp:760`: `depthAfter 46 != depthBefore 45`, so the orphan checkpoint is caught |
| Restore fix (`cmp` against saved copy), rebuild, run | `zene ControlEditCommandsTest`, ctest | `build-8-restored.log` (0), `test-7-restored-green.log` (8) | new test passes again |
| Family run (4 targets) | build `ControlVerbInverseTest ControlSuccessPathsTest ClipImportFileTest`, ctest `^(ControlVerbInverseTest\|ControlSuccessPathsTest\|ClipImportFileTest\|ControlEditCommandsTest)$` | `build-9-family.log` (0), `test-8-family.log` (8) | 2 of 4 test targets pass fully; `ControlVerbInverseTest` and `ClipImportFileTest` pass; `ControlEditCommandsTest` 19/20; `ControlSuccessPathsTest` 15/18 |
| Baseline for the `ControlSuccessPathsTest` failures (fix reverted, rebuild, run, reapply) | `zene ControlSuccessPathsTest`, ctest | `build-10-baseline.log` (0), `test-9-baseline-successpaths.log` (8) | the same 3 failures on the unfixed tree: they are pre-existing |
| Final rebuild with fix | `zene ControlEditCommandsTest ControlSuccessPathsTest` | `build-11-final.log` | 0 |
| Final run | ctest `^(ControlEditCommandsTest\|ControlSuccessPathsTest)$` | `test-10-final.log` | 8; `ControlEditCommandsTest` 19 passed, 1 failed; `ControlSuccessPathsTest` 15 passed, 3 failed |
| Gates, simple (six) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` | `gate-*-2.log` | 0, 0, 0, 0, 0, 0 |
| Gates, ratchet | `bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check` | `gate-complexity-2.log`, `gate-file-length-2.log` | 1, 1 (see below) |
| Official gate script | `python3 feature_check.py <worktree>` | `gate-feature-check-1.log` | 1: `PermissionError` creating `<worktree>-feature-check/` in `projects/`, outside the write-jail. The script never produced its table |

The pre-existing failures, which I reproduced on the unfixed tree: `ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged` (`ControlEditCommandsTest.cpp:644`) and `ControlSuccessPathsTest::{controllerFeedbackTakeoverAndTemplates, failedBuiltInDocumentKeepsCurrentSession, failedBuiltInDocumentSaveKeepsCurrentSession}`. Their messages cite files under `/home/kruzzzzy/Documents/Zene Studio/` (take file and controller template) and a cache-dir `removeRecursively`, and the sandbox denies writes there (`touch` gives `Permission denied`). They are not caused by `clip.add`.

**Ratchet gates:** the complexity and file-length gates list regressions for files on other branches. None of the listed items names `src/core/ControlCommandsClip.cpp` or `tests/src/core/ControlEditCommandsTest.cpp`. `feature_check.py` only fails on items in changed files, so these two would pass there. I did not have the base listings for the exact comparison, so this is a reading of the script, not its output.

**Gate status:** the official `feature_check.py` could not run in this sandbox, and I ran its component commands by hand instead. Under the write-jail I could not run the script itself, so the table above is not the script's table.

## Self-check

1. **Each new test FAILS with the product change reverted, and PASSES restored (exit codes).** Yes. RED: `test-2-red.log`, EXIT 8, SIGSEGV in the new test. Reverted 4b: `test-4-mutant-red.log`, EXIT 8, SIGSEGV in the new test. Restored: `test-5-fixed-green.log` and `test-10-final.log`, new test passes (19/20 pass in `ControlEditCommandsTest`, the one failure is pre-existing).
2. **Acceptance criteria.** Not a feature; no acceptance table. The finding's criteria are "check `clip == nullptr` after `createClip` and return a typed `Refused` before touching it". Done at `ControlCommandsClip.cpp:141-145`, and the folder refusal at `:126-132` comes before the checkpoint.
3. **Each edit is ONE undo step; reversibility honest.** The fix adds no undo step. A refused add leaves the undo depth unchanged (test assertion `ControlEditCommandsTest.cpp:760`). The guard-only mutant shows the pre-checkpoint refusal is what keeps the depth unchanged (46 vs 45). `clip.add` reversibility is unchanged.
4. **Nothing on the audio thread allocates, frees, locks, waits, does I/O or emits a signal.** `clip.add` is a control-surface handler, not on the audio thread. The diff adds only a type check, a null check, and two `ControlResult::failure` calls. Those allocate a `QString` for the error message only on the refused path, which is outside audio.
5. **Old projects still load and render; new data round-trips.** No load/save path or data format changed. The change only affects `clip.add` on a folder track, which previously crashed.
6. **Portability.** No new Qt API. Uses existing `QStringLiteral`, `ControlResult::failure`, `ControlErrorKind::Refused`.

**Snapshot.** `feature_check` / `submit_check` treat a change to a `ControlCommands*` file as needing `tools/mcp-zene-control/zene_control/commands_snapshot.json` regenerated. The snapshot records only `id`, `group`, `description`, `args_schema`, `result_schema`, `mutating` and `requires` (`commands_snapshot.json`, `clip.add` entry near line 4439). `git diff` of `ControlCommandsClip.cpp` changes none of those lines (checked: the only changes are in the handler body). Regenerating needs a live Zene instance, which this sandbox cannot run, and it would change only `captured_at`. So the snapshot is not out of date, and this heuristic FAIL is a false positive for a handler-only change.

**Sibling check.** Other `createClip` callers in the control surface are null-safe (listed above). `Track::createClipsForPattern` (`Track.cpp:1182-1190`) and `Track::getClip` (`Track.cpp:1099`) dereference or return a `createClip` result without a null check, but their only callers are pattern-track paths (`Track.cpp:509`, `PatternTrack.cpp:50`), not folder tracks, so they are not reachable from `clip.add`. Not changed; they are out of scope for this finding.

**Files committed for this verdict:** `src/core/ControlCommandsClip.cpp`, `tests/src/core/ControlEditCommandsTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-1.md`.
