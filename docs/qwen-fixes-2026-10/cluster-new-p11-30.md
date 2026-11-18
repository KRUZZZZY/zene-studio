# Cluster new-p11-30

**Verdict: FIXED**

`adjust()` in `src/core/ControlCommandsMixerTools.cpp:211-225` now computes the clamped outputs before creating an undo step. If every selected volume and pan remains unchanged, it returns `adjusted: 0` without journalling or dirtying the song. The regression at `tests/src/core/MixerToolsTest.cpp:364-404` exercises a channel at maximum volume and pan `1` with positive volume and pan requests, and checks the result, model values, undo depth and modified flag.

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs`.

## Regression evidence

| Run | Exact command | Exit | Result |
|---|---|---:|---|
| Original code RED (`test-06.log`) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-06.log 2>&1; echo EXIT=$?` | 8 | 18 passed, 1 failed; `undoDepth` was 1, expected 0. |
| Fixed code GREEN (`test-07.log`) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-07.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. |
| Reverse-patch RED (`test-08.log`) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-08.log 2>&1; echo EXIT=$?` | 8 | 18 passed, 1 failed; same undo-depth assertion. |
| Reapplied-patch GREEN (`test-09.log`) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-09.log 2>&1; echo EXIT=$?` | 0 | 19 passed, 0 failed; 1/1 CTest target passed. |

The affected family contains one CTest target, `MixerToolsTest`, with 19 QtTest cases. The reverse-patch proof used:

```sh
git diff -- src/core/ControlCommandsMixerTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/fix.patch; echo EXIT=$?
# EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/fix.patch; echo EXIT=$?
# EXIT=0
```

## Builds and gates

Each listed build exited 0 and rebuilt both `zene` and `MixerToolsTest`:

```sh
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-06.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-07.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-08.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-09.log 2>&1; echo EXIT=$?  # EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/gate-01.log 2>&1; echo EXIT=$?  # EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/gate-02.log 2>&1; echo EXIT=$?  # EXIT=0
```

Earlier fixture iterations are retained as `test-01.log` through `test-05.log` and each exited 8. They exposed test setup issues (an unavailable metaobject method, a dirty fixture, stale journal references after `clearProject()`, and running the test before other pan-law cases). They are excluded from the RED/GREEN evidence above; the final test avoids clearing mixer objects and runs last in the family.

The exact commands and observed exits for those fixture iterations were:

```sh
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-01.log 2>&1; echo EXIT=$?  # EXIT=8
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-02.log 2>&1; echo EXIT=$?  # EXIT=8
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-03.log 2>&1; echo EXIT=$?  # EXIT=8
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-04.log 2>&1; echo EXIT=$?  # EXIT=8
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/test-05.log 2>&1; echo EXIT=$?  # EXIT=8
```

Builds `build-01.log` through `build-05.log` also exited 0; each used the same target command as the later builds, with its matching numbered log destination:

```sh
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-01.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-02.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-03.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-04.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene MixerToolsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-30.r1/logs/build-05.log 2>&1; echo EXIT=$?  # EXIT=0
```

## Files in the fix commit

- `src/core/ControlCommandsMixerTools.cpp`
- `tests/src/core/MixerToolsTest.cpp` (already registered in `tests/fork-sources.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p11-30.md`
