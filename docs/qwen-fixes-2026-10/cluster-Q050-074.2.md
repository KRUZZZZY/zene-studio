IMPLEMENTED

# Q050-074.2 - Audio-device timing and xrun diagnostics (stage 2/2 of Q050-074)

Branch `feat/050-Q050-074.2-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). Stage 1 (Q050-074.1) was already
merged into the base, so this stage builds on its `mixer.cpu_load` and CPU diagnostics window. Feature record:
`docs/050-features/Q050-074.2.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-074.2.r1/logs/`.

## Review round 1 (REWORK) - what was fixed

Blocking: a separately selected ALSA capture PCM can negotiate its own rate, but the report took the input
latency's milliseconds at playback's rate and did not expose the capture rate. Fixed in `f2f0830c30`:

- `DeviceTiming::inputSampleRate` (`include/AudioDevice.h`): the input's own rate. ALSA reports the capture
  PCM's granted rate: `m_captureGrantedRate` is set in `openCapture()`, cleared in `closeCapture()`, and only
  published while capture is open. JACK reports the server rate, the one clock for every port.
- `mixer.cpu_load.audio_device.input_sample_rate` (`{available, value, unit: "Hz"}`). `input_latency.ms` is
  now converted at the input rate and `output_latency.ms` at the output rate (`src/core/AudioDeviceDiagnostics.cpp`).
- The CPU diagnostics window has an **Input sample rate** row, read from the same reply.
- Regressions:
  - `AudioDeviceDiagnosticsTest::alsaCaptureAtItsOwnRateReportsInputLatencyAtThatRate` uses a real ALSA `null`
    capture PCM. The test interposes `snd_pcm_hw_params_set_rate_near` (the poll-descriptor test's technique;
    only the capture path calls it) and forwards to ALSA, so the capture is really granted 48 kHz, or 96 kHz
    when playback runs at 48. The slot checks:
    - the input rate equals the capture's granted rate and differs from `sample_rate`;
    - input ms equals the capture period divided by the capture rate, and does not equal it divided by the playback rate;
    - output ms is taken at playback's rate;
    - both input readings become unavailable after `closeCapture()`.
  - `CpuDiagnosticsDialogTest::theDeviceTableShowsInputAndOutputAtTheirOwnRates` uses a stub device (output
    44.1 kHz, input 48 kHz). The window shows "48000 Hz" and "480 frames (10.00 ms, ...)", which are the
    command's values.
- The existing slots and socket tests also check `input_sample_rate`: unavailable without a device or
  capture, 48000 on JACK.

Round-1 runs (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-074.2.r2/logs/`):

| Step | Result |
|---|---|
| Build `zene`, both QtTests, `tripleoscillator` | EXIT=0 (build-1.log) |
| `./AudioDeviceDiagnosticsTest` | EXIT=0, 6 passed, 0 skipped (test-1.log) |
| `./CpuDiagnosticsDialogTest` | EXIT=0, 5 passed, 0 skipped (test-2.log) |
| ctest, the 4 focused tests | EXIT=0, 4/4 (test-3.log) |
| Snapshot regenerated live | EXIT=0, 878 ids / 77 groups; `doc-figures-gate.py` EXIT=0 |
| Gates (`feature_check.py` copy, see Gates) | 8/8 PASS, EXIT=0 |
| **Revert A**: the 8 product hunks of `f2f0830c30` reversed | `zene` EXIT=0. QtTests do not build: EXIT=2, no `inputSampleRate` / `m_captureGrantedRate` (build-3/4-revert). Socket ctest EXIT=8: `ControlCpuDiagnostics` "input_sample_rate is unavailable, not zero: None"; `ControlDeviceDiagnostics` "the input rate is the server's 48000 Hz: {}" (test-4-revert.log) |
| **Revert B** (behavioural): only the input-latency conversion put back to the output rate, APIs kept | build EXIT=0 (build-8-msrevert.log). `AudioDeviceDiagnosticsTest` EXIT=1: the new ALSA slot fails, input ms 5.80 instead of capture period / capture rate (test-8). `CpuDiagnosticsDialogTest` EXIT=1: the new window slot fails, 10.884 ms instead of 10.0 (test-9). A first attempt at Revert B did not build (unused-variable `-Werror`), so its runs used stale binaries and were discarded |
| Restored (`git checkout HEAD`), rebuilt | build EXIT=0; ctest 4/4, EXIT=0 (test-10-final.log) |

## What changed

- `AudioDevice::DeviceTiming` + virtual `deviceTiming()` (empty by default) + `backendXruns()` /
  `countBackendXrun()` (relaxed atomic) - `include/AudioDevice.h`.
