IMPLEMENTED

# Q050-124 Freeze mixer buses

Branch `feat/050-Q050-124-oc` (from `050/m0-train-24`, `2a7fd9f9c7`). Design, commands, format and known limits:
`docs/050-features/Q050-124.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-124.r1/logs/` (one numbered file
per build/test run).

Round 0 had produced no code (four provider quota failures). This run follows the agreed checkpoint plan: a
MixerChannel-owned take, rendered and decoded off the audio thread, published under the render lock, substituted at
the channel's pre-fader output point, persisted on `<mixerchannel>`, one registered command pair used by both the
strip menu and the socket, a focused `MixerBusFreezeTest`, a new socket transcript, and the transcript registered with
`SKIP_RETURN_CODE 77`.

## Acceptance

| Criterion | Test (command) | Proves | Exit with change | Exit reverted |
| --- | --- | --- | --- | --- |
| Render comparison: capture includes routed inputs, playback substitutes the take, unfrozen mix unchanged | `ctest -R '^MixerBusFreezeTest$'` (frozenBusPlaysItsCaptureInPlaceOfTheLiveInputs, aShortTakeEndsCleanly..., songRenderSurvivesProjectSaveAndOpen); `ctest -R '^ControlBusFreezeTranscript$'` | capture = processed bus to 1e-6; frozen with sources silent = live mix to 1e-6; unfrozen = live mix; real child render with the source deleted = live reference, 0.0 16-bit steps over 302080 samples | 0 (test-29, test-32) | 8 (test-26, test-28) |
| Freeze/unfreeze survives save/load | MixerBusFreezeTest::commandsUndoAndSaveLoad, aMissingOrUnreadableTake..., songRender...; ControlBusFreezeTranscript | `frozenAudio` round-trips (mixer element and full project save/open), plays after load; absent attribute = unfrozen; missing/unreadable take = frozen, live inputs play | 0 | 8 |
| Registered command, UI action, control-socket test | `ctest -R '^MixerBusFreezeMenuTest$'`; ControlBusFreezeTranscript; MixerBusFreezeTest::refusalsAndFailedRendersChangeNothing | right-click on the bus strip -> "Freeze bus" runs `freeze.bus` (bus.list: frozen), then "Unfreeze bus" runs `freeze.unfreeze_bus`; socket freeze/readback/save/open/unfreeze/undo/refusals; failed renders change nothing | 0 | 8 (test-27, test-28) |
| Realtime: playback path allocates nothing | MixerBusFreezeTest::framePlaybackAllocatesNothing | 6 frozen periods through fader/sends/meters/master: 0 allocations, 0 frees, and the take audibly played | 0 | 8 (test-26) |

Neighbours, exit 0 (test-31-neighbours): BounceInPlaceTest, AutomationRenderAllocationTest, PhaseDSidechainTest,
FreezeRegionRepairTest, ReversibilityContractTest, MixerWindowsTest, ControlCommandsSnapshot,
ControlFreezeCommandsTranscript, ControlBusCommands.

The transcript needs an instrument module: `cmake --build build --target tripleoscillator` (build-11). Without one it
exits 77 and CTest reports Skipped (seen once, test-8, before the module was built) - never Passed.

## Found and not changed (out of scope)

Headless, `track.set_mixer_channel` on a track created before the target channel existed answers the new index but the
track keeps playing to the master (its channel model's range was set when it was made; the saved file says
`mixch="0"`). Seen while building the socket fixture (logs test-10/test-11 era; reproduced with a saved project in
/tmp). The transcript creates its channels first. Worth its own finding.

## Self-check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-124`: every mechanical item passes except
"gates (feature_check.py)", which cannot run in this lane's write-jail - it creates
`<worktree>/../zene-feat-q050-124-feature-check/` outside the writable tree (PermissionError, feature-check-1.log). I
ran the same gates directly from the worktree instead:

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 (880 ids / 77 groups) |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check`, compared with feature_check's cached base listing for `2a7fd9f9c7` and its own key()/item_file() | no new item in my files (after splitting `busfreeze::refusal` and the transcript's WAV reader) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check`, same comparison | no new item in my files |

