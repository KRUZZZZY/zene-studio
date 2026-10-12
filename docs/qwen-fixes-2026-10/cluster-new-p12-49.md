# Cluster new-p12-49

Verdict: **FIXED**

`selectInArranger` in `src/gui/ControlCommandsViewMemory.cpp:106-116` now reports whether an arranger was available. `applySelection` at `:326-331` marks the `clips` part restored only when live clips were passed through that arranger path. With no arranger, it still selects the first live clip on the control surface, while the response correctly lists `clips` under `skipped`.

The regression at `tests/src/core/ControlEditCommandsTest.cpp:318-354` creates two live clips in the headless engine, saves and applies both, checks that `clips` is skipped and none are missing, and confirms the surface selection remains the first clip.

## Evidence

The original production code failed the targeted assertion (`restored` incorrectly contained `clips`). The same test failed again with the production patch reversed, then passed after it was reapplied. The final affected test family ran 10 QtTest cases: 10 passed, 0 failed. CTest reported 1/1 family target passed.

| Evidence | Exact command or result |
|---|---|
| RED | CWD `build/tests`: `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` — **EXIT=8** |
| GREEN | CWD `build/tests`: `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| Affected-family count | CWD `build/tests`: `ctest -R '^ControlEditCommandsTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-family-verbose-1.log 2>&1; echo EXIT=$?` — **EXIT=0**, 1/1 CTest target and 10 QtTest cases passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/gate-complexity-2.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/gate-file-length-2.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| Files committed | `src/gui/ControlCommandsViewMemory.cpp`; `tests/src/core/ControlEditCommandsTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p12-49.md`. |

Commands below were run from the stated working directories; each line includes the exact shell command and its observed exit code. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs`.

### Initial RED and GREEN

- CWD: worktree
  - `cmake --build build --target ControlEditCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
- CWD: `build/tests`
  - `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` — **EXIT=8**; targeted assertion failed, 9 passed and 1 failed.
- CWD: worktree
  - `cmake --build build --target zene ControlEditCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
- CWD: `build/tests`
  - `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` — **EXIT=0**

### Sensitivity proof

- CWD: worktree
  - `git diff -- src/gui/ControlCommandsViewMemory.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**
  - `cmake --build build --target zene ControlEditCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/build-sensitivity-red-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
- CWD: `build/tests`
  - `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-sensitivity-red-1.log 2>&1; echo EXIT=$?` — **EXIT=8**; the same targeted assertion failed.
- CWD: worktree
  - `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**
  - `cmake --build build --target zene ControlEditCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/build-sensitivity-green-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
- CWD: `build/tests`
  - `ctest -R '^ControlEditCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-sensitivity-green-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
  - `ctest -R '^ControlEditCommandsTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/test-family-verbose-1.log 2>&1; echo EXIT=$?` — **EXIT=0**; 10 passed, 0 failed.

### Required gates

- CWD: worktree
  - `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` — **EXIT=0**
  - `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-49.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` — **EXIT=0**

## Commit payload

- `src/gui/ControlCommandsViewMemory.cpp`
- `tests/src/core/ControlEditCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-49.md`
