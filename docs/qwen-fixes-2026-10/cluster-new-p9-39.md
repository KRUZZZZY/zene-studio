# Cluster new-p9-39: `clip.slip` auto-resize on undo

**Verdict: FALSE-POSITIVE**

## Evidence

- `src/core/ControlCommandsClipTrim.cpp:238-248` takes a clip journal checkpoint, changes the source offset, clears auto-resize, and records `clip.slip` as the inverse operation. The transaction does not set `inverse.applies`.
- `src/core/ControlEditSupport.cpp:481-492` constructs that inverse with only `op` and `args`.
- `src/core/ControlCommandsControl.cpp:330-336` defaults missing `inverse.applies` to `journal`; it dispatches the inverse operation only when `applies` is `command`. `undoLastCommand` falls through to `undoThroughJournal` at lines 412-417, which calls `ProjectJournal::undo()` at lines 300-309.
- The checkpoint carries the flag: `src/tracks/MidiClip.cpp:441-446` writes `autoresize`, and `src/tracks/MidiClip.cpp:552-558` restores it.
- I temporarily added assertions to `ControlVerbInverseTest::clipSlipIsOneUndoableStep` for auto-resize being true before slip, false after slip, and true after undo. The focused test passed without any production change, so the reported state leak does not reproduce.

## Verification

| Check | Command and result |
| --- | --- |
| Build before the candidate regression run | `cmake --build build --target zene ControlVerbInverseTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-39.r1/logs/build-1.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc"; exit "$build_rc"` — EXIT=0 |
| Candidate RED attempt (no production change) | From `build/tests`: `ctest -R ControlVerbInverseTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-39.r1/logs/test-1.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc"; exit "$test_rc"` — EXIT=0; 1/1 ctest tests passed, so no RED was observed |
| GREEN after fix | Not applicable: the candidate regression passed before a fix and no product fix was made |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-39.r1/logs/gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc"; exit "$gate_rc"` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-39.r1/logs/gate-2.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc"; exit "$gate_rc"` — EXIT=0 |

Affected-family count: 1 ctest test (`ControlVerbInverseTest`), passed.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p9-39.md`.
