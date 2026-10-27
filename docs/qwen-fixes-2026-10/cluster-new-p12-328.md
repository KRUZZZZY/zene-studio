BLOCKED: the aliased-harmonic defect is REAL in the shipped tables for square and triangle; the regression test is written but could not be built or run in this lane, and no product fix is committed.

## Evidence (src/core/BandLimitedWave.cpp, current tree)

- saw loop, lines 99-111: `do { ...add harm...; harm++ } while (hlen > 2.0)` adds harmonic k when len/(k-1) > 2, so it admits k with len/k < 2. Square loop 143-153 and triangle loop 184-195 do the same with `harm += 2`.
- For the saw, every TLENS length is even except 3, and for even len the extra harmonic is exactly the Nyquist one, which is zero at every sample point. Table-level effect of the saw bug is zero (len 3 buggy-vs-fixed deviation 1.3e-15, all other saw tables 0.0). The finding's saw example is real in the code but changes no stored sample.
- For square and triangle with len >= 8 the extra odd harmonic is above Nyquist and aliases onto a lower harmonic, changing the stored shape: square len 8 deviates 0.293 from the in-band series, len 64 deviates 0.036; triangle len 8 deviates 0.044, len 64 deviates 1.5e-3.
- The shipped `data/wavetables/sqr.bin` and `tri.bin` (and `saw.bin`) match the BUGGY generator to ~1e-7 (float-pi rounding) and differ from the in-band series by the same amounts above. Production loads these files (generateWaves uses them when present, src/core/BandLimitedWave.cpp:126,167), so a generator-only fix would change no audible output; the shipped square/triangle/moog binaries would also need regenerating.

Evidence logs (LOG FOLDER): `model-compare-1.log`, `shipped-decode-1.log` (Python models of the generator and of the shipped binaries; these are reference models, not the C++ test).

## What is in this commit

- `tests/src/core/BandLimitedWaveTest.cpp`: two QTest functions. `fallbackTablesHoldOnlyInBandHarmonics` empties the `data:` search path to force the fallback generator; `shippedTablesHoldOnlyInBandHarmonics` points it at `QT_TESTCASE_SOURCEDIR/../data`. Both compare every table except len 2 (saw, square, triangle, moog) against an in-band reference with tolerance 1e-4, and reset `s_wavesGenerated`/`s_waveforms` first.
- `tests/CMakeLists.txt`: `LIST(APPEND LMMS_TESTS ...)` and two `add_test` lines for the fresh-process scenarios.
- `tests/fork-sources.txt`, `tests/all-sources.txt`: the new file.

No product source and no wavetable binary is changed.

## Commands and exit codes (what I actually ran)

| Step | Command | EXIT |
| --- | --- | --- |
| Reconfigure (to create the new target) | `cmake -S . -B build` | 97 (lane guard: "configuring is the parent's job") |
| RED build attempt | `cmake --build build --target BandLimitedWaveTest -j4` | 2 (`No rule to make target 'BandLimitedWaveTest'`) |
| Feature gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (`PermissionError` creating `<worktree>-feature-check/` outside the write-jail; gate not evaluated) |

RED and GREEN ctest runs were not possible: the test target does not exist in this lane's configured tree. I did not use `lmms_build` to reconfigure, because that is the same configure step the guard reserves for the parent.

## Missing capability

1. The parent must reconfigure the build tree (`cmake -S . -B build`) so `BandLimitedWaveTest` exists, then run `cmake --build build --target BandLimitedWaveTest -j4` and `ctest -R BandLimitedWave --output-on-failure` from `build/tests` for RED.
2. Fix at the cause (not done here): one shared `2 * harm <= len` cutoff for the saw, square, and triangle loops, and regenerate `sqr.bin`, `tri.bin`, and `moog.bin` from the corrected generator (saw.bin is unchanged). Regenerating needs a C++ run or a Python replica that reproduces the shipped files to ~1e-7 first.
3. The gate script needs its log directory somewhere writable.

## Self-check

Not run: `submit_check.py` (depends on the gate above and a built test). The fix itself is unverified in C++, so this report does not claim RED or GREEN.
