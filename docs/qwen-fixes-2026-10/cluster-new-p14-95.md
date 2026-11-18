FIXED

# Cluster new-p14-95: DrumSynth converts large envelope levels to `short` before clamping

## Finding

`LoudestEnv()` (`src/core/DrumSynth.cpp:124-138`) returns the largest enabled section level squared, as a float. For `[Tone] Level=50000` that is `2.5e9`, which is outside the `short` range. Before the fix, the distortion branch of `GetDSFileSamples` converted that float straight to `short` (`src/core/DrumSynth.cpp:619`, original line) and only then capped the result at 32700 (`src/core/DrumSynth.cpp:620`, original line). A float-to-integer conversion whose value is out of range is undefined behaviour.

`LoudestEnv()` has one caller, `src/core/DrumSynth.cpp:619` (grep for `LoudestEnv` in `src/core`). Its result only sets `clippoint` at line 623: the initial `DAtten` is overwritten at line 626 (`DAtten = std::exp2(2.0 * distortionBits)`) before `DAtten` is used as a divisor at line 980. So the clamp affects only the clip point, and the clip point is capped at 32700.

The finding's `line 539` no longer points at this code. Line 539 is now the overtone `F1` read; the real cast is the distortion block at 617-623.

## Verdict reasoning

REAL. The defect is reproducible on this branch and was reproduced before the fix:

- Unfixed code, `[Tone] Level=50000` with `[Distortion] On=1`: rendered peak `0`, where the contract is `32700` (`test-red-1.log`, line `Actual (peak) : 0`). On this x86-64 build the out-of-range float-to-short conversion produced 0, which silences the tone. Other compilers or flags may produce a different undefined value.

## Fix

`src/core/DrumSynth.cpp:619-622`: clamp `LoudestEnv()` to the short range `[0, 32767]` in float before the cast. In-range values are unchanged, so integer truncation is kept; out-of-range values saturate, and the existing 32700 cap then applies.

```
const float loudestEnv = std::clamp(LoudestEnv(), 0.f, 32767.f);
DAtten = DGain * static_cast<short>(loudestEnv);
```

Registration: `tests/upstream-modifications.txt` (last line) records the change for the inherited file `src/core/DrumSynth.cpp`. `DrumSynthLevelTest.cpp` is already in `tests/fork-sources.txt:2423`.

## Regression tests

Both are in `tests/src/core/DrumSynthLevelTest.cpp` (existing fixture):

- `largeToneLevelWithDistortionClampsBeforeShortCast` (line 58): `Level=50000` with distortion on. Asserts the rendered peak is exactly 32700. RED before the fix, GREEN after.
- `inRangeToneLevelWithDistortionKeepsClipPoint` (line 101): `Level=100` with distortion on (square 10000, in range). Asserts the peak is exactly 10000. Passes both before and after the fix; it guards that in-range output is unchanged.

## Evidence table

