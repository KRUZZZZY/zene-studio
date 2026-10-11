# Cluster new-p9-295: Monstro render-pool capacity

**Verdict: FIXED**

## Finding and fix

Luna's round-2 review was valid. Monstro sized its modulation scratch pool with a fresh `QThread::idealThreadCount()` query at instrument construction. AudioEngine had already fixed its render concurrency from its own query, so a later or thread-specific count could leave the pool too small and make `playNoteImpl()` return without rendering at [Monstro.cpp:1075-1085](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/plugins/Monstro/Monstro.cpp:1075).

AudioEngine now exposes its fixed render context count as `m_numWorkers + 1`, including the caller-side context ([AudioEngine.h:124-125](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/include/AudioEngine.h:124), [AudioEngine.cpp:154-156](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/src/core/AudioEngine.cpp:154)). Monstro allocates one scratch slot per such context during instrument construction ([Monstro.cpp:1046-1053](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/plugins/Monstro/Monstro.cpp:1046)). The pool therefore covers the engine's maximum render concurrency; scratch vectors remain preallocated and atomically leased on the render path.

The regression counts AudioEngine's render-thread children, narrows the test thread's CPU affinity while constructing a second Monstro instrument, and drives one synchronized voice callback per engine context. It compares each voice with serial output across 64 periods ([NotePlaybackTest.cpp:104-113](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/tests/src/core/NotePlaybackTest.cpp:104), [NotePlaybackTest.cpp:352-377](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/tests/src/core/NotePlaybackTest.cpp:352)). The older sizing failed this assertion with product hunks reversed; the fixed sizing passes.

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs`. Test commands ran from `build/tests`; build and gate commands ran from the worktree root.

| Check | Exact command | Exit/result |
|---|---|---|
| Regression build before fix | `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/build-1.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |
| Initial RED on old pool sizing | `ctest -R '^NotePlaybackTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/test-1-red.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 8 — 11 passed, 1 failed; concurrent voice output mismatch |
| Final product-only reversal | `git diff -- include/AudioEngine.h src/core/AudioEngine.cpp plugins/Monstro/Monstro.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/fix.patch; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` and `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/fix.patch; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 for both commands |
| Reversed-product rebuild | `cmake --build build --target zene monstro NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/build-6-final-reversed.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |
| **Final RED with only product hunks reversed** | `ctest -R '^NotePlaybackTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/test-8-final-red.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | **8** — 11 passed, 1 failed at the concurrent render-concurrency assertion |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/fix.patch; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |
| Final restored rebuild | `cmake --build build --target zene monstro NotePlaybackTest PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/build-7-final-restored.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |
| **Final GREEN** | `ctest -R '^NotePlaybackTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/test-9-final-green.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | **0** — 12 passed, 0 failed, 0 skipped |
| Plugin migration regression | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/test-10-final-pluginports.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 — 8 passed |
| Initial complexity check during test refactor | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/gate-1-complexity.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 1 — regression method CCN 16; helpers were extracted and no limit or baseline changed |
| Final complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/gate-5-complexity.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |
| Final file-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r3/logs/gate-6-file-length.log 2>&1; status_code=$?; printf 'EXIT=%s\n' "$status_code"; exit "$status_code"` | 0 |

The affected test family ran 12 Qt test cases in one CTest target; PluginPortsMigrationTest ran 8 Qt cases in one CTest target. The initial complexity check during refactoring exited 1 because the regression method was CCN 16; after extracting helpers, the final complexity gate above passed without changing its limit or baseline.

The fff grep tool was unavailable in this session; build and test logs were searched with `rg -n` for the requested terms.

## Files in this commit

- `include/AudioEngine.h`
- `src/core/AudioEngine.cpp`
- `plugins/Monstro/Monstro.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-295.md`
