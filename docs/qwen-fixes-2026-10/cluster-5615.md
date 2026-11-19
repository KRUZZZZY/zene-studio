ALREADY-FIXED

# Cluster 5615: an unreadable preset must not be overwritten

## Verdict

Overwriting a chain or export preset whose existing bytes cannot be read is already fixed at the tested product commit fa4a5f2e78 (the docs commit does not change the product code). The guard predates this job: `git blame` puts line 161 of `src/core/ControlCommandsChain.cpp` on commit 9c7a95196e, and that commit is an ancestor of HEAD (`git merge-base --is-ancestor` → ANCESTOR_EXIT=0, `/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/evidence-1.log:77`). This job changes no product or test file.

## Code evidence (HEAD)

- `src/core/ControlCommandsChain.cpp:161`: `if (*existed && !controlReadFileBytes(path, &previous, error)) { return false; }`. It returns before the write at :170 (`controlWriteFileBytes`) and before `recordStoreWrite` at :171. A failed read therefore leaves the file alone and records no journal step.
- `src/core/ControlCommandsExportPresets.cpp:147`: the sibling guard, same shape, in `writePresetDocument`.
- `src/core/ControlDeviceState.cpp:50`: `ControlDataReadLimit = 64 MiB`. `controlReadBoundedFile` (:60-99) refuses with `return false` at :69, :82, :89 and :97. `controlReadFileBytes` (:215-231) returns false at :223 and :229 and calls the bounded reader at :231.
- `include/ControlDeviceSupport.h:299-300`: `purpose = ControlFileReadPurpose::ControlData` is the default.
- Callers: `chain.save` is registered at `src/core/ControlCommandsChain.cpp:284` (`cmd.id = QStringLiteral("chain.save")`), and its handler calls `storeWrite` at :334-338. The retained `/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/evidence-2.log` shows the `chain.save` id (line 6), the 64 MB `ControlDataReadLimit` (line 73) and both guard lines (lines 139-146; line 138 is a section header). Its `rg` shows the definition at :150 and one call at :334. A search of `src/`, `include/` and `tests/src/` for `storeWrite(` finds only that definition and that call. `storeWrite` is inside the anonymous namespace opened at `ControlCommandsChain.cpp:72` and closed at :356, so no other translation unit can call it. This search covers `storeWrite` only, not every reader of `controlReadFileBytes`.

## Historical fix record

- Commit 9c7a95196e, 2026-10-05 13:13:18 +0100, subject `WIP: Luna in-progress state at Space Bunny takeover (uncommitted at 2026-10-05 13:15)`. The fix landed inside a WIP snapshot commit.
- `docs/bunny-fixes-2026-10-05/presets/takeover.md:90-120`: the original FIXED verdict for the same test. Pre-fix `ctest -R '^ControlChainPresetTest$'` EXIT=8, FAIL `(overwrite refused=0, file unchanged=1, history unchanged=0)`. Post-fix EXIT=0.
- `docs/luna-presets-2026-10-05-logs/ctest-red.log.md:38-40`: an earlier revision of the test, run from another worktree (`zene-fix-presets`), failing at `ControlChainPresetTest.cpp(463)` with `'!overwritten.ok' returned FALSE`. That is a different assertion message from the current test, so I cite it only as an older reproduction of the defect.

## Regression test

`tests/src/core/ControlChainPresetTest.cpp:520-571`, `anUnreadablePresetCannotBeOverwritten` (function at :527, data rows `chain` and `export`).

It creates a preset, makes the file write-only (`QFile::setPermissions(path, WriteOwner)`), and runs the command again with `overwrite:true`. It asserts that the call is refused (`ControlErrorKind::Refused`), that the file bytes are unchanged, and that the journal's top serial is unchanged. The test QSKIPs if no built-in instrument exists or if the runner can read write-only files. Neither skip fired this session: the runner is uid 1000, and both rows reported PASS or FAIL, not SKIP.

## RED/GREEN evidence (this session)

Procedure: `sed` deleted only line 161, and the one-line deletion (hunk `@@ -158,7 +158,6 @@`, removed line 9) was saved as `/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/reversal-5615.patch`. I rebuilt, ran the test, restored with `git apply -R` on that patch, rebuilt, and ran the test again.

