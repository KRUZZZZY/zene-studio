# Cluster new-p10-483: ALSA poll descriptor count

**Verdict: FIXED**

`src/core/audio/AudioAlsa.cpp:107-123` now performs the CLOEXEC allocation and iteration only when `snd_pcm_poll_descriptors_count()` returns a positive count. Zero and negative results skip that optional setup while leaving successful PCM initialization intact.

`tests/src/core/AudioAlsaPollDescriptorTest.cpp:64-82` forces a negative count while constructing the device with ALSA's `null` PCM plugin. On the original code, the constructor does not complete; with the guard it completes successfully. The test is registered in `tests/CMakeLists.txt:935-937` when ALSA is available.

## Evidence

Commands below ran from the worktree root unless a working directory is specified. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-483.r1/logs/`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build regression against original code | `cmake --build build --target zene AudioAlsaPollDescriptorTest` | 0 | `build-6.log` |
| RED | `ctest -R AudioAlsaPollDescriptorTest --output-on-failure` (cwd `build/tests`) | 8 | `test-1.log`; 1/1 test failed at `constructionCompleted` |
| Build with fix | `cmake --build build --target zene AudioAlsaPollDescriptorTest` | 0 | `build-7.log` |
| GREEN | `ctest -R AudioAlsaPollDescriptorTest --output-on-failure` (cwd `build/tests`) | 0 | `test-2.log`; 1/1 test passed |
| Save fix and reverse it | `git diff -- src/core/audio/AudioAlsa.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-483.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-483.r1/logs/fix.patch` | 0 | `fix.patch` |
| Build with fix reversed | `cmake --build build --target zene AudioAlsaPollDescriptorTest` | 0 | `build-8.log` |
| RED after reversal | `ctest -R AudioAlsaPollDescriptorTest --output-on-failure` (cwd `build/tests`) | 8 | `test-3.log`; 1/1 test failed at `constructionCompleted` |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-483.r1/logs/fix.patch` | 0 | — |
| Rebuild restored fix | `cmake --build build --target zene AudioAlsaPollDescriptorTest` | 0 | `build-9.log` |
| GREEN after restoration | `ctest -R AudioAlsaPollDescriptorTest --output-on-failure` (cwd `build/tests`) | 0 | `test-4.log`; 1/1 test passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The affected ctest family executed **1 test**; it passed with the fix and failed both times the product change was absent.

## Files committed

- `src/core/audio/AudioAlsa.cpp`
- `tests/src/core/AudioAlsaPollDescriptorTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-483.md`
