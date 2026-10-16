IMPLEMENTED

Both acceptance criteria are met and proven by tests I ran and saw pass in this
worktree (branch feat/050-Q050-026-oc, base de5a85a96).

| Criterion | Test / command | Evidence | EXIT |
|---|---|---|---|
| A regression test configures two MIDI ports and proves clock events from the unselected port are ignored while selected-port events update clock state | `ctest -R ^MidiClockSourceTest$ --output-on-failure` from `build/tests` (new executable, 8 slots) | `onlyTheConfiguredSourceCanDriveTheFollower`: 48 pulses at 140 BPM from the UNCHOSEN port leave `pulses` 0, `locked` false, `tempo_bpm` 0, `Song::getTempo()` 100, `ignored` +48; the CHOSEN port's 48 pulses lock at 140 and `applyMeasuredTempo()` writes 140. Also `aSecondClockingDeviceDoesNotReachTheFollower` (two devices clocking in the same ms), `theSourceMatchIsTheWholeName`, `switchingTheSourceForgetsTheMeasurement`, `anySourceIsFollowedWhileNoneIsChosen` (raw `parseData` bytes), `aNameTheFollowerCannotHoldIsRefused`, `theStateReportsTheSourceAndTheIgnoredCount`, `oneUndoStepRestoresThePreviousSource`. 8 passed / 0 failed | 0 |
| (the same test is a real regression, not a new-green) | pre-fix probe: `acceptsSource()` temporarily forced to `return true`, rebuild, `ctest -R ^MidiClockSourceTest$` | 5 of 8 slots FAIL, e.g. `the unselected port fed 48 pulses`; log `regression-proof-pre-fix-failures.log` | 8 (expected: the probe must fail) |
| A control-socket test reads back the configured source and follower state | `ctest -R ^ControlClockCommands$ --output-on-failure` from `build/tests` (real `build/zene` over `--control-socket`, 64 checks ok) | `clock.slave_set answers with the source it was given`; `clock.get_state reads the source back beside the follower state` (source_port + enabled + follow_tempo + locked + pulses + tempo_bpm + ignored); `the source vocabulary is midi.device_list's readable ports`; `a switched source is the one the next read reports`; `one control.undo puts the previous source back`; `a source name the engine cannot hold is invalid_args`; `the refused source changed nothing`. Log `control-clock-commands-transcript.log` | 0 |
| GUI half of the same action (Edit > MIDI Clock > Clock Source) | `ctest -R ^MidiClockMenuTest$ --output-on-failure` | `theSourceSubmenuDrivesTheSameCommand`: the entry reaches the engine through `clock.slave_set`, keeps the follower's mode, and offers exactly what `midi.device_list` offers. 6 passed / 0 failed | 0 |
| Existing tests in the area stay green | `ctest -R ^MidiClockTest$` / `^MidiOutQueueTest$` / `^ReversibilityContractTest$` / `^ControlCommandsSnapshot$` / `^agent_surface$` / `^ControlSurfaceReferenceTest$` / `^AgentSurfaceReportTest$` | all Passed; the ten-test run together: 100% tests passed, 0 failed out of 10 | 0 |
| Build | `cmake --build build --target zene MidiClockTest MidiClockSourceTest MidiClockMenuTest MidiOutQueueTest ReversibilityContractTest -- -j4` | linked clean | 0 |
| Gate 1 fork sources | `bash tests/fork-sources-gate.sh` | "every tracked source in scope is registered (1793 fork-NEW, 1212 inherited, 48 tooling)", 0 stale | 0 |
| Gate 9 all-sources recipe | `bash tests/all-sources-reproduce.sh` | REPRODUCES | 0 |
| Gate 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | "every change to upstream-inherited code ... is declared (1497 changed paths)" | 0 |
| Gate 3 no tautology | `bash tests/no-tautology-gate.sh` | no violation (incl. the new test file) | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 errors | 0 |
| Gate 4 complexity | `bash tests/complexity-gate.sh --check` | red on train 5 BY DESIGN; the REGRESSION listing is byte-identical to the capture taken before my first edit (`diff` of both listings is empty) - no function of mine is newly listed | 1 |
| Gate 7 file length | `bash tests/file-length-gate.sh --check` | red on train 5 BY DESIGN; identical listing (every file of mine is under 500 code lines; the new test is 234, `MidiClock.cpp` 415, `control-clock-commands.py` 486) | 1 |
| Commands snapshot canary | `python3 tests/doc-figures-gate.py` | "snapshot measures 828 ids / 76 groups ... RESULT: PASS" - unchanged, no new command id | 0 |

What changed, in two commits: the engine half (the follower's input entry point
takes the source the message arrived on; anything else is dropped and counted in
`clock.get_state`'s new `slave.ignored`; switching the source forgets the
measurement; the ALSA sequencer passes the sender's port name, a raw client passes
an empty name which is the truth about one device) and the surface half (Edit >
MIDI Clock > Clock Source lists what `midi.device_list` lists through the same
registered `clock.slave_set`; an unholdable name is refused `invalid_args`). File
and line detail: docs/050-features/Q050-026.md.

Honest limits, all stated in docs/050-features/Q050-026.md and
docs/KNOWN-LIMITATIONS.md: a client with ONE unnamed input (ALSA Raw-MIDI, JACK,
OSS, sndio, the dummy) cannot be narrowed and only follows "any input"; the
sequencer sender name is cached for the client's life; WinMM and CoreMIDI never
deliver the clock family in this tree; the control-socket transcript cannot inject
an incoming clock, so the drop half is proved by the QTest over the same entry
point; ThreadSanitizer does not run on this box, so the publication race is
reasoned about (odd-is-writing version counter, fixed UTF-16 buffer, no allocation
and no lock on the reader thread) and not measured.