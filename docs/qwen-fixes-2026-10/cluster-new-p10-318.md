FIXED

## Evidence and decision

**REAL, but narrower than the finding says.** The division is real. The observable result is not NaN.

- The defect: [PeakControllerEffect.cpp:138](../../plugins/PeakControllerEffect/PeakControllerEffect.cpp) computes `sqrt_neg(sum / inOut.frames())`. `sum` is a `double`, so a zero-frame block computes `0.0 / 0`, which is NaN under IEEE 754. There is no trap and no crash.
- The path: `AudioPlugin::processAudioBuffer(AudioBus&)` ([AudioPlugin.h:246-284](../../include/AudioPlugin.h)) calls `router.process(...)` ([AudioPorts.h:175-211](../../include/AudioPorts.h)), which calls `processImpl` with the bus frame count and has no zero-frame check. The public entry `EffectChain::processAudioBuffer(AudioBus&)` ([EffectChain.cpp:368](../../src/core/EffectChain.cpp)) has no zero-frame check either, so a zero-frame bus reaches `processImpl`.
- Reachability in production is not proven. The engine sets the period to `min(buffer, DEFAULT_BUFFER_SIZE)` ([AudioEngine.cpp:479](../../src/core/AudioEngine.cpp)). I did not prove that a zero-frame period can never occur. `Rack.cpp:107` already returns early on zero frames, so the codebase treats the case as possible.
- What `m_lastSample` actually gets: NaN does not survive `qBound`. In the Qt 6.12 headers ([qminmax.h:20-28](/usr/include/qt6/QtCore/qminmax.h)), NaN is clamped to the lower bound, so `m_lastSample` becomes **0.0**. Measured: RED test `Actual 0` against `Expected 0.8535534` (see `test-1-red.log`). This is a spurious jump of the controller output on an empty block. The clamp target depends on the Qt release; I checked only 6.12.
- Fix: return early for a zero-frame block, before the RMS, so nothing is measured and the output holds ([PeakControllerEffect.cpp:100-104](../../plugins/PeakControllerEffect/PeakControllerEffect.cpp)). I chose hold over "treat as silence", because an empty block carries no measurement and silence would jump the output to base. The mute path does nothing on zero frames either. Siblings: the only division by frame count in this file is line 138, and no other path in this file reads the frame count. Other effects with similar RMS code were not audited.

## Regression test

`tests/src/plugins/PeakControllerEffectZeroFrameTest.cpp` (new). It compiles the real plugin sources and drives the public `Effect::processAudioBuffer(AudioBus&)`. Its target is registered in [tests/CMakeLists.txt](../../tests/CMakeLists.txt) after `DelayControlsTest`.

| Case | Test function | Without fix |
| --- | --- | --- |
| Non-empty block measures the signal (positive control) | `nonEmptyBlockMeasuresSignal` | PASS |
| Zero-frame block after a measured block keeps the output | `zeroFrameBlockHoldsLastOutput` | **FAIL** (the regression) |
| Measured block after an empty one matches the first | `nonEmptyBlockAfterZeroFrameBlockMatchesFirst` | PASS (a no-state-leak check, not regression proof: `m_lastSample` is rewritten on every measured block) |
| Mute on, abs off, sleep / auto-quit, audio-thread constraints | not covered | the change is an early return before all of them; no allocation, lock or log is added |

## Commands and exit codes

