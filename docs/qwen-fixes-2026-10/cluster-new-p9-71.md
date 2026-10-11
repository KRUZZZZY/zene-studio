# cluster new-p9-71: repeated stretch mode must not consume undo

Verdict: **FIXED**.

`warp.stretch` now checkpoints and reports an inverse transaction only when the requested mode differs from the clip's current mode. A repeated mode request reports `changed: false` and leaves the preceding useful edit at the top of undo.

## Evidence

| Evidence | Result |
| --- | --- |
| Production path | [ControlCommandsWarpEdit.cpp](../../src/core/ControlCommandsWarpEdit.cpp:378) checks `previous != wanted`; the checkpoint and setter are at lines 382–389, and the inverse payload is added only for a change at lines 395–407. |
| Regression | [SampleClipStretchTest.cpp](../../tests/src/tracks/SampleClipStretchTest.cpp:464) changes a warped clip to `preserve_pitch`, requests that same mode again, then verifies one undo restores `resample`. |
| RED on original code | `ctest -R '^SampleClipStretchTest$' --output-on-failure` from `build/tests`: EXIT=8; the new assertion read `preserve_pitch` after undo instead of `resample` (log `test-1.log`). |
| Reverse-patch proof | With `fix.patch` reversed and both targets rebuilt, `ctest -R '^SampleClipStretchTest$' --output-on-failure -V`: EXIT=8; the same assertion failed (log `test-5.log`). |
| GREEN after reapplying fix | With `zene` and `SampleClipStretchTest` rebuilt, `ctest -R '^SampleClipStretchTest$' --output-on-failure -V`: EXIT=0; 1/1 CTest target passed and QtTest reported 8 passed, 0 failed (log `test-6.log`). |
| Affected-family count | 1 CTest target; 8 QtTest cases passed. |
| Ratchet gates | `bash tests/complexity-gate.sh --check`: EXIT=0 (`gate-1.log`, rerun after the report in `gate-3.log`). `bash tests/file-length-gate.sh --check`: EXIT=0 (`gate-2.log`, rerun after the report in `gate-4.log`). |

The first corrected build attempt still reported a transaction payload for the no-op; `control.undo` then refused because that payload had no corresponding journal step (`test-2.log`, EXIT=8). The final change omits that payload when unchanged, allowing the registry to treat the call as a no-op.

## Build and test commands

All build and test output was redirected to a new numbered log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-71.r1/logs`, with each command's exit code appended to its log.

| Exact command | EXIT | Logs |
| --- | ---: | --- |
| `cmake --build build --target SampleClipStretchTest` | 0 | `build-1.log`, `build-3.log`, `build-5.log`, `build-7.log`, `build-9.log` |
| `cmake --build build --target zene` | 0 | `build-2.log`, `build-4.log`, `build-6.log`, `build-8.log` |
| `ctest -R '^SampleClipStretchTest$' --output-on-failure` (from `build/tests`) | 8 | `test-1.log` (original-code RED), `test-2.log` (intermediate payload failure); 0 in `test-3.log` and `test-4.log` after the fix |
| `ctest -R '^SampleClipStretchTest$' --output-on-failure -V` (from `build/tests`) | 8 | `test-5.log` (reverse-patch RED); 0 in `test-6.log` after reapplying the fix |
| `git diff -- src/core/ControlCommandsWarpEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-71.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-71.r1/logs/fix.patch` | 0 | Reverse-patch proof setup |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-71.r1/logs/fix.patch` | 0 | Restored the fix before final rebuilds |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`, `gate-3.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`, `gate-4.log` |
| `git diff --check` | 0 | No whitespace errors |

The available tools did not expose the requested `fff` log-search command. The numbered logs were searched with `rg` for `error`, `FAILED`, and assertion/test summary terms; the relevant red and green assertions and all exit codes above are from those logs.

## Files committed

- `src/core/ControlCommandsWarpEdit.cpp`
- `tests/src/tracks/SampleClipStretchTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-71.md`
