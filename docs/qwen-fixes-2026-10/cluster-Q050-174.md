IMPLEMENTED

# Q050-174 - Low-latency monitoring for latent instruments (B-MX-159, L01, P1, M)

Branch `feat/050-Q050-174-oc`, cut from `050/m0-train-19` (`5c1c6715d9`). Commits:

- `966a1e9230` feat: engine flag and command policy, with the GUI, socket and render tests
- `b042754fb4` docs: manual, limitations page, feature page; snapshot regenerated live
- `0ef86a6ed9` test: the render cases moved into their own executable, because of the file-length cap
- the commit that adds this report

The full write-up is in `docs/050-features/Q050-174.md`.

## What changed

- `include/AudioBusHandle.h`: `setMonitorUnaligned()` / `monitorUnaligned()` on one `std::atomic<bool>`.
- `src/core/AudioBusHandle.cpp`: in `doProcessing()`, while the flag is set, the PDC delay is 0 instead of the
  channel alignment minus `alignedLatencyFrames()`. One relaxed load per period.
- `src/core/Mixer.cpp`: the clamp check skips a flagged handle, because it applies no delay. Its latency still
  counts in the alignment, so every other path stays aligned.
- `src/core/ControlCommandsLowLatency.cpp`: instrument tracks in scope (armed, or every one under `scope: all`)
  whose instrument's `sourceLatencyFrames()` is over `max_ms` get the flag. Each call re-evaluates. Off clears
  the tracks it set. The result gains `instruments`, one entry per path taken: `track`, `name`, `channel`, `path`,
  `latency_frames`, `latency_ms`, `alignment_skipped_frames`. The command stays `not_mutating`.
- GUI: the existing Project > Low-Latency Monitoring On/Off (`src/gui/shell/ShellCommandMenus.cpp:510`) runs the
  same command. No GUI source change was needed.

## Acceptance

| # | Criterion | Test | Green exit | Red exit (product `.cpp` reverted) |
|---|---|---|---|---|
| 1 | Headless control-surface regression with a latency-reporting instrument fixture: policy applied to the armed instrument path, and the path reported | `ctest -R '^ControlLowLatencyCommands$'` (real `zene` binary, CLAP fixture latent at 2000 frames; 13 checks) | 0 (`test-19-ControlLowLatencyCommands.log`) | 8 (`test-17-red-socket.log`) |
| 1 (GUI) | The same command from the Project menu | `ctest -R '^DevicePanelTest$'` → `theProjectMenuMonitorsAnArmedLatentInstrument` | 0 (`test-19-DevicePanelTest.log`) | 8 (`test-17-red-gui.log`) |
| 2 | Monitoring avoids the extra PDC alignment delay, and off restores normal alignment and output | `ctest -R '^InstrumentLowLatencyMonitorTest$'` → `lowLatencyMonitorsTheArmedLatentInstrumentWithoutAlignment`. Onsets: normal 2256, monitored 513 (base 256 + own latency 257), then SHA-256 identical to normal after off | 0 (`test-19-InstrumentLowLatencyMonitorTest.log`) | 1 (`test-17-red-a.log`, all three `.cpp`); 1 (`test-18-red-engine.log`, `AudioBusHandle.cpp` only: monitored onset stays 2256) |
| 3 | Session-only (no saved state, no undo), and non-armed paths unchanged under `scope: armed` | Same executable. `lowLatency...` checks the unarmed renders are SHA-256 identical to normal; `theLowLatencyModeIsNeitherSavedNorUndone` checks `control.undo_depth` does not move, the save with the mode on equals the save with it off (save journal aside), and a reopen leaves no flag set | 0 | 1 (`test-17-red-b.log`); 8 for the whole ctest (`test-17-red-ctest.log`) |

