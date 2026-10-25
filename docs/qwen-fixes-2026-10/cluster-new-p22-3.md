FIXED

new-p22-3: FIXED

`clip.set_gain` now returns its normal state reply before journalling when the
computed `ClipEdits` value equals the current value. A changed gain still takes
one Clip checkpoint and records the same transaction. The regression is
`ClipPolarityTest::settingTheSameGainRecordsNoSecondUndoStep`
(`tests/src/core/ClipPolarityTest.cpp:242-263`): set -6 dB twice, reject an
out-of-range request, then prove one undo returns to unity.

RED/GREEN proof (logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-3.r1/logs`):

- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-3.r1/logs/fix.patch` — EXIT=0 (`035-proof-remove-final.log`).
- `cmake --build build --target zene ClipPolarityTest` — EXIT=0 (`036-proof-red-build-current.log`).
- From `build/tests`, `ctest -R '^ClipPolarityTest$' --output-on-failure` — EXIT=8 (`037-proof-red-test-current.log`); the new assertion fails because one undo leaves the clip at -6 dB.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-3.r1/logs/fix.patch` — EXIT=0 (`038-proof-restore-final.log`).
- `cmake --build build --target zene ClipPolarityTest ClipOffsetFadeTest` — EXIT=0 (`039-proof-green-build-current.log`).
- From `build/tests`, `ctest -R '^ClipPolarityTest$' --output-on-failure` — EXIT=0 (`040-proof-green-test-current.log`).
- From `build/tests`, `ctest -R '^(ClipOffsetFadeTest|ClipPolarityTest)$' --output-on-failure` — EXIT=0, 2/2 CTest targets (`041-final-family-test-current.log`).

`src/core/ControlCommandsClipEdits.cpp:346-352` still validates the dB range
before editing, and `resolveAudioClip` refuses unsupported clip types before a
checkpoint. The command has no resource queue, exhaustion or reconfiguration
path; a real change still calls the same checkpoint and setter. The edit is in
the control handler; no render/audio-thread code or serialized clip data
changed. The current Linux C++20/Qt build compiled `zene` and both test targets.

new-p14-14: ALREADY-FIXED

The transaction uses the finding's stated alternative: `ClauseTrackJournalled`
names the ProjectJournal Track checkpoint and says the track's serialized clip
list is the inverse (`src/core/ControlCommandsClipEdits.cpp:66-68`). The record
sets `inverse.op` to `control.undo`; the adjacent comment explicitly says that
the checkpoint is the inverse and the prior-fade args are audit data
(`src/core/ControlCommandsClipEdits.cpp:602-608`). Thus the partial `before`
fields are not presented as the replayable inverse. The existing
`ClipOffsetFadeTest::theCrossfadeCommandUsesTheRampsAndUndoRestoresThem`
(`tests/src/core/ClipOffsetFadeTest.cpp:313-324`) also checks that one undo
restores the complete prior `ClipEdits`, including offset and gap fields.

Evidence command: from `build/tests`,
`ctest -R '^(ClipOffsetFadeTest|ClipPolarityTest)$' --output-on-failure` —
EXIT=0, 2/2 targets (`041-final-family-test-current.log`). No code change was
needed for this finding, so a reverse-patch RED/GREEN run is not applicable.

new-p14-11: FALSE-POSITIVE

Clamp behavior is an explicit owner contract: `docs/CLIP-CROSSFADE.md:6-13`
defines default edge fades and says contained pairs do not create offset fades;
`docs/KNOWN-LIMITATIONS.md:1833-1842` records the 2026-10-09 ruling. The
reported contained geometry therefore intentionally fades at each clip edge.
`ClipOffsetFadeTest::aDefaultContainedCrossfadeClampsToClipEdges`
(`tests/src/core/ClipOffsetFadeTest.cpp:163-192`) asserts those offsets, edges
and rendered levels. Container mode remains the explicit option for ramps at
the shared containment boundaries, as tested by
`theCrossfadeCommandUsesTheRampsAndUndoRestoresThem`.

Evidence command: from `build/tests`,
`ctest -R '^(ClipOffsetFadeTest|ClipPolarityTest)$' --output-on-failure` —
EXIT=0, 2/2 targets (`041-final-family-test-current.log`). No code change was
needed for this finding, so a reverse-patch RED/GREEN run is not applicable.

## Gate results

| Gate | Result |
| --- | --- |
| fork-sources | PASS, EXIT=0 |
| all-sources-reproduce | PASS, EXIT=0 |
| no-upstream-regression | PASS, EXIT=0 |
| check-namespace | PASS, EXIT=0 |
| no-tautology | PASS, EXIT=0 |
| doc-figures | PASS, EXIT=0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |
| feature_check.py | PASS, EXIT=0 (`043-feature-check-post-report.log`) |
| submit_check.py | PASS, EXIT=0 (`044-submit-check.log`) |

To make the source-manifest gates pass, `tests/all-sources.txt` was regenerated
from its published recipe. It removed only the 1,148 stale, vendored
`plugins/SfzPlayer/sfizz/` paths that the recipe excludes; the recipe check
passed (`021-all-sources-reproduce.log`). No gate threshold or baseline changed.

## Self-check

- **RED/GREEN:** With the product patch reversed, the rebuilt
  `ClipPolarityTest` fails at the one-undo assertion (EXIT=8); with the patch
  restored it passes (EXIT=0). The command-level family passes 2/2 targets
  (EXIT=0).
- **Determinism:** The regression invokes commands and undo synchronously on
  the test thread; it uses no clock, sleep, race or timing seam.
- **Undo and sibling consistency:** A changed gain still creates one Clip
  checkpoint; an unchanged gain returns before both the checkpoint and
  transaction payload. `clip.set_polarity` already uses the same equality
  guard. `clip.set_fade`, `clip.crossfade` and the other handlers in this
  translation unit edit different fields and were not changed.
- **Realtime and saved data:** No new code runs on the audio thread. The
  comparison and early return are in the control command handler; clip render
  and serialization code are untouched, so old project data and its rendering
  retain their existing representation.
- **Portability/build:** `zene`, `ClipPolarityTest` and `ClipOffsetFadeTest`
  built in the configured Linux C++20/Qt tree. The fix uses existing C++ and Qt
  APIs and introduces no platform-specific code; no other platform toolchain
  is configured in this worktree.
- **Completion check:** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p22-3 new-p22-3` — EXIT=0; each emitted `?` item is answered above.
- **Committed files:** `src/core/ControlCommandsClipEdits.cpp`,
  `tests/src/core/ClipPolarityTest.cpp`, `tests/all-sources.txt`, and this
  report. The product/test commits are `136748694`, `200127217`, and
  `460eabf76`; the source-manifest gate repair is `8f03e5b0e`.
