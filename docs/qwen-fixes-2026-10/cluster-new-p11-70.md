# Cluster new-p11-70: crossfade editor live updates

**Verdict: BLOCKED**

The finding is real and the behavior fix is implemented. The required regression and reversal proof pass as expected. Cluster acceptance remains blocked because both mandatory ratchet gates exit 1 on violations in untouched files; their baselines were not changed.

## File evidence

- `src/gui/CrossfadeEditorDialog.cpp:170`: the dialog now reloads from both clips' `positionChanged`, `lengthChanged`, and `dataChanged` signals.
- `include/Clip.h:174` and `src/core/Clip.cpp:247`: `setClipEdits()` is out of line, ignores identical edits, and emits `dataChanged()` after a real edit so external fade edits reach open editors.
- `tests/src/gui/CrossfadeEditorTest.cpp:165` and `:199`: regressions cover moving/resizing either clip and applying after an external fade edit.

## Regression evidence

All CTest commands below ran from `build/tests`. The affected family is one CTest test, `CrossfadeEditorTest` (CTest test #328).

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build original-code regression | `cmake --build build --target zene CrossfadeEditorTest` | 0 | `logs/build-2.log` |
| RED, before the fix | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 8 | `logs/test-2.log`: 1/1 CTest test; 5 passed and 2 failed on the stale summary and overwritten fade assertions |
| GREEN, after the fix | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 0 | `logs/test-3.log`: 1/1 passed |
| Reverse product patch | `git diff -- include/Clip.h src/core/Clip.cpp src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r1/logs/fix.patch` | 0 | The regression remained in the worktree |
| Build with product patch reversed | `cmake --build build --target zene CrossfadeEditorTest AutomationEditorEditingTest ArrangementModesTest MidiClipLinkedEditTest ClipGainEnvelopeTest ClipTransposeTest ClipFadesRenderTest ClipOffsetFadeTest ClipEditsTest ClipConsolidateTest ClipOffsetFadePairTest` | 0 | `logs/build-5.log` |
| RED, with the product patch reversed | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 8 | `logs/test-4.log`: the same 2 assertions fail; 5 pass |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-70.r1/logs/fix.patch` | 0 | Product changes restored |
| Rebuild with product patch restored | `cmake --build build --target zene CrossfadeEditorTest AutomationEditorEditingTest ArrangementModesTest MidiClipLinkedEditTest ClipGainEnvelopeTest ClipTransposeTest ClipFadesRenderTest ClipOffsetFadeTest ClipEditsTest ClipConsolidateTest ClipOffsetFadePairTest` | 0 | `logs/build-6.log` |
| GREEN, with product patch restored | `ctest -R '^CrossfadeEditorTest$' --output-on-failure` | 0 | `logs/test-5.log`: 1/1 passed |

The shared build wrapper rejected the unscoped full-build command `cmake --build build` with exit 97 (`logs/build-3.log`), requiring named targets. The named build above includes `zene` and every test target found to call `setClipEdits()`.

## Required gates

| Gate | Exact command | Exit | Result |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 1 | `logs/gate-1.log`: six unbaselined over-limit function regressions in untouched files, including `tests/src/gui/XyPadPairTest.cpp` and `src/core/DawProjectReadTracks.cpp` |
| File length | `bash tests/file-length-gate.sh --check` | 1 | `logs/gate-2.log`: 19 files exceed 500 code lines, with new over-limit files including `src/core/SmfInterchangeReader.cpp` and `src/core/TakeLane.cpp` |

These gate failures are outside the changed files. The gates and their baselines remain unchanged.

## Files committed

- `include/Clip.h`
- `src/core/Clip.cpp`
- `src/gui/CrossfadeEditorDialog.cpp`
- `tests/src/gui/CrossfadeEditorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-70.md`
