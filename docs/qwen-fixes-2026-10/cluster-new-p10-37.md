# Cluster new-p10-37

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsMixerTools.cpp:204-216` contains `adjust`. Before the fix it read `volume_db` and `pan` and proceeded to `pow` and `std::clamp` without checking finiteness. The new guard at line 211 returns `InvalidArgs` before the no-op path or any model write when either value is NaN or infinite.
- `tests/src/core/MixerToolsTest.cpp:175` and `:190` exercise NaN `volume_db` and NaN `pan` through `mixer.adjust`, respectively. Each checks for `InvalidArgs` and unchanged fader/pan values.
- Before the production fix, both assertions failed because `mixer.adjust` returned success. Removing only the production guard reproduced both failures; restoring it made the same test command pass.
- Affected family count: CTest ran **1 test**, `MixerToolsTest`; the green run passed **1/1**. The two new QtTest cases both failed in the red runs and passed in the green runs.

## Commands and exit codes

Initial regression build, before the production change:

```sh
cmake --build build --target MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/build-1.log 2>&1; build_status=$?; printf 'EXIT=%d\n' "$build_status"; exit "$build_status"
```

Exit: **0**

Initial RED run against the unfixed production code:

```sh
ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/test-1.log 2>&1; test_status=$?; printf 'EXIT=%d\n' "$test_status"; exit "$test_status"
```

Exit: **8**. Both `mixerAdjustRejectsNaNDecibels` and `mixerAdjustRejectsNaNPan` failed.

Fixed build and GREEN run:

```sh
cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/build-2.log 2>&1; build_status=$?; printf 'EXIT=%d\n' "$build_status"; exit "$build_status"
ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/test-2.log 2>&1; test_status=$?; printf 'EXIT=%d\n' "$test_status"; exit "$test_status"
```

Exits: **0**, **0** (1/1 CTest tests passed).

Removal proof: reverse the production-only patch, rebuild, and rerun the same test:

```sh
git diff -- src/core/ControlCommandsMixerTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/fix.patch; proof_status=$?; printf 'EXIT=%d\n' "$proof_status"; exit "$proof_status"
cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/build-3.log 2>&1; build_status=$?; printf 'EXIT=%d\n' "$build_status"; exit "$build_status"
ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/test-3.log 2>&1; test_status=$?; printf 'EXIT=%d\n' "$test_status"; exit "$test_status"
```

Exits: **0**, **0**, **8**. Both NaN assertions failed again.

Restore the patch, rebuild, and rerun:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/fix.patch; apply_status=$?; printf 'EXIT=%d\n' "$apply_status"; exit "$apply_status"
cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/build-4.log 2>&1; build_status=$?; printf 'EXIT=%d\n' "$build_status"; exit "$build_status"
ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/test-4.log 2>&1; test_status=$?; printf 'EXIT=%d\n' "$test_status"; exit "$test_status"
```

Exits: **0**, **0**, **0** (1/1 CTest tests passed).

Required ratchet gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%d\n' "$gate_status"; exit "$gate_status"
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-37.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%d\n' "$gate_status"; exit "$gate_status"
```

Exits: **0**, **0**.
