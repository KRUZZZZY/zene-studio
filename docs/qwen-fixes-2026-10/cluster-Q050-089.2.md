IMPLEMENTED

# Q050-089.2 - Real-time hardware insert export (stage 2 of 3)

Branch `feat/050-Q050-089.2-oc`, cut from `c6ee4fc7bc` (050/m0-train-20). Commits: `cc85c23b25` (code + tests),
`80276125e7` (docs, live snapshot, A16), the refactor commit (complexity split, no behaviour change), then this report.
Details in `docs/050-features/Q050-089.2.md`.

The new command `render.realtime {out, tail_ms}` plays the song through the running device, so each enabled hardware
insert's send goes out and its return is recorded. The master mix is captured into a WAV by
`RealtimeExportCapture` (`include/RealtimeExport.h`), which is tapped in `Mixer::masterMix`. The delay-compensation
head is dropped, so frame k of the file is song frame k, the same as offline. The reply gives the path, frames,
seconds, sha256, the compensation dropped and every insert played. The command refuses, writes nothing and never
falls back to offline in these cases:
- no insert is on (`requires`);
- the device delivers no input, or an insert's send or return is beyond what the device opened (`refused`, naming the
  channel, the pair and the status);
- something else is busy (`busy`);
- the song is longer than 600 s.

`render.render`'s reply now has `hardware_inserts` (`offline`, hardware `unavailable`, `bypassed`, the inserts). GUI:
the channel's **Hardware I/O > Render Through Hardware (Real Time)...** runs the same command through its form.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-089.2.r1/logs/` (build-N / test-N).

## Acceptance

| Criterion | Test (command) | Exit with change | Exit with product hunks reverted |
| --- | --- | --- | --- |
| A real-time render captures the processed hardware return into a WAV that opens and can be measured; the command returns its file path and details. | `ctest -R '^ControlRealtimeExport$'`. Real `zene`, Dummy loopback 3/4 → in 1/2 through a 0.5 gain box. The file matches the offline render x 0.5 sample for sample (ratio 0.5 ± 0.01, residual < 0.002) and is frame-aligned (tone onset 37800 in both). Reply path/frames/sha256 match the file; it names insert 812 `measured` and the 812-frame compensation. `meter.measure_file` reads 6 dB below dry | 0 (test-3, test-9, test-11, test-12) | 8: 6 of 13 checks FAILED (test-10-reverted) |
| (GUI, same registered command) | `ctest -R '^RealtimeExportMenuTest$'`. The menu entry (`controlCommand` = `render.realtime`) is triggered and the form accepted. The file exists, the form shows its path, and the true peak is about -12 dBTP (processed; dry would be -6) | 0 (test-9, test-11, test-12) | 8: "the Hardware I/O menu has no real-time render entry" (test-10-reverted) |
| (capture unit + realtime) | `ctest -R '^HardwareIoTest$'` `theRealtimeCaptureTrimsEndsAndAllocatesNothing`. 0 allocations while pushing; head trim, end-tick-mid-period end + tail, gain | 0 (test-6, test-12) | build fails, exit 2: `RealtimeExport.h` missing (build-13-reverted-hwio) |
| Offline export remains dry and identifies hardware as unavailable and bypassed; unavailable real-time hardware produces a clear refusal. | `ctest -R '^ControlRealtimeExport$'`. `render.render` with the insert on writes the dry tone (peak 0.5), and `hardware_inserts` = offline / unavailable / bypassed / that channel. `render.realtime` is refused with nothing written: with no insert (`requires`); with send 7/8 on a 4-output device (`refused`, names `7/8` + `beyond_device`); and on a second instance with no input (`refused`, "delivers no input") | 0 | 8 (same run) |
| (refusal in the GUI) | `RealtimeExportMenuTest::aSendTheDeviceCannotCarryIsRefusedInTheForm`. The form shows `beyond_device` and no file is written | 0 | 8 |

Regression runs with the change, all exit 0 (test-9, test-11, test-12): `ControlHardwareIo`, `ControlPdcCommands`,
`MixerWindowsTest` (it walks the Hardware I/O menu), `MixerInsertLatencyTest` and `PdcMixerTest` (test-9),
`ControlRenderPresets` (render.render replies), `ReversibilityContractTest`, `ControlCommandsSnapshot` and `DocFiguresGate`.

