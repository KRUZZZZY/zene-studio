# Cluster new-p9-322 — NES held-note noise retuning

## Verdict: FIXED

`NesObject::updatePitch()` previously recalculated channel 4 in note-frequency mode only when the note frequency changed. It also ignored quantization changes while the note stayed held. The per-note object now caches both controls and recalculates the wavelength when either control changes.

Evidence:

- `plugins/Nes/Nes.cpp:463-482` detects note-frequency mode and quantization changes, recalculates `m_wlen4`, and updates the cached controls.
- `plugins/Nes/Nes.cpp:96-99` initializes the cached controls before the initial pitch update.
- `plugins/Nes/Nes.h:181-184` declares the per-note cached values.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:163-222,305-329` renders a held NES note and asserts that toggling note-frequency mode and quantization changes the noise output; `:294-302` initializes the engine and note handle manager for the test.

## Evidence table

| Check | Exact command | Exit/result |
|---|---|---|
| Original-code RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | `EXIT=8`; 1/1 CTest test failed. The note-mode comparison was `0.03671`, below the `0.05` threshold. The QtTest total was 8 passed, 1 failed. Log: `test-5.log`. |
| Fixed-code GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | `EXIT=0`; 1/1 CTest test passed, covering both mode and quantization toggles. Log: `test-6.log`. |
| 4b reverse-patch RED | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-322.r1/logs/fix.patch`, then `cmake --build build --target zene PluginPortsMigrationTest`, then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | Reverse apply `EXIT=0`; rebuild `EXIT=0`; test `EXIT=8` at the same assertion and `0.03671` comparison. Logs: `build-7.log`, `test-7.log`. |
| 4b restored-patch GREEN | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-322.r1/logs/fix.patch`, then `cmake --build build --target zene PluginPortsMigrationTest`, then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | Apply `EXIT=0`; rebuild `EXIT=0`; test `EXIT=0`, 1/1 passed. Logs: `build-8.log`, `test-8.log`. |
| Build command | `cmake --build build --target zene PluginPortsMigrationTest` | `EXIT=0` for builds 5–8. Logs: `build-5.log` through `build-8.log`. |
| Affected-family test count | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 1 CTest test executed per run; 1/1 passed on green runs. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `EXIT=0`. Final log: `gate-5.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` | `EXIT=0`. Final log: `gate-6.log`. |

Patch proof commands:

```sh
git diff -- plugins/Nes/Nes.cpp plugins/Nes/Nes.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-322.r1/logs/fix.patch
# EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-322.r1/logs/fix.patch
# EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-322.r1/logs/fix.patch
# EXIT=0
```

## Files committed

- `plugins/Nes/Nes.cpp`
- `plugins/Nes/Nes.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-322.md`
