FIXED

Cluster new-p21-98: `build()` ignores `QDomDocument::setContent()` failure (src/core/ControlProjectTransplantChannels.cpp, finding line 189; on this tree the unchecked call is at line 190).

## What the code does (pre-fix, file:line on the pre-fix tree)

- `build()` ran `doc.setContent(plan.documents[i])` and never looked at the result. On failure `doc.documentElement()` is null, and that null element went on to `settleChannel()` (which resolved the need, creating a channel) and `Track::create()`.
- `Track::create(element, ...)` returns nullptr for an element whose name is not `track` (src/core/Track.cpp:557-559), so the null element made no track. But the channel had already been made: `dropUnservedChannels()` then deleted it again (ControlProjectTransplantChannels.cpp:154-170). The net effect for a document that builds nothing was a create/describe/delete of a channel.
- Sibling with the same flaw: `resolveNeed()` ran `doc.setContent(need.xml)` unchecked (pre-fix line 124). A channel description that does not parse was applied to a null element, which created a channel with an empty name. A VALID track whose need named that description was then routed onto that nameless channel instead of staying on the master. The RED run showed this (see the table, row 4).

## Reachability (not proven)

`TrackPlan::documents` is produced in-process by `QDomDocument::toString()` (src/core/ControlCommandsImportTracks.cpp:176 and :276). I did not find a producer that emits unparseable XML, and I have NOT shown a user-reachable input that does. The regression drives a malformed plan directly through the public `TrackPlan` / `buildAsOneStep()` API (include/ControlProjectTransplant.h:73-117), which is a test seam, not a user path. Impact should be read as a defect in the transplant's own handling, not as a demonstrated user-facing failure.

## Fix

- `build()`: `if (!doc.setContent(...)) { continue; }` before any channel is resolved. No track, no channel, no description. `plan.need` is indexed by document position, so later documents stay aligned.
- `resolveNeed()`: `if (!doc.setContent(need.xml)) { return -1; }` before `createChannel()`. No channel is made.
- `settleChannel()`: `if (index < 0) { return; }`. A need with no channel leaves the track where `detach()` put it, on the master.

Design choice: a track whose channel description does not parse is kept and put on the master, not dropped. The track data itself is valid; only its channel description is not.

## Test

