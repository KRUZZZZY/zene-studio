# Cluster new-p10-136: file reveal fallback

**Verdict: FIXED**

On Linux, `FileRevealer::getDefaultFileManager()` now caches `xdg-open` if MIME discovery and command normalization leave an empty manager. `openDir()` therefore receives a usable fallback. `reveal()` retains its existing `QDesktopServices` fallback when file selection is unsupported.

## Evidence

- `src/gui/FileRevealer.cpp:103` applies the fallback before caching the discovered manager.
- `tests/src/gui/FileRevealerTest.cpp:36` removes executable lookup from `PATH`, selects a non-special desktop name, and asserts the fallback. Before the fix, the assertion reported actual `""`, expected `"xdg-open"`.
- `tests/CMakeLists.txt:793` registers `FileRevealerTest`.
- The affected CTest family contains one test; the green runs executed 1/1.

## Regression and proof commands

Commands ran from the worktree root except CTest commands, which ran from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs`.

| Phase | Exact command | Exit |
| --- | --- | ---: |
| RED build, original code (`build-2.log`) | `cmake --build build --target zene FileRevealerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/build-2.log 2>&1` | 0 |
| RED regression (`test-1.log`) | `ctest -R FileRevealerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/test-1.log 2>&1` | 8 |
| GREEN build, fixed code (`build-3.log`) | `cmake --build build --target zene FileRevealerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/build-3.log 2>&1` | 0 |
| GREEN regression (`test-2.log`) | `ctest -R FileRevealerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/test-2.log 2>&1` | 0 |
| Reverse production patch (`fix.patch`) | `git diff -- src/gui/FileRevealer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/fix.patch` | 0 |
| RED proof build without fix (`build-4.log`) | `cmake --build build --target zene FileRevealerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/build-4.log 2>&1` | 0 |
| RED proof regression (`test-3.log`) | `ctest -R FileRevealerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/test-3.log 2>&1` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/fix.patch` | 0 |
| Final build (`build-5.log`) | `cmake --build build --target zene FileRevealerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/build-5.log 2>&1` | 0 |
| Final regression (`test-4.log`) | `ctest -R FileRevealerTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/test-4.log 2>&1` | 0 |

## Gates

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Source registration (`gate-1.log`) | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/gate-1.log 2>&1` | 0 |
| Complexity (`gate-2.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/gate-2.log 2>&1` | 0 |
| File length (`gate-3.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-136.r1/logs/gate-3.log 2>&1` | 0 |

## Files committed

- `src/gui/FileRevealer.cpp`
- `tests/src/gui/FileRevealerTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-136.md`
