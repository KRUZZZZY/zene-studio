FIXED
# Cluster new-p9-390-port-port-port: RmsHelper array deallocation

## Finding and fix

The finding is real. Before the change, `include/RmsHelper.h:88` released
`m_buffer` with scalar `delete` in the `setSize()` growth branch, although the
buffer is allocated with `new float[size]` at lines 89 and 101. The branch now
uses `delete[] m_buffer` at line 88.

The accepted commit `89937d1ea` was inspected with `git show`; this change was
re-applied by hand on the newer branch. No merge, cherry-pick or rebase was
used. The class's other releases already match their array allocations: copy
assignment (`include/RmsHelper.h:63`), destruction (`:74`), and the growth
branch now (`:88`). The shrink branch retains the same allocation and does not
release the buffer.

## Regression

`DeviceDspTest::growingRmsBufferUsesArrayDeallocation`
(`tests/src/core/DeviceDspTest.cpp:439`) arms the test-only allocation probe,
grows a four-sample window to eight samples, and asserts that no tracked
`new[]` block was released by scalar `delete`. The probe uses thread-local
counters and a fixed-capacity pointer table, so it adds no allocations of its
own. The test calls the inline method through a volatile member pointer to
observe the defect at runtime. It uses no clock, sleep or race.

The initial RED run failed on the new assertion. It also found that this build
was missing the Frequency Shifter module; after building that module, the
reverse-patch run isolated the regression as the only failure. The same test
passed after the product patch was restored.

## Evidence

Commands ran from the worktree root unless the working directory is stated.
Build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-390-port-port-port.r1/logs`.

| Check | Exact command | Working directory | Exit / evidence |
| --- | --- | --- | ---: |
| Regression build with the bug present | `cmake --build build --target zene DeviceDspTest -j4` | worktree root | 0 (`build-1.log`) |
| **RED** | `ctest -R '^DeviceDspTest$' --output-on-failure` | `build/tests` | 8 (`test-1.log`); the new assertion failed, along with the missing Frequency Shifter module case |
| Build fixed header and all direct users | `cmake --build build --target zene DeviceDspTest PluginPortsMigrationTest dynamicsprocessor partc_ref_dynamicsprocessor frequencyshifter -j4` | worktree root | 0 (`build-2.log`) |
| **GREEN** | `ctest -R '^DeviceDspTest$' --output-on-failure` | `build/tests` | 0; 1/1 registered CTest tests passed (`test-6.log`) |
| Save product-only negative-control patch | `git diff -- include/RmsHelper.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-390-port-port-port.r1/logs/fix.patch` | worktree root | 0 (`patch-1.log`) |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-390-port-port-port.r1/logs/fix.patch` | worktree root | 0 (`patch-1.log`) |
| Negative-control rebuild | `cmake --build build --target zene DeviceDspTest PluginPortsMigrationTest dynamicsprocessor partc_ref_dynamicsprocessor frequencyshifter -j4` | worktree root | 0 (`build-3.log`) |
| **Negative control** | `ctest -R '^DeviceDspTest$' --output-on-failure` | `build/tests` | 8 (`test-4.log`); the new assertion was the only failure (27 passed, 1 failed, 12 skipped) |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-390-port-port-port.r1/logs/fix.patch` | worktree root | 0 (`patch-2.log`) |
| Final rebuild | `cmake --build build --target zene DeviceDspTest PluginPortsMigrationTest dynamicsprocessor partc_ref_dynamicsprocessor frequencyshifter -j4` | worktree root | 0 (`build-4.log`) |
| **Restored GREEN** | `ctest -R '^DeviceDspTest$' --output-on-failure` | `build/tests` | 0; 1/1 registered CTest tests passed (`test-6.log`) |
| RMS sibling user | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `build/tests` | 8 (`test-5.log` with product patch reversed; `test-7.log` restored); 66 passed, 1 failed, 2 skipped. The sole failure is `nonRealtimeLadspaEffectsAreInvalid`: its SWH Decimator fixture is absent in both product states. The RMS copy regression passed. |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-390-port-port-port` | worktree root | 0, PASS (`gate-3.log`) |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-390-port-port-port new-p9-390-port-port-port` | worktree root | 0, PASS (`submit-1.log`) |

The selected CTest family contains one registered test (`DeviceDspTest`), and
it passes with the fix. The neighboring RMS user was also run: its single
registered CTest executable has one unrelated missing-fixture failure that
reproduces with and without the product patch.

## Self-check

1. **Regression proof:** `DeviceDspTest::growingRmsBufferUsesArrayDeallocation`
   failed with exit 8 before the fix and in the reverse-patch negative control
   (exit 8), then passed with exit 0 after restoration. See `test-1.log`,
   `test-4.log`, and `test-6.log`.
2. **Determinism:** the test uses an opt-in, thread-local allocation probe and a
   fixed table of tracked array pointers. There are no sleeps, wall-clock
   assertions or cross-thread observations.
3. **Cause and callers:** the relevant include sites were checked in
   `plugins/DynamicsProcessor/DynamicsProcessor.cpp`,
   `tests/reference/DynamicsProcessor/DynamicsProcessor.cpp`,
   `tests/src/core/DeviceDspTest.cpp`, and
   `tests/src/plugins/PluginPortsMigrationTest.cpp`. The Dynamics Processor
   resizes through its existing `m_needsUpdate` branch; the change only selects
   the matching array deallocator. The copy, assignment, destructor and resize
   paths were checked for matching allocation/deallocation forms. All direct
   users were included in the rebuild command above.
4. **Collateral behavior:** only the deallocation form changed. No project
   fields, saved data, limits, values or control commands changed. The
   `PluginPortsMigrationTest` failure is the same missing SWH Decimator fixture
   with and without the product patch; its RMS copy case passes.
5. **Realtime, undo and commands:** no command, undo action, lock, wait, log or
   additional allocation/free was added. `setSize()` already frees and
   reallocates on the existing growth path; this change pairs that existing
   free with its `new[]` allocation.

## Files in the fix commit

- `include/RmsHelper.h`
- `tests/src/core/AllocationProbe.cpp`
- `tests/src/core/AllocationProbe.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-390-port-port-port.md`

`DeviceDspTest.cpp` was already registered in `tests/fork-sources.txt`, and
the existing CMake include scan already links `AllocationProbe.cpp` into this
test. No new source file or test registration was needed.
