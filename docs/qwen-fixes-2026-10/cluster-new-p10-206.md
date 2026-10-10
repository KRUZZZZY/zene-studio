# Cluster new-p10-206: LV2 feature pointer list on reload

## Verdict: FIXED

`Lv2Features::clear()` cleared `m_features` but retained `m_featurePointers`. Recreating the list appended after the previous null terminator, so each reload retained another stale list and grew the pointer vector. The fix clears the pointer vector before clearing the feature storage.

The regression establishes stale-list accumulation (`16` entries where the current feature set requires `8`, including its terminator). It does not establish that a plugin loses features: the reported plugin-visible consequence is not needed to demonstrate the concrete vector-lifecycle defect.

## File and line evidence

- [src/core/lv2/Lv2Features.cpp:110](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-206/src/core/lv2/Lv2Features.cpp:110): `Lv2Features::clear()` now clears `m_featurePointers` before `m_features`.
- [include/Lv2Features.h:58](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-206/include/Lv2Features.h:58): grants the regression accessor access to the pointer count, without changing object layout or runtime behavior.
- [tests/src/core/Lv2StateTest.cpp:119](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-206/tests/src/core/Lv2StateTest.cpp:119): `featurePointersResetOnReload()` checks the initial list and the list after `clear()` and rebuild.

The affected CTest family ran **1 registered test** (`Lv2StateTest`). The initial RED failed in `featurePointersResetOnReload()` with actual `16`, expected `8`. The reverse-patch proof failed at the same assertion.

## Commands and exit codes

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs`.

1. Build the regression target and `zene` before the original-code RED:

   ```sh
   cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-1.log; exit "$rc"
   ```

   EXIT=0.

2. Original-code RED, from `build/tests`:

   ```sh
   ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-1.log; exit "$rc"
   ```

   EXIT=8; `featurePointersResetOnReload()` failed (`16` actual, `8` expected).

3. Build with the fix and rerun the same test:

   ```sh
   cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-2.log; exit "$rc"
   ```

   EXIT=0.

   ```sh
   ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-2.log; exit "$rc"
   ```

   EXIT=0; CTest reported `100% tests passed, 0 tests failed out of 1`.

4. Rebuild the LV2 instrument target that includes `Lv2Proc.h`:

   ```sh
   cmake --build build --target lv2instrument > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-3.log; exit "$rc"
   ```

   EXIT=0.

5. Reverse the product fix, rebuild, and confirm the regression fails again:

   ```sh
   git add include/Lv2Features.h && git diff -- include/Lv2Features.h src/core/lv2/Lv2Features.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/fix.patch
   ```

   EXIT=0.

   ```sh
   cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-4.log; exit "$rc"
   ```

   EXIT=0.

   ```sh
   ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-3.log; exit "$rc"
   ```

   EXIT=8; the same assertion failed (`16` actual, `8` expected).

6. Restore the fix, rebuild, and rerun the test:

   ```sh
   git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/fix.patch
   ```

   EXIT=0.

   ```sh
   cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/build-5.log; exit "$rc"
   ```

   EXIT=0.

   ```sh
   ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/test-4.log; exit "$rc"
   ```

   EXIT=0; CTest reported `100% tests passed, 0 tests failed out of 1`.

7. Required ratchet gates, after the final code and manifest edits:

   ```sh
   bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/gate-1.log; exit "$rc"
   ```

   EXIT=0.

   ```sh
   bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-206.r1/logs/gate-2.log; exit "$rc"
   ```

   EXIT=0.

## Files committed

- `include/Lv2Features.h`
- `src/core/lv2/Lv2Features.cpp`
- `tests/src/core/Lv2StateTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-206.md`
