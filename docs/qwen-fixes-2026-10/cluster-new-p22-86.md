FIXED

# Cluster new-p22-86: `vca.edit_trim` startTimeOffset underflow

## Independent QA finding

The builder's FALSE-POSITIVE verdict was incorrect. Negative offsets are valid, but the signed subtraction can exceed the `tick_t` range for an offset loaded from a project. `SampleClip::loadSettings` and `MidiClip::loadSettings` read `off` with `QString::toInt()` and do not constrain it to `MaxSongLength` (`src/core/SampleClip.cpp:1020`, `src/tracks/MidiClip.cpp:565`). With an offset of `INT_MIN`, a rightward head trim of 100 ticks computes `INT_MIN - 100` in `tick_t` at the former `vca.edit_trim` line 237. That is signed overflow. The existing `-100` assertion only proves that representable negative offsets are intended.

The same unchecked head-trim calculation existed in the sibling `clip.trim` path. On this Linux build, the overflow produced a positive value outside its result schema after the clip had already been changed, causing a `Refused` result rather than an atomic validation error.

## Fix

Both trim paths now compute the resulting offset in `qint64`, validate it against the representable `tick_t` bounds before any clip is written, and narrow it only after validation (`src/core/ControlCommandsVcaEditMore.cpp:230-247`, `src/core/ControlCommandsClipTrim.cpp:137-146`). The preflight in `vca.edit_trim` covers every locked clip before the first checkpoint or mutation. `clip.trim` rejects an unrepresentable plan before taking its checkpoint. Normal negative offsets remain valid, including the existing `-100` locked-trim case.

A deterministic regression was added to the existing `ControlVcaLockedEditsTest` (`tests/src/core/ControlVcaLockedEditsTest.cpp:157-189`). It checks that both trim commands refuse the underflow and leave clip positions, lengths and offsets unchanged.

## Evidence and commands

CTest commands below were run from `build/tests`.

| Check | Command | Exit | Result |
| --- | --- | ---: | --- |
| Regression before product fix | `cmake --build build --target zene ControlVcaLockedEditsTest -j4` | 0 | Built test with the new trim regressions. |
| Regression before product fix | `QT_QPA_PLATFORM=offscreen ctest -R ControlVcaLockedEditsTest --output-on-failure` | 8 | The locked trim returned success for the unrepresentable offset; after unlocking the fixture, `clip.trim` returned `Refused` because the wrapped offset violated its result schema. |
| Focused build with fix | `cmake --build build --target zene ControlVcaLockedEditsTest ControlVerbInverseTest LinkedClipEditsTest -j4` | 0 | Built product and all focused test targets. |
| Focused tests with fix | `QT_QPA_PLATFORM=offscreen ctest -R '^(ControlVcaLockedEditsTest|ControlVerbInverseTest|LinkedClipEditsTest)$' --output-on-failure` | 0 | 3/3 CTest targets passed. |
| Reverse only product changes | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-86.r1/logs/qa-products.patch` | 0 | Left the regression tests in place. |
| Build with product changes reversed | `cmake --build build --target zene ControlVcaLockedEditsTest ControlVerbInverseTest LinkedClipEditsTest -j4` | 0 | Rebuilt reversed product and focused targets. |
| Regression with product changes reversed | `QT_QPA_PLATFORM=offscreen ctest -R '^(ControlVcaLockedEditsTest|ControlVerbInverseTest|LinkedClipEditsTest)$' --output-on-failure` | 8 | Both new underflow regressions failed; the other two targets passed. |
| Restore staged product changes | `git checkout -- src/core/ControlCommandsVcaEditMore.cpp src/core/ControlCommandsClipTrim.cpp` | 0 | Restored the exact product patch from the index. |
| Rebuild after restoration | `cmake --build build --target zene ControlVcaLockedEditsTest ControlVerbInverseTest LinkedClipEditsTest -j4` | 0 | Rebuilt product and focused targets. |
| Regression after restoration | `QT_QPA_PLATFORM=offscreen ctest -R '^(ControlVcaLockedEditsTest|ControlVerbInverseTest|LinkedClipEditsTest)$' --output-on-failure` | 0 | 3/3 CTest targets passed. |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p22-86` | 0 | All gates passed; no new complexity or file-length item. |
| Whitespace check | `git diff --check` | 0 | No whitespace errors. |

## Files committed

- `src/core/ControlCommandsVcaEditMore.cpp`
- `src/core/ControlCommandsClipTrim.cpp`
- `tests/src/core/ControlVcaLockedEditsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p22-86.md`

Commit: `qa-fix(new-p22-86): guard trim offset overflow`.

## Self-check

- **Commit and clean worktree:** the QA fix is committed on top of the lane tip; `git status` is clean.
- **Regression red/green/reversed:** the two underflow regressions fail with only the product changes reversed and pass after restoration; see the command table.
- **Deterministic:** the tests use synchronous command calls and fixed clip state; they contain no sleeps, timers or races.
- **Cause and siblings:** both the locked VCA trim and the matching `clip.trim` head-trim plan validate the widened offset before taking a checkpoint or writing clip state.
- **Compatibility and undo:** representable negative offsets remain accepted; the existing focused trim tests pass, and refusals happen before a checkpoint is taken.
- **Registration and gates:** the regression extends an already registered test file; `feature_check.py` passes with no new complexity or file-length item.
- **Realtime and platform review:** these changes are control-command planning code, not audio-thread code; no platform-specific API was introduced.
