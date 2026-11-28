# Phase 0 S3 from inside the product: hosted results (Q050-525)

This is the first hosted run of `.github/workflows/rune.yml` in the product repository. It ran on the
push of `feat/050-Q050-525-fable` to `ci/fable-rune-Q050-525`.

- Run: https://github.com/KRUZZZZY/zene-studio/actions/runs/46160274973 (workflow `rune`, conclusion
  **success**, 2026-11-28 15:31-15:46 UTC)
- Commit: `0ad40688ed310e26cf5c4908d4a3e3ad519bc4fd`; Rune `b53fe7236ab11225108f6aee837c2d9942f63991`
- Every figure below comes from the run's uploaded `rune-<os>/summary.json` artifacts (all three
  report `all_ok: true`).

| Runner | Build (perf) | Tests | Snapshot | Window ready | GPU |
|---|---|---|---|---|---|
| ubuntu (ubuntu-24.04) | ok, 330 s | 26 passed, 0 failed, 2 ignored | ok, 195,391 bytes | 1130 ms | llvmpipe (LLVM 20.1.2), wgpu Vulkan, Cpu |
| windows (windows-latest, MSVC) | ok, 438 s | 26 passed, 0 failed, 0 ignored | ok, 195,370 bytes | 120 ms | Microsoft Basic Render Driver, wgpu Dx12, Cpu |
| macos (macos-latest, arm64) | ok, 221 s | 26 passed, 0 failed, 2 ignored | ok, 205,477 bytes | 1627 ms | Apple Paravirtual device, Rune Metal backend |

The two ignored tests are the engine spikes in `app/src/engine_tests.rs`. They are `cfg(unix)`, so
they are not compiled on Windows. On macOS the informational snapshot through wgpu's Metal backend
also passed (205,477 bytes).

## The product's `build` workflow on the same push (not part of this unit)

The run was still going when this file was written (2026-11-28 15:48 UTC). This records how far it
had got; it is not a final verdict.

| Job | Pushed sha `0ad40688ed` (run 46160274918) | Base sha `3f0657056d` (run 46145610160, `ci/m0-train-55-3f0657056de9`) |
|---|---|---|
| linux-x86_64 | failure | failure |
| macos-arm64 | failure | failure |
| macos-x86_64 | in progress | failure |
| linux-arm64 | in progress | failure |
| msvc-x64 | in progress | failure |
| mingw64 | in progress | failure |
| windows-arm64 | in progress | failure |

On the same push, `checks`, `quality-gates` and `doxygen` all succeeded. All seven `build` jobs
had already failed on the base sha, so the red `build` jobs predate this change.
