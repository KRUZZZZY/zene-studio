# Zene Studio `sample.*` control commands — batch 12

Lane: `cmds2`. Branch `bugs/bunny-cmds2-2026-10-05`, starting at `e006ac817`.
6 findings: **3 FIXED**, **3 FALSE-POSITIVE**, 0 skipped.

### Cluster 2936 — `sample.generate` converts numeric ticks before validating them

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsSample.cpp:195-196` declares `position` and `length`
as `integerProperty(0, MaxSongLength)` / `integerProperty(1, MaxSongLength)`, so the schema
already pins them to integers in range. `src/core/ControlRegistry.cpp:251` runs `validateArgs`
before the handler is ever reached, `src/core/ControlSchema.cpp:50-53` rejects non-integral
values, and `:84-99` enforces the min/max. `parseLevel` re-checks the bounds at `:119`, so no
fractional or out-of-range `tick_t` is ever produced. The reported line 116 is inside that
already-guarded path.

Commit: none

Test: none

Red/green: none

### Cluster 4880 — Sample bakers may not mark the project modified

Verdict: FALSE-POSITIVE

Evidence: `commitBake` does swap the buffer via `setSampleBuffer` at
`src/core/ControlCommandsSampleEdit.cpp:135`, and `src/core/SampleClip.cpp:165` calls
`Engine::getSong()->setModified()` from inside that setter. The dirty flag is therefore set by
the same path the other range and sample-tool edits rely on; an explicit call in `commitBake`
would only duplicate it.

Commit: none

Test: none

Red/green: none

### Cluster 2102 — A one-frame range fade-out does nothing

Verdict: FIXED

Evidence: the ramp denominator in `applied()` was
`std::max<std::size_t>(1, to - from - 1)`, so a one-frame range produced `span == 1` and
`t == 0` — `fade_out` applied a gain of 1 and left the frame untouched. It is now the true
span `to - from - 1`, with `t = 1.0` when that span is 0, so the single frame lands on the
fade endpoint exactly as `sampleops::fade` does (`src/core/SampleOperators.cpp:166-183`).
Wider ranges are bit-for-bit unchanged.

Commit: 6fea1ba27

Test: AudioAnalysisTest::aRangeFadeReachesItsEndpoint

Red/green:
- red: `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=8, frame stayed at `-0.0156698` where `0` was expected
- green: `cmake --build build --target zene AudioAnalysisTest` EXIT=0; `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=0, 100% tests passed, 0 failed out of 1

### Cluster 1231 — Sample range edits silently clamp invalid frame bounds

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsSampleRange.cpp:123-124` does clamp `from`/`to` into the
source, which is this command's documented behaviour, not an accident:
`src/gui/ControlCommandsSampleEditor.cpp:88-89` states the range is "clamped to the source and
swapped if reversed" for the interactive editor. Refusing instead would make the wire command
disagree with the GUI it mirrors, so the finding asks for a behaviour change, not a defect fix.

Commit: none

Test: none

Red/green: none

### Cluster 1232 — `gain_db` can refuse operations that do not use gain

Verdict: FIXED

Evidence: the `-60..24 dB` check on `gain_db` ran for every op, so a `crop`, `delete`,
`reverse`, `silence` or `fade` — none of which read the value — was refused outright. The
check now lives in `gainRefused()` and only fires when `op == "gain"`; `gain` itself still
validates the parameter it actually applies. Extracted as a helper rather than inlined so
`editRange` stays within the CCN ≤ 10 ratchet.

Commit: 3e9f53051

Test: AudioAnalysisTest::aRangeIgnoresAnUnrelatedGain

Red/green:
- red: `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=8, "a crop was refused over an unused gain_db"
- green: `cmake --build build --target zene AudioAnalysisTest` EXIT=0; `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=0, 100% tests passed, 0 failed out of 1
- neighbours after the change, all EXIT=0: `ControlSampleOperatorTest`, `SampleClipStretchTest`, `SampleClipWarpTest`, `SampleClipWindowTest`

### Cluster 4881 — Range crop and delete leave warp-marker frames stale

Verdict: FIXED

Evidence: a crop re-bases frame 0 onto the requested `from` and a delete closes the range up,
so any marker would then pin a different frame of the audio or one the source no longer holds
— `editRange` updated only the sample window. `movesWarpMarkers()` now refuses exactly those
two ops when the clip carries markers, and leaves the frame-for-frame ops (`gain`, `silence`,
`reverse`, both fades) alone. This matches the refusal `sample.crop` already makes at
`src/core/ControlCommandsSampleTools.cpp:86-92`. Refusing was preferred over silently rebasing
markers, which would be a design change beyond this lane's scope.

Commit: acfc1da6d

Test: AudioAnalysisTest::aRangeEditRespectsWarpMarkers

Red/green:
- red: `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=8, the crop returned `errorKind` 0 (Ok) where `Refused` was expected (re-verified by reverting the source to `e006ac817` with all three regression tests in place: 7 passed, 3 failed)
- green: `cmake --build build --target zene AudioAnalysisTest` EXIT=0; `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` → EXIT=0, 100% tests passed, 0 failed out of 1
- gates before every commit: `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

Leftovers: the commit message for `acfc1da6d` records the warp red run as EXIT=1, but the verified
ctest exit for that failure state is EXIT=8 (QtTest returns the failure count as its code; the
run that produced EXIT=1 was never captured). The code above is the re-verified number, taken by
reverting the source to `e006ac817` with all three regression tests in place — 7 passed, 3 failed,
EXIT=8. The message was left uncorrected because amending it would rewrite published history.
Three consecutive blank lines also remain between `movesWarpMarkers` and `gainRefused` in
`src/core/ControlCommandsSampleRange.cpp` — cosmetic only, both gates pass, left for a follow-up.
Cluster 1231 is a live design question rather than a closed finding: if the wire command should
refuse out-of-range bounds instead of clamping them to match the GUI, that is a deliberate
behaviour change and needs an owner. No audio-thread path was touched by any fix in this batch.