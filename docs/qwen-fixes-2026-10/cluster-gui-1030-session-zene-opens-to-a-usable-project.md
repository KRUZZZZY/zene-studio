Verdict: FIXED

gui-1030-session-zene-opens-to-a-usable-project: FIXED

## Finding and decision

Luna reported that the note editor's startup scale query is refused while the engine is still starting, and the refusal shows as an error toast on the first window. The finding is REAL on this branch.

Root cause, with evidence:

- `NoteEditor` is built during window construction and calls `setClip(nullptr)` at [NoteEditor.cpp:105](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:105). `setClip` calls `readScale()` at [NoteEditor.cpp:219](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:219), which issues `scale.get_state` through `run()`.
- `scale.get_state` requires the engine. `ControlRegistry::invoke` refuses it with `Busy` and the text "the engine is not addressable yet [engine_starting]" at [ControlRegistry.cpp:286-293](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/core/ControlRegistry.cpp:286). The engine becomes ready only at `setReady(true)` in [main.cpp:1839](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/core/main.cpp:1839), after the window exists.
- The second half of the defect: the original `readScale()` recorded `m_scaleRoot` and `m_scaleSeen` *before* the refused query ([NoteEditorToolbar.cpp:357-358](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditorToolbar.cpp:357) on the unmodified tree). `followScale()` ([NoteEditor.cpp:144-148](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:144), unmodified) compares against those values, so the refused read was never retried. The editor kept an empty scale until the scale changed. The RED run below shows this.

## Fix

- [NoteEditorToolbar.cpp:355-365](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditorToolbar.cpp:355): `readScale()` returns before any query while `ControlRegistry::isEngineReady()` is false, and sets `m_scaleDeferred`. Once the engine answers, it clears the flag and reads as before. The include of `ControlRegistry.h` is at [NoteEditorToolbar.cpp:38](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditorToolbar.cpp:38).
- [NoteEditor.cpp:147](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:147): `followScale()` also reads when `m_scaleDeferred` is set. It already runs on the editor's 33 ms playhead timer ([NoteEditor.cpp:110-113](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:110)), so the deferred query is answered on the first tick after readiness.
- [NoteEditor.h:232](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/include/NoteEditor.h:232): the new `bool m_scaleDeferred` member.

No command was added and no control-command table changed. No audio-thread code is touched: `readScale` and `followScale` run on the GUI thread, and nothing new logs or allocates on any path.

## Regression test

`NoteEditorTest::scaleIsReadWhenTheEngineIsReadyAfterStartup` at [NoteEditorTest.cpp:683-703](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/tests/src/gui/NoteEditorTest.cpp:683) (plus `#include <QTimer>` at [NoteEditorTest.cpp:31](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/tests/src/gui/NoteEditorTest.cpp:31)).

It sets a scale while the engine is ready, builds a `NoteEditor` while `ControlRegistry::setReady(false)`, restores ready, fires the editor's own 33 ms playhead timer once through `QMetaObject::invokeMethod`, and expects `state().scaleName` to equal the name `scale.get_state` reports. The test resets the scale at the end, as the neighbouring test does.

Test matrix:

- Normal path (editor built after readiness): covered by the existing `keepInScaleDrawsOnTheNearestScaleKey` and the other NoteEditor tests, all green.
- Built before readiness, then readiness, then the tick: the new test.
- Engine still not ready on ticks: `followScale` calls `readScale` each tick, which returns early without a query. Covered by the same test's ordering, and the refusal line no longer appears in any GREEN log.
- Scale changed later: the existing `followScale` path is unchanged (`m_scaleRoot`/`m_scaleSeen` comparison).
- Audio-thread and platform: none (GUI-only), offscreen platform as in the rest of the family.

