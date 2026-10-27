FIXED

# Cluster new-p10-456: Waveshaper converts non-finite or huge samples to `int`

## Decision

REAL, now FIXED. With clip off, the Waveshaper computed its table index with
`static_cast<int>( qAbs( s[i] ) * 200.0f )` before checking the range. The conversion is
undefined when the value is NaN, is infinite, or is outside the `int` range.

Evidence on the original code (`git show HEAD:plugins/WaveShaper/WaveShaper.cpp`, lines 106-109):

- `const int lookup = static_cast<int>( qAbs( s[i] ) * 200.0f );`
- `const float frac = fraction( qAbs( s[i] ) * 200.0f );`

The conversion is undefined when `|s| * 200 >= 2^31`, i.e. `|s| >= ~1.07e7`. It is also
undefined when `|s| * 200` overflows float to +inf, i.e. `|s| > ~1.7e36`, and for NaN.
On this x86-64 build the conversion yields INT_MIN, so the `lookup < 1` branch runs
with `frac` equal to 0 or NaN. Measured on the original code (`logs/test-red-2.log`): a `+1e9`
input came out as 0, and a `3e38` input came out as a zeroed channel after the bus sanitizer.

## The fix (plugins/WaveShaper/WaveShaper.cpp:108-115)

The table position is saturated to `[0, 200]` before the conversion. The comparison
`position < 200.0f ? position : 200.0f` sends NaN to 200 as well. The `else` branch
(`lookup >= 200`) never uses `frac`, so the saturated value takes the same branch that
any out-of-table value already took.

Behaviour preservation: for every input where the old conversion was defined, the output
is bit-identical. For `position < 200` the value is unchanged. For `position >= 200` the
old and new code both take the `else` branch, which ignores `frac`. Only the cases that were
undefined in C++ change. No data format, save file, or control command changes.

No audio-thread allocation, locking, or logging is added. The change is one compare and
one select.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp`, slot
`waveshaperLookupHandlesSamplesPastTheTableEnd` (definition at line 1687; declared in the
class's private slots). It loads the migrated `waveshaper` module through the existing
`PART_C_MIGRATED_waveshaper` path, the same route as the other slots in this file. It uses
clip off, input gain 1, output gain 1, wet 1, and `autoquit` 0. The last table entry is set to
`0x1p-20f` through `graphModel::setSampleAt`. The bus clamps track output to +/-1000
(`src/core/AudioBus.cpp:121`), so the scaled table keeps the waveshaped value inside that range.

Inputs and expected outputs:

| frame | input (L / R) | expected (L / R) | why |
|---|---|---|---|
| 0 | 1e9 / -1e9 | 1e9 * 2^-20 / -(1e9 * 2^-20) | lookup far past int range |
| 1 | 2e7 / -2e7 | 2e7 * 2^-20 / -(2e7 * 2^-20) | lookup about 4e9, past int range, finite |
| 2 | 3e38 / -3e38 | 1000 / -1000 | `|x|*200` overflows to +inf; the bus clamp applies |
| 3 | 0.5 / -0.5 | 0.5 / -0.5 | in-table lookup (index 100), unchanged |

Determinism: fixed inputs, a fixed table value, no timing, no sleeps, no RNG, no threads.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-456.r1/logs`

| step | command (run from worktree or build/tests as noted) | EXIT | log |
|---|---|---|---|
| build #1 (no change yet) | `cmake --build build --target PluginPortsMigrationTest -j4` | 2 | build-1.log (ccache could not write its temp file under the sandbox path; not a code error) |
| build #2 (ccache fixed) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-456 cmake --build build --target PluginPortsMigrationTest -j4` | 0 | build-2.log |
| RED #1 (original code, first test draft) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | test-red-1.log: `data[0].left()` Actual 0, Expected 1e9 |
| build #3 (first fix) | same build command | 0 | build-3.log |
| GREEN #1 (first fix, first draft) | same ctest command | 8 | test-green-1.log: bus clamp gave 1000, so draft test was wrong; test rewritten |
| build #4 (diagnostic) | same build command | 0 | build-4-diag.log (temporary diagnostics, since removed) |
| diagnostic run | same ctest command | 8 | test-diag-1.log: output +/-1000 confirmed the bus clamp |
| 4b-a: save patch | `git diff -- plugins/WaveShaper/WaveShaper.cpp > logs/fix.patch` | 0 | (in logs) |
| 4b-b: revert fix | `git apply -R logs/fix.patch` | 0 | |
| build #5 (fix reverted, test v2) | same build command | 0 | build-5-red.log |
| **RED (fix reverted)** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | test-red-2.log: `FAIL! waveshakerLookupHandlesSamplesPastTheTableEnd`, Actual `data[0].left()` = 0, Expected `1e9f * tailGain` = 953.6743 (`PluginPortsMigrationTest.cpp:1733`) |
| 4b-c: re-apply fix | `git apply logs/fix.patch` | 0 | |
| build #6 (fix applied) | same build command | 0 | build-6-green.log |
| **GREEN (fix applied)** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | test-green-2.log: `PASS : waveshaperLookupHandlesSamplesPastTheTableEnd()`; the one FAIL is `nonRealtimeLadspaEffectsAreInvalid` (see below); `Totals: 83 passed, 1 failed, 2 skipped` |

