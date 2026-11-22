FIXED

# new-p10-404: undo snapshot restore inflated its input without an output bound

## Verdict: REAL
- `src/core/SongUndoSnapshot.cpp` (`Song::restoreUndoSnapshot`) called `qUncompress( compressed )` on the blob. `qUncompress` allocates what the 4-byte header claims and then grows past it.
- The blob is not always one the session just captured. `UndoHistory::loadSteps` (`src/core/UndoHistory.cpp:~488-504`) base64-decodes steps from a project file and pushes them as undo steps, and an undo of one restores it through this function. `stepDamaged` (`UndoHistory.cpp:101`) checks only the byte count and a sha256 stored in the same file, and a crafted file can carry a matching hash. So a crafted history entry could force the allocation.
- Confidence was "low" in the finding. The memory exhaustion itself was not reproduced: the red test shows that a snapshot expanding past the cap was restored. A header of 0xFFFFFFF0 over a tiny body is already refused by Qt/zlib in this build, so that test is a control and is not the red proof.

## Fix
`src/core/SongUndoSnapshot.cpp`: the blob is inflated with `control::inflateQtCompressedWithinCap( compressed, control::MaxProjectAssetDocumentBytes, &document )` (`src/core/ControlProjectAssets.cpp:443`). That function counts the bytes actually produced and refuses once they pass the cap, whatever the header says. The 256 MiB cap is the one DataFile recovery and the project-asset verbs already use. An over-cap, malformed or empty blob returns false before the temp dir is touched, as an undecodable blob already did. zlib is a required dependency (`src/CMakeLists.txt:437`), so the fail-closed no-zlib branch does not apply to this build.
No header, command or audio-thread code changed.

## Tests (tests/src/core/ProjectContainerSaveTest.cpp, ...TestParts2.h)
- `anUndoSnapshotExpandingPastTheCapIsRefused`: a genuine snapshot padded with trailing newlines to cap+1 bytes is refused and the session state is unchanged. The unpadded snapshot still restores.
- `anUndoSnapshotAdvertisingAHugeSizeIsRefused`: header 0xFFFFFFF0 over a tiny body is refused and the state is unchanged. This is a control: it passes without the fix too.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p10-404.r1/logs)
- RED build: `cmake --build build --target ProjectContainerSaveTest -j4` > build-1.log, EXIT=0
- RED run (`ctest -R ProjectContainerSaveTest` from build/tests, test-1-red.log): EXIT=8, `FAIL! anUndoSnapshotExpandingPastTheCapIsRefused() '!song->restoreUndoSnapshot( oversized )' returned FALSE`
- GREEN: build `zene ProjectContainerSaveTest` (build-2.log) EXIT=0; ctest (test-2-green.log) EXIT=0
- Family: `ctest -R "ReversibilityUndoTest|UndoBoundsTest|UndoHonesty|RevisionRestoreCreatedFileUndoTest|UndoHistory|SessionCheckpoint"` (test-4-family.log): EXIT=8, only because `UndoHistoryPanelTest` (a GUI target) was not built ("Not Run"). The other 9 tests passed.
- 4b: `git diff -- src/core/SongUndoSnapshot.cpp > fix.patch && git apply -R fix.patch` APPLYR=0; rebuild (build-5-reverted.log) EXIT=0; `ctest -R ProjectContainerSaveTest` (test-5-red-reverted.log) RED_EXIT=8 with the same FAIL line; `git apply fix.patch` APPLY=0; rebuild (build-6-fixed.log) EXIT=0; ctest (test-6-green.log) GREEN_EXIT=0.
- Gates: `feature_check.py <worktree>` (gates-1.log) GATES_EXIT=0, RESULT: PASS, all 8 rows PASS.

## Self-check
- No wall-clock timing, sleeps or races in the tests: the inputs are fixed byte strings.
- The 256 MiB cap could in principle reject a legitimate snapshot whose XML expands past that. The same cap already limits DataFile recovery and the asset verbs, and capture is unchanged. This is unverified for projects with very large embedded audio.
- Local TSan does not run here. The restore path is unchanged apart from the earlier return, and it runs on the main thread, not the audio thread.

- Red/green: the test fails with the product change reverted (RED_EXIT=8) and passes with it (GREEN_EXIT=0), see the 4b line above.
- Determinism: the inputs are a captured snapshot padded with newlines to an exact byte count, and a fixed header value. There is no clock, sleep, thread or injected timestamp.
- Callers: every restore goes through the one function I changed: `UndoHistory.cpp:136,139`, `SessionCheckpoint.cpp:56`, `ControlCommandsDocuments.cpp:177` (found with `rg restoreUndoSnapshot`). Both overloads share the body, so there is no other path into it. The command files touched in this branch (app.* etc.) are not changed by this fix; the only command-side caller is the checkpoint restore above, which gets the same bound.
- Collateral: capture is unchanged and old history sections load as before. A snapshot that inflates to 256 MiB or less restores exactly as before, which the existing restore tests (undo, redo, checkpoints) show passing. The only newly refused input is one that expands past the cap or is not a decodable qCompress frame, which already returned false.
- Sibling `src/core/SessionCheckpoint.cpp:93` also calls `qUncompress` on a snapshot, but only to compute a diff of in-memory snapshots the session captured itself, not from a saved file. I left it out of this fix.

## Round 2 (proof manifest needs a state-sequence)
`src/core/SongUndoSnapshot.cpp` is an undo-state file, so the manifest carries a `state-sequence` for `anUndoSnapshotExpandingPastTheCapIsRefused`. The states that test drives, in order (`stateOf()` is the session state the test compares):
1. S0: session after `buildSession()`; `captureUndoSnapshot()` taken.
2. S1: after `transport.set_tempo` 90 (the test asserts S1 != S0).
3. S1 again: after `restoreUndoSnapshot(oversized)` returns false (asserted equal to S1: a refused restore leaves the session untouched).
4. S0 again: after `restoreUndoSnapshot(*before)` returns true (asserted equal to S0).
Product code is unchanged since b6c0b679e3. Fresh 4b run in this round (logs in new-p10-404.r2/logs, patch = the fix commit's SongUndoSnapshot.cpp diff):
- `git apply -R fix.patch` APPLYR=0; build-1-reverted.log EXIT=0; `ctest -R ProjectContainerSaveTest` test-1-red.log RED_EXIT=8 with `FAIL! anUndoSnapshotExpandingPastTheCapIsRefused() '!song->restoreUndoSnapshot( oversized )' returned FALSE` (23 passed, 1 failed).
- `git apply fix.patch` APPLY=0; build-2-fixed.log EXIT=0; test-2-green.log GREEN_EXIT=0.
- `git status --short` empty afterwards.

Delegation: Haiku ran the revert, rebuild, red ctest, re-apply, rebuild and green ctest sequence and reported the exit codes; I checked the FAIL line in the logs and that `git status` was clean. I wrote the report and manifest myself.

## Other findings
- `ControlUndoStructuralTranscript` fails in this worktree with "plugin.load returned no instance id". It fails identically with my change reverted (test-5b-transcript-baseline.log, EXIT=8), so it is not caused by this fix. Not investigated.
- `UndoHistoryPanelTest` was not built or run.