The three cases are in `tests/src/core/ImportTrackIoTest.cpp` (the existing test file for this area): `anUnparsableTrackDocumentMakesNoTrackAndNoChannel`, `anUnparsableChannelDescriptionLeavesItsTrackOnTheMaster`, `anUnparsableDocumentDoesNotShiftTheOthers`. A new test file was not used because the configured build tree has no target for one and the lane build tree refuses reconfiguration (rows 1-2 of the table). The "no channel described" check counts `Model::dataChanged` on the mixer, which `applyDescription()` (ControlProjectTransplantChannels.cpp) is the only emitter of during a build.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-98.r1/logs/`. All commands run from the worktree, `build/` configured. `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p21-98` was exported for the builds (see row 3).

| # | Command (abridged) | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| 1 | `cmake --build build --target ImportTrackBuildTest -j4` (new file in a new test target) | red-build-1.log | 2 | No rule: the target is not in the configured tree. Test moved to ImportTrackIoTest.cpp. |
| 2 | `cmake -S . -B build` (reconfigure) | reconfigure-1.log | 97 | Refused by `bunny-guard`: "configuring is the parent's job". Not retried. |
| 3 | `cmake --build build --target ImportTrackIoTest -j4` | red-build-2.log | 2 | ccache could not create its temp file in the bunny-fix area (outside the write-jail). Fixed by exporting `CCACHE_TEMPDIR` into `$TMPDIR`. |
| 4 | `cmake --build build --target ImportTrackIoTest -j4` (test had `transplant::` unqualified) | red-build-3.log | 2 | Compile error in my test code ("'transplant' has not been declared"). Corrected to `control::transplant::`. |
| 5 | **RED** build, unfixed product: `cmake --build build --target ImportTrackIoTest -j4` | red-build-4.log | 0 | Builds. |
| 6 | **RED** run, unfixed: `cd build/tests && ctest -R ImportTrackIoTest --output-on-failure` | red-test-1.log | 8 | 6 passed, 2 FAILED: `anUnparsableTrackDocumentMakesNoTrackAndNoChannel` ("described.count()" actual 1, expected 0) and `anUnparsableChannelDescriptionLeavesItsTrackOnTheMaster` ("'made.channels.empty()' returned FALSE"). |
| 7 | **GREEN** build: `cmake --build build --target zene ImportTrackIoTest -j4` | green-build-1.log | 0 | Builds. |
| 8 | **GREEN** run: `cd build/tests && ctest -R ImportTrackIoTest -V` | green-test-2.log | 0 | Totals: 8 passed, 0 failed. Family count: 1 test (ImportTrackIoTest), 8 cases. |
| 9 | Family build: `cmake --build build --target ProjectToolsTest ArrangingSpacerReferenceTest ArrangingCatchUpTest LibraryProjectsTest -j4` | family-build-1.log | 0 | Builds. |
| 10 | Family run: `cd build/tests && ctest -R "ImportTrackIoTest\|ProjectToolsTest\|ArrangingSpacerReferenceTest\|ArrangingCatchUpTest\|LibraryProjectsTest" -V` | family-test-1.log | 8 | LibraryProjectsTest 21 passed; ProjectToolsTest 18 passed; ImportTrackIoTest 8 passed; ArrangingSpacerReferenceTest 12 passed, 1 skipped; ArrangingCatchUpTest 12 passed, 1 skipped, **1 failed** (`queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`, see row 12). |
| 11 | 4b revert: `git diff -- src/core/ControlProjectTransplantChannels.cpp > logs/fix.patch` (exit 0), `git apply -R logs/fix.patch` (exit 0); then build `zene ImportTrackIoTest ArrangingCatchUpTest` and `ctest -R "ImportTrackIoTest\|ArrangingCatchUpTest" -V` | fix.patch, 4b-unfixed-build-1.log, 4b-unfixed-test-1.log | build 0, ctest 8 | Reverted: ImportTrackIoTest 6 passed, 2 FAILED (the same two cases as row 6); ArrangingCatchUpTest fails identically (row 12). |
| 12 | 4b re-apply: `git apply logs/fix.patch` (exit 0); rebuild as row 11; same ctest | 4b-fixed-build-1.log, 4b-fixed-test-1.log | build 0, ctest 8 | Re-applied: ImportTrackIoTest 8 passed, 0 failed. ArrangingCatchUpTest: the same one failure. |
| 13 | Gate: `python3 .../qwen/feature_check.py <worktree>` | feature-check-1.log | 1 | Traceback: `PermissionError` creating `<worktree>-feature-check/` (the gate writes its logs outside the write-jail). Gate not runnable as shipped; substitutes below. The first hand-run loop (exit 127 for every gate: zsh does not word-split the unquoted command) left `gate-N-bash_tests_*.log` files in the log folder; they are superseded by the rows below. |
| 14 | Gates run by hand from the worktree: `bash tests/fork-sources-gate.sh` | gate-1-fork-sources.log | 0 | PASS |
| 15 | `bash tests/all-sources-reproduce.sh` | gate-2-all-sources.log | 0 | PASS |
| 16 | `bash tests/no-upstream-regression-gate.sh` | gate-3-no-upstream.log | 0 | PASS |
| 17 | `tests/scripted/check-namespace` | gate-4-tests_scripted_check-namespace.log | 0 | PASS |
| 18 | `bash tests/no-tautology-gate.sh` | gate-5-no-tautology.log | 0 | PASS |
| 19 | `python3 tests/doc-figures-gate.py` | gate-6-doc-figures.log | 0 | PASS |
| 20 | Ratchet `bash tests/complexity-gate.sh --check` | gate-4-complexity.log | 1 | Red on the train by design. None of its REGRESSION items names `ControlProjectTransplantChannels.cpp` or `ImportTrackIoTest.cpp`, so under the gate's rule (items in changed files only) it has no new item in this change. |
| 21 | Ratchet `bash tests/file-length-gate.sh --check` | gate-7-file-length.log | 1 | Same as row 20: no item names either changed file. |

## Notes on row 12 (`ArrangingCatchUpTest`)

`queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` fails on the UNFIXED tree too (row 11), and the logs show `The plugin "tripleoscillator" wasn't found`, so the test's instrument fell back to a dummy. The test does not reach the transplant code. Its root cause in this build tree is not determined; `build/plugins/TripleOscillator` exists, so the cause is probably the plugin search path at test time. Not investigated further.

## Self-check

- **Regression fails without the fix and passes with it.** RED: rows 5-6 (two of three new cases fail on the unfixed product). GREEN: rows 7-8 (8/8). Reverse-and-reapply proof: rows 11-12 (the same two cases fail after `git apply -R`, and pass after `git apply`). The exit codes are in the table above.
- **Deterministic.** No sleeps, timers or threads. The inputs are a hand-built `TrackPlan` (public in include/ControlProjectTransplant.h) with one unparseable document or description. The "no channel described" check counts `Model::dataChanged` on the mixer, which only `applyDescription()` emits during a build; nothing depends on timing or order.
- **Fix at the cause and complete.** The three unchecked `setContent()` call sites that a malformed plan reaches were all changed: `build()` (the finding), `resolveNeed()` (the sibling in the same file, which made the nameless channel). `settleChannel()` was changed so the "no channel" result does not write `mixch = -1`. Call sites checked with `rg`: `build()` is called only from `buildAsOneStep()` and its redo lambda (ControlProjectTransplantChannels.cpp:270-271); `settleChannel()` only from `build()`; `resolveNeed()` only from `settleChannel()`. The other transplant users (`ControlCommandsImportTracks.cpp`, `ControlCommandsReference.cpp`, `ReferenceProject.cpp`) do not call `build()`, and their `setContent()` calls are on documents they serialise themselves.
- **No collateral damage.** Parseable documents and descriptions take the same path as before; the new branches run only when `setContent()` fails. `resolveNeed()` returned an index of 1 or more for every need before this change, so the new `index < 0` return cannot occur on a valid path. No limit or check was added. Old project files: their documents are parsed by the existing code before they reach `build()`, so nothing that loaded before is now refused.
- **Verdict FIXED**, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed. Reachability from user input is marked unproven in the body above.
