# Cluster new-p9-295: Monstro note-start allocation and concurrent voices

**Verdict: FIXED**

## Finding and fix

Luna's round-1 review is correct. The prior fix removed note-start allocations, then placed the four modulation vectors on `MonstroInstrument`. `MonstroSynth::renderOutput()` writes those vectors before using them, and the audio engine can run separate note handles for one instrument on multiple workers at once.

The instrument now prepares a bounded pool with one scratch slot per audio render worker. Each slot owns its two LFO and two envelope vectors, sized to the period during instrument construction ([Monstro.cpp:1048](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/plugins/Monstro/Monstro.cpp:1048), [Monstro.h:420](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/plugins/Monstro/Monstro.h:420)). `playNoteImpl()` atomically leases a slot for the callback, passes its buffers to that voice, then releases it ([Monstro.cpp:1071](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/plugins/Monstro/Monstro.cpp:1071)). Separate concurrent voices therefore render into distinct scratch. The synth remains constructed in the note handle's aligned storage, and rendering performs no allocation or locking.

`NotePlaybackTest` retains `monstroNoteStartDoesNotAllocate()` ([NotePlaybackTest.cpp:192](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/tests/src/core/NotePlaybackTest.cpp:192)). The added `monstroConcurrentVoicesKeepIndependentModulation()` configures an audible LFO, advances one voice, and compares serial output with two synchronized concurrent voices across 64 periods ([NotePlaybackTest.cpp:216](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-295/tests/src/core/NotePlaybackTest.cpp:216)). It skips only when the engine reports fewer than two render workers; this run did not skip.

The configured tree registers the reference Monstro comparison as `PluginPortsMigrationTest` (with a `monstro` instrument entry), rather than a CTest named `Monstro`. That comparison also passed.

## Evidence

Log directory: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs`.

| Check | Exact command | Exit and result |
|---|---|---|
| Initial test-source build attempt | `cmake --build build --target monstro NotePlaybackTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 2 — test helper called protected `loadSettings()`; corrected to public `restoreState()`. |
| Corrected test-source build | `cmake --build build --target monstro NotePlaybackTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Regression first run against round-1 product code | `ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` (cwd: `build/tests`) | 8 — concurrent modulation assertion failed. |
| Product hunks reversed | `git diff -- plugins/Monstro/Monstro.cpp plugins/Monstro/Monstro.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Reversed-product rebuild | `cmake --build build --target zene monstro NotePlaybackTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/build-12.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| **RED proof** with product hunks reversed | `ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/test-11.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` (cwd: `build/tests`) | **8** — `monstroConcurrentVoicesKeepIndependentModulation()` failed at the serial/concurrent sample comparison. |
| Product patch restored | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Fixed-product rebuild | `cmake --build build --target zene monstro NotePlaybackTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/build-13.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| **GREEN proof** with product patch restored | `ctest -R '^NotePlaybackTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/test-12.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` (cwd: `build/tests`) | **0** — one CTest target; Qt totals: 12 passed, 0 failed, 0 skipped. This includes the retained zero-allocation regression. |
| Reference behavior rebuild | `cmake --build build --target PluginPortsMigrationTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/build-10.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Reference behavior test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/test-13.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` (cwd: `build/tests`) | 0 — one CTest target passed using the rebuilt Monstro module. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/gate-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-295.r2/logs/gate-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |

Two affected CTest targets ran: `NotePlaybackTest` (12 Qt test cases passed) and `PluginPortsMigrationTest` (1 target passed). The first test-source build attempt exited 2 because its helper called protected `loadSettings()`; it now calls public `restoreState()`, and the rebuilt target passed. A first `PluginPortsMigrationTest` build launch did not return a captured shell status, so the identical build was rerun as `build-10.log`, which returned 0. The `fff` grep tool was unavailable in this session; build and test logs were searched with `rg -n` using the requested single terms.

## Files in this commit

- `plugins/Monstro/Monstro.cpp`
- `plugins/Monstro/Monstro.h`
- `tests/src/core/NotePlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-295.md`
