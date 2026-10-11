# Cluster new-p10-468

Verdict: **FIXED**

## Finding and code evidence

`ValueAnnouncer::noteChange()` in [AccessibleWidgets.cpp](../../src/gui/AccessibleWidgets.cpp#L90) now delegates the value/settle decision to [ValueAnnouncementState.h](../../src/gui/ValueAnnouncementState.h#L40). When the value equals the last announced value, the policy clears `pending` before returning `NoChange` (line 44), so the timer has no stale event to announce.

The regression in [ItemListPanelTest.cpp](../../tests/src/gui/ItemListPanelTest.cpp#L283) covers the sequence: announce B, defer A while settling, then return to B. It asserts that the timer's pending flag is false. It failed before the fix with `pending == true`.

The test drives the same state decision used by the paint filter. The configured Qt offscreen platform reports `QAccessible::isActive() == false` even after `QAccessible::setActive(true)`; the policy test keeps this case deterministic without depending on a platform accessibility bridge.

## RED/GREEN evidence

RED before the fix:

```text
cmake --build build --target ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/test-5.log 2>&1; code=$?; echo EXIT=$code
EXIT=8
```

The failing assertion was `!pending`; QtTest reported 11 passed and 1 failed. After rebuilding `zene`, the same red test was repeated:

```text
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/build-5.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/test-6.log 2>&1; code=$?; echo EXIT=$code
EXIT=8
```

GREEN after the fix:

```text
cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/build-8.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/test-9.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
```

Affected family: **ItemListPanelTest — 12 QtTest cases passed, 0 failed, 0 skipped; CTest ran 1/1 registered test.** The verbose count came from:

```text
ctest -R '^ItemListPanelTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/test-10.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
```

## Patch-removal proof

The product diff was saved, removed, rebuilt, and tested. The same regression failed without the fix, then passed after the patch was restored:

```text
git diff -- src/gui/AccessibleWidgets.cpp src/gui/ValueAnnouncementState.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/fix.patch; code=$?; echo EXIT=$code
EXIT=0
cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/build-7.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/test-8.log 2>&1; code=$?; echo EXIT=$code
EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/fix.patch; code=$?; echo EXIT=$code
EXIT=0
```

The restored build and GREEN run are `build-8.log` and `test-9.log` above.

## Gates

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-468.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code
EXIT=0
```

## Files committed

- `src/gui/AccessibleWidgets.cpp`
- `src/gui/ValueAnnouncementState.h`
- `tests/fork-sources.txt`
- `tests/src/gui/ItemListPanelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-468.md`