The ctest exit code is 8 in both runs because of a second slot,
`nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"). That
slot also failed in RED #1, when the product code was still the original. It concerns the
LADSPA test fixture, which this environment does not have, and nothing in this change touches
LADSPA. The GREEN evidence for this cluster is the `PASS` line for the new slot.

Test count: ctest ran 1 test (`PluginPortsMigrationTest`, ctest #587), with 86 slots
(83 passed, 1 failed, 2 skipped). The test family named in the lane brief
(`^WaveShaperControls$`, `^WaveShaper$`, `^WaveShaperControlDialog$`) does not exist as a ctest
name in this tree. The files under `tests/reference/WaveShaper/` are frozen pre-migration copies
(`tests/reference/ORIGIN.tsv`; `tests/CMakeLists.txt:3312-3319` and `:3523`) and are only built as
`partc_ref_*` reference modules, not as ctest entries, so I did not edit them. The covering test here is `PluginPortsMigrationTest`.

## Gates

`feature_check.py` could not run here: it writes its logs to
`<worktree>/../<name>-feature-check`, which is outside the write-jail. I ran the same commands it runs, one by one:

| gate | command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The ratchet gates exit 1 because they list pre-existing items in other files (for example
`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/TakeLane.cpp`). The
wrapper only counts items in files the branch changed. Neither changed file (`plugins/WaveShaper/WaveShaper.cpp`,
`tests/src/plugins/PluginPortsMigrationTest.cpp`) appears in either gate's regression list
(`rg WaveShaper|PluginPortsMigrationTest` on both logs: no match). By the wrapper's own rule,
both ratchet rows should pass. I did not rerun the wrapper's base-listing step, which needs
writable paths outside the jail. The wrapper's own failure is logged in `logs/feature-check-attempt.log` (EXIT 1, PermissionError).

`tests/upstream-modifications.txt` gained two lines (WaveShaper.cpp and
PluginPortsMigrationTest.cpp, cluster new-p10-456). `tests/all-sources.txt` needed no change
because no file was added.

## Siblings and notes (not changed)

- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:187` has the same pattern,
  `static_cast<int>( sm_peak[i] * 200.0f )`. `sm_peak` comes from an RMS follower over the
  input (`DynamicsProcessor.cpp:140-159`) with no upper bound, so the same undefined conversion
  is reachable. It is a different plugin and a different finding, so I did not change it.
  Recommend a separate finding.
- `tests/reference/WaveShaper/WaveShaper.cpp:108` (frozen evidence) has the same line. It is
  left as is by design.

## Self-check

1. **RED/GREEN, with the product change reverted and applied.** RED: `test-red-2.log`, the new
   slot FAILs with Actual 0, Expected 953.6743 (EXIT 8 for the family run). GREEN: `test-green-2.log`,
   the new slot PASSes (EXIT 8 only because of the unrelated LADSPA slot, which also failed in RED #1).
2. **Deterministic.** Fixed input values, a fixed table value (`0x1p-20f` via
   `graphModel::setSampleAt`), no clocks, sleeps, or random sources. Output is compared
   exactly or against a fixed number.
3. **Caller and sibling check.** The only path into the lookup is `processImpl`, which loops
   over both channels with the same code. The old `lookup < 200` branch and the `else` branch are
   unchanged. `DynamicsProcessor` has the same pattern, reported above and not changed.
4. **No collateral damage.** For every input where the old conversion was defined, the output is
   identical (see "Behaviour preservation"). No saved data, file format, or command changes. The
   LADSPA slot failure is environmental and existed before the change.
5. **Verdict.** FIXED. Not ALREADY-FIXED and not FALSE-POSITIVE: the original code has the
   undefined conversion (line evidence above) and the regression test failed on it.
