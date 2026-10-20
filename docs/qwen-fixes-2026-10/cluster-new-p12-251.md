# Cluster new-p12-251: SlicerT fade timing with playback speed

FALSE-POSITIVE

The finding claims `framesLeft * speedRatio` is wrong because `framesLeft` is a source-sample
distance while `i`/`fadeOutFrames` are output frames, and proposes `framesLeft / speedRatio`.
`speedRatio` is the output/input ratio, not input/output, so the multiplication is the correct
conversion and the proposed division is the regression:

- include/AudioResampler.h:96-116 - "ratio is output/input"; `src_ratio = 2.0` consumes 4096 input
  frames and generates 8192 output frames. The convention is pinned on the linked library by
  tests/src/core/AudioResamplerRatioTest.cpp (passes, EXIT=0 in logs/test-family-committed2.log).
- src/core/Sample.cpp:125 - `state->m_resampler.setRatio(sampleRateRatio * freqRatio * ratio)`, so
  one source frame yields `speedRatio` output frames.
- plugins/SlicerT/SlicerT.cpp:347-348 - `getMidi()` computes the slice's length as
  `m_originalSample.sampleSize() * speedRatio`: the OUTPUT length of the slice is the source
  length times the speed ratio.
- plugins/SlicerT/SlicerT.cpp:158 - the product line, unchanged on this branch:
  `framesLeft * speedRatio - static_cast<int>(i)` is the number of output frames left at output
  frame `i`, so the fade belongs in the last `fadeOutFrames` OUTPUT frames.

With the proposed division the fade ends a quarter of the way before the slice's end (measured:
the slice's audible output shrank from 42751 to 38779 output frames, logs/test-red-committed.log),
which is the audible defect the finding claims to repair.

Round 2 (Luna's REWORK) asked for a regression that really exercises a speed ratio. The round-2
test drove the speed through the track's pitch model, which is clamped to +/-100 cents
(src/tracks/InstrumentTrack.cpp:68, include/Pitch.h:35-36) and left BPM sync off, so it never
reached speed 2 and passed with the divide formula too. That test is gone. The test below drives
the real speed ratio through the plugin's own BPM sync (plugins/SlicerT/SlicerT.cpp:104-106): the
original BPM is set to twice the song tempo, so `speedRatio` is exactly 2.

## What the regression does

tests/src/core/SlicerTPlayheadTest.cpp `fadeEnvelopeIsTimedInOutputFrames`:

- fixture: a constant-amplitude 16-bit mono WAV written at the engine's own output rate, half a
  second long - the SlicerT fade is then the only thing that changes the level, and the resampler
  ratio is exactly the speed ratio (2).
- speed: BPM sync on, the plugin's original BPM set to twice the song tempo => speedRatio 2.
- fade: the plugin's fade length set to 100 ms (a fifth of the slice).
- measurement: the rendered frames give the slice's audible span, the level before the fade, and
  the fade's midpoint (the half-level crossing of the cosine ramp). All three are compared with
  the output geometry the ratio convention predicts.

Measured with the branch's multiplication formula (the engine ran at 44100 Hz, the slice's source
was 22050 frames, so the expected output is 44100 frames): the slice sounded for 42751 frames
(0.97 of the expected output - the rest is the tail below a fifth of the level) and the fade's
midpoint was at 41984 against an expected 42151 (167 frames, the sample staging's own granularity
at include/Sample.h:72). With the divide formula the same two measurements are 38779 and 34549.

## Evidence table

