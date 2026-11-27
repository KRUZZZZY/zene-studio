FIXED

# cluster new-p18-19: muted crossover bands stop updating their filter histories

## Verdict: REAL
`plugins/CrossoverEQ/CrossoverEQ.cpp` (before the fix, lines ~148-186) ran each band's
`StereoLinkwitzRiley::update()` only inside `if (muteN)`. The model "Mute Band N" is `true`
when the band is ON (default `true`, `CrossoverEQControls.cpp:44-47`), so a band switched off
froze its filter state, and switching it on replayed that stale state as a transient.

## Fix
`plugins/CrossoverEQ/CrossoverEQ.cpp`: the four copy-pasted band loops became one helper
`addBand()` (anonymous namespace). It always advances the band filter and adds `out * gain`
to the work buffer only when the band is on. Band summing, gains, crossover frequencies,
dry/wet and saved settings are unchanged; with all bands on the output is identical.
No allocation, lock or logging was added on the audio thread (a loop over existing buffers).
Cost: an off band now costs its filter CPU, which is what the finding's fix asks for.

## Regression test
`tests/src/plugins/CrossoverEQMuteHistoryTest.cpp::unmutedBandHasNoStaleTransient`
(CMake target in `tests/CMakeLists.txt`, plugin sources compiled in, like DelayLfoAutomationTest).
Band 1 on, DC drives its low-pass state to ~1; band 1 off for 1 s of silence; band 1 on again
with silent input: the first period must be below 1e-3. Rendered audio only, no clocks.

## Commands (logs in ~/.config/kads/zene/state/runs/new-p18-19.r2/logs)
| Step | Command | EXIT |
|---|---|---|
| regenerate build tree | `cmake --build build --target DelayLfoAutomationTest -j4 > build-regen-1.log` (`cmake -S` is refused by bunny-guard) | 0 |
| RED build | `cmake --build build --target CrossoverEQMuteHistoryTest -j4 > build-red-1.log` | 0 |
| RED test | `ctest -R CrossoverEQMuteHistoryTest --output-on-failure > test-red-1.log` (build/tests) | 8 (FAIL: `un-muting band 1 replayed a stale filter state: peak=1 on silent input (limit 0.001)`, test file line 159) |
| GREEN build | `cmake --build build --target CrossoverEQMuteHistoryTest -j4 > build-green-1.log` | 0 |
| GREEN test | `ctest -R CrossoverEQMuteHistoryTest --output-on-failure > test-green-1.log` | 0 (pass) |
| zene build | `cmake --build build --target zene -j4 > build-zene-1.log` | 0 |
| family | `ctest -R 'Crossover\|crossover' > test-family-2.log` | 0 (1/1 passed; this test is the only ctest with "crossover" in its name) |
| 4b revert | `git diff -- plugins/CrossoverEQ/CrossoverEQ.cpp > fix.patch && git apply -R fix.patch` | 0 |
| 4b rebuild reverted | `cmake --build build --target CrossoverEQMuteHistoryTest -j4 > build-revert-1.log` | 0 |
| 4b test reverted | `ctest -R CrossoverEQMuteHistoryTest ... > test-revert-1.log` | 8 (FAIL, same message, peak=1) |
| 4b reapply | `git apply fix.patch` | 0 |
| 4b rebuild | `... > build-reapply-1.log` | 0 |
| 4b test | `ctest -R CrossoverEQMuteHistoryTest ... > test-reapply-1.log` | 0 (pass) |

## Registration
`tests/all-sources.txt`: new test source added in sorted place (same as the DelayLfoAutomationTest
precedent, commit 37768bcb43). `tests/upstream-modifications.txt`: reason for
`plugins/CrossoverEQ/CrossoverEQ.cpp` extended.

## Gates
`python3 .../feature_check.py <worktree>` could NOT run: it exits 1 (`gates-1.log`) because the
script runs `git -C .../gate-baselines/tree checkout -q --detach <sha>` and the session guard
refuses that git command (`bunny-guard: ... is not a...`). Not worked around. Complexity was
reasoned by hand: `addBand` has CCN 3; `processImpl` lost four `if` branches.

## Unverified
- feature_check.py gates (blocked, above).
- Stereo right channel and bands 2-4 are covered by the same helper but the test drives only band 1.
- No TSan (not available on this box); the change adds no shared state.

## Other findings
- `tests/reference/CrossoverEQ/` is a frozen reference copy and still has the old behaviour (intentional).

Delegation: Haiku ran the RED build/test and the GREEN/revert/reapply build-and-test sequence (all exit codes above were re-checked against its logs' reported values and the test-family run was done directly).

## Self-check
- Fails reverted, passes with the fix: RED `ctest -R CrossoverEQMuteHistoryTest` EXIT=8 (`test-red-1.log`, and again with `git apply -R fix.patch`, `test-revert-1.log`, EXIT=8, `peak=1`); GREEN EXIT=0 (`test-green-1.log`, `test-reapply-1.log`).
- Deterministic: the test renders fixed-size blocks of constant input (1.0 then 0.0) through the effect via `processAudioBuffer`; no clock, sleep, thread or random value. The seam is the engine-less call into the compiled-in plugin; the only variable is the engine's period size and sample rate, which the assertion (a 1.0-versus-1e-3 gap) does not depend on.
- Complete at the cause: the only place the four band filters were gated on the on/off flag was the four loops in `CrossoverEQEffect::processImpl`; all four now go through `addBand`. `clearFilterHistories()` (called from `loadSettings`, `CrossoverEQControls.cpp:93`) and `sampleRateChanged()` are unchanged, and the two pre-band filters (`m_lp2`, `m_hp3`) were already unconditional.
- No collateral damage: saved settings keys and defaults are unchanged (`CrossoverEQControls.cpp:59-91` untouched); with all bands on, per-sample arithmetic is the same (each band's filter output times its gain, summed in the same order). Only behaviour while a band is off changes: its filter state tracks the input instead of freezing. Off bands cost filter CPU; no allocation, lock or logging added.
- Verdict is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE item does not apply.
- Gates: `feature_check.py` cannot run here (its internal `git checkout --detach` is refused by bunny-guard, `gates-1.log`); `submit_check.py` therefore reports that gate FAIL. Reported as BLOCKED for that gate, not worked around.
