# Cluster new-p12-111

Verdict: **FIXED**

## Finding and change

`src/core/LinkSync.cpp:65` now defines the normalized revision owner as
`revisionOwner` when present and otherwise the packet's peer `id`.
`src/core/LinkSync.cpp:282` compares that normalized owner for same-revision
tiebreaking, and `src/core/LinkSync.cpp:295` stores the same normalized value
when adopting. This keeps the comparison order consistent with the adopted
session state.

`tests/src/core/ControlLinkCommandsTest.cpp:286` adds a same-revision packet
with an empty owner and a peer id greater than the current owner. Before the
fix it failed because the tempo stayed at 122 instead of adopting 134; it
passed with the fix. `src/core/LinkSyncWire.cpp:88` confirms that packet decode
preserves an empty owner, so this input is accepted on the receive path.

## Command evidence

Build commands ran from the worktree root. CTest commands ran from
`build/tests`. The CTest family contains one registered test target,
`ControlLinkCommandsTest`.

| Stage | Exact command | Exit |
|---|---|---:|
| Initial build | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED run | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Fixed build | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN run | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Remove production patch | `git diff -- src/core/LinkSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Negative-control build | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Negative-control run | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored build | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored GREEN run | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-111.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The RED and negative-control runs each executed one registered CTest test and
failed on the new assertion (`session_tempo` remained 122, expected 134). The
fixed and restored GREEN runs each executed one registered CTest test and
passed. Both final gates passed.

## Files committed

- `src/core/LinkSync.cpp`
- `tests/src/core/ControlLinkCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-111.md`
