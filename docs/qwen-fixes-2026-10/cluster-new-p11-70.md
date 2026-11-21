# Cluster new-p11-70: crossfade editor live updates

**Verdict: FIXED**

The finding was real. The editor now reloads for clip moves, resizes, and clip-edit changes. It no longer reloads for unrelated `dataChanged` notifications such as a clip rename, so pending control values survive those edits. External fade edits still update the controls.

## File evidence

- `include/Clip.h:318`: declares `Clip::clipEditsChanged`.
- `src/core/Clip.cpp:251`: `setClipEdits()` emits that signal only when the edit value changes; line 252 retains the general `dataChanged()` notification.
- `src/gui/CrossfadeEditorDialog.cpp:171-173`: retains the position and length listeners and listens to `clipEditsChanged` instead of generic `dataChanged`.
- `tests/src/gui/CrossfadeEditorTest.cpp:199`: the rename regression; lines 209-210 verify the pending ticks and shape remain.
- `tests/src/gui/CrossfadeEditorTest.cpp:213`: the external fade regression; lines 226-227 verify the dialog controls refresh before Apply.

## Regression and build evidence

CTest commands ran from `build/tests`; build and gate commands ran from the worktree root. Each command's output and unpiped exit code are in the new numbered log listed below.

The affected CTest family contains one test, `CrossfadeEditorTest`. Both green runs passed 1/1; both red runs failed 1/1 on `unrelatedClipChangesPreservePendingControls`.

| Check | Exact command | Exit | Log |
|---|---|---:|---|
| Build test with regression added, before product fix | `cmake --build build --target zene CrossfadeEditorTest` | 0 | `build-1.log` |
| RED against the previous product code | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 8 | `test-1.log` |
| Build fixed product and all existing `Clip.h` dependent targets | `cmake --build build --target zene zene_api lmmsobjs AutomationEditorEditingTest ArrangementModesTest ClipConsolidateTest ClipEditsTest ClipFadesRenderTest ClipGainEnvelopeTest ClipOffsetFadePairTest ClipOffsetFadeTest ClipTransposeTest CrossfadeEditorTest MidiClipLinkedEditTest` | 0 | `build-2.log` |
| GREEN with the fix | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 0 | `test-2.log` |
| Reverse only product hunks | `git diff -- include/Clip.h src/core/Clip.cpp src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r2/logs/fix.patch` | 0 | `proof-1.log` |
| Rebuild with product hunks reversed | `cmake --build build --target zene CrossfadeEditorTest` | 0 | `build-3.log` |
| RED with only product hunks reversed | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 8 | `test-3.log` |
| Restore product hunks | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r2/logs/fix.patch` | 0 | `proof-2.log` |
| Rebuild all existing `Clip.h` dependent targets after restore | `cmake --build build --target zene zene_api lmmsobjs AutomationEditorEditingTest ArrangementModesTest ClipConsolidateTest ClipEditsTest ClipFadesRenderTest ClipGainEnvelopeTest ClipOffsetFadePairTest ClipOffsetFadeTest ClipTransposeTest CrossfadeEditorTest MidiClipLinkedEditTest` | 0 | `build-4.log` |
| Final GREEN | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 0 | `test-4.log` |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r2/logs`. The fff grep tool was not exposed in this session and `command -v fff` returned 1; I searched the logs with single-term `rg -n` queries instead.

## Required gates

| Gate | Exact command | Exit | Result |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 1 | `gate-1.log`: six over-limit functions in untouched files, including `tests/src/gui/XyPadPairTest.cpp` and `src/core/DawProjectReadTracks.cpp`. |
| File length | `bash tests/file-length-gate.sh --check` | 1 | `gate-2.log`: fourteen over-500-line findings in untouched files, including `src/core/SmfInterchangeReader.cpp` and `src/core/TakeLane.cpp`. |

Neither gate names a changed file; gate thresholds and baselines were left unchanged.

## Files committed

- `include/Clip.h`
- `src/core/Clip.cpp`
- `src/gui/CrossfadeEditorDialog.cpp`
- `tests/src/gui/CrossfadeEditorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-70.md`
