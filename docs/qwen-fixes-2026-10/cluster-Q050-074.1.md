IMPLEMENTED

# Q050-074.1 - Track synthesis CPU and overload history (stage 1/2 of Q050-074)

Branch `feat/050-Q050-074.1-oc`, cut from `050/m0-train-21` (`37eaf0755`). Design, file list and limits:
`docs/050-features/Q050-074.1.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-074.1.r1/logs/`.

Summary: each instrument track's play handles time their own rendering into the track's `SynthesisLoad`.
That time is folded once per period after the Instruments stage. The engine also records render periods
that took longer than they last, at the song tick they started, in a lock-free ring (`LatePeriodLog`).
- `mixer.cpu_load` gains `tracks` / `synthesis_total_percent` / `late_periods`.
- `track.overview` gains `synthesis_cpu_percent`.
- The header CPU readout's right-click action "CPU diagnostics..." opens a window that reads `mixer.cpu_load` through `ControlRegistry`.
- No new command, no format change.

## Acceptance

| # | Criterion | Test(s) | Command (from `build/tests` unless noted) | Exit |
|---|---|---|---|---|
| 1 | Each track's synthesis-only period CPU in mixer.cpu_load and track.overview, keyed by track id, distinct from channel/effect CPU | `TrackSynthesisLoadTest::eachInstrumentReportsItsOwnSynthesisLoad` (3 real tripleoscillator tracks on one channel; playing > 0, silent = 0, same channel id and `cpu_percent` for all, both reports equal, decay after stop); `ControlCpuDiagnostics` (`tests/control-cpu-diagnostics.py`, real binary over the socket, Dummy device) | `ctest -R "^(TrackSynthesisLoadTest|CpuDiagnosticsDialogTest|ControlCpuDiagnostics)$" --output-on-failure` | 0 |
| 2 | A controlled late render period is recorded at its song position, shown in the CPU UI, and a headless read returns the same marks | `TrackSynthesisLoadTest::aLatePeriodIsMarkedAtItsSongPosition` (a direct slot on `nextAudioBuffer` spins 3 periods inside the timed render at tick 384, so the mark is "3.1", playing, took > period, and the same serial/tick/took is in the engine log and the command reply); `CpuDiagnosticsDialogTest::theCpuReadoutOpensTheMarksAgentsRead` (late period at tick 240, then the readout's QAction is **triggered**: the window's late table row = "2.2", tick 240, "stopped", the same serial as the command's newest mark, the row count = the command's marks, the synthesis table lists the track by id); `ControlCpuDiagnostics` (`late_periods` shape over the socket) | same runs | 0 |
| RT | No allocation on the render path with the meters on | `TrackSynthesisLoadTest::aSteadyPeriodWithTheMetersAllocatesNothing` (AllocationProbe: a playing period that folds the loads and records a late mark makes 0 allocations) | same runs | 0 |

