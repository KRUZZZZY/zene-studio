# Cluster new-p8-298

Verdict: **FIXED**.

## Finding and change

SPEC A16 says a reversible transaction must be reversed by `control.undo` and `control.redo` ([`SPEC-zene-studio.md:146`](../specs/SPEC-zene-studio.md#L146)). The creating branch reported `reversible: true` while its mechanism explicitly said redo was unavailable. The action now serializes the created automation track when undo removes it, then restores that state and resolves its parameter links on redo. It refreshes the track ID so a later undo still removes the restored track.

Evidence: [`ControlAutomationSupport.cpp:404`](../../src/core/ControlAutomationSupport.cpp#L404) reports the paired behavior; [`ControlAutomationSupport.cpp:426`](../../src/core/ControlAutomationSupport.cpp#L426) records the undo/redo action; [`SampleAccurateAutomationTest.cpp:433`](../../tests/src/core/SampleAccurateAutomationTest.cpp#L433) creates a point, undoes it, redoes it, checks the exact tick and value, then undoes it again.

## Build and regression evidence

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-298.r1/logs`.

| Phase | Command (run from) | Exit | Log and result |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene SampleAccurateAutomationTest` (worktree root) | 0 | `build-red-2.log` |
| RED | `ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` (`build/tests`) | 8 | `test-red-2.log`: targeted assertion failed because `redone` was false; 13 passed, 1 failed. |
| Reverse-patch build | `cmake --build build --target zene SampleAccurateAutomationTest` (worktree root, fix reverted) | 0 | `build-reverse-1.log` |
| Reverse-patch RED | `ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` (`build/tests`, fix reverted) | 8 | `test-reverse-1.log`: the same `redone` assertion failed. |
| GREEN build | `cmake --build build --target zene SampleAccurateAutomationTest` (worktree root, fix reapplied) | 0 | `build-reapply-1.log` |
| GREEN | `ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` (`build/tests`) | 0 | `test-reapply-1.log`: 100% passed, 0 failed out of 1 CTest target. |

The affected family executed one CTest target. The RED run shows 14 Qt cases (13 passed and the new regression failed); the GREEN CTest run passed the family.

The reverse-patch commands were `git diff -- src/core/ControlAutomationSupport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-298.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-298.r1/logs/fix.patch` (exit 0), followed after RED by `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-298.r1/logs/fix.patch` (exit 0).

## Gates

| Command | Exit | Log |
| --- | ---: | --- |
| `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-2.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-2.log` |

## Files to commit

- `src/core/ControlAutomationSupport.cpp`
- `tests/src/core/SampleAccurateAutomationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-298.md`

The first regression placement in `AutomationLanesTest` was discarded because that suite's instrument fixture exposed no parameters in this build; the regression was moved to the existing rack-backed `SampleAccurateAutomationTest`. The first redo implementation also exposed that loading an automation track requires `AutomationClip::resolveAllIDs()`; the final GREEN run covers the corrected implementation.
