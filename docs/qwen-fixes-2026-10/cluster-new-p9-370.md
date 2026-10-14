# Cluster new-p9-370: render progress race

**Verdict: FIXED**

`ProjectRenderer::run()` wrote `m_progress` while `updateConsoleProgress()` read it on another thread. `volatile` did not synchronize those accesses. The field is now `std::atomic<int>` ([include/ProjectRenderer.h:140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-370/include/ProjectRenderer.h:140)). The render thread uses relaxed atomic stores and loads, and the console reads one relaxed snapshot for both the progress bar and percentage ([src/core/ProjectRenderer.cpp:264](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-370/src/core/ProjectRenderer.cpp:264), [src/core/ProjectRenderer.cpp:285](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-370/src/core/ProjectRenderer.cpp:285), [src/core/ProjectRenderer.cpp:356](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-370/src/core/ProjectRenderer.cpp:356)).

The regression in [SessionSchedulerRenderTest.cpp:95](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-370/tests/src/core/SessionSchedulerRenderTest.cpp:95) checks that the field is atomic. It failed before the fix and after removing the production patch, then passed with the fix restored.

## Evidence

All ctest invocations ran from `build/tests`. The affected ctest family contains one CTest target; the fixed run executed 1/1 target successfully.

| Check | Command | Exit/result |
|---|---|---|
| RED before fix | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 8; `progressStorageIsAtomic()` returned false |
| GREEN after fix | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 0; 1/1 test passed |
| RED with fix patch removed | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 8; the same assertion returned false |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

The exact logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-370.r1/logs/` (`test-1.log` and `test-3.log` are RED; `test-2.log` and `test-4.log` are GREEN; `gate-1.log` and `gate-2.log` contain the gate results).

## Build and proof commands

Commands below ran in the worktree unless noted. Build and test output was redirected to the listed new log file, with the shell exit code appended to it.

| Stage | Command | Exit |
|---|---|---:|
| Pre-fix regression build (`build-1.log`) | `cmake --build build --target zene SessionSchedulerRenderTest` | 0 |
| Pre-fix RED (`test-1.log`; from `build/tests`) | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 8 |
| Unscoped build attempt (`build-2.log`) | `cmake --build build` | 97; repository guard requires named targets |
| Fixed dependent build (`build-3.log`) | `cmake --build build --target zene ClipFadesRenderTest MasteringTest SessionSchedulerRenderTest StemExportTest LoudnessReportTest RenderJobQueueTest SessionPlaybackRenderTest ExportWavDitherTest` | 0 |
| First GREEN (`test-2.log`; from `build/tests`) | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 0 |
| Remove production patch (`fix.patch`) | `git diff -- include/ProjectRenderer.h src/core/ProjectRenderer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-370.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-370.r1/logs/fix.patch` | 0 |
| Rebuild without the fix (`build-4.log`) | `cmake --build build --target zene SessionSchedulerRenderTest` | 0 |
| RED without the fix (`test-3.log`; from `build/tests`) | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-370.r1/logs/fix.patch` | 0 |
| Final dependent build (`build-5.log`) | `cmake --build build --target zene ClipFadesRenderTest MasteringTest SessionSchedulerRenderTest StemExportTest LoudnessReportTest RenderJobQueueTest SessionPlaybackRenderTest ExportWavDitherTest` | 0 |
| Final GREEN (`test-4.log`; from `build/tests`) | `ctest -R '^SessionSchedulerRenderTest$' --output-on-failure` | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0 |

## Files committed

- `include/ProjectRenderer.h`
- `src/core/ProjectRenderer.cpp`
- `tests/src/core/SessionSchedulerRenderTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-370.md`
