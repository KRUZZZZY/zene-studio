# Cluster new-p9-196

Verdict: **FIXED**

## Evidence

`src/core/InstrumentLayers.cpp:114` creates a track from each layer XML entry. The cast-failure branch now deletes the created track inside `ChangeGuard` and restores the previous auto-assigned instrument track if creation changed it (`src/core/InstrumentLayers.cpp:120`). The regression in `tests/src/core/InstrumentLayersCommandsTest.cpp:116` loads a sample-track entry, expects no layers and an empty private container (`:137`), and checks the prior MIDI assignment. The test seam is the friend declaration in `include/InstrumentLayers.h:141`.

Before the fix, the assertion observed one sample track in the container (expected zero). After the fix it observed zero. Reverting the production fix reproduced the same assertion failure.

## Commands and results

Commands ran from the worktree root unless a different working directory is stated. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs`.

- Initial test build: **EXIT=0** (`build-2.log`).
  ```sh
  cmake --build build --target InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Initial harness attempt from `build/tests`: **EXIT=8** (`test-1.log`); it used a plugin-dependent track and failed at `owner != nullptr` because this build had no loadable instrument.
  ```sh
  ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Corrected regression build against original production code: **EXIT=0** (`build-3.log`).
  ```sh
  cmake --build build --target InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Original-code red run from `build/tests`: **EXIT=8** (`test-2.log`); assertion reported actual 1, expected 0.
  ```sh
  ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- First fix iteration build: **EXIT=0** (`build-4.log`).
  ```sh
  cmake --build build --target zene InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- First fix iteration test from `build/tests`: **EXIT=0** (`test-3.log`).
  ```sh
  ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Revert-proof patch creation and reversal: **EXIT=0**.
  ```sh
  git diff -- src/core/InstrumentLayers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/fix.patch
  ```
- Reverted build: **EXIT=0** (`build-5.log`).
  ```sh
  cmake --build build --target zene InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Reverted red run from `build/tests`: **EXIT=8** (`test-4.log`); the same actual 1, expected 0 assertion failed.
  ```sh
  ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Restore fix: **EXIT=0**.
  ```sh
  git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/fix.patch
  ```
- Final build of `zene` and the affected test target: **EXIT=0** (`build-6.log`).
  ```sh
  cmake --build build --target zene InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/build-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Final green run from `build/tests`: **EXIT=0** (`test-5.log`), **1 CTest test executed and passed**.
  ```sh
  ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- Complexity gate: **EXIT=0** (`gate-5.log`).
  ```sh
  bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/gate-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```
- File-length gate: **EXIT=0** (`gate-6.log`).
  ```sh
  bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-196.r1/logs/gate-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
  ```

## Files committed

- `include/InstrumentLayers.h`
- `src/core/InstrumentLayers.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-196.md`
