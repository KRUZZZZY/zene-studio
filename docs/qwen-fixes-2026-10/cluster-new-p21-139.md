FIXED

Cluster new-p21-139: Disto may process with null temporary buffers (`plugins/SfzPlayer/sfizz/src/sfizz/effects/Disto.cpp:137` in the finding).

## Decision

REAL. Before this change `Disto::Impl::_temp[0]/_temp[1]` are null until `setSamplesPerBlock` (`Disto.cpp:50`, `Disto.cpp:90-96`) and `process` builds spans from `_temp.get()` with size `_oversampling * nframes` without any null or capacity check (`Disto.cpp:137-156`). Processing before `setSamplesPerBlock` dereferences null, and processing more frames than prepared writes past the allocations. `Synth.cpp:546-550` prepares effects on the ordinary host path and `Effects.h:36-40` says callers should not exceed the configured maximum, but neither makes direct unprepared or over-capacity calls safe. The new `SfzDistoBuffersTest` proves both paths on the original code: the over-capacity block aborts the process with `free(): invalid size` from heap corruption.

## Fix (`plugins/SfzPlayer/sfizz/src/sfizz/effects/Disto.cpp`)

- `Impl` gains `_tempCapacity` (frames). The constructor allocates both buffers for `config::defaultSamplesPerBlock` (1024) outside `process()` and records the capacity, so a freshly constructed `Disto` is safe to process.
- `setSamplesPerBlock` ignores non-positive values, otherwise allocates both replacements into temporaries and moves them into `_temp` only after both succeed. A failure on either allocation preserves the old pair.
- `process` returns immediately for zero frames, falls back to a copy when buffers are missing or capacity is zero, and otherwise runs the existing LPF/upsample/stages/downsample/mix pipeline in fixed-capacity chunks (`min(capacity, remaining)`). Single-chunk behaviour is unchanged; oversized calls are now bounded. `process` allocates and frees nothing. The Synth preparation path is untouched.

## Regression (`tests/src/plugins/SfzDistoBuffersTest.cpp`)

One test links the built `sfzplayer_sfizz` library and covers the matrix from the diagnostic checkpoint: prepared ordinary path (1, partial, N frames with guards); unprepared path (zero-frame no-op then positive frames without `setSamplesPerBlock`); capacity exhaustion (N, N+1, >2N with guards); failed reconfiguration (failure injected on the second `new[]` while growing 16 to 1024 via an operator-new hook local to this test, then processing across the old boundary plus a successful grow/shrink); realtime path (zero allocations/frees around `process` for prepared, chunked oversized and zero-frame calls); Synth prepared path (SFZ with `<effect> type=disto` rendered via `Synth::renderBlock`); unprepared safety additionally covers `process(0)`. Portability is covered by construction: the same cases run against the x86 SSE/AVX kernels the build picks via `SIMDConfig.h`/`cpuid` (helper alignment is covered by `SfzSimdHelperAlignmentTest`). No sleeps, timers or races.

## Evidence

| Step | Command | EXIT |
| --- | --- | --- |
| Configure (real cmake; shim blocks configure) | `/usr/bin/cmake -S . -B build` | 0 |
| Build test, RED (original Disto) | `cmake --build build --target SfzDistoBuffersTest` | 0 |
| Test, RED | `ctest -R '^SfzDistoBuffersTest$' --output-on-failure` from `build/tests` | 8 (aborted; `preparedOrdinaryPath` PASS, `capacityExhaustionUsesBoundedChunks` aborts with `free(): invalid size`) |
| Build zene | `cmake --build build --target zene` | 0 |
| Build + test family | `cmake --build build --target SfzPlayerTest SfzPlayerRealtimeTest SfzEffectBusViewTest` then `ctest -R 'SfzPlayerTest\|SfzPlayerRealtimeTest\|SfzEffectBusViewTest\|SfzDistoBuffersTest'` | 0 (4/4 passed) |
| Proof: revert product fix | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/effects/Disto.cpp > <logs>/fix.patch && git apply -R <logs>/fix.patch` | 0 |
| Rebuild, RED again | `cmake --build build --target SfzDistoBuffersTest` | 0 |
| Test, RED again | `ctest -R '^SfzDistoBuffersTest$' --output-on-failure` | 8 (same abort) |
| Proof: re-apply fix | `git apply <logs>/fix.patch` | 0 |
| Rebuild, GREEN | `cmake --build build --target SfzDistoBuffersTest` | 0 |
| Test, GREEN | `ctest -R '^SfzDistoBuffersTest$' --output-on-failure` | 0 (all 6 slots passed) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-139.r5/logs/` (`config-2.log`, `build-red-3.log`, `test-red-1.log`, `build-green-1.log`, `test-green-1.log`, `build-family-1.log`, `test-family-3.log`, `fix.patch`, `build-red-4.log`, `test-red-2.log`, `build-green-2.log`, `test-green-2.log`, `gate-2.log`).

## Self-check

- Red/green reversal: without the product fix the test aborts on heap corruption (EXIT 8, `test-red-1.log` and `test-red-2.log` after `git apply -R fix.patch`); with it all slots pass (EXIT 0, `test-green-1.log`/`test-green-2.log`). Both directions recorded above with commands and exits.
- Determinism: no wall-clock, sleeps or races; allocation-failure injection is a thread-local operator-new hook that throws `bad_alloc` on the second `new[]` only while armed; all assertions compare finite outputs and intact guard values.
- Cause and completeness: the capacity invariant lives in `Disto` (`_tempCapacity`, constructor allocation, transactional `setSamplesPerBlock`, chunked `process`). Callers checked: `Synth::setSamplesPerBlock` (`Synth.cpp:1048-1062`, `Synth.cpp:546-550`) and `EffectBus::setSamplesPerBlock` still prepare the same way; no other caller calls `Disto::process` directly. Sibling effects are untouched.
- Collateral: prepared ordinary-path output is unchanged (same pipeline and math, single chunk when `nframes <= capacity`); zero-frame and in-capacity behaviour verified by the test; family 4/4 passed. Non-positive `setSamplesPerBlock` is now a no-op preserving the old pair instead of constructing a huge allocation. No new control commands, no undo changes.
- Realtime: `process` in the fix contains no allocation, free, lock, wait or log (only spans, stack temporaries and the unbounded chunk loop); the test counts allocations/frees around `process` and requires zero. `setSamplesPerBlock` and the constructor remain the only allocating paths, off the audio thread.
- Portability: the test is backend-agnostic and exercises the stochastic/vector-agnostic float pipeline; it passes on this x86 build where the SFZ AVX/SSE kernels are selected at runtime. Scalar/NEON paths compile from the same source; no backend-specific test code was added.
- Files committed: `plugins/SfzPlayer/sfizz/src/sfizz/effects/Disto.cpp`, `tests/src/plugins/SfzDistoBuffersTest.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p21-139.md`.