All commands run from the worktree or `build/tests` as noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-95.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build attempt (ccache temp dir outside write-jail) | `cmake --build build --target DrumSynthLevelTest -j4` | 2 | `build-red-1.log` |
| Build, ccache disabled | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthLevelTest -j4` | 0 | `build-red-2.log` |
| RED (unfixed, clamp test only) | `cd build/tests && ctest -R '^DrumSynthLevelTest$' --output-on-failure` | 8 | `test-red-1.log` |
| GREEN family (fix, before guard test added) | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthLevelTest DrumSynthTest SampleBufferResampleTest -j4` then `ctest -R '^(DrumSynthLevelTest\|DrumSynthTest\|SampleBufferResampleTest)$' --output-on-failure` | 0 / 0 | `build-green-1.log`, `test-green-1.log` (3 of 3 passed) |
| GREEN, DrumSynthLevelTest with guard test | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthLevelTest -j4` then `ctest -R '^DrumSynthLevelTest$' --output-on-failure` | 0 / 0 | `build-green-2.log`, `test-green-2.log` (1 of 1 passed) |
| Proof, fix saved | `git diff -- src/core/DrumSynth.cpp > .../logs/fix.patch` | 0 | — |
| Proof, fix reverted | `git apply -R .../logs/fix.patch` | 0 | — |
| Proof, rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthLevelTest -j4` | 0 | `build-proof-unfixed.log` |
| Proof, unfixed test run | `ctest -R '^DrumSynthLevelTest$' --output-on-failure` | 8 | `test-proof-unfixed.log` (large-level test FAIL, peak 0 vs 32700; in-range guard PASS; 4 passed, 1 failed) |
| Proof, fix re-applied | `git apply .../logs/fix.patch` | 0 | — |
| Proof, rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthLevelTest -j4` | 0 | `build-proof-fixed.log` |
| Proof, fixed test run | `ctest -R '^DrumSynthLevelTest$' --output-on-failure` | 0 | `test-proof-fixed.log` (1 of 1 passed) |
| Family after proof: build | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthLevelTest DrumSynthTest SampleBufferResampleTest -j4` | 0 | `build-family-1.log` |
| Family after proof: test | `ctest -R '^(DrumSynthLevelTest\|DrumSynthTest\|SampleBufferResampleTest)$' --output-on-failure` | 0 | `test-family-1.log` (3 of 3 passed: #111 DrumSynthLevelTest, #261 SampleBufferResampleTest, #329 DrumSynthTest) |

The regression test fails on the unfixed code (RED, EXIT 8) and passes after the fix (GREEN, EXIT 0). The in-range guard passes in both states.

### Gates

`feature_check.py` could not run as specified in this write-jail. `python3 .../qwen/feature_check.py <worktree>` exited 1 with `PermissionError` (`feature_check.py:71`), because it creates `<worktree>-feature-check/` next to the worktree, which is outside the allowed write area (`gate-feature-check-1.log`). I did not edit the script. I ran each of its gate commands from the worktree, with the same arguments, and compared the ratchet output by hand.

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-2.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-2.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-2.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-2.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-2.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` (7 REGRESSION lines, none in a changed file) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log` (22 REGRESSION lines, none in a changed file) |

The complexity and file-length gates exit 1 because of items already in the train, on files I did not change. The script's rule counts only regressions whose file is in the changed set, and no `REGRESSION` line names `DrumSynth.cpp`, `DrumSynthLevelTest.cpp` or `upstream-modifications.txt` (`grep` exit 1). So by the script's rule both ratchet rows pass. The new test functions are not reported as new over-limit functions.

## Behaviour change

Only inputs that previously hit undefined behaviour change. With distortion on, a Tone `Level` above 181 (square above 32767) used to convert an out-of-range value; it now saturates at the clip point, which is capped at 32700. Ordinary levels (square at most 32767) are byte-identical in output; the in-range guard shows this for 10000.

## Not changed: sibling cast on line 623

`clippoint = DAtten > 32700 ? 32700 : static_cast<short>(DAtten);` is unchanged. It is undefined only if `DAtten` is NaN. Reasoning (not run): `DGain = fastPow10f(0.05 * Level)` (`DrumSynth.cpp:467`, `include/lmms_math.h:335-339`) overflows to infinity for a `[General]` Level above roughly 770. With `DGain` infinite and Tone disabled (`LoudestEnv()` = 0), `DAtten = inf * 0` is NaN. I did not write a regression for it: I could not get a red test, because on this build the NaN conversion also yields 0, so the output does not change with the fix. Per the rules, I left it unfixed and report it for a separate finding.

## Commit

Files committed: `src/core/DrumSynth.cpp`, `tests/src/core/DrumSynthLevelTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p14-95.md`.

## Self-check

`submit_check.py` (log `submit-check-1.log`, EXIT 1) before this commit reported: commits on the branch, clean tree and tests-changed FAIL only because nothing was committed yet; the `gates (feature_check.py)` row FAILs because the script cannot create its log folder outside the write-jail (see Gates above). The gate commands were run individually and are listed above.

- **Regression fails with the change reverted and passes with it.** Yes. Reverted with `git apply -R .../logs/fix.patch`: `build-proof-unfixed.log` EXIT 0, `test-proof-unfixed.log` EXIT 8, the large-level test reports `Actual (peak) : 0`, `Expected (32700): 32700`. Re-applied with `git apply`: `build-proof-fixed.log` EXIT 0, `test-proof-fixed.log` EXIT 0. The in-range guard passes in both states, as required.
- **Deterministic.** The test writes a fixed `.ds` text into a `QTemporaryDir`, renders it synchronously through `DrumSynth::GetDSFileSamples`, and compares exact integer values. It uses no clock, sleep, thread or race. The only state is the file-static parse state the existing `largeToneLevelDoesNotWrapBeforeSquaring` test already uses.
- **At the cause, complete for this finding.** `LoudestEnv()` has a single caller (`DrumSynth.cpp:619` before the edit, `:621` after). Its result is used only by `clippoint` (`:623`), because the initial `DAtten` is overwritten at `:626` before it is used as the divisor at `:980`. The clamp is therefore the only place the value enters the output. Sibling: the `static_cast<short>(DAtten)` at `:623` is undefined only for a NaN `DAtten`, which needs an infinite `DGain`. This is NOT fixed here. It is reported above as a separate, unverified residual, because I could not write a regression that fails without a fix for it.
- **No collateral damage.** The clamp is the identity on `[0, 32767]`, so every in-range Tone level, with or without distortion, produces the same output (guarded by `inRangeToneLevelWithDistortionKeepsClipPoint`, which passes before and after). Only the out-of-range inputs, where the old conversion was undefined, change. The clamp rejects nothing and blocks no caller. The change is on the file-load path (`GetDSFileSamples`), not on the audio thread: no allocation, lock, log or Qt call was added. No control command was added, and saved projects are not touched. The `.ds` parser reads the same keys as before.
- **Verdict basis.** REAL, so no already-fixed or false-positive citation is needed. Verified on this branch: `DrumSynth.cpp:619` (original line) is the cast. The finding's `line 539` is the overtone `F1` read on this tree and is not the defect.

Environment notes. The first build failed on a ccache temp directory outside the write-jail (`build-red-1.log`, EXIT 2). Every later build used `CCACHE_DISABLE=1` for the same command; no source or build setting was changed. The `feature_check.py` gate row cannot be satisfied from inside this write-jail, and I did not edit the shared script.