- JACK: `jack_set_xrun_callback` -> `AudioJack::xrunCallback`; `deviceTiming()` = server rate, period
  (`jack_get_buffer_size`), first ports' latency ranges; buffer unavailable - `src/core/audio/AudioJack.cpp`.
- ALSA: playback underrun (`-EPIPE` in `handleError`) counted; granted rate read back after
  `snd_pcm_hw_params`; `deviceTiming()` = rate, period, buffer, output latency = playback buffer, and while
  capture is open the capture PCM's own granted rate plus one capture period as input latency (round 1) -
  `src/core/audio/AudioAlsa.cpp`.
- `audioDeviceReport()` (new `src/core/AudioDeviceDiagnostics.cpp`) -> `mixer.cpu_load.audio_device`; each
  reading `{available, value, unit}`, unavailable readings carry no value.
- CPU diagnostics window: **Audio device** table filled from the same `mixer.cpu_load` reply
  (`src/gui/shell/CpuDiagnosticsDialog.cpp`).
- Manual (`data/manual/index.html`, The window > Header), feature record, registrations, live snapshot.

## Acceptance

| # | Criterion | Test (command) | Result |
|---|---|---|---|
| 1 | JACK/ALSA: UI and headless query expose backend xruns apart from engine late periods, negotiated rates, period/buffer frames, input/output latency; unsupported = unavailable, not zero | `AudioDeviceDiagnosticsTest` slots `alsaReportsWhatThePcmGrantedAndCountsUnderruns` (ALSA `null` PCM) and `jackReportsTheServersTimingAndXruns` (private `jackd -d dummy` 48 kHz/256; a render spun 3 periods long raises BOTH `late_periods.count` and `xruns`); `ControlDeviceDiagnostics` (real binary on JACK over the socket: 48000 Hz, 256 frames, buffer unavailable, latencies with ms + basis, xruns count, late_periods separate); `CpuDiagnosticsDialogTest::theDeviceTableShowsTheCommandsDeviceState` (window opened by triggering the CPU readout's action; ALSA rows equal the command's values) | `ctest -R '^(AudioDeviceDiagnosticsTest\|CpuDiagnosticsDialogTest\|ControlCpuDiagnostics\|ControlDeviceDiagnostics)$'` from `build/tests`: EXIT=0, 4/4 passed (test-9.log, on HEAD). Direct runs: `./AudioDeviceDiagnosticsTest` EXIT=0, 5 passed **0 skipped** (test-2.log); `./CpuDiagnosticsDialogTest` EXIT=0, 4 passed 0 skipped (test-3.log). JACK slot repeated 5x: EXIT=0 each (test-4-1..5.log) |
| 2 | No-device headless query still returns CPU/history and marks device-only values unavailable; UI and command read the same state | `AudioDeviceDiagnosticsTest::noDeviceStillReportsCpuAndMarksDeviceReadingsUnavailable`; `ControlCpuDiagnostics` check 6 (real binary, Dummy device: backend Dummy, `reports_timing` false, every device reading `available:false` with no value, engine numbers, `engine_percent`/`tracks`/`late_periods` in the same reply); `CpuDiagnosticsDialogTest::theDeviceTableShowsTheCommandsDeviceState` (Dummy rows read "unavailable"; engine-rate, engine-period and late-period rows equal the command's) | same runs as above, EXIT=0 |

### Revert proof (product hunks reversed, rebuilt, run, restored)

`git diff HEAD~1 HEAD -- <11 product files> | git apply -R` (AudioDevice.h, AudioAlsa.h/.cpp, AudioJack.h/.cpp,
AudioDeviceDiagnostics.h/.cpp, src/core/CMakeLists.txt, ControlCommandsTrackLooks.cpp, CpuDiagnosticsDialog.h/.cpp):

| Test | Reverted | Restored (`git checkout HEAD -- <files>`, rebuilt) |
|---|---|---|
| `zene` build | EXIT=0 (build-5-revert.log) | EXIT=0 (build-8-restore.log) |
| `AudioDeviceDiagnosticsTest` | build EXIT=2 - no `m_grantedRate`, no device-timing API (build-6-revert-test1.log) | Passed |
| `CpuDiagnosticsDialogTest` | build EXIT=2 - no `CpuDiagnosticsDialog::audioDevice()` (build-7-revert-test2.log) | Passed |
| `ControlCpuDiagnostics` | **Failed** at runtime: "mixer.cpu_load has no audio_device" | Passed |
| `ControlDeviceDiagnostics` | **Failed** at runtime: every JACK reading missing | Passed |

ctest on the reverted binary: EXIT=8 (test-7-revert.log); restored: EXIT=0, 4/4 (test-8-restored.log). The two
QtTests fail by not compiling (they use the API this change adds); the two socket tests fail at runtime.

## Gates

- Gates (`feature_check.py`): all 8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity, file-length), EXIT=0 (feature-check-4.log).
  **Note:** the stock script writes its logs to `<worktree>-feature-check`, outside this session's
  write-jail (PermissionError, feature-check-1.log), so I ran a copy at `/tmp/q0742-fc/feature_check.py`.
  The only change in the copy is that the logs go to `/tmp/q0742-fc/logs`. Gates, ROOT and baselines are
  unchanged. The first run flagged two functions over the complexity target; I split both (commit
  `29133e1f84`) and the rerun passed. `submit_check.py` reports "gates FAIL" for the same jail reason (it
  calls the stock script).
