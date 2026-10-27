FIXED - the square and triangle tables and the moog table held harmonics above Nyquist, and the saw loop had the same ordering defect. The feature gate could not be run in this lane (write-jail), so gate status is NOT RUN.

## Evidence (src/core/BandLimitedWave.cpp)

- Before: the saw, square and triangle loops were `do { ...add harm...; harm++ } while (hlen > 2.0)`, so harmonic k was added whenever len/(k-1) > 2. For len >= 8 the square and triangle loops added one odd harmonic with period below two samples (lines 111, 153, 195 of the old file).
- After: all three loops use `for( harm = 1; withinNyquist( len, harm ); harm++ / += 2 )`, with `withinNyquist` = `2 * harm <= len` (src/core/BandLimitedWave.cpp:34-44). The boundary is inclusive, and harmonic 1 always passes, so len=2 keeps its single harmonic.
- The saw defect is real in code but changes no stored sample: every even TLENS length already stops at the Nyquist harmonic, and the len-3 change is removed by normalization (replica check: buggy and fixed saw differ by 1.3e-15 in double).
- Square and triangle at len >= 8 deviated from the in-band series by 0.293 (square len 8) and 0.044 (triangle len 8) before the fix.

## Assets

- `data/wavetables/sqr.bin`, `tri.bin`, `moog.bin` regenerated (163800 bytes each, same format). `saw.bin` unchanged: the fixed saw agrees with it to 3.6e-7 (float rounding).
- Regeneration used a Python replica of the generator (float pi, float32 storage). The replica reproduces the shipped files: saw worst 1.3e-22, square/triangle/moog exact (`replica-validate-1.log`).

## Test (tests/src/core/BandLimitedWaveTest.cpp)

- `fallbackTablesHoldOnlyInBandHarmonics`: empty `data:` search path, forces the fallback generator.
- `shippedTablesHoldOnlyInBandHarmonics`: loads `QT_TESTCASE_SOURCEDIR/../data`, which is the shipped set.
- Both compare every table of len >= 3 (saw, square, triangle, moog) with an in-band reference, tolerance 1e-4. The reference uses the generator's `pi_v<float>`; with double pi the len-6144 square differed by 1.45e-4 from the product's own constant, which was a test-side mismatch and was corrected, not the tolerance.
- Registered as the foreach `BandLimitedWaveTest` plus fresh-process `BandLimitedWaveFallback` and `BandLimitedWaveShipped`.

## Commands and exit codes

| Step | Command (run from build/tests unless noted) | EXIT | Log (LOG FOLDER) |
| --- | --- | --- | --- |
| RED, before fix | `ctest -R BandLimitedWave --output-on-failure` | 8 (3/3 failed, square len 8 0.292893) | test-red-1.log |
| Build test (fix) | `CCACHE_DISABLE=1 cmake --build build --target BandLimitedWaveTest -j4` | 0 | build-green-4.log |
| Build zene (fix) | `CCACHE_DISABLE=1 cmake --build build --target zene -j4` | 0 | build-zene-3.log |
| GREEN, with fix | `ctest -R BandLimitedWave --output-on-failure` | 0 (3/3 passed) | test-green-3.log |
| Reverse product changes | `git apply -R fix.patch` (fix.patch = `git diff --binary --no-ext-diff` of src/core/BandLimitedWave.cpp and data/wavetables/{moog,sqr,tri}.bin) | 0 | - |
| Build test (reversed) | `CCACHE_DISABLE=1 cmake --build build --target BandLimitedWaveTest -j4` | 0 | build-reversed-2.log |
| RED, reversed | `ctest -R BandLimitedWave --output-on-failure` | 8 (3/3 failed, square len 8 0.292893) | test-reversed-2.log |
| Restore product changes | `git apply fix.patch` | 0 | - |
| Build test (restored) | `CCACHE_DISABLE=1 cmake --build build --target BandLimitedWaveTest -j4` | 0 | build-restored-1.log |
| GREEN, restored | `ctest -R BandLimitedWave --output-on-failure` | 0 (3/3 passed) | test-restored-1.log |
| Realtime control | `ctest -R PluginPortsMigration --output-on-failure` | 8: one failure, `nonRealtimeLadspaEffectsAreInvalid` (SWH Decimator fixture not loaded, unrelated); the Lb302 checks pass (migratedPluginsPreserveBehaviour, slice6PluginsAreLiveAndExact) | test-ppm-1.log |
| Feature gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (PermissionError creating the sibling `-feature-check` log folder, outside the write-jail) | gates-2.log |

Family test count: 3 executed (BandLimitedWaveTest, BandLimitedWaveFallback, BandLimitedWaveShipped), 3 passed with the fix.

ccache: the default cache temp folder is outside the write-jail, so builds were run with `CCACHE_DISABLE=1` (environment only).

## Files committed

- src/core/BandLimitedWave.cpp
- data/wavetables/sqr.bin, data/wavetables/tri.bin, data/wavetables/moog.bin
- tests/src/core/BandLimitedWaveTest.cpp, tests/CMakeLists.txt (already committed in 69434193e1)
- tests/upstream-modifications.txt (registration of the inherited source and three binaries)

## Self-check

- Feature gates: NOT RUN (write-jail). A pass cannot be claimed.
- `submit_check.py`: not re-run after the fix.
- Test is deterministic: no timing, sleeps or races. The seam is the `data:` search path plus the reset of `s_wavesGenerated`/`s_waveforms`.
- Callers of the three loops: `generateWaves()` only (src/core/Engine.cpp:65). Moog derivation is inside the same function. Oscillator lookup (`BandLimitedWave::oscillate`) reads the stored tables and is unchanged. No header change; no new per-sample work.
- Old projects: unaffected by table content except the square, triangle and moog samples for len >= 8, which are the defect being fixed.
