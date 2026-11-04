IMPLEMENTED

# Cluster Q050-311 - Built-in synth pressure and timbre (feature, L10, member R-instruments-39)

Goal: users can play MPE pressure and timbre expression through the built-in synthesizers.

The built-in synths never see MIDI (`Instrument::handleMidiEvent` is the base no-op), so pressure and CC74
were stored and forwarded but never heard on them. A new per-note stage, `MpeSynthExpression`
(`include/MpeSynthExpression.h`, `src/core/MpeSynthExpression.cpp`), is applied in
`InstrumentTrack::processAudioBuffer` (`src/tracks/InstrumentTrack.cpp:303-309`). It runs on every note of
every per-note (not single-streamed) instrument, right after that note's envelopes:

- **Pressure** sets intensity: gain `1 + p/127`.
- **CC74** sets brightness: a high-band emphasis around a 1 kHz one-pole corner, up to +12 dB at 127.

Both mappings are neutral at 0 and exactly the identity there. A note without expression or stream is never
touched, so old projects are bit-identical. Values are stream-aware
(`NotePlayHandle::effectiveMpePressure/Timbre`, `src/core/MpeExpressionStreamPlayback.cpp:142,154`). A
value change ramps across one block. `Instrument::appliesMpeExpressionItself()` (`include/Instrument.h:94`)
lets a per-note instrument that maps the MIDI itself opt out; the `MpeTestConsumer` fixture does this.

Scope decisions, since the brief marked them UNCLEAR:
- **Coverage:** every per-note built-in, through the one hook. Single-streamed built-ins (LB302, OpulenZ,
  SF2/GIG/SFZ, ZynAddSubFX) are unchanged and documented as such.
- **Default mapping:** pressure sets intensity and CC74 sets brightness, both neutral at 0. The reasoning
  is in `docs/050-features/Q050-311.md`.

No new command, no changed command metadata, no format change.

## Acceptance

