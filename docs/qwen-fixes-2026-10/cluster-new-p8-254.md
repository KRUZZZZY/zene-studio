# Cluster new-p8-254: same-macro XY pad state

**Verdict: FIXED**

The finding is real. `XyPadDialog::send` sends only X when both axes select one macro. After a successful send, it now sets the displayed point to `(X, X)`, keeping Y aligned with the value the macro actually received. Refused sends still resync from rack state. The regression uses the pad's Up-key path and compares the displayed Y value with the rack macro value.

Evidence is at `src/gui/XyPad.cpp:274` and `src/gui/XyPad.cpp:289`; the regression setup is at `tests/src/gui/XyPadPairTest.cpp:122`.

## Verification evidence

Commands below ran in the worktree; CTest commands ran from `build/tests`. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-254.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R '^XyPadPairTest$' --output-on-failure` | 8 | 1 test failed at the new macro-versus-dot assertion (`test-1.log`). |
| GREEN with fix | `ctest -R '^XyPadPairTest$' --output-on-failure` | 0 | 1/1 test passed (`test-5.log`). |
| Inverse-patch RED | `ctest -R '^XyPadPairTest$' --output-on-failure` | 8 | Rebuilding without the product fix reproduced the same assertion failure (`test-4.log`). |
| Affected family count | `ctest -R '^XyPadPairTest$' --output-on-failure` | 0 | 1 test executed, 1 passed on the final run. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | PASS (`gate-3.log`). |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | PASS (`gate-4.log`). |

The inverse proof used:

```sh
git diff -- src/gui/XyPad.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-254.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-254.r1/logs/fix.patch  # EXIT=0
cmake --build build --target zene XyPadPairTest  # EXIT=0, build-4.log
ctest -R '^XyPadPairTest$' --output-on-failure  # EXIT=8, test-4.log
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-254.r1/logs/fix.patch  # EXIT=0
cmake --build build --target zene XyPadPairTest  # EXIT=0, build-5.log
ctest -R '^XyPadPairTest$' --output-on-failure  # EXIT=0, test-5.log
```

Build/test command history: `cmake --build build --target XyPadPairTest` exited 0 (`build-1.log`); its original-code CTest run exited 8 (`test-1.log`). `cmake --build build --target zene XyPadPairTest` exited 0 (`build-2.log`), while the first post-fix run exited 8 (`test-2.log`) because the new test left Y on the shared macro before the existing refusal case. The test now restores Y to macro 1. The corrected build exited 0 (`build-3.log`) and CTest exited 0 (`test-3.log`). Both gates were rerun after the report was written and passed with exit 0.

The fff grep tool was not exposed in this session, so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/gui/XyPad.cpp`
- `tests/src/gui/XyPadPairTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-254.md`
