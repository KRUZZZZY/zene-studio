# Cluster new-p11-73: effect-load redo failure

**Verdict: FIXED**

## Finding and fix

In [ControlStructuralSupport.cpp](../../src/core/ControlStructuralSupport.cpp:465), `journalEffectLoad()` discarded the `ControlResult` from `controlInstantiateDevice()` in its redo callback. A missing plugin therefore left the effect absent while `control.redo` returned success. The callback now sends the instantiate error through the existing structural restore-failure channel when the chain still exists. A missing chain remains a no-op.

The regression is in [UndoHonestyTest.cpp](../../tests/src/core/UndoHonestyTest.cpp:236). It appends a local test effect, records an unavailable built-in module through `journalEffectLoad()`, and drives `control.undo` / `control.redo`. The local effect keeps the test deterministic in this build, which discovers no effect modules. The test checks the typed `NotFound` failure and confirms that no effect remains.

## Evidence

| Check | Result |
| --- | --- |
| RED, original callback | Targeted test exit 1; `!redo.ok` failed because `control.redo` claimed success. CTest family exit 8. |
| GREEN, fixed callback | Targeted test exit 0. CTest family exit 0: 1/1 CTest test; QtTest totals 10 passed, 0 failed, 4 environment skips. |
| Rollback proof | Reversing `fix.patch`, rebuilding `zene` and `UndoHonestyTest`, then running the targeted test failed again (exit 1). Reapplying the patch, rebuilding, and rerunning passed (exit 0). |
| Complexity gate | Exit 0. |
| File-length gate | Exit 0. |

The decisive commands and their exit codes follow. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs`.

### Build commands

```sh
cmake --build build --target zene UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/build-6.log; exit $result
# EXIT=0 (original callback, final regression source)

cmake --build build --target zene UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/build-7.log; exit $result
# EXIT=0 (fixed callback, final regression source)
```

### RED and GREEN commands

```sh
./UndoHonestyTest redoReportsLoadedEffectItCannotInstantiate > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-10.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-10.log; exit $result
# EXIT=1 (RED after removing the fix)

ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-11.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-11.log; exit $result
# EXIT=8 (RED family run: 1/1 CTest entry failed)

./UndoHonestyTest redoReportsLoadedEffectItCannotInstantiate > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-12.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-12.log; exit $result
# EXIT=0 (GREEN after reapplying the fix)

ctest -R '^UndoHonestyTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-13.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/test-13.log; exit $result
# EXIT=0 (GREEN family run: 1/1 CTest entry; 10 passed, 4 skipped)
```

The targeted test commands ran from `build/tests`; CTest ran from `build/tests` as well.

### Patch rollback and gates

```sh
git diff -- src/core/ControlStructuralSupport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/fix.patch
# EXIT=0

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/fix.patch
# EXIT=0

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/gate-5.log; exit $result
# EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-73.r1/logs/gate-6.log; exit $result
# EXIT=0
```

`fff` was not available as a tool or executable in this session, so log terms were checked with `rg`.

## Files committed

- `src/core/ControlStructuralSupport.cpp`
- `tests/src/core/UndoHonestyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-73.md`

The two code files were already registered in `tests/fork-sources.txt`.
