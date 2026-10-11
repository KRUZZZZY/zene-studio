# Cluster new-p9-139

Verdict: **FIXED**. The finding is real: `WaveSample` owns `m_samples` and its destructor deletes that array, while its implicit copy operations previously shallow-copied the pointer.

## Evidence

- [ExprSynth.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-139/plugins/Xpressive/ExprSynth.h:81) deletes copy construction and copy assignment; the owning destructor is at lines 87–90.
- [PluginAudioPortsTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-139/tests/src/core/PluginAudioPortsTest.cpp:178) checks that `WaveSample` is neither copy constructible nor copy assignable. With the fix reversed, the first check failed: `!std::is_copy_constructible_v<WaveSample>` returned false.
- The product header is recorded in [upstream-modifications.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-139/tests/upstream-modifications.txt:1054); the product-new test source is registered in [fork-sources.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-139/tests/fork-sources.txt:2064).
- The affected family ran **1 CTest target** (`PluginAudioPortsTest`): red 1/1 failed; green 1/1 passed.

## RED/GREEN and reversal proof

Commands below were run in the stated working directories. Build and CTest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs`.

Fix removed with the required product-only patch:

```sh
git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc
```

Exit: **0**.

RED build, from the worktree root:

```sh
cmake --build build --target zene PluginAudioPortsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/build-7.log; echo EXIT=$rc; exit $rc
```

Exit: **0**.

RED test, from `build/tests`:

```sh
ctest -R '^PluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/test-7.log; echo EXIT=$rc; exit $rc
```

Exit: **8** (the regression assertion failed).

Fix restored:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc
```

Exit: **0**.

GREEN build, including every target that includes `ExprSynth.h`, from the worktree root:

```sh
cmake --build build --target zene xpressive partc_ref_xpressive PluginAudioPortsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/build-9.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/build-9.log; echo EXIT=$rc; exit $rc
```

Exit: **0**.

GREEN test, from `build/tests`:

```sh
ctest -R '^PluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/test-9.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/test-9.log; echo EXIT=$rc; exit $rc
```

Exit: **0** (`1/1` CTest target passed).

## Ratchets

An interim regression placement in `PluginPortsMigrationTest.cpp` made both gates fail because registering that 532-code-line source exposed existing over-limit functions and file length. That placement was discarded; no gate or baseline was changed. The exact interim gate runs were:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-1.log; echo EXIT=$rc; exit $rc
```

Exit: **1** (the temporary test location exposed two over-limit functions).

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-2.log; echo EXIT=$rc; exit $rc
```

Exit: **1** (the temporary test source exceeded the file-length limit).

With the regression in `PluginAudioPortsTest.cpp`, the required post-report gate commands and results are:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-5.log; echo EXIT=$rc; exit $rc
```

Exit: **0**.

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-6.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-139.r1/logs/gate-6.log; echo EXIT=$rc; exit $rc
```

Exit: **0**.

## Files committed

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/core/PluginAudioPortsTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-139.md`