| # | Command (abridged) | Log (LOG FOLDER) | EXIT |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target PeakControllerEffectZeroFrameTest -j4` | build-1-red.log | 2 (no such target yet: "No rule to make target") |
| 2 | `cmake -S . -B build` | configure-1.log | 97 (bunny-guard: "configuring is the parent's job") |
| 3 | `bash manual-build.sh <wt> build/tests $TMPDIR/pcz-p10-318` (unfixed tree, RED) | build-5-red-manual.log | 0 |
| 4 | `QT_QPA_PLATFORM=offscreen <bin>` in build/tests (RED) | test-1-red.log | **1**: `FAIL! zeroFrameBlockHoldsLastOutput`, Totals 4 passed, 1 failed |
| 5 | `manual-build.sh` with fix (GREEN) | build-6-green-manual.log | 0 |
| 6 | run the same binary (GREEN) | test-2-green.log | **0**: Totals 5 passed, 0 failed |
| 7 | `git diff -- plugins/PeakControllerEffect/PeakControllerEffect.cpp > fix.patch` | fix.patch | 0 |
| 8 | `git apply -R fix.patch` | (none) | 0 |
| 9 | `manual-build.sh` (reverted, RED) | build-7-reverted-manual.log | 0 |
| 10 | run the binary (reverted, RED) | test-3-reverted-red.log | **1**: FAIL `zeroFrameBlockHoldsLastOutput`, Totals 4 passed, 1 failed |
| 11 | `git apply fix.patch` | (none) | 0 |
| 12 | `manual-build.sh` (fixed again) | build-8-fixed-manual.log | 0 |
| 13 | run the binary (fixed again, GREEN) | test-4-fixed-green.log | **0**: Totals 5 passed, 0 failed |
| 14 | `cmake --build build --target peakcontrollereffect -j4` | build-9-plugin-module.log | 2 (ccache could not write temp files outside the jail) |
| 15 | `CCACHE_DISABLE=1 cmake --build build --target peakcontrollereffect -j4` (real plugin module, with fix, product flags incl. `-Werror`) | build-10-plugin-module-noccache.log | 0 |
| 16 | `python3 -I <copy of feature_check.py> <wt>` (logs redirected, see deviation 4) | gates-2.log | 0 (RESULT: PASS) |
| 17 | `python3 feature_check.py <wt>` (unmodified) | gates-1.log | 1 (PermissionError on `<wt>-feature-check`, outside the jail) |
| 18 | `python3 -I <copy of feature_check.py> <wt>` on the pre-commit tree (final gate run before commit 1) | gates-4-precommit.log | 0 |

Gate table from gates-4-precommit.log: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in your files), file-length PASS (no new item in your files). RESULT: PASS.

An earlier gate run (gates-3.log, EXIT=1) failed fork-sources and all-sources-reproduce because I had hand-added the new test to the two manifests before it was committed. Both manifests are derived from git (`fork-sources` from HEAD with `--diff-filter=A`, `all-sources` from `git ls-files`), so a file can only be registered after it is committed. I removed the hand edits and registered the test in a follow-up commit, as described under "Files committed".

Affected test family: the finding's names (`ctest -R '^PeakControllerEffect$'`, `…Controls$`, `…ControlDialog$`) are not in the configured ctest list. `ctest -N` shows no PeakController entry. The `tests/reference/PeakControllerEffect/` sources are compiled into the `partc_ref_*` modules for `PluginPortsMigration`, and `PluginPortsHarness.h:104` says this plugin is not rendered there. So no existing test exercises this effect, and the affected ctest family has 0 registered tests. The new test has 5 QTest functions (4 test cases plus init/cleanup), all run by the manual binary.

## Deviations and blockers (observed, not assumed)

1. **The new CMake target cannot be built in this lane.** The lane tree is configured by the parent. Commands 1 and 2 show the target is absent, and configuring from the lane is refused. So `ctest -R PeakControllerEffectZeroFrameTest` does not work until the parent reconfigures. The registration is committed in `tests/CMakeLists.txt`.
2. **RED/GREEN were produced by a manual build.** `manual-build.sh` (LOG folder) compiles the test, the plugin's `PeakControllerEffect.cpp`, `PeakControllerEffectControls.cpp` and `PeakControllerEffectControlDialog.cpp`, plus their `moc` output, into `$TMPDIR/pcz-p10-318`. It uses the flags of the configured `DelayControlsTest` target and adds `-std=gnu++20`, which the configured flags do not show. It links the configured objects minus the delay-specific ones. The binary was run from `build/tests` with `QT_QPA_PLATFORM=offscreen`. The scratch directory is deleted at the end.
3. **Gate log folder.** `feature_check.py` writes its logs to `<worktree>-feature-check`, a sibling of the worktree, which is outside my write-jail. Command 17 was denied. I ran a copy in `$TMPDIR` with the log folder pointed at the LOG FOLDER and `ROOT` pinned to the original qwen folder (for `FIX-BASE` and the baseline cache). The gate logic and limits are unchanged (`diff` shows only those two lines).
4. **ccache.** The real plugin build failed on ccache temp files outside the jail (command 14). Command 15 disables ccache for that command only, through the environment.
5. **Harmless QWARNs** in the test log: missing `build/tests/misc/*.ogg` (from `Engine::init`) and "Could not save file ~/.zenestudio.xml" at cleanup. Both come from the Engine setup that other Engine-based tests share.
6. **Scope.** The fix covers the zero-frame case only. A NaN in the input samples is out of scope. The `sanitize` step runs after `processImpl`, and I did not change it.