Known limit of the harness: the toast the finding describes is not observable in this test binary, because `announce()` returns early when `getGUI()` is null ([CommandRun.cpp:90](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/CommandRun.cpp:90)). The test therefore asserts the observable contract (the scale is read once the engine is ready). The refusal itself is visible in the RED log's stderr line, quoted below.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-zene-opens-to-a-usable-project.r1/logs/`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Build attempt 1 | `cmake --build build --target zene NoteEditorTest -j4 > logs/build-1.log 2>&1` | 2 | build-1.log: ccache could not create a temp file under `dsh/home/ccache-tmp-r6` (Permission denied). Not a code error. |
| Build (ccache off) | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > logs/build-2.log 2>&1` | 0 | build-2.log |
| RED | `cd build/tests && ctest -R '^NoteEditorTest$' --output-on-failure > logs/test-red-1.log 2>&1` | 8 | test-red-1.log: `Totals: 41 passed, 1 failed`; `FAIL! NoteEditorTest::scaleIsReadWhenTheEngineIsReadyAfterStartup()`, `Actual ""`, `Expected "C Major"`, at NoteEditorTest.cpp:700; stderr `NoteEditor: scale.get_state refused: ... [engine_starting] ...` |
| Fix build | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest NoteEditorFlTest PianoRollBenchmarkTest TouchLayoutTest PanelShortcutsTest LaneReadoutTest ItemListPanelTest ItemListButtonsTest CanvasKeyboardTest -j4 > logs/build-3.log 2>&1` (every target that includes `NoteEditor.h`) | 0 | build-3.log |
| GREEN (target) | `cd build/tests && ctest -R '^NoteEditorTest$' --output-on-failure > logs/test-green-1.log 2>&1` | 0 | test-green-1.log: 100% passed |
| GREEN (family, verbose) | `cd build/tests && ctest -R '^(NoteEditorTest\|NoteEditorFlTest\|PianoRollBenchmarkTest\|PianoRollRecordTest\|CanvasKeyboardTest\|PanelShortcutsTest\|LaneReadoutTest\|ItemListPanelTest\|ItemListButtonsTest\|TouchLayoutTest)$' -V > logs/test-green-2.log 2>&1` | 8 | test-green-2.log: 9 of 10 passed; `PianoRollRecordTest` "Not Run" because its binary had not been built yet |
| Build PianoRollRecordTest | `CCACHE_DISABLE=1 cmake --build build --target PianoRollRecordTest -j4 > logs/build-4.log 2>&1` | 0 | build-4.log |
| GREEN (PianoRollRecordTest) | `cd build/tests && ctest -R '^PianoRollRecordTest$' -V > logs/test-green-3.log 2>&1` | 0 | test-green-3.log: `Totals: 8 passed, 0 failed` |
| Proof: save patch | `git diff -- include/NoteEditor.h src/gui/noteeditor/NoteEditor.cpp src/gui/noteeditor/NoteEditorToolbar.cpp > logs/fix.patch` | 0 | fix.patch |
| Proof: revert | `git apply -R logs/fix.patch` | 0 | — |
| Proof: rebuild without fix | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > logs/build-5-reverted.log 2>&1` | 0 | build-5-reverted.log |
| Proof: must FAIL | `cd build/tests && ctest -R '^NoteEditorTest$' -V > logs/test-proof-reverted.log 2>&1` | 8 | `Totals: 41 passed, 1 failed`, same assertion as RED |
| Proof: re-apply | `git apply logs/fix.patch` | 0 | — |
| Proof: rebuild with fix | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > logs/build-6-fixed.log 2>&1` | 0 | build-6-fixed.log |
| Proof: must PASS | `cd build/tests && ctest -R '^NoteEditorTest$' -V > logs/test-proof-fixed.log 2>&1` | 0 | `Totals: 42 passed, 0 failed` |

Affected family, GREEN, after the final rebuild: NoteEditorTest 42/0, NoteEditorFlTest 18/0, PianoRollBenchmarkTest 6/0, PianoRollRecordTest 8/0, TouchLayoutTest 12/0, CanvasKeyboardTest 18/0, ItemListPanelTest 24/0, ItemListButtonsTest 9/0, LaneReadoutTest 10/0, PanelShortcutsTest 4/0. Ten test families executed, zero failures.

Gates. `feature_check.py` exited 1 before running any gate (log `gates-1.log`): it writes its logs to `<worktree>/../<name>-feature-check/`, the `zene-studio-program` directory, which this session cannot create in (`PermissionError`). The gate script was not changed. I ran the same eight commands the script runs, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets exit 1 for the whole tree, as the train is red on purpose. Under the script's own rule (fail only for items in files this branch changed), both rows pass: the 7 complexity REGRESSION lines are in `WasmSandbox.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`, `VideoAudioExtract.cpp` and `XyPadPairTest.cpp`. The file-length REGRESSION lines are in `Vst3Host.cpp`, `WasmSandbox.cpp` and new test or source files. None names `NoteEditor.h`, `NoteEditor.cpp`, `NoteEditorToolbar.cpp` or `NoteEditorTest.cpp`. `readScale` and `followScale` stay far under CCN 10. `NoteEditorToolbar.cpp` grew 418 to 427 lines, under 500.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: `test-red-1.log`, ctest EXIT=8, 41 passed and 1 failed. Proof with the patch reverted: `test-proof-reverted.log`, EXIT=8. Proof with the patch re-applied: `test-proof-fixed.log`, EXIT=0, 42 passed.
- **The test is deterministic.** No sleeps, no wall-clock waits. The seam is the existing `ControlRegistry::setReady(false/true)` toggle. The editor's own 33 ms playhead `QTimer` (found by `findChildren<QTimer*>()` with interval 33, at NoteEditorTest.cpp:697) is fired once with `QMetaObject::invokeMethod(tick, "timeout")`, so the follow-the-scale step runs synchronously.
- **The fix is at the cause, and callers and siblings were checked.** The cause is the unconditional query in `readScale()` and the recorded "seen" values that stop `followScale()` from retrying. Callers of `readScale()`: `setClip` ([NoteEditor.cpp:219](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:219)), `followScale` ([NoteEditor.cpp:147](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditor.cpp:147)), and the scale-menu actions after user input ([NoteEditorToolbar.cpp:334](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/noteeditor/NoteEditorToolbar.cpp:334)), which run only after the editor is interactive and are unchanged. Siblings: `ChordProgressionPanel::refresh()` also calls `scale.get_state` ([ChordProgressionPanel.cpp:173](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-session-zene-opens-to-a-usable-project/src/gui/ChordProgressionPanel.cpp:173)). I did not verify whether it is constructed before readiness, and it is outside this finding, so it is unchanged and noted here.
- **No collateral damage.** Once the engine is ready, `readScale` runs exactly as before. Before readiness, the grid keeps its default mask (`NoteGrid.h:187-188`: empty mask, root 0), which is the state the refused query used to leave. No saved data, project load or limit is touched. `scale.get_state` is non-mutating, so undo is unaffected. No new control command was added, so `ControlCommandsSnapshot` is unchanged. No new files, so `fork-sources.txt` and `upstream-modifications.txt` are unchanged.
- **Verdict FIXED**, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
