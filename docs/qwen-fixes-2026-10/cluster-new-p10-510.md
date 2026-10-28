FIXED

new-p10-510 (BUG 19): non-finite `framesPerTick` reaches the automation knot builder. Verdict FIXED is a defensive fix at the cause. The defect is real in the code (see Decision), but I found no current producer that yields a non-finite `framesPerTick`; see "Reachability" below.

## Decision

REAL (code-level). `AutomationClip::writeBlockRamp()` guards `framesPerTick <= 0.0` only (`src/core/AutomationClip.cpp:1078` before the fix). A NaN passes that test. The tick loop at `src/core/AutomationClip.cpp:1125-1157` then never reaches its `knotFrame > frames` break, because every comparison with NaN is false, and `addRampKnot()` (`src/core/AutomationClip.cpp:1058`) converts NaN positions with `static_cast<int>( std::floor(...) )`, which is undefined behaviour. The RED run below shows the hang directly.

## Reachability

- The tempo model is bounded to `MinTempo`..`MaxTempo` = 10..999 (`src/core/Song.cpp:122`, `include/Song.h:78-80`).
- `Engine::updateFramesPerTick()` / `framesPerTick(rate)` divide by that tempo (`src/core/Engine.cpp:137-147`); `sample_rate_t` is `std::uint32_t` (`include/LmmsTypes.h:42`), so no NaN source appears there.
- `Engine::updateFramesPerTickForTempo(bpm)` (`src/core/Engine.cpp:153-162`) is fed `max(1, floor(tempo))` by Link (`src/core/LinkAudioFollower.cpp:251`) and by the tempo map after `validEvent()`.
- Conclusion: no current path I found produces NaN or infinity. The guard is defensive. The regression test drives the function with those values directly.

## Fix

- `src/core/AutomationClip.cpp:1078-1080`: `writeBlockRamp()` now returns before building knots when `!std::isfinite( framesPerTick )`, in addition to the existing `<= 0.0` check.
- `src/core/Song.cpp:1582`: the caller gate in `Song::buildAutomationRamps()` (the sibling check) refuses the same non-finite value before it publishes anything. Without this, a NaN would reach `writeBlockRamp()`, return an empty ramp, and the empty ramp would be published to the targets, unlike the `<= 0` path, which publishes nothing.
- Behaviour change, limited to non-finite input: `+inf` used to yield a two-knot ramp (value held at the frame-0 value); it now yields no ramp, as the finding's contract ("require finite") asks. No caller passes `+inf` (see Reachability).
- Not changed: other `framesPerTick` readers in `src/core/Song.cpp` (around 473, 565-637, 1255, 1539, and `AutomationLatency::shiftBlock` at 1613). They were not audited for NaN.

## Regression test

`tests/src/core/AutomationPreFirstValueTest.cpp`: `aNonFiniteFramesPerTickBuildsNoRamp` (data rows `infinite`, `nan`; lines ~533-557). Each row first checks a finite tick builds knots (positive control, so an empty result cannot come from a clip without automation), then calls `writeBlockRamp()` with the non-finite tick and requires `knotCount() == 0` and `refusals() == 0`. The file is product-new (already in `tests/fork-sources.txt:3063`), so no fork-sources entry was added; the inherited files changed are registered in `tests/upstream-modifications.txt` (two lines appended, same commit).