| Check | Command | Result |
| --- | --- | --- |
| mutant patch (the finding's proposal) | `git diff -- plugins/SlicerT/SlicerT.cpp > logs/fix.patch` | the patch is `framesLeft * speedRatio` -> `framesLeft / speedRatio` |
| RED: the finding's proposed divide formula | `git apply logs/fix.patch` (EXIT=0), `cmake --build build --target slicert` (logs/build-red-committed.log, EXIT=0), `ctest -R '^SlicerTPlayheadTest$' --output-on-failure` (logs/test-red-committed.log) | ctest **EXIT=8** - FAIL: "the slice sounded for 38779 output frames, expected about 44100" |
| RED: the fade position alone | the audible-span assertion temporarily relaxed, `./SlicerTPlayheadTest fadeEnvelopeIsTimedInOutputFrames` (logs/test-red-final2.log) | EXIT=1 - FAIL: "the fade midpoint is at 34549, expected about 42151" (the window is 882 frames) |
| restore the product line | `git apply -R logs/fix.patch` (EXIT=0; `git diff -- plugins/SlicerT/SlicerT.cpp` empty again) | EXIT=0 |
| GREEN: the product line on the branch | `cmake --build build --target slicert SlicerTPlayheadTest zene` (logs/build-green-committed.log, EXIT=0), `ctest -R '^SlicerTPlayheadTest$' --output-on-failure` (logs/test-green-committed.log) | build EXIT=0, ctest **EXIT=0** (1/1) |
| affected family | `ctest -R 'SlicerT' --output-on-failure` (logs/test-family-committed.log) | **EXIT=0**, 3/3 passed (SlicerTPlayheadTest, SlicerTWaveformTest, SlicerTZoomTest) |
| ratio convention + slicing neighbours | `ctest -R 'AudioResampler|AudioEditing' --output-on-failure` (logs/test-family-committed2.log) | **EXIT=0**, 3/3 passed (AudioResamplerRatioTest, AudioEditingTest, ControlAudioEditing) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (logs/gate-feature-check-committed.log) | **EXIT=0**, RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length "no new item in your files") |
| complexity ratchet | `bash tests/complexity-gate.sh --check` (logs/gate-complexity-committed.log) | EXIT=1 - 6 REGRESSION entries, all in files this change does not touch; the list is byte-identical to the same gate run on the branch without this change (logs/gate-complexity-base.log) |
| file-length ratchet | `bash tests/file-length-gate.sh --check` (logs/gate-file-length-committed.log) | EXIT=1 - 14 REGRESSION entries, all in files this change does not touch; the list is byte-identical to the same gate run without this change (logs/gate-file-length-base.log) |

Files committed with this report:

- `tests/src/core/SlicerTPlayheadTest.cpp` - the regression (the round-2 test that never reached
  speed 2 is replaced; `prepareSlicerTrack` now takes the fixture path so the test can supply its
  own constant WAV; 488 lines, under the 500-line gate).
- `docs/qwen-fixes-2026-10/cluster-new-p12-251.md` - this report.

No product file is changed: `plugins/SlicerT/SlicerT.cpp` is byte-identical to the branch HEAD
(`git diff -- plugins/SlicerT/SlicerT.cpp` is empty), and the branch's own line is the
multiplication the finding asks to replace.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  The finding's proposal IS the product change to revert here, so the mutant is that proposal:
  `framesLeft * speedRatio` -> `framesLeft / speedRatio` (logs/fix.patch). With the mutant built
  (`cmake --build build --target slicert`, EXIT=0) the test FAILS: `ctest -R '^SlicerTPlayheadTest$'`
  EXIT=8, "the slice sounded for 38779 output frames, expected about 44100" (logs/test-red-committed.log);
  with the audible-span assertion relaxed it fails on the fade position instead, "the fade midpoint
  is at 34549, expected about 42151" (logs/test-red-final2.log). After `git apply -R logs/fix.patch`
  (EXIT=0) and a rebuild (EXIT=0) the same ctest command is EXIT=0 (logs/test-green-committed.log).
  The branch's own line is the multiplication, so the branch state IS the GREEN state and no
  product change is retained.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It drives the engine's own
  render loop (`AudioEngine::renderNextPeriod()`, the seam the other cases in this file use) for a
  fixed number of periods, with a fixed constant-amplitude WAV written at the engine's output rate
  and a fixed speed ratio (2) and fade length (100 ms) set through the plugin's own parameter
  models (`Instrument::parameterModel`, the public parameter API). The values it reads are the
  rendered sample frames; nothing is timed, slept on or raced.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked
  and stays consistent.** Nothing in the product changed, so the check is on the evidence: the
  ratio convention (include/AudioResampler.h:96-116, measured by tests/src/core/AudioResamplerRatioTest.cpp),
  the resampler call that consumes the ratio (src/core/Sample.cpp:125), the sibling that computes
  the slice's output length with the same ratio (plugins/SlicerT/SlicerT.cpp:347-348) and the fade
  line itself (plugins/SlicerT/SlicerT.cpp:158) all agree that output = input * speedRatio. The
  fade's only sibling, `m_fadeOutFrames` (plugins/SlicerT/SlicerT.cpp:154-155), is measured in
  output frames at the engine's output rate and is unchanged.
- **No collateral damage: old projects and saved data still load as before, and any limit or check
  you added does not hit other callers that legitimately exceed it.** No product code changed, so no
  saved data, project or plugin setting changes meaning. The test's own checks are local to the
  test (it reads the plugin's parameters through `Instrument::parameterModel`; it adds no limit to
  the product). The only shared helper touched is the test-local `prepareSlicerTrack`, whose
  signature now takes the fixture path; the two other callers pass the same tone WAV they used
  before.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it.** See the top of this report: include/AudioResampler.h:96-116 (the ratio
  is output/input), src/core/Sample.cpp:125, plugins/SlicerT/SlicerT.cpp:347-348 and
  plugins/SlicerT/SlicerT.cpp:158. The finding's own premise (that `framesLeft` is in source
  frames while `i` is in output frames) is correct; its proposed conversion is not.
