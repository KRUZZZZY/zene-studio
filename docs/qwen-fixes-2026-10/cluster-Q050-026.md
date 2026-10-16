IMPLEMENTED

Both acceptance criteria are met and proven by tests I ran and saw pass in this
worktree (branch feat/050-Q050-026-oc, base de5a85a96).

| Criterion | Test / command | Evidence | EXIT |
|---|---|---|---|
| A regression test configures two MIDI ports and proves clock events from the unselected port are ignored while selected-port events update clock state | `ctest -R ^MidiClockSourceTest$ --output-on-failure` from `build/tests` (new executable, 9 slots) | `onlyTheConfiguredSourceCanDriveTheFollower`: 48 pulses at 140 BPM from the UNCHOSEN port leave `pulses` 0, `locked` false, `tempo_bpm` 0, `Song::getTempo()` 100, `ignored` +48; the CHOSEN port's 48 pulses lock at 140 and `applyMeasuredTempo()` writes 140. Also `aSecondClockingDeviceDoesNotReachTheFollower` (two devices clocking in the same ms), `theSourceMatchIsTheWholeName`, `switchingTheSourceForgetsTheMeasurement`, `anySourceIsFollowedWhileNoneIsChosen` (raw `parseData` bytes), `aNameTheFollowerCannotHoldIsRefused`, `theStateReportsTheSourceAndTheIgnoredCount`, `oneUndoStepRestoresThePreviousSource`, and **r2's** `concurrentReconfigurationNeverAdmitsAnotherPort` (a real writer thread races a real reader; every pulse from a port that is not the published one is dropped). 9 passed / 0 failed | 0 |
| (the same test is a real regression, not a new-green) | pre-fix probe: `acceptsSource()` temporarily forced to `return true`, rebuild, `ctest -R ^MidiClockSourceTest$` | the deterministic slots FAIL, e.g. `the unselected port fed 48 pulses`; log `regression-proof-pre-fix-failures.log` | 8 (expected: the probe must fail) |
| A control-socket test reads back the configured source and follower state | `ctest -R ^ControlClockCommands$ --output-on-failure` from `build/tests` (real `build/zene` over `--control-socket`, 63 checks ok) | `clock.slave_set answers with the source it was given`; `clock.get_state reads the source back beside the follower state` (source_port + enabled + follow_tempo + locked + pulses + tempo_bpm + ignored); `the source vocabulary is midi.device_list's readable ports`; `a switched source is the one the next read reports`; `one control.undo puts the previous source back`; `a source name the engine cannot hold is invalid_args`; `the refused source changed nothing`. Log `control-clock-commands-transcript.log` | 0 |
| GUI half of the same action (Edit > MIDI Clock > Clock Source) | `ctest -R ^MidiClockMenuTest$ --output-on-failure` | `theSourceSubmenuDrivesTheSameCommand`: the entry reaches the engine through `clock.slave_set`, keeps the follower's mode, and offers exactly what `midi.device_list` offers. 6 passed / 0 failed | 0 |
| Existing tests in the area stay green | `ctest -R '^(MidiClockTest\|MidiClockSourceTest\|MidiClockMenuTest\|ControlClockCommands)$'` and `ctest -R '^(MidiOutQueueTest\|ReversibilityContractTest\|ControlCommandsSnapshot\|agent_surface\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest)$'` from `build/tests` | both runs 100% passed, 0 failed (4 + 6 = the same ten tests) | 0 |
| Build | `cmake --build build --target zene MidiClockTest MidiClockSourceTest MidiClockMenuTest -- -j4` | linked clean | 0 |
| Gate 1 fork sources | `bash tests/fork-sources-gate.sh` | "every tracked source in scope is registered (1793 fork-NEW, 1212 inherited, 48 tooling)", 0 stale | 0 |
| Gate 9 all-sources recipe | `bash tests/all-sources-reproduce.sh` | REPRODUCES | 0 |
| Gate 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | "every change to upstream-inherited code ... is declared (1497 changed paths)" | 0 |
| Gate 3 no tautology | `bash tests/no-tautology-gate.sh` | no violation (incl. the new test file) | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 errors | 0 |
| Gate 4 complexity | `bash tests/complexity-gate.sh --check` | red on train 5 BY DESIGN; **r2** removed the one function this unit had added (`check_the_configured_source_is_read_back`, CCN 11) by splitting it into `source_read_back_is_consistent` (CCN 7) and `check_the_refused_source_changes_nothing` (CCN 1); no function of mine is listed | 1 |
| Gate 7 file length | `bash tests/file-length-gate.sh --check` | red on train 5 BY DESIGN; no file of mine newly listed (new test 265 code lines, `control-clock-commands.py` 476, `MidiClock.cpp` 417) | 1 |
| `git diff --check` | over the attempt | **r2** removed the two space-before-tab indents at `MidiAlsaSeq.cpp` 765/768 (restored to the original tabs) | 0 |
| Commands snapshot canary | `python3 tests/doc-figures-gate.py` | "snapshot measures 828 ids / 76 groups ... RESULT: PASS" - unchanged, no new command id | 0 |

What changed: the engine half (the follower's input entry point takes the source
the message arrived on; anything else is dropped and counted in `clock.get_state`'s
new `slave.ignored`; switching the source forgets the measurement; the ALSA
sequencer passes the sender's port name, a raw client passes an empty name which is
the truth about one device) and the surface half (Edit > MIDI Clock > Clock Source
lists what `midi.device_list` lists through the same registered `clock.slave_set`;
an unholdable name is refused `invalid_args`). File and line detail:
docs/050-features/Q050-026.md.

Rework r2 (Luna verdict REWORK) addressed every point in the code review: (1) the
published source name is now an array of `std::atomic<quint16>` code units, so the
control-thread writer and the MIDI-reader-thread reader no longer race on the
buffer by the C++ memory model (the sequence counter keeps the snapshot consistent,
it no longer papers over a plain array); a concurrency slot races a real writer
against a real reader and `MidiClockSourceTest` is registered in the hosted TSan
job; (2) `check_the_configured_source_is_read_back` was split below the CCN target
without dropping an assertion; (3) the two space-before-tab indents at
`MidiAlsaSeq.cpp` 765/768 are gone, so `git diff --check` is clean.

Honest limits, all stated in docs/050-features/Q050-026.md and
docs/KNOWN-LIMITATIONS.md: a client with ONE unnamed input (ALSA Raw-MIDI, JACK,
OSS, sndio, the dummy) cannot be narrowed and only follows "any input"; the
sequencer sender name is cached for the client's life; WinMM and CoreMIDI never
deliver the clock family in this tree; the control-socket transcript cannot inject
an incoming clock, so the drop half is proved by the QTest over the same entry
point; ThreadSanitizer does not run on this box (it crashes at thread creation), so
the publication is made race-free by construction and covered by the concurrency
slot and the hosted TSan job, and is not measured locally.