# Cluster new-p9-449

Verdict: **FIXED**

## Evidence

- `src/core/SessionTuningScala.cpp:232-243`: `.kbm` base frequencies now have to fit in `float` and remain nonzero after conversion.
- `src/core/SessionTuning.cpp:145-159`: mapped table entries that convert to float zero are refused; zero remains valid for keys whose keymap degree is `-1`.
- `tests/src/core/SessionTuningTest.cpp:909-974`: the regression covers a `1e-100` base frequency, a positive mapped frequency that narrows to zero, table preservation on refusal, and an intentional `x` mapping.

## Verification

The regression was run through `mts.load_keymap`. For the isolated RED proof, the product-only fix was reversed, then the test target and `zene` were rebuilt before ctest. The fix was reapplied and both targets rebuilt before the GREEN run.

The `fff` grep tool was not exposed and `command -v fff` returned no executable; logs were searched with one-term `rg -n` queries for `error`, `FAILED`, and `Assertion`.

| Check | Command and working directory | Exit | Result |
|---|---|---:|---|
| RED proof | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` | 8 | `test-6.log`: 14 passed, 1 failed; `aKeymapFrequencyThatUnderflowsTheRenderFloatIsRefused` failed at line 934 because the `1e-100` base was accepted. |
| GREEN proof | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` | 0 | `test-7.log`: CTest test #46, `SessionTuningTest`, passed (1/1). |
| Affected-family count | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` | 0 | One registered CTest test ran and passed; no zero-test run. |
| Complexity gate | `bash tests/complexity-gate.sh --check` from the worktree | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` from the worktree | 0 | Passed. |

Commands and supporting results:

- `git diff -- src/core/SessionTuning.cpp src/core/SessionTuningScala.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-449.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-449.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target SessionTuningTest` — EXIT=0 before RED (`build-6.log`) and before GREEN (`build-8.log`).
- `cmake --build build --target zene` — EXIT=0 before RED (`build-7.log`) and before GREEN (`build-9.log`).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-449.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target tripleoscillator` — EXIT=0 (`build-5.log`); this supplied the instrument fixture used by the existing live-note slot.
- Before the fixture build, ctest runs exited 8 (`test-1.log` through `test-4.log`): the RED assertion was observed, while the existing live-note slot also lacked a loadable instrument. After building the fixture, the fixed test passed (`test-5.log`, EXIT=0); the isolated revert proof is recorded separately above.
- The final gate runs after this report edit are recorded in `gate-5.log` and `gate-6.log`.

## Files committed

- `src/core/SessionTuning.cpp`
- `src/core/SessionTuningScala.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-449.md`