The retained logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/`. The RED and GREEN rebuild logs were not retained (quiet-run), so the retained RED and GREEN test logs are the evidence. No RED build log is claimed.

- GREEN 1 (`/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/test-unreadable-green-1.log`): EXIT=0 (line 11). Both rows PASS (lines 6-7). Totals 4 passed, 0 failed (line 9).
- RED (`/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/test-unreadable-red-1.log`): EXIT=1 (line 12). The chain row FAILs at line 6 with `overwrite refused=0, file unchanged=1, history unchanged=0`. The export row PASSes (line 8), as expected, since only the chain guard was removed. Totals 3 passed, 1 failed (line 10).
- Restore: the restore step's exit code and the post-restore `git diff` check were not retained as logs. The `git status --short` run for this report shows only the report, so the tree is at HEAD.
- GREEN 2 (`/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/test-unreadable-green-2.log`): EXIT=0 (line 11). Both rows PASS (lines 6-7). Totals 4 passed, 0 failed (line 9). Its rebuild log was not retained.

The reversal was temporary. The tree is back at HEAD with no product or test file changed.

## Commands and EXIT

| Label | Command | EXIT | Key result | Log (`/home/kruzzzzy/.config/kads/zene/state/runs/5615.r1/logs/`) |
|---|---|---|---|---|
| build-tripleoscillator-1 | `cmake --build build --target tripleoscillator` | 0 | EXIT=0 (line 18) | `build-tripleoscillator-1.log` |
| build-chainpreset-1 | `cmake --build build --target ControlChainPresetTest` | 0 | EXIT=0 (line 17) | `build-chainpreset-1.log` |
| test-unreadable-green-1 | `cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 QT_QPA_PLATFORM=offscreen ./ControlChainPresetTest anUnreadablePresetCannotBeOverwritten` | 0 | 4 passed, 0 failed (line 9) | `test-unreadable-green-1.log` |
| RED build (guard line removed) | same build | — | not retained (quiet-run) | none |
| test-unreadable-red-1 | same test, guard line removed | 1 | chain FAIL: `overwrite refused=0, file unchanged=1, history unchanged=0`; 3 passed, 1 failed (line 10) | `test-unreadable-red-1.log` |
| restore | `git apply -R` on the reversal patch | not retained | tree at HEAD; `git status --short` shows only the report | `reversal-5615.patch` |
| GREEN 2 rebuild | rebuild on restored tree | — | not retained (quiet-run) | none |
| test-unreadable-green-2 | same test, restored | 0 | 4 passed, 0 failed (line 9) | `test-unreadable-green-2.log` |
| test-chainpreset-1 | `cd build/tests && ctest -R '^ControlChainPresetTest$' --output-on-failure` | 8 | 24 passed, 2 failed (line 38); EXIT=8 (line 49) | `test-chainpreset-1.log` |

The RED and GREEN rebuild logs were not retained. The retained RED and GREEN test logs are the key evidence.

## Test matrix

| Case | Covered by | Result this session |
|---|---|---|
| Normal readable store edits and rename, unchanged | `theStoreEditsAreReversible` (`test-chainpreset-1.log` line 35) and `renamingToTheCurrentNameRecordsNoUndoStep` (line 36) | PASS: the normal readable store edits and rename are unaffected |
| Unreadable file must be refused | `anUnreadablePresetCannotBeOverwritten` (chain, export) | PASS with guard (green-1, green-2); FAIL (chain) without (red-1) |
| More than 64 MB is refused | `controlReadBoundedFile` (ControlDeviceState.cpp:60-99) and `fileReaderRefusesOversizedFiles` (ControlChainPresetTest.cpp:246-267) | PASS: `fileReaderRefusesOversizedFiles` at `test-chainpreset-1.log` line 8 |
| No reconfiguration path | Packet claim | Not re-checked |
| No audio-thread code | Packet claim; this job changes no product code | Not checked against a call graph |
| No new platform code | This job changes no code | Diff is empty |

## Other findings

- Two `ControlChainPresetTest` failures remain at HEAD and are not fixed by this job. `aFailedApplyKeepsTheOriginalDevicesAndHistory` fails with "Compared values are not the same" (`test-chainpreset-1.log` line 27; the log does not show its cause). `aCapturedChainAppliesToASecondTrackWithTheSameDevicesAndValues` fails with `'applied.ok' returned FALSE` and "this build has no device 'amp_mono (amp)'" (line 33). Both ran because tripleoscillator was built. Only the second shows the `amp_mono (amp)` message. The earlier p2 report did not list these two tests; the packet says they were skipped there, and the retained logs do not show that.
- The Python `ControlChainPresets` test was run by p2 twice. `test-1.log` (before the LADSPA host was built) fails with "this build loaded only 0 effect(s), the proof needs 2" (line 7) and "the LADSPA host plugin 'ladspaeffect' is not in this build" (line 61). `test-2.log` (after `ladspaeffect` was built, `build-ladspaeffect-1.log` line 36 EXIT=0) reports 13 failed checks (lines 210-237; summary line 243).
- In `test-2.log`, the six apply checks (lines 210-226) and the rename, remove and undo checks (lines 231-237) fail. The `amp_mono (amp)` identity mismatch appears in the apply path (`test-2.log` lines 74 and 215). The logs show that message only there, so the rename, remove and undo failures are not diagnosed here. `docs/qwen-fixes-2026-10/cluster-new-p10-29.md:61-67` documents the same `amp_mono (amp)` mechanism for `ChainPresetMenuTest` and the Python test.

## Self-check

The packet mentions submit_check ASK items but does not list them, so I answer by topic.

- Determinism: the permission seam is real. The runner is uid 1000, so the write-only file is not readable by it, and neither QSKIP branch fired. Each run was done once.
- Completeness: checked the only caller (`chain.save`, :284 and :334-338) and the sibling (`ControlCommandsExportPresets.cpp:147`). A search of `src/`, `include/` and `tests/src/` for `storeWrite(` found only the definition and that one call.
- No collateral: no product or test file changed. The reversal was reverted. `git status --short` shows only this report. Pre-existing failures are named under Other findings.
- ALREADY-FIXED code evidence: see Code evidence above.
- Sibling commands: export guard at `ControlCommandsExportPresets.cpp:147`. The export row PASSes in both RED and GREEN.
- Undo honesty: the guard returns before `recordStoreWrite` (:171), so a refused overwrite records no journal step. Without the guard, the RED run shows `history unchanged=0`, meaning a journal step was recorded. That is the state the guard prevents.
- Audio thread: this job changes no product code. Whether the control path runs on the audio thread was not checked.
- Old projects: no product change, so no effect on existing projects.
- Edge cases: normal (`test-chainpreset-1.log` lines 35-36 PASS), unreadable (tested and passing with the guard), oversized (refused by the bounded reader; `fileReaderRefusesOversizedFiles` PASS at `test-chainpreset-1.log` line 8).