Logs are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-174.r1/logs/`. The red builds kept the header change so
the tests compile, and every hunk was restored afterwards (`cmp` against saved copies: identical).

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** I reverted
  `src/core/AudioBusHandle.cpp`, `src/core/Mixer.cpp` and `src/core/ControlCommandsLowLatency.cpp` to `5c1c6715d9`,
  then ran `cmake --build build --target zene InstrumentLowLatencyMonitorTest DevicePanelTest` (exit 0).
  - Red results: `InstrumentLowLatencyMonitorTest lowLatencyMonitors…` exit 1, `…theLowLatencyModeIsNeitherSavedNorUndone`
    exit 1, `ctest -R '^InstrumentLowLatencyMonitorTest$'` exit 8, `ctest -R '^ControlLowLatencyCommands$'` exit 8,
    `ctest -R '^DevicePanelTest$'` exit 8.
  - Engine-only red (only `AudioBusHandle.cpp` reverted): exit 1, failing on the onset, actual 2000 against expected 257.
  - Restored and rebuilt (exit 0), every test exits 0: `InstrumentLowLatencyMonitorTest`, `InstrumentLatencyPdcTest`,
    `DevicePanelTest`, `ControlLowLatencyCommands`, `PdcMixerTest`, `ControlResultSchemaTest`,
    `ReversibilityContractTest`, `TrackOffsetTest`, `AudioBusPlayHandleQueueTest`, `ControlCommandsSnapshot`.
- **Every acceptance criterion is proven by a test I ran.** See the table above: row 1 is `ControlLowLatencyCommands`
  (exit 0) plus `DevicePanelTest` (exit 0), row 2 is `InstrumentLowLatencyMonitorTest` (exit 0), row 3 is
  `InstrumentLowLatencyMonitorTest` (exit 0), both of its cases.
- **One undo step per edit, and honest reversibility.** There is no edit to undo: the mode is session state.
  `transport.low_latency` keeps `cmd.mutating = false` and its `not_mutating` entry. `control.undo_depth` is unchanged
  across enabling it, checked both in process and over the socket. `ReversibilityContractTest` exits 0, and the A16
  histogram did not change.
- **Realtime.** The audio thread gains one `std::atomic<bool>::load(relaxed)` in `AudioBusHandle::doProcessing` and
  one in the `Mixer::updateLatencyCompensation` clamp loop. Neither allocates, frees, locks, waits, does I/O or emits a
  signal. Allocation and JSON building happen only in the command, on the control thread. TSan does not run on this
  box. Race reasoning: the flag is written on the control thread and read by the audio thread. A stale read lasts at
  most one period and only picks which of two delays applies, and the `LatencyCompensation` delay itself is already
  an atomic.
- **Old projects load and render exactly as before; new data round-trips.** No project format change: nothing is
  written and nothing is read. The flag defaults to false, so the delay arithmetic is unchanged. `TrackOffsetTest`,
  `PdcMixerTest` and `InstrumentLatencyPdcTest` all exit 0 unchanged. The saved-file comparison in
  `theLowLatencyModeIsNeitherSavedNorUndone` shows the mode adds nothing to a project.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** Only Qt 5 API is used (`QRegularExpression`, `QTemporaryDir`,
  `QPointer`, `qsizetype`, all available since Qt 5.10). There is no `std::filesystem`, no POSIX call, and no
  identifier named `min`/`max`/`small`/`near`/`far`/`interface`. `std::max` takes `int` arguments throughout.
- **Gates.** `feature_check.py` gives 8/8 PASS (`feature-3.log`). I ran a copy from `/tmp/q174-fc/` that differs
  only in its log directory: the original writes `<worktree>/../zene-feat-q050-174-feature-check`, which is outside
  this session's write jail and was refused (PermissionError). `doc-figures-gate.py` exits 0, and the snapshot was
  regenerated live with `snapshot_live.py` (868 ids / 76 groups, unchanged; only the `transport.low_latency`
  description moved).
- **submit_check warnings.** "No GUI file changed" is expected: the menu entry already existed and calls this
  command, and `DevicePanelTest` drives it. The "canary figures" warning names existing ids and two temp file names
  read out of the test sources, not new commands. The id count is unchanged, and the README canary still says 868 / 76.

## Known limits

These are also stated in `docs/KNOWN-LIMITATIONS.md` and the feature page.

- The instrument's own latency is still heard.
- A PDC delay added further downstream, on a send from its channel to a later channel or the master, still applies.
- A multi-output instrument's routed output buses keep their alignment.
- `alignment_skipped_frames` reads the alignment the mixer last published.
- The build environment's ccache temp directory (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) refused
  writes, so I built with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q174`.