## Commands and results

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-510.r1/logs/`.

| Step | Command (cwd noted) | EXIT | Log |
|---|---|---|---|
| build (first try) | `cmake --build . --target AutomationPreFirstValueTest -j4` (build/) | 2 | build-1.log: ccache could not create its temp file (permission denied) |
| build | `CCACHE_DISABLE=1 cmake --build . --target AutomationPreFirstValueTest -j4` (build/) | 0 | build-2.log |
| **RED** | `ctest -R '^AutomationPreFirstValueTest$' --timeout 60 --output-on-failure` (build/tests/) | **8** | test-red-1.log: `aNonFiniteFramesPerTickBuildsNoRamp(infinite)` FAIL, `Actual (ramp.knotCount()): 2`, `Expected (0): 0`; the `nan` row then never returns, ctest Timeout at 60 s |
| fix (rebuild) | `CCACHE_DISABLE=1 cmake --build . --target zene AutomationPreFirstValueTest -j4` (build/) | 0 | build-3.log |
| **GREEN** | `ctest -R '^AutomationPreFirstValueTest$' --timeout 300 --output-on-failure` (build/tests/) | **0** | test-green-1.log, 100 % passed |
| GREEN detail | `./AutomationPreFirstValueTest` (build/tests/) | 0 | test-green-2.log: `PASS aNonFiniteFramesPerTickBuildsNoRamp(infinite)`, `PASS ...(nan)`, `Totals: 12 passed, 0 failed` |

Step 4b, the mutation proof, with the product files only:

| Step | Command | EXIT | Log |
|---|---|---|---|
| save fix | `git diff -- src/core/AutomationClip.cpp src/core/Song.cpp > .../logs/fix.patch` | 0 | fix.patch |
| revert | `git apply -R .../logs/fix.patch` | 0 | |
| rebuild | `CCACHE_DISABLE=1 cmake --build . --target zene AutomationPreFirstValueTest -j4` (build/) | 0 | build-4-reverted.log |
| **RED (4b)** | `ctest -R '^AutomationPreFirstValueTest$' --timeout 60 --output-on-failure` (build/tests/) | **8** | test-red-2.log: same `infinite` FAIL and the `nan` Timeout |
| restore | `git apply .../logs/fix.patch` | 0 | (a preceding `git apply --check -R` on the already-reverted tree returned 1, as expected, and changed nothing) |
| rebuild | `CCACHE_DISABLE=1 cmake --build . --target zene AutomationPreFirstValueTest -j4` (build/) | 0 | build-5-reapplied.log |
| **GREEN (4b)** | `ctest -R '^AutomationPreFirstValueTest$' --timeout 300 ...` (build/tests/) | **0** | test-green-3.log |

Affected family (`ctest -R`, from build/tests/, `--timeout 600`): build of `SampleAccurateAutomationTest AutomationClipDisconnectTest AutomationThinTest AutomationWriteToEndTest` EXIT 0 (build-6-family.log).

| Test | Result |
|---|---|
| AutomationPreFirstValueTest | Passed (12 slot runs) |
| SampleAccurateAutomationTest | Passed |
| AutomationClipDisconnectTest | Passed |
| AutomationThinTest | Passed |
| AutomationWriteToEndTest | Passed (after the plugin below) |

Family run 1 (test-family-1.log) gave EXIT 8: `AutomationWriteToEndTest` failed at `AutomationWriteToEndTest.cpp:109` (`!parameter.isEmpty()`), with `The plugin "tripleoscillator" wasn't found`. The targeted build had not produced that plugin: `cmake --build . --target TripleOscillator` (EXIT 2, build-7-plugin.log) was the wrong target name. `cmake --build . --target tripleoscillator -j4` (EXIT 0, build-8-plugin.log) produced `plugins/libtripleoscillator.so`. Family run 2 (test-family-2.log): EXIT 0, 5 of 5 passed. The failure was a missing prerequisite, and it occurred before any changed code ran.

Gates. `python3 .../qwen/feature_check.py <worktree>` exited 1 with `PermissionError` before any gate ran: it writes its logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail (gates-1.log). I ran the same gate commands from the worktree, each logging to the log folder:

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets exit 1 on items in files this branch does not touch (WasmSandbox, XyPad, DawProjectReadTracks, Vst3Host, ControlCommands*, and new test files). None of the reported items names `AutomationClip.cpp`, `Song.cpp` or `AutomationPreFirstValueTest.cpp` (grep of gate-complexity.log and gate-file-length.log). By feature_check's rule (fail only on new items in changed files) both rows pass, but feature_check itself did not produce that verdict, so that is my reading, not a script result.

## Self-check

- Decision with evidence: yes (`src/core/AutomationClip.cpp:1078-1080, 1125-1157, 1058`; reachability above).
- Regression fails without the fix and passes with it: yes, both directions (RED 1, RED 2 / GREEN 1, GREEN 3). The `infinite` row gives a clean assertion failure. The `nan` row hangs without the fix: that is the bug's symptom, observed through ctest's timeout. It is not an assertion, and the test contains no timing.
- No wall-clock or sleeps in the test: yes; the only timeout is in the shell/ctest command.
- Complete at the cause, and siblings checked: yes for the two callers of this guard (`writeBlockRamp` and the `Song::buildAutomationRamps` gate). `addRampKnot` and the other `framesPerTick` readers are not changed (listed above).
- Collateral: finite values unchanged (family 5/5). Only the non-finite path changes, including the `+inf` behaviour noted above.
- Realtime: the change adds one `std::isfinite` call to an existing guard. No allocation, lock, log or wait.
- Undo and commands: not touched; no control command added; no snapshot change.
- Registration and gates: test file is product-new and already in fork-sources; inherited changed files registered in `tests/upstream-modifications.txt`. Gates as listed above.
- Environment deviations, all stated above: ccache disabled for three build commands (permission error on ccache's temp dir); feature_check.py unable to write its logs in the jail, so the gates were run by hand; the `tripleoscillator` plugin built to satisfy a test prerequisite.
- Not covered by a test: the `Song.cpp:1582` guard. A NaN cannot be set into `Engine::s_framesPerTick` from a test (no public setter with a float input; only the int-bpm path, which has no NaN source), so the caller guard has no regression of its own.
