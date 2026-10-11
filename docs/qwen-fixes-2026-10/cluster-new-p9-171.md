# Cluster new-p9-171: non-finite hardware insert mix

**Verdict: FIXED**

`src/core/HardwareInsert.cpp:222-223` now checks the parsed mix with `std::isfinite()` before clamping it. A non-finite value uses the existing default `Settings{}.mix` (`1.f`), keeping both wet and dry gains finite.

The regression at `tests/src/core/HardwareIoTest.cpp:286-309` loads `<hardwareinsert mix="nan">`, confirms Qt parses the value as NaN, then processes a period and checks both output channels frame by frame.

## Evidence

The affected CTest family is `HardwareIoTest`: one CTest target with 10 scenario slots, plus `initTestCase` and `cleanupTestCase`. Both green runs executed the target successfully. The red runs reported 11 passes and the regression failing on the first output sample.

| Check | Command / working directory | Exit |
| --- | --- | ---: |
| Original code, RED (`test-1.log`) | `ctest -R HardwareIoTest --output-on-failure` from `build/tests` | 8 |
| Fixed code, GREEN (`test-2.log`) | `ctest -R HardwareIoTest --output-on-failure` from `build/tests` | 0 |
| Fix removed with the required reverse patch, RED (`test-3.log`) | `ctest -R HardwareIoTest --output-on-failure` from `build/tests` | 8 |
| Fix reapplied, GREEN (`test-4.log`) | `ctest -R HardwareIoTest --output-on-failure` from `build/tests` | 0 |

In both RED runs, `HardwareIoTest::aNonFiniteSavedMixDoesNotContaminateAudio()` failed because `std::isfinite(frame[0])` returned false. The reverse-patch check used:

```sh
git diff -- src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-171.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-171.r1/logs/fix.patch
```

Exit 0 (`proof-1.log`). Reapplying that patch with
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-171.r1/logs/fix.patch` also exited 0 (`proof-2.log`).

## Builds and gates

`cmake --build build --target zene HardwareIoTest` exited 0 in all four builds (`build-1.log` through `build-4.log`), including before each RED run and after each production-code change.

| Gate command | Log | Exit |
| --- | --- | ---: |
| `bash tests/complexity-gate.sh --check` | `gate-1.log` | 0 |
| `bash tests/file-length-gate.sh --check` | `gate-2.log` | 0 |

Build, test, proof, and gate logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-171.r1/logs`.
The fff grep tool was not available in this session; I searched each required log term separately with `rg -n -i`.

## Files in the commit

- `src/core/HardwareInsert.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-171.md`
