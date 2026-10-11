# Cluster new-p12-7: FIXED

## Finding and change

`automation.transform` accepted `end <= start` and called `editableClip` first. On a parameter without automation, that helper could create an automation track before `transformRange` ran.

At [ControlCommandsAutomationLanes.cpp:167](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-7/src/core/ControlCommandsAutomationLanes.cpp:167), `transform` now rejects an inverted or empty range with `InvalidArgs` before resolving the parameter or calling `editableClip`. The regression at [ControlAutomationScriptTest.cpp:239](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-7/tests/src/core/ControlAutomationScriptTest.cpp:239) verifies the typed refusal, no automation clip, and no added track.

## Evidence

| Check | Result |
|---|---|
| RED, guard removed | `ControlAutomationScriptTest::transformRefusesAnInvertedRangeWithoutCreatingAutomation` failed at `QVERIFY(!refused.ok)`; ctest exit 8. |
| GREEN, guard restored | Same test family passed; ctest exit 0. |
| Affected-family count | 1/1 ctest entry: `ControlAutomationScriptTest`. |
| Complexity gate | exit 0. |
| File-length gate | exit 0. |

The RED/GREEN commands below are the final proof cycle, using the regression in its final test file. All builds include `zene`, the test target, and `tripleoscillator`; the test target needs that plugin to expose a parameter.

### Final RED/GREEN proof commands

```text
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/build-9.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/test-9.log 2>&1; echo EXIT=$?
EXIT=8

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/build-10.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/test-10.log 2>&1; echo EXIT=$?
EXIT=0
```

The red log contains: `FAIL! : ControlAutomationScriptTest::transformRefusesAnInvertedRangeWithoutCreatingAutomation() '!refused.ok' returned FALSE.` The green log reports `1/1 Test #22: ControlAutomationScriptTest ... Passed`.

### Final gates

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/gate-3.log 2>&1; echo EXIT=$?
EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-7.r1/logs/gate-4.log 2>&1; echo EXIT=$?
EXIT=0
```

## Iteration notes

- The first `AutomationLanesTest` run had no built `tripleoscillator` module and failed because its fixture exposed no parameters (`build-1.log` exit 0; `test-1.log` exit 8). After building the module, the regression failed on the original handler (`build-2.log` exit 0; `test-2.log` exit 8), and passed after the guard was added (`build-3.log` exit 0; `test-3.log` exit 0).
- The initial regression placement made `AutomationLanesTest.cpp` exceed its exactly-500-code-line baseline. The file-length check correctly failed (`gate-2.log` exit 1); the test was moved to the already registered control automation command suite. No gate baseline or limit was changed. The corresponding complexity check passed (`gate-1.log` exit 0).
- One intermediate build failed because the moved test needed the concrete `ControlTarget` declaration (`build-6.log` exit 2). `ControlDeviceSupport.h` was added, and subsequent builds succeeded.
- The regression was also checked once in each placement with the product patch removed and restored. Those intermediate runs used `build-4.log`/`test-4.log` (exit 0/8), `build-5.log`/`test-5.log` (0/0), then `build-7.log`/`test-7.log` (0/8) and `build-8.log`/`test-8.log` (0/0). The final proof above repeats the check against the final test source.
- The `fff` command was unavailable in the environment (`fff: command not found`), so log matches were checked with single-term `rg -n` searches instead of printing complete logs.

## Commit contents

- `src/core/ControlCommandsAutomationLanes.cpp`
- `tests/src/core/ControlAutomationScriptTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-7.md`

