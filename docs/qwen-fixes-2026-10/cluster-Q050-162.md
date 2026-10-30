IMPLEMENTED

# Q050-162 — Add synced transient-repeat stretching

## Integration r3 (2026-10-30): accepted patch onto `050/m0-train-22` (`4ee5321ffc`)

Accepted source `feat/050-Q050-162-oc` @ `545f8cb9b9` (base `37eaf07557`). The driver's three-way apply conflicted in
`include/SampleClip.h`, `src/core/SampleClip.cpp`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` and
`tools/mcp-zene-control/zene_control/commands_snapshot.json`. Every other file applied cleanly. Logs:
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-162.r3/logs`.

**How the conflicts were resolved (both sides kept; no new design):**
- `include/SampleClip.h`: the newer line (Q050-111, per-clip effects) had `prepareWarpStretchMode` inline as
  `ensureRubberBandPool(mode)` plus the `m_clipEffects` members. This unit declares it out of line, and its definition
  calls `ensureRubberBandPool(mode)` and then analyses transients for Repeat / Gate. Resolution: keep this unit's
  declaration and transient members, keep all Q050-111 members, drop the inline body. The result is a superset.
- `src/core/SampleClip.cpp` (copy constructor): keep Q050-111's `copyClipEffects(orig)`, then this unit's two
  `refreshTransients` connects and `prepareWarpStretchMode(warpStretchMode())`.
- `tests/fork-sources.txt`, `tests/upstream-modifications.txt`: union of both sides' appended blocks.
  `fork-sources` and `no-upstream-regression` gates pass.
- `commands_snapshot.json`: only `captured_at` / `source_socket` / `version` conflicted. Regenerated live from this
  tree afterwards, giving 878 ids / 77 groups. That is unchanged from the train: only the metadata lines differ, so
  the merged `warp.stretch` schema equals the live one.

**Affected failure paths and platform hazards reviewed together:**
1. Copying a clip now does two things: it builds the clip's own effect chain (Q050-111) and it analyses transients if
   it is Repeat / Gate (Q162). Both happen on the control thread in the constructor and are independent.
   `ClipEffectsTest` and `SampleClipTransientRepeatTest` pass.
2. Render: in `SamplePlayHandle::play` (`src/core/SamplePlayHandle.cpp:590-635`) the Repeat / Gate branch writes
   `workingBuffer` like the other paths. The clip-edit envelope, take gate, comp and then Q050-111's
   `processClipEffects` run after it unchanged. So a Repeat / Gate clip gets its clip effects, and nothing new runs
   on the audio thread. The realtime analysis is unchanged from round 2 (see the self-check).
3. Rubber Band pool publication order: `setWarpStretchMode` builds the pool first and stores the mode second. This is
   unchanged, because the out-of-line `prepareWarpStretchMode` still calls `ensureRubberBandPool(mode)` first.
   `SampleClipRubberBandTest` and `SampleClipWarpModesTest` pass.
4. Portability: the resolution adds no new API. It is the same code as the accepted patch, already reviewed for
   Qt 5.15, MSVC and macOS < 10.15. It was not compiled on those hosts here.

## Acceptance (r3, on the integrated tree)

| Criterion | Test (command) | What it proves | Exit |
| --- | --- | --- | --- |
| Stretch a transient fixture in repeat and gate modes and verify the selected free or tempo-synced rate fills the target duration. | `SampleClipTransientRepeatTest` (`ctest -R` from `build/tests`; `test-1.log`, `test-4-final.log`) | Repeat and Gate fill the 4.000 s warped span exactly. Free 300 ms retriggers on its grid, including the partial last interval. Synced 1/8 follows the tempo (250 ms at 120 BPM, 500 ms at 60 BPM). Free does not follow the tempo. | 0 |
| Verify mode selection and rate through the UI, agent state, save/load, and a rendered-audio regression. | `SampleClipWarpModeMenuTest`, `SampleClipTransientRepeatTest` (command/undo, round-trip, older-file cases), `ControlTransientRepeatTranscript` (real `zene` over the socket) (`test-1.log`, `test-4-final.log`) | Menu items drive `warp.stretch`. The agent reply and `warp.list` report mode, rate and transient count, with one undo per edit. Save/load round-trips and renders bit-identically. An older `<warp>` element loads the default. Over the socket, Repeat and Gate give different render hashes, undo restores the hash, and save/open gives the same hash. | 0 |

