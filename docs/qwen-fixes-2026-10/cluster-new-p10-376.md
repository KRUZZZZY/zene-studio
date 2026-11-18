# Cluster new-p10-376: SlicerT slice edits publish changes

**Verdict: FIXED**

The finding is real. Waveform deletion, movement, and insertion changed `SlicerT::m_slicePoints` without notifying observers. Right-click deletion and double-click insertion now emit `dataChanged()` after their mutations. A drag records the starting position and emits once on release only when the selected point changed.

## File evidence

- `plugins/SlicerT/SlicerTWaveform.cpp:381-385` emits after deleting a selected slice.
- `plugins/SlicerT/SlicerTWaveform.cpp:398-406` sorts after a drag and emits if the dragged point differs from its starting position.
- `plugins/SlicerT/SlicerTWaveform.cpp:469-483` emits after inserting and sorting a new slice.
- `plugins/SlicerT/SlicerTWaveform.h:92-93` stores the active drag index and starting position.
- `tests/src/core/AudioEditingTest.cpp:235-323` exercises the real SlicerT view and checks add, move, and delete notifications and mutations.
- `tests/upstream-modifications.txt:809-810` records the inherited source and header changes.

## Regression and gate evidence

| Evidence | Exact command | Exit and result |
| --- | --- | --- |
| Original behavior RED | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/test-5.log 2>&1; echo EXIT=$?` | `8`; add-slice assertion observed `changedSpy.count() == 0`, expected `1`. |
| Reverse-patch RED | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/test-8.log 2>&1; echo EXIT=$?` | `8`; after reversing the product patch, the same add-slice assertion observed `0`, expected `1`. |
| Reapplied-patch GREEN | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/test-9.log 2>&1; echo EXIT=$?` | `0`; `AudioEditingTest` reported 10 passed, 0 failed, 0 skipped. |
| Final build | `cmake --build build --target zene slicert AudioEditingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/build-10.log 2>&1; echo EXIT=$?` | `0`. |
| Final test | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/test-10.log 2>&1; echo EXIT=$?` | `0`; one CTest target ran, containing 10 QtTest cases: 10 passed, 0 failed, 0 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/gate-5.log 2>&1; echo EXIT=$?` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | `0`. |

The product-only reverse patch round trip also succeeded:

```sh
git diff -- plugins/SlicerT/SlicerTWaveform.cpp plugins/SlicerT/SlicerTWaveform.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/fix.patch; echo DIFF_EXIT=$?
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/fix.patch; echo REVERSE_EXIT=$?
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-376.r1/logs/fix.patch; echo APPLY_EXIT=$?
```

Each command exited `0`. The configured tree initially lacked the built `slicert` module, so the first test run skipped both SlicerT cases; the final build command includes the module target. The final headless test log includes icon-resource warnings from the plug-in view, but all 10 QtTest cases pass. The `fff` grep tool was not exposed in this session; required log terms were checked with `rg`.

## Files committed

- `plugins/SlicerT/SlicerTWaveform.cpp`
- `plugins/SlicerT/SlicerTWaveform.h`
- `tests/src/core/AudioEditingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-376.md`
