FIXED

# Cluster new-p16-67: Dynamics Processor float-to-int overflow on large levels

## Finding

Luna patrol finding: `plugins/DynamicsProcessor/DynamicsProcessor.cpp:~184` casts `sm_peak[i] * 200` to `int` before checking the table range. A level above about 10.7 million (x200 beyond `INT_MAX`), or an infinite level, makes that conversion undefined.

## Evidence (verdict: REAL, fixed)

- Bug site, before the fix: `plugins/DynamicsProcessor/DynamicsProcessor.cpp:187-188` (base `5c1c6715d`):
  `const int lookup = static_cast<int>( sm_peak[i] * 200.0f );` and `const float frac = fraction( sm_peak[i] * 200.0f );`
- Fix, after: `plugins/DynamicsProcessor/DynamicsProcessor.cpp:188-190`:
  `const float level = std::min( sm_peak[i] * 200.0f, 200.0f );`, then `lookup = static_cast<int>( level )`, `frac = fraction( level )`.
- Why the clamp keeps behaviour: for `level < 200` nothing changes. For `level >= 200` the code takes the `else { gain = samples[199]; }` branch at `DynamicsProcessor.cpp:197`, and `frac` is never read there. So every finite input gives the same output as before. Only the undefined conversion is removed.
- `fraction()` (`include/lmms_math.h:64`) is float-only (`x - std::trunc(x)`), so it has no int conversion of its own.
- Sibling checks: `rg` for other `static_cast<int>` of `* 200` in this file finds only the one site. `tests/reference/DynamicsProcessor/` is a frozen pre-migration copy for behaviour comparison, so it is deliberately left unchanged.
- Test seam: `PluginPortsMigrationTest::dynamicsProcessorHugeLevelReadsLastCurvePoint` (`tests/src/plugins/PluginPortsMigrationTest.cpp:3948`, declared at `:1065`). It loads the migrated module through `lmms_plugin_main` (same pattern as `dynamicsProcessorRejectsShortWaveShape`), uses a curve with `samples[0]=0.005` and `samples[199]=1.0`, settles a constant input over 64 x 128 frames (no wall clock), and requires the last output sample to be `1.0` within 0.01. It checks two levels: `2.0` (in-range control, never overflowed) and `1.0e8` (overflow, `x200 = 2.0e10`).

## Commands and exit codes

Build note: the first build (`build-1-red.log`) failed with `EXIT=2`. The cause was a ccache permission error on the shared temp dir, not a compile error. The retry used `CCACHE_DISABLE=1`, as other lanes on this machine do.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4` (unfixed) | `build-2-red.log` | 0 |
| RED run | `build/tests/PluginPortsMigrationTest dynamicsProcessorHugeLevelReadsLastCurvePoint` (unfixed) | `test-1-red.log` | **139** (SIGSEGV inside `DynProcEffect::processImpl`, the `samples[lookup-1]` read) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4` (fixed) | `build-3-green.log` | 0 |
| GREEN run | same binary and function (fixed) | `test-2-green.log` | **0** (`Totals: 3 passed, 0 failed`) |
| 4b save patch | `git diff -- plugins/DynamicsProcessor/DynamicsProcessor.cpp > logs/fix.patch` | (none) | 0 |
| 4b revert | `git apply -R logs/fix.patch` | `bxdax1t3w` output | 0 |
| 4b revert build | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4` | `build-4-revert.log` | 0 |
| 4b revert run | same binary and function (reverted) | `test-3-revert-red.log` | **139** (same SIGSEGV) |
| 4b reapply | `git apply logs/fix.patch`; `git diff` compared with `fix.patch` (identical) | `boj53r7bs` output | 0 |
| 4b reapply build | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4` | `build-5-reapply.log` | 0 |
| 4b reapply run | same binary and function (reapplied) | `test-4-green-reapplied.log` | **0** (`3 passed, 0 failed`) |

The 4b proof: the test fails (exit 139) on the unfixed code and passes (exit 0) with the fix, and it reproduced both ways.

Regression family:

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `ctest -R '^(PluginPortsMigrationTest\|GraphWaveformLoadTest)$' --output-on-failure` (from `build/tests`) | `test-6-family.log` | 0 | `100% tests passed out of 2` |
| `./PluginPortsMigrationTest` (full binary) | `test-7-full-ppm.log` | 0 | `Totals: 87 passed, 0 failed, 2 skipped` (both skips: Sf2Player not built in this configuration) |
| `./GraphWaveformLoadTest` (full binary) | `test-8-full-gwl.log` | 0 | `Totals: 93 passed, 0 failed, 0 skipped` |

Earlier family run `test-5-family.log` exited 8 with one failure, `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"). Cause: `decimator_1202.so` was missing from `build/plugins/ladspa/`, because the ccache error in the first build also stopped the SWH LADSPA fixtures. I built `decimator_1202 amp_1181 dj_eq_1901` with `CCACHE_DISABLE=1` (`build-7-swh.log`, EXIT 0). The rerun above (`test-6-family.log`) then passed. The failure is unrelated to this change.

Gates:

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | `gate-1-feature-check.log` | 1 | Crashed before any check: `PermissionError` creating `<worktree-parent>/zene-qwen-new-p16-67-feature-check`, which is outside this lane's write-jail. |
| `feature_check.py` source run with only its log directory redirected to the lane log folder (`gate-2`, `gate-3`) | `gate-3-feature-check.log` | 0 | `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item). |

`gate-2` exited 1 from a bug in my own redirect wrapper (`sys.argv` was overwritten before it was read), not from the gate. `gate-3` is the valid run. The gate script itself was not edited. Its checks and baselines are unchanged, and only its log directory changed.

`submit_check.py`: see the Self-check section below.

## Files committed

- `plugins/DynamicsProcessor/DynamicsProcessor.cpp` (product fix, the clamp at lines 188-190)
- `tests/src/plugins/PluginPortsMigrationTest.cpp` (regression `dynamicsProcessorHugeLevelReadsLastCurvePoint`)
- `tests/upstream-modifications.txt` (two registration lines, one per changed inherited file)
- `docs/qwen-fixes-2026-10/cluster-new-p16-67.md` (this report)

## Notes and open items

- The finding names ctest targets (`^DynamicsProcessor$`, `^DynamicsProcessorControls$`, `^DynamicsProcessorControlDialog$`). They do not exist in this tree. The live coverage is `PluginPortsMigrationTest` (`ctest` #608) and `GraphWaveformLoadTest` (#120). The regression lives in the first.
- The RED is a SIGSEGV, not an assertion failure. On x86-64 the overflowing cast gives `INT_MIN`, which then indexes `samples[lookup-1]` out of range inside `processImpl`. The crash itself shows the bug.
- Not fixed, observed by reading only and not tested: `m_currentPeak` can become permanently infinite. `inf * coeff + (1 - coeff) * t` stays infinite, and `std::max(DYN_NOISE_FLOOR, inf)` keeps it. The RmsHelper float buffer can also hold `inf` from an input around 1e20. This is outside the cast bug, so I left it alone and report it for a separate cluster.

## Self-check

Pending: `submit_check.py` result is recorded below after it runs.
