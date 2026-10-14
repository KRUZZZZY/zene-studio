# Cluster new-p12-5

Verdict: **FIXED**

`track.add`'s public description said the command was not reversible. The handler records a reversible action checkpoint (`src/core/ControlCommandsArrangement.cpp:182-190`); undo removes the created track, and redo creates it again while storing its new ID (`:165-173`). The description now says undo removes the track and redo recreates it with a fresh stable `trk-<n>` ID (`:118-120`).

The regression reads the live `track.add` entry from `control.commands_list` and checks that its description advertises undo, redo, and a fresh ID (`tests/control-undo-structural.py:63-73`). It failed against the original description and passed after the fix. The affected CTest family contained one test, `ControlUndoStructuralTranscript` (1/1 executed).

## Evidence

Commands ran in the worktree unless a different working directory is shown. Build and test output was redirected to the listed new log file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-5.r1/logs`.

| Stage | Command | Exit | Log / result |
|---|---|---:|---|
| Original binary rebuild | `cmake --build build --target zene` | 0 | `build-1.log` |
| RED, from `build/tests` | `ctest -R ControlUndoStructuralTranscript --output-on-failure` | 8 | `test-1.log`; 1/1 ran, new description assertion failed |
| Fixed binary rebuild | `cmake --build build --target zene` | 0 | `build-2.log` |
| GREEN, from `build/tests` | `ctest -R ControlUndoStructuralTranscript --output-on-failure` | 0 | `test-2.log`; 1/1 passed |
| Reverse the product patch | `git diff -- src/core/ControlCommandsArrangement.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-5.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-5.r1/logs/fix.patch` | 0 | The original description was restored |
| Rebuild with patch reversed | `cmake --build build --target zene` | 0 | `build-3.log` |
| Reversal RED, from `build/tests` | `ctest -R ControlUndoStructuralTranscript --output-on-failure` | 8 | `test-3.log`; the same description assertion failed, 1/1 ran |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-5.r1/logs/fix.patch` | 0 | The fix was restored |
| Final rebuilt binary | `cmake --build build --target zene` | 0 | `build-5.log` |
| Final GREEN, from `build/tests` | `ctest -R ControlUndoStructuralTranscript --output-on-failure` | 0 | `test-5.log`; 1/1 passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File length ratchet | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The fff grep tool was not exposed in this session, so the numbered logs were searched with `rg` for `error`, `FAILED`, `Assertion`, the regression assertion, and the CTest summary. `src/core/ControlCommandsArrangement.cpp` and `tests/control-undo-structural.py` were already registered in `tests/fork-sources.txt`; no registration change was needed.

## Files committed

- `src/core/ControlCommandsArrangement.cpp`
- `tests/control-undo-structural.py`
- `docs/qwen-fixes-2026-10/cluster-new-p12-5.md`