Answers to the ASK items:

1. **Fails reverted, passes restored.** Behaviour hunks of Mixer.cpp, AudioEngine.cpp, main.cpp, MixerChannelView.cpp,
   ControlRegistryRegistrations.cpp and ControlCommandsBus.cpp reversed with `git diff 2a7fd9f9c7 HEAD -- ... | git
   apply -R`, rebuilt (build-23-reverted, exit 0), run: MixerBusFreezeTest exit 8 (7 of 7 cases fail, test-26),
   MixerBusFreezeMenuTest exit 8 (test-27), ControlBusFreezeTranscript exit 8 (test-28). Restored with `git checkout
   HEAD -- <files>`, rebuilt, all three exit 0 (test-29; again test-32 on the final tree). The first revert run showed
   the allocation case passing on silence; it was strengthened to require the take to sound (commit c45b7574e9) and
   then failed reverted. The negative GUI case (a non-bus strip offers no entry) passes either way by nature; its test
   binary fails reverted through the positive case.
2. **Every criterion proven by a test I ran** - see the acceptance table (exit codes from test-29/test-32 and the
   transcript's verbose run test-12).
3. **One undo step, honest classes.** `freeze.bus` and `freeze.unfreeze_bus` are `true_inverse`: each pushes ONE
   `control::addUndoStep` (undo/redo closures that install/clear the take by stable channel id, re-reading the file).
   Proven: core `control.undo`/`control.redo` (commandsUndoAndSaveLoad), GUI undo after the menu's unfreeze, socket
   `control.undo_depth` drops by exactly 1. `ReversibilityContractTest` exit 0 with the two rows and the updated A16
   histogram (test-14, test-31). `bus.list` stays `not_mutating`.
4. **Audio thread.** New audio-thread code: `busfreeze::setPeriod` (two relaxed atomic stores, render thread),
   `busfreeze::substitute` (null/rate/position checks, `std::copy_n` from the preloaded buffer, `zeroSampleFrames`,
   two bitset resets), `busfreeze::capture` / `replaceOutput` (a null test everywhere but a render child; there a
   `std::copy_n` into storage allocated by `armCapture` on the control thread), and `MixerChannel::processInserts`
   (existing code, moved unchanged). No allocation, free, lock, wait, I/O or signal: the take is decoded by
   `loadTake` on the control thread, installed under `requestChangesGuard` (so a worker never sees a half-written
   pointer/count pair - TSan does not run on this box, this is the design argument: the pointer, count and rate are
   written only while no period runs, and read only by mixer jobs inside a period), and the previous buffer is
   released on the control thread after the guard. Measured: framePlaybackAllocatesNothing, 0 allocations/frees.
5. **Old projects.** `frozenAudio` is written only for a frozen bus and absent means unfrozen (reset on absence), so an
   older project loads unfrozen and an unfrozen bus saves byte-identically; the substitution is a no-op unless a
   take is installed. Proven: commandsUndoAndSaveLoad (attribute stripped -> unfrozen, live render),
   PhaseDSidechainTest / BounceInPlaceTest / FreezeRegionRepairTest / ControlFreezeCommandsTranscript exit 0.
   New data round-trips (mixer save/load, project save/open in core and over the socket).
6. **Portability.** No Qt-6-only API (I replaced a `QJsonValue::toInteger` with `toDouble`); no `small`/`near`/`far`/
   `interface`/`min`/`max` identifiers; no std::filesystem; no POSIX-only calls added (file identity reuses
   `BounceInPlace::namesTheSameFile`, already guarded). C++17 if-initialisers only. The tests include `<algorithm>`,
   `<cstddef>`, `<QFile>` explicitly for MSVC; `sndfile.h` is already used by other tests on every platform. Built
   here with Qt 6.12 / GCC 16 only - the hosted Qt 5.15 / MSVC / macOS < 10.15 / clang-arm64 jobs were not run from
   this lane.
