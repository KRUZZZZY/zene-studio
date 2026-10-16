# Cluster new-p12-205 — destructive sample edits reset clip playback settings

**Verdict: FIXED**

## The finding

`SampleClip::replaceSample` replaced the clip's whole `Sample` with
`Sample(std::move(sb))`, and that constructor starts every playback setting at its
default. A crop, a trim, a head/tail frame edit or a chain print therefore made a
reversed, pitched or gained clip play forward at base pitch and unity gain.

## Evidence (file:line)

- `src/core/SampleClip.cpp:356` — `SampleClip::replaceSample`, the one seam the sample
  tools use (its upstream-modifications row calls it "one call for the sample tools").
- `src/core/Sample.cpp:40-47` — `Sample::Sample(std::shared_ptr<const SampleBuffer>)`
  initialises only `m_buffer` and the four frame points; `m_amplification`
  (`1.0f`), `m_frequency` (`DefaultBaseFreq`) and `m_reversed` (`false`) keep the
  in-class defaults of `include/Sample.h:135-137`.
- `src/core/SampleClip.cpp:362` (before the fix) — `m_sample = Sample(std::move(sb));`
  was the reset: a default-constructed `Sample` took over the clip's settings.
- User-visible paths that reach it: `src/core/ControlCommandsSampleTools.cpp:75`
  (`swapIn` → `sample.crop`, `sample.make_unique`), `src/core/SampleFrameEdits.cpp:85`
  (`authorWindow` — the head/tail frame edits, which pass the clip's OWN buffer), and
  `src/core/ControlCommandsClipProcess.cpp:136` (`clip.process`).
- Who sets those settings: `src/gui/clips/SampleClipView.cpp:514` (the reverse
  toggle), `src/gui/shell/InspectorPanel.cpp:405` (the gain binding, reading
  `sample().amplification()`), and the `reversed` attribute in
  `SampleClip::loadSettings` (`src/core/SampleClip.cpp:880-887`).

## The fix

`src/core/SampleClip.cpp:358-374` — `replaceSample` reads `amplification`,
`frequency` and `reversed` off the old sample and stores them on the new one, inside
the existing `requestChangesGuard()` block, so the swap and the settings still change
under one guard (no audio period sees a half-updated clip) and nothing new is
allocated, locked or logged on the audio thread. Only `replaceSample` changes:
`SampleClip::setSampleBuffer` (the recording-take path) and `Sample::setBuffer` (the
pooled play-handle voice) keep their historical behaviour.

Note for the record: `clip.process` bakes the clip's RAW source frames
(`ControlCommandsClipProcess.cpp:124`, `prepared()` reads `sample().data()` forward and
ignores the direction flag), so for a reversed clip the printed source is forward in
both the old and the new behaviour; the clip's own settings now simply survive the
print instead of being reset. That is the pre-existing contract of the process command,
not a change made here.

## Regression

`tests/src/core/ControlSampleOperatorTest.cpp` —
`aSourceEditKeepsTheClipPlaybackSettings()`: a generated clip gets an authored window
and the three playback settings a user would set, `sample.crop` replaces the source,
the frames are asserted to be cropped to exactly the window (`after.frames.size() ==
to - from`, first and last frame equal to the window's edges) so the edit provably ran,
and then `reversed`, `amplification` and `frequency` must be unchanged.

## Commands and exit codes

| # | Command (from the worktree root unless noted) | EXIT |
|---|---|---|
| RED (fix absent, new test present) | `cmake --build build --target ControlSampleOperatorTest` (log `build-3.log`) | 0 |
| RED | `ctest -R '^ControlSampleOperatorTest$' --output-on-failure` in `build/tests` (log `test-4-without-fix.log`) | **8** — `FAIL! … 'sampleClip->sample().reversed()' returned FALSE (the edit reset the clip's reversed flag)`; `Totals: 12 passed, 1 failed` |
| fix applied | `cmake --build build --target zene ControlSampleOperatorTest` (log `build-5-green.log`) | 0 |
| GREEN | `ctest -R '^ControlSampleOperatorTest$' --output-on-failure` in `build/tests` (log `test-5-green.log`) | **0** — `1/1 Test #68: ControlSampleOperatorTest … Passed` |
| family | `ctest -R '^(SampleClipWindowTest\|SampleClipRubberBandTest\|SampleClipStretchTest\|SampleClipWarpTest\|DeviceToolsChainTest\|AudioAutomationToolsTest\|ClipSerialisationTest\|TakeLaneCompTest\|ControlSampleOperatorTest)$' --output-on-failure` in `build/tests` (log `test-6-family.log`) | **0** — `100% tests passed out of 9` |

Proof the test catches the bug (step 4b):

- `git diff -- src/core/SampleClip.cpp > <LOGS>/fix.patch && git apply -R <LOGS>/fix.patch`
  → `git apply -R` EXIT **0**; `cmake --build build --target zene ControlSampleOperatorTest`
  (log `build-4-without-fix.log`) EXIT **0**; `ctest -R '^ControlSampleOperatorTest$'`
  (log `test-4-without-fix.log`) EXIT **8**, same assertion — RED without the fix.
- `git apply <LOGS>/fix.patch` EXIT **0**; rebuild EXIT **0**; `ctest -R '^ControlSampleOperatorTest$'`
  (log `test-5-green.log`) EXIT **0** — GREEN with the fix.

## Gates

| Gate | Before this change (HEAD tree, my files reverted) | After this change |
|---|---|---|
| `bash tests/complexity-gate.sh --check` | EXIT **1** (`gate-complexity-baseline.log`) | EXIT **1** (`gate-complexity-2.log`) |
| `bash tests/file-length-gate.sh --check` | EXIT **1** (`gate-file-length-baseline.log`) | EXIT **1** (`gate-file-length-2.log`) |

Both gates already fail on this branch, before this change, and the REGRESSION lists are
byte-identical before and after (`diff` of the two lists is empty, logged as "unchanged").
Every entry names a file this cluster did not touch — e.g. `tests/src/gui/XyPadPairTest.cpp`
(CCN 16), `src/core/DawProjectReadTracks.cpp` (CCN 13),
`src/core/ControlCommandsMixerTools.cpp` (CCN 12) and thirteen test files over the 500-line
limit. No baseline or limit was re-anchored, and neither `src/core/SampleClip.cpp` nor
`tests/src/core/ControlSampleOperatorTest.cpp` appears in either REGRESSION list, so this
change adds no new gate entry.

## Files

- `src/core/SampleClip.cpp` — the fix (product, inherited → `tests/upstream-modifications.txt`)
- `tests/src/core/ControlSampleOperatorTest.cpp` — the regression (fork-owned, already in
  `tests/fork-sources.txt:2255`)
- `tests/upstream-modifications.txt` — the declared reason for the inherited file
- `docs/qwen-fixes-2026-10/cluster-new-p12-205.md` — this report
