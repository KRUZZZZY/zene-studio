# Cluster new-p9-452

Verdict: **FIXED**

## Evidence

`Sf2Instrument::openFile()` at [plugins/Sf2Player/Sf2Player.cpp:382] now loads a candidate while the existing font remains available, verifies the candidate's FluidSynth ID, and replaces the selected font only after validation. It commits `m_fontId`, `m_filename`, and the track name only on success. On failure, the previously selected font and saved source remain active.

The regression at [tests/src/plugins/PluginPortsMigrationTest.cpp:731] loads the configured valid SoundFont, attempts a missing/corrupt file, then checks both the serialized source and non-silent rendering. The original implementation failed with `source preserved: no; retained-font peak: 0`.

## Test evidence

| Check | Exact command | Exit |
|---|---|---:|
| RED on original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/test-1.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc"; exit "$test_rc"` | 8 |
| GREEN after fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/test-2.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc"; exit "$test_rc"` | 0 |
| Revert proof RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/test-3.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc"; exit "$test_rc"` | 8 |
| Reapply proof GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/test-4.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc"; exit "$test_rc"` | 0 |
| Affected family count | `PluginPortsMigrationTest`: 1 CTest test; the RED QtTest summary recorded 8 passed and the regression failed (9 outcomes total). | 1 test executed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc"; exit "$gate_rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/gate-2.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc"; exit "$gate_rc"` | 0 |

## Build and revert proof commands

Each build below exited 0:

- `cmake --build build --target zene sf2player PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/build-1.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc"; exit "$build_rc"`
- `cmake --build build --target zene sf2player PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/build-2.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc"; exit "$build_rc"`
- `cmake --build build --target zene sf2player PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/build-3.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc"; exit "$build_rc"`
- `cmake --build build --target zene sf2player PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/build-4.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc"; exit "$build_rc"`

The patch-only revert and reapply proof both exited 0:

- `git diff -- plugins/Sf2Player/Sf2Player.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/fix.patch; proof_rc=$?; printf 'EXIT=%s\n' "$proof_rc"; exit "$proof_rc"`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r1/logs/fix.patch; proof_rc=$?; printf 'EXIT=%s\n' "$proof_rc"; exit "$proof_rc"`

The `fff` grep tool was not exposed in this environment (`command -v fff` returned no path); log terms were searched individually with `rg -n`.

## Files committed

- `plugins/Sf2Player/Sf2Player.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-452.md`