## Files committed

Commit 1 (fix, test, registration of the CMake target, reason line, this report):

- `plugins/PeakControllerEffect/PeakControllerEffect.cpp` (fix, +6 lines)
- `tests/src/plugins/PeakControllerEffectZeroFrameTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (test target, +28 lines)
- `tests/upstream-modifications.txt` (reason line for the inherited file that was changed)
- `docs/qwen-fixes-2026-10/cluster-new-p10-318.md` (this report)

Commit 2 (manifest registration, after commit 1 so that the derived manifests can see the test): `tests/fork-sources.txt` (test and this report) and `tests/all-sources.txt` (test). Their gate results are in the self-check below.

## Self-check

Completion check: `submit_check.py` run as `submit-check-1.log` (EXIT=1) before commit 2. Its items:

- **commits on the branch**: PASS (commit 1 `e5773c6eaa`).
- **working tree clean**: FAIL before commit 2 only, because the two manifest files were still uncommitted. Commit 2 commits them (see "Files committed").
- **report**: PASS. **tests added or changed**: PASS. **new test files registered in tests/CMakeLists.txt**: PASS.
- **gates (feature_check.py)**: FAIL, and environmental. `submit_check.py` runs the unmodified `feature_check.py`, which tries to create `<worktree>-feature-check` outside my write-jail. It fails with the same PermissionError as command 17 (`gates-1.log`). The gate content passes with the log folder redirected (`gates-4-precommit.log`, `gates-5-postcommit1.log`, both EXIT=0). I did not change the gate.

Answers to the ask items:

1. **Fails with the change reverted, passes with it.** Yes. Revert: `test-3-reverted-red.log`, EXIT=1, FAIL `zeroFrameBlockHoldsLastOutput`. With the fix: `test-2-green.log` and `test-4-fixed-green.log`, both EXIT=0, 5 of 5 passed. The revert and re-apply are steps 7-11 of the command table, with the patch in `fix.patch`.
2. **Deterministic.** The test has no sleeps, no wall-clock reads and no threads. Its input is a fixed buffer of 0.25 samples, and the frame count is passed explicitly in `AudioBus{busData, 1, frames}` (0 for the empty block). `Engine::init(true)` only sets up the headless Dummy audio device used by the other Engine tests. The expected value is `0.5 + sqrt(0.125)` from the default base (0.5), amount (1) and abs model. The result does not depend on timing or order.
3. **Fix at the cause; callers and siblings.** The cause is the single division at `PeakControllerEffect.cpp:138`. The guard at `:100-104` runs before the mute step and the RMS, so it covers both the abs and non-abs branches. Callers that reach `processImpl`: `AudioPlugin.h:184` (`playImpl`) and `:282-284` (`processAudioBuffer`), both through the router (`AudioPorts.h:175-211`). `processImpl` returns `Continue` for an empty block, which is the same status these paths already handle. `lastSample()` (`PeakControllerEffect.h:26-29`) is the public read; I did not audit its consumers.
4. **No collateral.** The guard applies only when `frames() == 0`. Non-empty blocks take the old path unchanged, which `nonEmptyBlockMeasuresSignal` checks. The change does not touch save or load, the control surface, or any limit, and no saved project changes. The plugin module builds with the product's `-Werror` flags (`build-10-plugin-module-noccache.log`, EXIT=0).
5. **Verdict.** REAL, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed. The NaN is real, but on Qt 6.12 it is clamped rather than propagated, so the observable effect is the spurious 0.0 output described above.

Realtime: the change adds one compare-and-return on the audio thread. It allocates nothing, takes no lock, and logs nothing.
