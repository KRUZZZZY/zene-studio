# Cluster new-p11-12

Verdict: **FIXED**

The panel previously cleared the invite when `collab.join` returned, even though that only starts the asynchronous connection. `CollabPanel::refresh()` now clears the field only when the peer state is `joined` (`src/gui/CollabPanel.cpp:134-138,164-171`). The regression test clicks Join with a valid invite to a local non-TLS listener, waits for the client to report `failed`, and verifies the invite remains (`tests/src/gui/CollabPanelTest.cpp:118-161`).

## Evidence

| Check | Result |
| --- | --- |
| Red before fix, with production code unchanged | `CollabPanelTest`: 3 QtTest cases passed, the regression failed because `inviteField->text()` was empty; CTest exit 8. |
| Green after fix | CTest ran 1/1 affected-family test (`CollabPanelTest`, test #304), exit 0. The executable has 4 QtTest cases. |
| Remove/reapply proof | Removing only the production patch rebuilt successfully and reproduced the same empty-field failure (CTest exit 8); restoring it rebuilt and passed (CTest exit 0). |
| Complexity gate | exit 0 |
| File-length gate | exit 0 |

## Commands and exits

All commands below ran in the worktree unless a test command says `build/tests`. Build and test output was saved in the stated new log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs`.

The first test attempt caught a headless setup issue in the new test (not the product bug); the test was corrected to temporarily enable the panel command.

```text
cmake --build build --target CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R '^CollabPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result
EXIT=8 (the new test was refused by the headless registry before reaching Join)

cmake --build build --target CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R '^CollabPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result
EXIT=8 (red: 3 passed, 1 failed; actual invite field was empty)

cmake --build build --target zene CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R '^CollabPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

git diff -- src/gui/CollabPanel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/fix.patch; result=$?; echo EXIT=$result
EXIT=0

cmake --build build --target zene CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R '^CollabPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result
EXIT=8 (patch removed: same regression assertion failed)

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/fix.patch; result=$?; echo EXIT=$result
EXIT=0

cmake --build build --target zene CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R '^CollabPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result
EXIT=0 (patch restored: 1/1 affected-family CTest test passed)

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-12.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

Files committed:

- `src/gui/CollabPanel.cpp`
- `tests/src/gui/CollabPanelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-12.md`
