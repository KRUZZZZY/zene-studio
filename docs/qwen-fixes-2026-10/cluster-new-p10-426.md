# Cluster new-p10-426

Verdict: **FIXED**

`TimePos::frames()` used an `assert` for negative ticks, so release builds could cast a negative value to unsigned `f_cnt_t`. It now returns zero for negative ticks, negative or nonfinite frame rates, and frame counts outside the representable range before performing the cast. Zero frame rate still returns zero.

Evidence:

- [include/TimePos.h:112](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-426/include/TimePos.h:112) validates ticks and rate, then bounds the product before converting.
- [tests/src/core/MidiLearnTest.cpp:87](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-426/tests/src/core/MidiLearnTest.cpp:87) and [tests/src/core/MidiLearnTest.cpp:92](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-426/tests/src/core/MidiLearnTest.cpp:92) cover negative ticks and a negative frame rate.
- [tests/upstream-modifications.txt:1054](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-426/tests/upstream-modifications.txt:1054) registers the inherited header change.

## Verification evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs`.

| Phase | Command (working directory) | Exit | Result / log |
|---|---|---:|---|
| Baseline build | `cmake --build build --target MidiLearnTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-2.log` |
| RED before fix | `ctest -R '^MidiLearnTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | Two failing `QCOMPARE`s: negative ticks and negative rate each produced `18446744073709551615` instead of `0`; `test-2.log` |
| Reverse product patch | `git diff -- include/TimePos.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | `fix.patch` contains only the production header diff |
| Reversed build | `cmake --build build --target zene MidiLearnTest MidiLearnThreadTest ClipEditsMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-4.log` |
| RED with fix removed | `ctest -R '^MidiLearnTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | The same two assertions fail; `test-5.log` |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Restored the saved header diff |
| Final rebuild | `cmake --build build --target zene MidiLearnTest MidiLearnThreadTest ClipEditsMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-5.log` |
| GREEN after fix | `ctest -R '^MidiLearnTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/test-6.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 0 | 1/1 CTest passed; `test-6.log` |
| Affected family | `ctest -R '^(ClipEditsMenuTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/test-7.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 0 | 3/3 CTest passed; `test-7.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-426.r1/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `gate-4.log` |

## Files committed

- `include/TimePos.h`
- `tests/src/core/MidiLearnTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-426.md`
