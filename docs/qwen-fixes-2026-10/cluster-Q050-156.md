IMPLEMENTED

# Q050-156 - Record directly into a sampler (integrated onto 050/m0-train-16)

Integration job: the accepted patch `feat/050-Q050-156-oc` @ `e76cb62c7` was applied with a
three-way merge onto `050/m0-train-16` @ `cf3618e61`. The accepted design and its regression
tests are kept unchanged; only the conflicts were resolved, plus the figures the train moved.
All logs are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-156.r7/logs/`.

## Conflicts and how they were resolved

| File | Conflict | Resolution |
|---|---|---|
| `src/core/AudioEngine.cpp` | train fix new-p12-200 gave `m_retroCapture.push(...)` a sample-rate argument; the patch adds the sampler tap after it | kept the train's call; the accepted `m_samplerTakeRecorder.push(...)` follows it unchanged |
| `plugins/AudioFileProcessor/AudioFileProcessorView.{h,cpp}` | train Q050-103 added attack/release knobs and a voice-limit box; the patch adds the Record take button | both kept (includes and members). The button stays at (5, 148), clear of the knob rows (y 62, 108) and the waveform (y 172) |
| `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | neighbouring list additions | both sides kept |
| `tests/all-sources.txt` | neighbouring additions | re-derived; `SamplerTakeTest.cpp` moved to its `LC_ALL=C` position. The header's verify recipe prints `REPRODUCES` |
| `README.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `commands_snapshot.json` | figures | kept the train's text, then regenerated the snapshot live (853 ids / 76 groups), set the canary to 853 and re-took the A16 block: `rows=862 true_inverse=426 snapshot=42 irreversible=24 not_mutating=370` (train's 858 + the 4 rows) |
| `tests/src/core/ControlRegistryTest.cpp` (telemetry-OFF only, applied cleanly) | figure moved with the train | 842 -> 858. **Derived, not measured**: 842 was measured on the accepted branch (snapshot 837); the train added 16 ids, none `telemetry.*` (checked by diffing the two snapshots). There is no telemetry-OFF build tree on this box and building one is a full 45 GB tree, so this was not run. The test is compiled only with `-DZENE_TELEMETRY=OFF`; the normal `ControlRegistryTest` passes |

No semantic difference from the accepted patch except the AudioEngine call order, which is
the train's call followed by the accepted one.

## Acceptance

| # | Criterion | Command run | What was seen | Exit |
|---|---|---|---|---|
| 1 | Record a bounded input fixture into each supported device; the resulting sample is loaded and saved | `ctest -R '^(SamplerTakeTest\|SamplerTakeGuiTest\|ControlRegistryTest\|ReversibilityContractTest\|ControlCommandsSnapshot\|AudioFileProcessorPlayheadTest\|SlicerTPlayheadTest\|SamplerEnvelopeTest\|SamplerVoiceReuseTest)$' --output-on-failure` from `build/tests` (test-5-restored.log) | `SamplerTakeTest` passed: `audioFileProcessorTakesBoundedInputAndPlaysIt` and `slicerTTakesBoundedInputAndSlicesIt` record a deterministic stereo fixture, check the WAV frame by frame, render a note, check SlicerT slices, and save and reopen the project. 9/9 passed | 0 |
| 2 | Expose the action through GUI and agent command tests | same run (test-5-restored.log) | `SamplerTakeGuiTest` clicks the real `audioFileProcessorRecordTake` and `slicerTRecordTake` buttons (`QTest::mouseClick`); `SamplerTakeTest::theAgentSurfaceRegistersTheTakeCommands` checks the four ids, schemas and A16 classes; `ControlCommandsSnapshot` matches the live-regenerated snapshot | 0 |
| - | One undo step, honest reversibility (DoD 3) | same run | undo/redo assertions in both device cases; `ReversibilityContractTest` passes with the re-taken 862-row histogram | 0 |
| - | The train's sampler work still holds (no regression) | same run | `SamplerEnvelopeTest`, `SamplerVoiceReuseTest` (Q050-103, the knobs next to the button), `AudioFileProcessorPlayheadTest`, `SlicerTPlayheadTest` all passed | 0 |

Supporting runs:

- Build: `cmake --build build --target zene SamplerTakeTest SamplerTakeGuiTest ReversibilityContractTest audiofileprocessor slicert -- -j4` (build-4.log, exit 0). build-1 to build-3 failed or were cut off for environment reasons only: build-1 and build-2 had a ccache temp dir outside the write jail (fixed with `CCACHE_TEMPDIR=/tmp/q156-ccache-tmp`), and build-3 was stopped by the tool's 10-minute limit.
- First test run (test-1.log, exit 8): `ReversibilityContractTest` failed on the stale A16 block (measured 853 = 849 + 4), and four neighbour tests were "Not Run" because they were not built yet. After re-taking the block and building them (build-5.log, exit 0): test-2.log, 7/7, exit 0.
- Snapshot: `snapshot_live.py $PWD` (snapshot-1.log, exit 0, 853 ids / 76 groups); `python3 tests/doc-figures-gate.py` (docfig-1.log, exit 0, PASS).
- `ControlRegistryTest` + `ControlCommandsSnapshot` + the three Q050-156 tests (build-6.log exit 0; test-3.log, 5/5, exit 0).
- **Reversal proof on the integrated tree:** `git checkout cf3618e61 --` the six product files (`src/core/AudioEngine.cpp`, `src/core/ControlRegistryRegistrations.cpp`, both AudioFileProcessor view files, both SlicerT view files); rebuilt (build-7-reversal.log, exit 0); `ctest -R '^(SamplerTakeTest|SamplerTakeGuiTest)$'` exit 8 (test-4-reversal.log): `no command 'record.sampler_capture'`, `record.sampler_capture is not registered`, `AudioFileProcessorView has no Record take button`, `SlicerTView has no Record take button`. Restored with `git checkout HEAD --`, rebuilt (build-8-restored.log, exit 0), and the 9-test run above passed (test-5-restored.log, exit 0).
- Gates: `feature_check.py` cannot write its default log directory (`<worktree>-feature-check`, outside the write jail; feature-check-1.log, PermissionError, exit 1). I ran a copy whose only changes are `ROOT` pinned to the original directory and logs sent to `/tmp/q156-fc/logs` (feature-check-2.log, exit 0): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS. The base listings came from the existing cache for `cf3618e61`.

## Hazards considered (failure paths and supported platforms)

- Realtime: the only audio-thread addition is the accepted one-line `m_samplerTakeRecorder.push()` tap. Idle cost is one relaxed atomic load; the ring is allocated in the control-thread `arm()`. The integration did not change it.
- Rate change mid-take: the train made the retrospective window rate-homogeneous (new-p12-200). The sampler take is labelled with the input rate read when it is armed, so a device change during a take of 30 s or less could mislabel frames. This is the accepted design and is not a regression. It is listed under Known limits in `docs/050-features/Q050-156.md`.
- AFP view layout: the train's knobs and the button do not overlap (coordinates above). The button is still found by object name and clicked by `SamplerTakeGuiTest`.
- Portability: the merged hunks add no API. The view only gains the train's existing `Knob.h`/`LcdSpinBox.h` includes beside the accepted ones. There is no Qt-6-only call, no `std::filesystem` and no reserved identifier.
- Telemetry-OFF builds: covered by the derived 858 above; not measured.

## Self-check

**? Each new test FAILS with your product change reverted, then PASSES restored.**
Reverted the six product files to `cf3618e61`, rebuilt (build-7-reversal.log exit 0), and `SamplerTakeTest` + `SamplerTakeGuiTest` failed (test-4-reversal.log exit 8, messages above). Restored them, rebuilt (build-8-restored.log exit 0), and they passed (test-5-restored.log exit 0).

**? Every acceptance criterion is proven by a test you ran yourself.**
Criterion 1: `SamplerTakeTest` (test-5-restored.log, exit 0). Criterion 2: `SamplerTakeGuiTest`, `SamplerTakeTest::theAgentSurfaceRegistersTheTakeCommands` and `ControlCommandsSnapshot` (same log, exit 0).

**? GUI and agent go through the same registered command; the GUI test drives the control.**
Both views' buttons call `record.sampler_capture*` through `ControlRegistry` (`src/gui/SamplerTakeButton.cpp`). `SamplerTakeGuiTest` uses `QTest::mouseClick` on each button. With the registration reverted, the command calls fail (test-4-reversal.log).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
Unchanged from the accepted patch: `record.sampler_capture_land` is `true_inverse` (one track checkpoint), and capture/stop/status are `not_mutating`. Both device tests assert that one `control.undo` restores the previous sample and `control.redo` re-lands it. `ReversibilityContractTest` passes with the 862-row block (test-5-restored.log).

**? Snapshot regenerated live, doc figures, A16 histogram.**
snapshot-1.log exit 0 (853 / 76); doc-figures-gate exit 0; A16 block re-taken, and `ReversibilityContractTest` passes.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
The integration adds no audio-path code. The accepted tap is one relaxed atomic load while idle; while recording it does a fixed-capacity ring write (see `include/SamplerTakeRecorder.h`). Local TSan does not run on this box; the thread boundary is reasoned in that header.

**? Old projects still load and render exactly as before; new data round-trips.**
No format change. The devices keep their `src`/`sampledata` and SlicerT slice formats. `SamplerTakeTest` saves and reopens with the sample and slice count intact. The AFP/SlicerT playhead, envelope and voice-reuse tests pass (test-5-restored.log).

**? Portability (Qt 5.15, MSVC, macOS < 10.15).**
The conflict resolution adds only existing includes and members; there is no new API. The accepted code uses Qt 5/6-common classes only, with no `std::filesystem`, no POSIX-only calls and no reserved identifiers.

**? Docs.**
`data/manual/index.html` (from the accepted patch, applied cleanly) and `docs/050-features/Q050-156.md` now have an "Integration onto 050/m0-train-16" section and the rate-change limit.