Neighbours re-run on the final tree: `AutomationRenderAllocationTest`, `ArticulationPlaybackTest`, `CustomAccessTest`
(mixer.cpu_load's existing assertions), `CPULoadWidgetTest`, `RecordRemainingTest` and `SessionPlaybackRenderTest` all
passed: `ctest` 9/9, exit 0 (build-6.log, test-3.log); 5/5 again after the last test edit (build-10.log, test-7.log).
`agent_surface` exit 0 (test-9-agent-surface.log).

## Self-check

- **Revert proof.** In the reverted tree I removed the two probes (`NotePlayHandle::play`, `InstrumentPlayHandle::play`),
  the `foldSynthesisLoads` and `noteLatePeriod` calls in `AudioEngine::renderNextPeriod`, and the readout's
  `addCpuDiagnosticsAction`. The new types were kept so the tests still compile. Then rebuilt (build-7-revert.log, exit 0)
  and ran:
  - `TrackSynthesisLoadTest` **EXIT=8**. It fails on "a playing track's synthesis: 0", "the late period was not counted" and the allocation case's late-mark precondition (test-5-revert-TrackSynthesisLoadTest.log, after build-8-revert.log exit 0). The first run, test-4, also showed an order artefact: a case that failed while the song played. `playFrom()` now stops the song first, so each case fails on its own assertion.
  - `CpuDiagnosticsDialogTest` **EXIT=8** with "the late period was not recorded" (test-4-revert-CpuDiagnosticsDialogTest.log).
  - `ControlCpuDiagnostics` **EXIT=8**, with the 2 synthesis checks failing (test-4-revert-ControlCpuDiagnostics.log).
  - With only the readout action removed and the engine restored (build-9-revert-action.log, exit 0), `CpuDiagnosticsDialogTest` **EXIT=8**: the context menu policy is DefaultContextMenu, so there is no action (test-6-revert-action.log). The GUI entry point is therefore under test on its own.

  With the product code restored: build-10 exit 0, and all five tests pass with exit 0 (test-7.log). `theRingKeepsTheNewestMarksAndTheWholeCount` is a unit test of the new class, so it passes either way.
- **Each criterion proven by a test I ran.** See the table: exit 0 on test-3.log and test-7.log, and exit 0 on the socket transcript after the last edit (test-8.log).
- **Undo / reversibility.** No edit and no new command. `mixer.cpu_load` and `track.overview` only gained reply fields and description text. They keep their `not_mutating` rows (`ControlReversibilityTableW3Custom.cpp`), empty argument schemas and `mutating:false`; the transcript checks this through `control.commands_list`. The snapshot was regenerated live with `snapshot_live.py`, exit 0: 871 ids / 76 groups, and the diff is only the two descriptions and capture metadata. `doc-figures-gate.py` exit 0. The submit check's canary WARN lists words from the descriptions and test file ("bar.beat", "clip.add", ...), not new ids; the count is unchanged.
- **Audio thread.** These functions now run on it:
  - `SynthesisLoad::Probe` (in `NotePlayHandle::play` and `InstrumentPlayHandle::play`, render workers): two `steady_clock::now()` reads and one relaxed `fetch_add`.
  - `AudioEngine::foldSynthesisLoads`, render thread after the workers have joined: a const loop over the engine's own handle list (`std::as_const`, so it never detaches) calling `SynthesisLoad::fold`. That is relaxed/release atomics, an `exchange` and a `std::pow`, with no allocation.
  - `AudioEngine::noteLatePeriod`, which calls `LatePeriodLog::record`: stores into a fixed `std::array` ring and a sequence-numbered slot (one writer, the render thread).
  - `AudioEngineProfiler::finishPeriod` stores one more integer.
  - `renderStageNoteSetup` reads one more tick.

  None of these allocates, frees, locks, waits, does I/O or emits a signal. The proof is `aSteadyPeriodWithTheMetersAllocatesNothing` (0 allocations) and `AutomationRenderAllocationTest` still passing.

  Off-thread readers (`percent()`, `recent()`) only load atomics. `recent()` builds its vector on the control thread. TSan does not run on this box. The race reasoning: every shared field is a `std::atomic`; workers only add; the fold runs after `startAndWaitForJobs()`; the ring uses per-slot seqlock-style sequence numbers. That reasoning was not checked by TSan.
- **Projects.** Nothing is serialised: the meters are runtime members, with no save or load path and no format change. Old projects load and render exactly as before, and rendering itself is untouched (the probes only read the clock). `SessionPlaybackRenderTest` and `ArticulationPlaybackTest` pass. There is no new persisted data to round-trip.
- **Portability.**
  - Qt API: `QTableWidget`, `QDialog`, `QAction`, `QJsonArray`, `QPointer`, `QTimer` exist in Qt 5.15 and 6. I used no Qt-6-only API: sizes are cast with `static_cast<int>`, and `QJsonValue::toVariant().toULongLong()` exists in 5.15.
  - No `std::filesystem`, no POSIX calls and no windows.h macro names. No identifier is `small`/`near`/`far`/`interface`/`min`/`max`, and the new C++ calls no `min`/`max` at all.
  - Standard library: `<atomic>`, `<chrono>`, `std::as_const` (C++17). `std::atomic<tick_t>` / `<std::uint64_t>` are used as elsewhere in the engine.
  - Only Linux Qt 6 was built here. Hosted CI was not run.
- **Gates.**
  - The stock `feature_check.py` cannot run in this lane: it creates `<worktree>/../…-feature-check` outside my write-jail (`PermissionError`, feature-check-4.log). That is why `submit_check.py` reports "gates FAIL".
  - I ran a copy of it, changed only so its logs go to `/tmp/q074fc/logs` (same gates, same base cache). It passed every row: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length. Exit 0 (feature-check-3.log).
  - Registration: new files are in `tests/fork-sources.txt`; inherited files changed are in `tests/upstream-modifications.txt` with reasons; `tests/all-sources.txt` was re-derived and `all-sources-reproduce.sh` printed REPRODUCES.
- **Not done (stage 2, Q050-074.2):** backend xruns, negotiated device rates, period/buffer frames, device latency, and device readings marked unavailable.