Revert check on the integrated tree: the Repeat / Gate render dispatch in `src/core/SamplePlayHandle.cpp` was
disabled (`if (false && m_repeatsTransients)`). Rebuild exit 0 (`build-6-reverted.log`).
`ctest -R '^(SampleClipTransientRepeatTest|ControlTransientRepeatTranscript)$'` exited 8 (`test-3-reverted.log`):
both failed, with the onsets at 0/1/2/3 s only. After restoring the file (byte-identical, `git diff --quiet`) the
rebuild exited 0 (`build-7-restored.log`) and the final run of 10 tests exited 0 (`test-4-final.log`). Rounds 1-2
reverted every behavioural hunk; that is recorded below.

## Self-check

- **Build:** `cmake --build build --target zene SampleClipTransientRepeatTest SampleClipTransientRepeatSafetyTest
  SampleClipWarpModeMenuTest SampleClipWarpModesTest SampleClipStretchTest ClipWarpPersistenceTest ClipEffectsTest
  -- -j4` exited 0 (`build-4.log`). `build-3.log` failed because ccache's configured `temporary_dir` is outside the
  write jail; it was rerun with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q162`. Neighbours `ReversibilityContractTest
  ControlWarpCommandsTest SampleClipRubberBandTest ClipEffectsDialogTest` built with exit 0 (`build-5.log`).
- **Focused tests:** 8/8 exit 0 (`test-1.log`), 4/4 exit 0 (`test-2.log`: ReversibilityContract, ControlWarpCommands,
  SampleClipRubberBand, ClipEffectsDialog), and a final 10/10 exit 0 after the revert check (`test-4-final.log`).
- **Fails without the change:** exit 8 reverted, exit 0 restored (above).
- **One undo per edit / reversibility:** `warp.stretch` stays `true_inverse` with one clip checkpoint, and `warp.list`
  stays `not_mutating`. `ReversibilityContractTest` exit 0. No new id, so the A16 histogram is unchanged.
- **Snapshot:** `snapshot_live.py $PWD` exit 0 (878 ids / 77 groups, `snapshot-1.log`).
  `python3 tests/doc-figures-gate.py` exit 0 (`docfig-1.log`).
- **Gates:** `feature_check.py` raised a PermissionError creating `<worktree>-feature-check`, which is outside the
  write jail (`feature-check-1.log`). I ran a copy that changes only `ROOT` (to the original's directory) and the
  log directory (`/tmp/q162-fc/logs`): all 8 gates PASS, exit 0 (`feature-check-2.log`). For the same reason,
  `submit_check.py`'s own gates row may FAIL when run from this lane.
- **Realtime:** the integration adds nothing to the audio thread. The analysis from round 2 still holds: transient
  analysis runs on the control thread, and the render reads a fixed-size snapshot. TSan does not run on this box.
- **Old projects:** unchanged from round 2. `repeat_rate` is written only when it is not the default.
  `ClipWarpPersistenceTest` and `SampleClipWarpModesTest` pass.
- **Not run:** the whole ctest suite (per the rules), MSVC/macOS/Qt 5 builds (hosted CI only), and
  `ControlSocketIntegration` (it has a known plugin-module failure unrelated to this unit, see round 2).

## History: rounds 1-2 (on base `37eaf07557`)

Branch `feat/050-Q050-162-oc` (cut from `050/m0-train-21`, `37eaf07557`). Commits: `8ae22f60e6` (feature +
tests), `c00913cd2b` (complexity refactor, no behaviour change), `ff822a02de` (docs); review round 2:
`a0a802b3fc` (fixes), `1d79a122ba` (safety tests split out), and this docs commit. Design, file map and known
limits: `docs/050-features/Q050-162.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-162.r1/logs` (round 1) and `.../Q050-162.r2/logs` (round 2).

Two new warp modes, `repeat` and `gate`, beside the existing six (Beats and the others render unchanged). A warped
clip is cut into segments at its transients (analysed on the control thread when the clip enters the mode); each
segment plays at its natural rate inside the slot the warp map gives it - Repeat retriggers it at every interval,
Gate cuts it at the first - so the render lasts exactly the warped span. The interval is a synced division
(1/1..1/32, follows the tempo) or a free 5..2000 ms (does not). Agent: `warp.stretch` `mode` + `rate_division` /
`rate_ms`; read back in `warp.list` (`repeat_rate`, `transient_count`). GUI: Warp mode menu > Repeat transients,
Gate transients, Repeat rate submenu - all through `warp.stretch`. Saved as `repeat_rate` on `<warp>` (only when
not the default).

### Review round 2 (Luna r1: REWORK) - what changed

| Blocking point | Fix | Proof (round-2 logs) |
| --- | --- | --- |
| The double-buffered transient table could be rewritten while a play handle on the audio thread copied it (reader preempted across two publishes). | `PublishedTransients` (`include/TransientRepeat.h`, `src/core/TransientRepeat.cpp`): three slots, a per-slot reader pin; the reader pins the published slot and re-checks it before copying (4 bounded attempts, never waits, empty table if all race); the control-thread writer only writes a slot that is neither published nor pinned, yielding on the control thread if needed. All seq_cst atomics; no slot is written while a reader copies it. `SampleClip::transientTable()` reads through it, `analyseTransients()` publishes through it. | `SampleClipTransientRepeatSafetyTest::aPinnedTableIsNeverOverwrittenByLaterPublishes` (deterministic: a pinned slot survives 6 publishes) and `concurrentPublishesNeverHandAReaderAMixedTable` (reader thread vs 20000 publishes: 76746 reads, 0 mixed) - exit 0 (`test-8-final.log`, `test-8-safety-direct.log`). |
| Repeat/Gate ignored the sample's amplification. | `TransientSource` carries `amplification`, read each period from the Sample (as `Sample::play` reads it); every frame is scaled by it. | `theAmplificationScalesTheRender`: amplification 0.5 renders exactly half the unity render (max error 0.0). |
| Repeat/Gate read a reversed sample forward. | `TransientSource::at` reads play frame p at buffer frame size - 1 - p for a reversed source (`Sample::render`'s mapping). The clip view's Reverse now emits the clip's `wasReversed`, on which a Repeat/Gate clip re-analyses its transients (they belong to the played direction). | `aReversedClipPlaysBackwards`: the first segment equals the reversed buffer (max error 0.0; 0.80 from the forward read). |

Also fixed while there: my round-1 report claimed segments were read in clip-window coordinates, but that edit had
not reached the committed file - a pass beginning mid-clip (seek, loop) read before its own window and was silent
until the next transient. Segments are now in clip-window coordinates and start on whole clip output frames;
`aPassStartingMidClipMatchesTheWholeRender` (a pass from 0.75 s equals the whole render, max error 0.0).

### Acceptance

| Criterion | Test (command) | What it proves | Exit |
| --- | --- | --- | --- |
| Stretch a transient fixture in repeat and gate modes and verify the selected free or tempo-synced rate fills the target duration. | `SampleClipTransientRepeatTest` (`ctest -R "^SampleClipTransientRepeatTest$"` from `build/tests`; logs `test-14-final.log`, `test-14-direct.log`) | 4-burst fixture, half-speed warp: Repeat and Gate both render exactly the 176400-frame (4.000 s) warped span; Repeat free 300 ms gives 16 onsets on the 300 ms grid of each segment slot (partial last interval at the clip end included); Gate 4 onsets and silence (< 1e-6) after 300 ms in each slot; synced 1/8 = 250 ms at 120 BPM, 500 ms at 60 BPM (span 8.000 s, same onsets per slot); free 300 ms unchanged at 60 BPM (28 onsets vs 16). | 0 |
| Verify mode selection and rate through the UI, agent state, save/load, and a rendered-audio regression. | `SampleClipWarpModeMenuTest` (`theTransientModesAndTheirRateDriveWarpStretch`); `SampleClipTransientRepeatTest` (`theModeAndRateGoThroughTheCommandAndUndo`, `modeAndRateRoundTripAndRenderTheSameAfterALoad`, `anOlderWarpElementLoadsTheDefaultRate`); `ControlTransientRepeatTranscript` (real `zene` over the socket) | Menu items triggered land through `warp.stretch`, checked rate follows, one undo per gesture; command schema/reply/`warp.list` report mode, rate, interval and transient count, typed refusals, one undo per call; project file carries `stretch`/`repeat_rate`, reload restores and renders bit-identically, older `<warp>` loads the default; socket: Repeat and Gate render different hashes, undo restores the Repeat hash, save/open renders the same hash. | 0 |

Round 2 final run (`r2/logs/test-8-final.log`, after the last rebuild): the three acceptance tests
(`SampleClipTransientRepeatTest` 8/8, `SampleClipWarpModeMenuTest`, `ControlTransientRepeatTranscript`), the new
`SampleClipTransientRepeatSafetyTest` 7/7, and the neighbours `SampleClipWarpModesTest`, `SampleClipStretchTest`,
`SampleClipRubberBandTest`, `ControlWarpCommandsTest`, `ReversibilityContractTest`, `ClipWarpPersistenceTest`,
`ClipTransposeTest`, `SampleClipWarpTest` - 12/12 passed, exit 0.

### Self-check

- **New tests fail with the change reverted, pass restored.** Reverted the behavioural hunks of
  `src/core/SamplePlayHandle.cpp`, `src/core/Clip.cpp` (base file plus only the two empty `case` labels the new enum
  values need to compile), `src/core/ControlCommandsWarpEdit.cpp`, `src/gui/ClipEditsMenu.cpp` to the base; headers
  kept so the tests compile (a full revert makes the tests fail to compile). Rebuild exit 0 (`build-12-reverted.log`);
  `ctest -R "^(SampleClipTransientRepeatTest|SampleClipWarpModeMenuTest|ControlTransientRepeatTranscript)$"` exit 8
  (`test-11-reverted.log`): SampleClipTransientRepeatTest failed 5 slots, SampleClipWarpModeMenuTest segfaulted (no
  "Repeat rate" item), the transcript failed 8 checks (identical Repeat/Gate hashes). An earlier attempt whose reverted
  build failed (exit 2, `build-11-reverted.log`) ran stale binaries and is NOT counted. Restored: rebuild exit 0, all
  pass, exit 0 (`test-12-restored.log`); after the later no-behaviour refactor, exit 0 again (`test-14-final.log`).
- **Round-2 tests fail with their fixes reverted.** Mutating `src/core/TransientRepeat.cpp` only (writer ignores
  pins, amplification dropped, reverse mapping dropped, boundaries unrounded): rebuild exit 0
  (`r2/logs/build-6-reverted.log`), `ctest -R "^SampleClipTransientRepeatTest$"` exit 8, direct run exit 3: the
  pinned-slot, amplification and reverse tests failed (`test-5-reverted-direct.log`; the tests were still in
  SampleClipTransientRepeatTest then). With the round-1 pass-relative coordinates instead: the mid-clip test failed,
  error 0.80 from frame 0 (`build-7-reverted.log`, `test-6-reverted-direct.log`). The concurrency stress test passed
  under the mutation - it is probabilistic; the deterministic pinned-slot test is the catcher. Restored: rebuild
  exit 0, all pass (`build-8.log`, `test-7-final.log`, then `test-8-final.log` after the split).
- **Every acceptance criterion proven by a test I ran:** table above - both rows exit 0.
- **One undo step per edit; honest class.** `warp.stretch` keeps `true_inverse`: mode and rate are written after ONE
  clip checkpoint (`ControlCommandsWarpEdit.cpp`, `warpStretch`), the recorded inverse carries the previous mode and
  rate; asserted by `theModeAndRateGoThroughTheCommandAndUndo` (three calls, three undos), the menu test (three
  gestures, three undos) and the transcript's A16 record check. `warp.list` stays `not_mutating`.
  `ReversibilityContractTest` exit 0, histogram unchanged (no new id).
- **Realtime.** On the audio thread: `SamplePlayHandle` construction calls `snapshotTransientRepeat`
  (`PublishedTransients::read` - at most 4 pin attempts of atomic increments / loads, a value copy of one fixed
  256-entry table, an unpin - plus the window and the rate word; `transientTicksAt` arithmetic over the
  fixed-capacity warp map; `addSegment` into fixed arrays) and `play()` calls `renderTransientRepeat` /
  `TransientRepeatRenderer::render` (per-frame arithmetic and reads of the sample buffer). No allocation, free, lock,
  wait, I/O or signal there. Transient analysis (`clipTransientFrames`, which allocates) runs only on the control
  thread: `SampleClip::prepareWarpStretchMode` and the `sampleChanged` / `wasReversed` slot (queued when emitted
  off-thread); the writer's yield is on that thread too. Races (TSan does not run on this box): rate = one atomic
  word; mode published after its table; the table through `PublishedTransients`, which never writes a slot a
  reader holds (pin / re-check / write-only-unpinned, seq_cst) - tested deterministically and under a thread.
  `renderTransientRepeat` reads the amplification and direction with relaxed atomic loads, as `Sample::play` does.
- **Old projects.** `<warp>` writes `repeat_rate` only when non-default and the two new `stretch` tokens only for
  the new modes; no other byte changes (`modeAndRateRoundTripAndRenderTheSameAfterALoad` asserts an untouched clip
  writes no `<warp>`; `anOlderWarpElementLoadsTheDefaultRate` an older element; `ClipWarpPersistenceTest`,
  `SampleClipWarpModesTest` round trips pass). New data round-trips and renders bit-identically (same test; socket
  hash equality).
- **Portability.** Round 2 adds `std::atomic` / `std::this_thread::yield` (C++11) and nothing platform-specific. No Qt-6-only API (QString/QLatin1String/QJson*, `QString::chopped` is Qt 5.10+), no
  `std::filesystem`, no `min`/`max`/`small`/`near`/`far`/`interface` identifiers, `HUGE_VAL` from `<cmath>`, C++20
  `std::span`/structured bindings as used elsewhere in the tree. Not compiled on MSVC/macOS here (hosted CI only).
- **Snapshot.** Regenerated live with `snapshot_live.py` (exit 0, 871 ids / 76 groups, unchanged count);
  `python3 tests/doc-figures-gate.py` exit 0.
- **Gates (round 2).** Same redirected copy: all 8 PASS, exit 0 (`r2/logs/feature-check-2.log`; the first round-2
  run flagged the test file over 500 lines and one test over the complexity target, fixed in `1d79a122ba`).
- **Gates (round 1).** `feature_check.py` writes its logs to `<worktree>-feature-check`, outside this lane's write jail
  (PermissionError). Ran an unmodified copy with only the log directory and its own ROOT path redirected
  (`/tmp/q162-fc/feature_check.py`): all 8 gates PASS, exit 0 (`feature-check-3.log`; the first run flagged four
  functions over the complexity target, fixed in `c00913cd2b`). The same PermissionError will make
  `submit_check.py`'s own gates row FAIL when run from this lane.
- **Not caused by this change, not fixed:** `ControlSocketIntegration` fails one check ("plugin.list is not broken
  down by format: {'ladspa': 10}") because no native plugin module is built in this worktree (`build/plugins` has no
  `.so`); this change touches no plugin code. Not re-run on a base build.
- **Build environment note:** ccache's configured `temporary_dir` is outside the write jail; builds ran with
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-q162`.
