# Cluster new-p9-174

Verdict: **FIXED**

## Finding and change

The finding is real. `FreeBoyInstrument::playNoteImpl()` previously constructed a `GbApuWrapper` on its first render, and `deleteNotePluginData()` deleted it when the note handle was destroyed.

`plugins/FreeBoy/FreeBoy.cpp:63-101` now defines a shared 1024-voice pool. Its wrappers and 25 ms sample buffers are prepared outside rendering, voices are claimed and returned with `std::atomic_flag`, and a recycled wrapper is reset without allocation. `plugins/FreeBoy/FreeBoy.cpp:302-321` acquires a prepared voice in the callback; `:489-493` returns it without deallocation. Pool selection is refreshed on `AudioEngine::sampleRateChanged` in the instrument constructor. `plugins/FreeBoy/GbApuWrapper.cpp:31-43` contains the configurable buffer setup and reset implementation.

The regression counts C++ allocations and deallocations around every FreeBoy period render and note-state cleanup in the existing migration-family integration render (`tests/src/plugins/PluginPortsMigrationTest.cpp:314-323`, `tests/src/plugins/PluginPortsHarness.h:1031-1064`). It also continues comparing FreeBoy output against the reference render.

The pool has a fixed capacity of 1024 active FreeBoy voices. If all slots are occupied, an additional note is silent until release; the callback records that state and does not retry or allocate.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED, original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` | EXIT=8; `render allocations=1, deallocations=1` in `test-5.log` after reversing the final product patch |
| GREEN, fixed code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` | EXIT=0; 1/1 CTest target passed in `test-6.log` |
| Affected-family count | `PluginPortsMigrationTest` | 1 CTest target executed; its FreeBoy render and the rest of the migration-family checks passed |
| Build | `cmake --build build --target zene` | EXIT=0 before RED and GREEN (`build-9.log`, `build-11.log`) |
| Test-target build | `cmake --build build --target PluginPortsMigrationTest` | EXIT=0 before RED and GREEN (`build-10.log`, `build-12.log`) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 (`gate-5.log`) |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 (`gate-6.log`) |

The saved product patch was created with `git diff -- plugins/FreeBoy/FreeBoy.cpp plugins/FreeBoy/FreeBoy.h plugins/FreeBoy/GbApuWrapper.cpp plugins/FreeBoy/GbApuWrapper.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-174.r1/logs/fix.patch` (EXIT=0), reversed with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-174.r1/logs/fix.patch` (EXIT=0), then restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-174.r1/logs/fix.patch` (EXIT=0). All build, test, and gate output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-174.r1/logs`.

## Files in this commit

- `plugins/FreeBoy/FreeBoy.cpp`
- `plugins/FreeBoy/FreeBoy.h`
- `plugins/FreeBoy/GbApuWrapper.cpp`
- `plugins/FreeBoy/GbApuWrapper.h`
- `tests/src/plugins/PluginPortsHarness.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-174.md`