## Self-check

- **Each new test fails reverted and passes restored.** I ran `git diff c6ee4fc7bc HEAD -- src include > /tmp/q0892-product.patch`
  and then `git apply -R`. The rebuild of `zene RealtimeExportMenuTest` gave BUILD_EXIT=0. `HardwareIoTest` would not compile
  (exit 2). `ctest -R '^(ControlRealtimeExport|RealtimeExportMenuTest)$'` gave TEST_EXIT=8, and both failed. With
  the product reverted, the GUI test crashed in its second case on a null action. I fixed that test, but only to guard
  against the crash: it now fails cleanly instead. I restored the product with `git checkout HEAD -- src include` and
  rebuilt (BUILD_EXIT=0). All 10 tests then passed (test-11, TEST_EXIT=0). After the complexity refactor I ran it again
  (build-15 EXIT=0, test-12 TEST_EXIT=0).
- **Every criterion proven by a test I ran:** see the table. Each criterion has a socket transcript and a GUI test,
  and the capture also has a unit test.
- **Undo / reversibility:** `render.realtime` is `not_mutating`. It has a new row in `ControlReversibilityTablePassive.cpp`, and
  the A16 histogram line was updated (`ReversibilityContractTest` exit 0). It writes only an output file. The loop
  switch and the metronome are put back after the pass, and the transport stops as a Stop would. Nothing goes into the
  undo history because nothing in the project changes. `render.render` stays `not_mutating`; its result schema gains
  `hardware_inserts`. I regenerated the snapshot from a live instance: 872 ids / 76 groups, README canary updated,
  `doc-figures-gate.py` PASS.
- **Audio thread:** these run on it: `realtimeexport::master()`, then `RealtimeExportCapture::push/noteEnd/finishIfDone/keep`
  (called from `Mixer::masterMix`), and the Dummy loopback's gain multiply (`AudioDummy::feedBack`, the fixture's device
  thread).
  - While nothing is armed, this costs one acquire load.
  - While armed, it reads the song position, which is the render thread's own state. It also reads `m_playing`, which
    is ordered by the change lock that both arm and play run under.
  - It scales into a fixed 512-frame member array and writes the accumulator's pre-allocated lock-free ring.
  - It never allocates, frees, locks, waits, does I/O or emits a signal. The allocation counter in `HardwareIoTest`
    measured 0 allocations.
  - The accumulator is created in `arm()` on the control thread, before the release store. It is taken back only once
    the state is Done, which is terminal for the audio thread. A timed-out pass is cancelled under the change lock.
  - The WAV is written on the control thread.
  - Local TSan does not run on this box, so this is reasoned from the design, not measured.
- **Old projects:** nothing new is saved or loaded. An offline export takes exactly the path it took before, and the
  tap only costs a load. The Dummy gain defaults to 1, so the loopback is unchanged when it is not set (`ControlHardwareIo`
  passes and still measures 812).
- **Portability:**
  - No Qt-6-only API: I used `QElapsedTimer`, `QThread::msleep`, `QDir`, `QFile::encodeName` and `qEnvironmentVariable`.
    `qEnvironmentVariable` is in Qt 5.10+.
  - libsndfile, which the core already links.
  - No `std::filesystem`, no POSIX calls, and no `min/max/near/far/small/interface` identifiers.
  - `std::llround` / `std::clamp` with `<cmath>` / `<algorithm>`.
  - The `0LL` / `long long` clamp types match on MSVC too.
- **Gates:** `feature_check.py` reports all 8 PASS. It writes its logs beside the worktree, which is outside my write
  jail, so I ran a /tmp copy whose only change is the log directory (`/tmp/q0892fc/feature_check.py`). It first flagged
  3 functions over CCN 10, and I split them.

## Notes

- **Stage boundary:** the project export dialog does not offer real-time export yet. That is stage 3. The GUI entry
  in this stage is the mixer channel's Hardware I/O menu, which goes through the same registered command.
- **Found, not fixed:** if a track was created before the mixer channel, `track.set_mixer_channel` replied with the
  new channel, but in 3 of 4 debug runs the track still played into the master. The tests create the channel first.
  This may be worth a bug-hunt entry.
- **Fixed a busy-check mistake:** `Song::isRecording()` stays true after a Stop (an inherited quirk), so the busy check
  now treats a recording as running only while the transport rolls.
