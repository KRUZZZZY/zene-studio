# Cluster new-p9-432: SlicerT playhead initialization

**Verdict: FIXED**

`plugins/SlicerT/SlicerTWaveform.h:97-99` declared `m_noteCurrent`, `m_noteStart`, and `m_noteEnd` without initial values. `updateUI()` calls `drawSeeker()` and `drawEditor()` before the first `isPlaying()` signal, so a repaint after loading a sample could use indeterminate floats. All three now initialize to `0.0f`.

Regression coverage is `tests/src/core/AudioEditingTest.cpp:233-243`. Its new `slicerWaveformPlayheadDefaultsAreExplicit()` assertion includes the SlicerT waveform header and checks all three declaration defaults. The existing plugin search setup now uses `QDir::addSearchPath()` instead of setting `LMMS_PLUGIN_DIR`.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED rebuild | `cmake --build build --target zene slicert AudioEditingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/build-4-red-inverse.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | Rebuilt after removing the product patch. |
| RED test | `ctest -R AudioEditingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/test-5-red-inverse.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 8 | `slicerWaveformPlayheadDefaultsAreExplicit()` failed on the missing `m_noteCurrent` initializer; 9 passed, 1 failed, 0 skipped. |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/fix.patch` | 0 | Restored the production fix. |
| GREEN rebuild | `cmake --build build --target zene slicert AudioEditingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/build-5-green-reapply.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | Rebuilt `zene`, `slicert`, and the affected test. |
| GREEN test | `ctest -R AudioEditingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/test-6-green-reapply.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | 1/1 ctest target passed. |
| QtTest case count | `./AudioEditingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/test-4-green-direct.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | 10 passed, 0 failed, 0 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/gate-1-complexity.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/gate-2-file-length.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | Passed. |

Patch reversal used the required command and exited 0: `git diff -- plugins/SlicerT/SlicerTWaveform.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-432.r1/logs/fix.patch`.

The `fff` command was unavailable (`command -v fff` exited 1), so build, test, and gate logs were searched with `rg -n -i`, one term per invocation (`error`, `FAILED`, and `Assertion`).

## Files committed

- `plugins/SlicerT/SlicerTWaveform.h`
- `tests/src/core/AudioEditingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-432.md`