All commands ran from `build/tests` with `QT_QPA_PLATFORM=offscreen` and no global `LMMS_PLUGIN_DIR`.
Logs are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-311.r1/logs/`.

| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| Send pressure and timbre through an MPE fixture and measure distinct modulation in a rendered note | `MpeSynthExpressionTest::aBuiltInSynthSoundsPressureAndTimbreDistinctly`. Real TripleOscillator, saw; level/brightness vs plain: pressure64 `1.5039/1.0000`, timbre64 `1.3222/1.7541`, both `1.9886/1.7541`; a neutral (0,0) note is bit-identical to a plain one. `::liveExpressionMovesOnlyItsOwnNote`: `device.mpe_set` on, live channel pressure on ch1 gives gain `1.5039`, live CC74 on ch2 gives brightness `1.6286`, and the neighbouring note is bit-identical to a run with no event. DSP slots: mapping, exact identity, band behaviour (CC74 127: 100 Hz `1.0668`x, 8 kHz `3.7651`x), ramp with 0 allocations | `ctest -R "^MpeSynthExpressionTest$" --output-on-failure` (test-9-restored.log) | 0 |
| Verify expression state and backwards-compatible project loading | `MpeSynthExpressionTest::expressionRoundTripsAndOldProjectsSoundTheSame`. A plain project is saved: no `mpe*` attributes in the file; after reopening, the note has no expression, renders bit-identically and never enters the stage. Then `note.expression_set` (100/80), save (the file has `mpepressure="100"`), reopen, and `note.expression_get` returns `has_expression`/100/80; the render is bit-identical to the pre-save one | same run | 0 |
| Regression neighbours | `MpePlaybackTest`, `MpeNoteStorageTest`, `MpeExpressionTest`, `ControlNoteExpressionCommandsTest`, `NotePlaybackTest`, `ArticulationPlaybackTest`, `MpeInputActionTest` (Edit > MPE Input GUI action) | `ctest -R "^(MpeSynthExpressionTest\|MpePlaybackTest\|MpeNoteStorageTest\|ControlNoteExpressionCommandsTest\|MpeExpressionTest\|NotePlaybackTest\|MpeInputActionTest\|ArticulationPlaybackTest)$"` (test-5.log) | 0 (8/8) |

One honest flake: in a later run of the same eight (test-6.log, exit 8),
`ArticulationPlaybackTest::aStackedChordFollowsItsArticulation` failed once (`held` 1, expected 0). It then
passed 5 of 5 alone (test-7-artic-1..5.log, all exit 0).

The cause is the fixture's plain `int m_held` (`tests/src/plugins/MpeTestConsumer.cpp:346-347`), which
render worker threads change for chord notes. That race predates this branch. This branch's stage never
runs for that fixture, because the fixture opts out. I did not reproduce the flake on the base build, so
"pre-existing" is reasoned, not measured.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.**
  1. Only the hook in `InstrumentTrack::processAudioBuffer` removed, on the final code. Build `EXIT=0`
     (build-12-reverted-hook.log). `ctest -R ^MpeSynthExpressionTest$` gives `EXIT=8`
     (test-8-reverted-hook.log): the three render slots fail (pressure ratio, live gain,
     `expressive.stageRan`). The four DSP slots exercise the new class directly, so they pass.
  2. Every non-test hunk reverted (the new class removed): the test does not compile. Build `EXIT=2`
     (build-8-full-revert.log, `MpeSynthExpression.h: No such file or directory`).
  3. Restored: build `EXIT=0` (build-13-restored.log), and
     `ctest -R "^(MpeSynthExpressionTest|MpePlaybackTest)$"` gives `EXIT=0` (test-9-restored.log).
- **Every acceptance criterion is proven by a test I ran.** See the table: `MpeSynthExpressionTest` slots,
  exit 0. Neighbours 8/8, exit 0.
- **One undo step per edit; honest reversibility classes.** No command was added or changed.
  - Authoring uses the existing `note.expression_set` (`true_inverse`, one clip checkpoint) and
    `note.expression_clear`.
  - Enabling live input uses `device.mpe_set` (`true_inverse`). `device.mpe_get_state` and
    `note.expression_get` are `not_mutating`.
  - The snapshot did not need regenerating: no id, schema or class changed. The `doc-figures` gate passes.
- **Realtime.** On the audio thread this branch adds three things:
  - `MpeSynthExpression::applyToNote` / `process` / `trackLowpass` / `shapeRamped` / `lowpassCoefficient`:
    float arithmetic on the note's own `State` value member, plus one `std::exp` per block. No allocation
    (`aChangeRampsAcrossTheBlockWithoutAllocating` counts 0), no lock, wait, I/O or signal.
  - `NotePlayHandle::effectiveMpePressure/Timbre`: a read of the handle's own immutable stream snapshot,
    using the same protocol as `effectiveMpePitchCents`.
  - A virtual `Instrument::appliesMpeExpressionItself()`: a const return.

  Live values are ints written by `loadMpeExpressionOntoChannelNotes` from the MIDI input path. That is the
  same pre-existing int handoff the pitch axis uses; `State` itself is touched only by the audio thread.
  TSan does not run on this box, so this is reasoned, not measured.
- **Old projects load and render exactly as before; new data round-trips.**
  `expressionRoundTripsAndOldProjectsSoundTheSame` covers both directions (exit 0). There is no format
  change. A note with no expression skips the stage entirely: `State::primed` stays false, which the test
  asserts.
- **Portability.**
  - No Qt-6-only API: QtTest, QDom, QJsonObject and QByteArray::contains all exist in Qt 5.15.
  - No `M_PI`: a local constant, because MSVC hides it. No `std::filesystem`, no POSIX calls, no
    windows.h macro names as identifiers.
  - The DSP slots run everywhere. The module-loading render slots `QSKIP` on Windows like
    `MpePlaybackTest`, because a test host there cannot load plug-in modules.
  - The test gets `LMMS_TEST_PLUGIN_DIR` and `add_dependencies(... tripleoscillator)` like
    `UnmuteChaseTest`.

  Built and run on Linux/Qt 6 only. The other CI configurations were not run here.
- **GUI and agent paths (the checker's WARN rows).** This feature changes what existing paths sound like.
  It adds no new user action:
  - GUI: Edit > MPE Input (`MpeInputAction`, tested by `MpeInputActionTest`, exit 0) and the note editor's
    Pressure/Timbre lanes.
  - Agent: `device.mpe_set` and `note.expression_set/get`, which the new test drives through
    `ControlRegistry`.

  The scope brief says no new UI action or command is owed, so none was added and no GUI file changed.
- **Gates.**
  - `feature_check.py` cannot run as shipped from this lane. It creates
    `<worktree>/../zene-feat-q050-311-feature-check`, outside my write area: `PermissionError`, logged in
    feature-check-1.log. That is also why `submit_check.py` shows the gates row as FAIL with no FAIL line.
  - I ran a copy that differs only in its log directory (`runs/Q050-311.r1/logs/feature-check/`).
    RESULT: PASS (feature-check-3.log). fork-sources, all-sources-reproduce, no-upstream-regression,
    check-namespace, no-tautology and doc-figures all exit 0. Complexity and file-length show no new item
    in my files; complexity did flag `process` at CCN 13, and it was split below 10 in commit 3.
  - `tests/all-sources.txt` was re-derived with its recipe, which printed REPRODUCES.
  - Inherited files changed are registered in `tests/upstream-modifications.txt`: `Instrument.h`,
    `NotePlayHandle.h`, `InstrumentTrack.cpp`, `src/core/CMakeLists.txt` and `tests/CMakeLists.txt`.
    New product files are registered in `tests/fork-sources.txt`.
- **Environment note.** The ccache config points its temp dir at a path this lane cannot write, so builds
  used `CCACHE_TEMPDIR=/tmp/ccache-q311` (build-1.log shows the failure without it).
