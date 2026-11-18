# Cluster new-p8-256

Verdict: **FIXED**

## Finding and regression

The dry-run command reports track gains outside the SampleTrack fader range in
`loss.track_gain_clamps`. The production call is at
`src/core/ControlCommandsAafImport.cpp:210`; its range matches the builder's
200% ceiling at `src/core/AafImportBuild.cpp:138`, with its range comparison
at line 140 and model write at line 141.

Luna's rework finding was valid: the prior over-range assertion exercised the
helper directly and did not prove that `interchange.aaf_read` populated the
loss report. `tests/src/core/AafImportTest.cpp:399` now reads
`tests/data/aaf/PR_WAV_Internal_TrackGain_250pct.aaf` through that command,
checks the parsed track gain is above 200%, and asserts a clamp count of 1 at
line 410. The existing in-range command assertion remains at
`tests/src/core/AafImportTest.cpp:389` and expects 0. The helper-level
assertions remain at lines 367 and 371.

The added fixture has a top-level Mono Audio Gain parameter of
250000000/100000000 (250%) and retains its embedded essence. Its provenance
and purpose are documented in `tests/data/aaf/README.md:15` and `:21`.

The red proof temporarily removed only the `readOnly()` population call. The
new command assertion then observed 0 instead of 1. The deletion patch was
reversed, and `git checkout -- src/core/ControlCommandsAafImport.cpp` restored
the committed production file before the green rebuild.

Affected family: CTest `AafImportTest` (Test #211), 1/1 registered test
executed on both red and green runs. The red run failed at the new command-path
assertion; the restored run passed.

## Commands and results

Build and test output is in the numbered files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs`.
Builds ran from the worktree root. CTest ran from `build/tests`.

| Stage | Command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene AafImportTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/build-1.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/build-1.log; exit "$ec"` | 0 |
| RED test | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/test-1.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/test-1.log; exit "$ec"` (cwd `build/tests`) | 8 |
| Reverse the single deletion hunk | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/fix.patch` | 0 |
| Restore production path | `git checkout -- src/core/ControlCommandsAafImport.cpp` | 0 |
| GREEN build | `cmake --build build --target zene AafImportTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/build-2.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/build-2.log; exit "$ec"` | 0 |
| GREEN test | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/test-2.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/test-2.log; exit "$ec"` (cwd `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-3.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-3.log; exit "$ec"` | 0 |
| Initial file-length check (temporary non-source manifest entries) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-2.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-2.log; exit "$ec"` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-4.log 2>&1; ec=$?; printf 'EXIT=%s\n' "$ec" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r3/logs/gate-4.log; exit "$ec"` | 0 |

An initial file-length check returned 1 because I temporarily listed the AAF
binary fixture in the C/C++ source manifest, where it was counted as 3,206
lines. I removed those non-source entries and reran the gate successfully; no
gate baseline was changed. The `fff` grep tool was not available in this
environment, so numbered logs were searched term-by-term with `rg`.

## Files in this commit

- `tests/src/core/AafImportTest.cpp`
- `tests/data/aaf/README.md`
- `tests/data/aaf/PR_WAV_Internal_TrackGain_250pct.aaf`
- `docs/qwen-fixes-2026-10/cluster-new-p8-256.md`
