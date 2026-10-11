# Cluster new-p9-258: invalid MCU fader-touch strip

**Verdict: FIXED**

`McuSurface::applyMix()` indexed `m_touched` directly from public input at [McuSurface.cpp:184](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-258/src/core/McuSurface.cpp:184). It now updates the array only for strips `0..mcu::MasterStrip`; invalid touch inputs are consumed without indexing it. Regression coverage at [McuSurfaceTest.cpp:168](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-258/tests/src/core/McuSurfaceTest.cpp:168) checks a negative strip and an invalid upper strip.

## Evidence

Commands below ran from the worktree unless noted. Test commands ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/build-2.log 2>&1` | 0 | Built original production code with the regression |
| RED test | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/test-2.log 2>&1` | 8 | 1/1 CTest target failed; both new cases failed (6 passed, 2 failed in QtTest) |
| GREEN build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/build-3.log 2>&1` | 0 | Built the fix |
| GREEN test | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/test-3.log 2>&1` | 0 | 1/1 CTest target passed |
| Reverse fix | `git diff -- src/core/McuSurface.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/fix.patch` | 0 | Reversed only the production fix |
| Reversal build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/build-4.log 2>&1` | 0 | Rebuilt without the fix |
| Reversal test | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/test-4.log 2>&1` | 8 | The same two new cases failed again |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/fix.patch` | 0 | Restored the production fix |
| Final build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/build-5.log 2>&1` | 0 | Rebuilt with the fix restored |
| Final test | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/test-5.log 2>&1` | 0 | 1/1 CTest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/gate-1.log 2>&1` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-258.r1/logs/gate-2.log 2>&1` | 0 | Passed |

## Files in commit

- `src/core/McuSurface.cpp`
- `tests/src/core/McuSurfaceTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-258.md`