- Snapshot regenerated live with `snapshot_live.py`: EXIT=0, 878 ids / 77 groups (unchanged count; only
  `mixer.cpu_load`'s description changed). `python3 tests/doc-figures-gate.py`: EXIT=0 PASS.
- `bash tests/all-sources-reproduce.sh`: REPRODUCES, EXIT=0.

## Self-check

- **Each new test fails with the product change reverted and passes restored:** yes. See the round-0 revert
  table below and the round-1 Reverts A and B above. In Revert B the two new input-rate slots fail at
  runtime, EXIT=1 each.
  Reverted: QtTest builds EXIT=2, socket ctest EXIT=8 (both failed). Restored: EXIT=0, 4/4.
- **Every acceptance criterion is proven by a test I ran:** criterion 1 is covered by AudioDeviceDiagnosticsTest
  (ALSA and JACK slots), ControlDeviceDiagnostics and CpuDiagnosticsDialogTest. Criterion 2 is covered by
  AudioDeviceDiagnosticsTest (no-device slot), ControlCpuDiagnostics and CpuDiagnosticsDialogTest. All EXIT=0
  (round 0: test-9.log; round 1: r2/test-3.log and test-10-final.log, 4/4). The round-1 input-rate fix is
  proven by the two new slots. Nothing was skipped on this box: jackd 1.9.22 and the ALSA null PCM are both present.
- **One undo step per edit / honest reversibility:** there is no edit and no new command. `mixer.cpu_load` stays
  `mutating: false`, keeps its `not_mutating` row, and keeps an empty argument schema; only its description and
  reply grew. No undo step is needed, and the A16 histogram is unchanged because no command was added and no class changed.
- **Realtime:** two additions run on audio/backend threads:
  - `AudioAlsa::handleError` (playback thread) calls `countBackendXrun()` on `-EPIPE`.
  - `AudioJack::xrunCallback` (JACK's notification thread) calls `countBackendXrun()`.

  `countBackendXrun()` is a single `std::atomic<uint64_t>::fetch_add(relaxed)`: it does not allocate, lock,
  wait, do I/O or emit a signal. Everything else runs on the main thread when the command is invoked:
  `deviceTiming()`, `jack_get_*` and `jack_port_get_latency_range` (JACK client queries, off the process
  thread), and the JSON and the widget. The ALSA readings are fields written in the constructor; the capture
  period is written in `openCapture()` on the main thread and gated by the `m_captureOpen` atomic. TSan does
  not run on this box, so this race-freedom is reasoned from the design, not measured.
- **Old projects load and render as before; new data round-trips:** nothing is serialised and no project
  format changed. These are runtime readings only, so there is nothing to round-trip. Rendering is unchanged:
  the ALSA and JACK process and write paths only gained the counter add.
- **Portability:**
  - The new API is `std::optional`, `std::atomic` and `QString` (C++17, Qt 5.15 and Qt 6). There is no
    Qt-6-only call.
  - `QList<std::pair<...>>` and `QJsonObject` work on both Qt versions.
  - No identifier is `small`/`near`/`far`/`interface`/`min`/`max`, and nothing uses `std::filesystem`.
  - JACK and ALSA code stays inside the existing `LMMS_HAVE_JACK` / `LMMS_HAVE_ALSA` files. The test guards
    both, and `QSKIP`s when jackd or the null PCM is missing.
  - `jack_set_xrun_callback` is in weakjack's table (`weak_libjack.def:27`) and in the standard JACK API.
  - I did not build Qt 5.15, MSVC or macOS locally; this is reasoned, not compiled.

## Known limits

- JACK: the buffer (period count) is unavailable. JACK's API does not expose it.
- ALSA output latency is the playback buffer size (the queue a blocking writer keeps full), not a measured
  `snd_pcm_delay`. Calling into the PCM from the main thread while the playback thread writes is unsafe.
- ALSA capture overruns stay in `record.input_get_state` and are not counted as playback xruns.
- The other backends (PulseAudio, PortAudio, SDL, OSS, sndio, soundio, Dummy) report every device reading as
  unavailable.
- The xrun count starts at 0 for each device object.
