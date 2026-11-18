IMPLEMENTED

Both acceptance criteria are met and proven by tests I ran and saw pass in this
worktree (branch feat/050-Q050-026-oc, base de5a85a96). Round 4 only reworked the
publication protocol and the concurrency test the r2 code review asked for; the
feature itself and every other test are unchanged and re-verified here.

| Criterion | Test / command | Evidence | EXIT |
|---|---|---|---|
| A regression test configures two MIDI ports and proves clock events from the unselected port are ignored while selected-port events update clock state | `ctest -R ^MidiClockSourceTest$ --output-on-failure` from `build/tests` (new executable, 11 slots) | `onlyTheConfiguredSourceCanDriveTheFollower`: 48 pulses at 140 BPM from the UNCHOSEN port leave `pulses` 0, `locked` false, `tempo_bpm` 0, `Song::getTempo()` 100, `ignored` +48; the CHOSEN port's 48 pulses lock at 140 and `applyMeasuredTempo()` writes 140. Also `aSecondClockingDeviceDoesNotReachTheFollower`, `theSourceMatchIsTheWholeName`, `switchingTheSourceForgetsTheMeasurement`, `anySourceIsFollowedWhileNoneIsChosen` (raw `parseData` bytes), `aNameTheFollowerCannotHoldIsRefused`, `theStateReportsTheSourceAndTheIgnoredCount`, `oneUndoStepRestoresThePreviousSource`, and the reworked `concurrentReconfigurationNeverAdmitsAnotherPort`. 11 passed / 0 failed; `25-source-stability2.log` (3 further runs EXIT 0) | 0 |
| (the same test is a real regression, not a new-green) | pre-fix probe: `acceptsSource()` temporarily forced to `return true`, rebuild, `ctest -R ^MidiClockSourceTest$` | SIX slots FAIL - `the unselected port fed 48 pulses`, `aSecondClockingDeviceDoesNotReachTheFollower` (120 vs 60), `theSourceMatchIsTheWholeName` (48 vs 0), `switchingTheSourceForgetsTheMeasurement` (48 vs 0), `theStateReportsTheSourceAndTheIgnoredCount` (ignored 0 vs 5), `concurrentReconfigurationNeverAdmitsAnotherPort` (ignored 0 vs 20046 - every overlapping read admitted); log `21-probe-test2.log` | 8 (expected: the probe must fail) |
| A control-socket test reads back the configured source and follower state | `ctest -R ^ControlClockCommands$ --output-on-failure` from `build/tests` (real `build/zene` over `--control-socket`, 63 checks ok) | `clock.slave_set answers with the source it was given`; `clock.get_state reads the source back beside the follower state` (source_port + enabled + follow_tempo + locked + pulses + tempo_bpm + ignored); `the source vocabulary is midi.device_list's readable ports`; `a switched source is the one the next read reports`; `one control.undo puts the previous source back`; `a source name the engine cannot hold is invalid_args`; `the refused source changed nothing`. Log `34-control-clock-transcript2.log` | 0 |
| GUI half of the same action (Edit > MIDI Clock > Clock Source) | `ctest -R ^MidiClockMenuTest$ --output-on-failure` | `theSourceSubmenuDrivesTheSameCommand`: the entry reaches the engine through `clock.slave_set`, keeps the follower's mode, and offers exactly what `midi.device_list` offers. 6 passed / 0 failed | 0 |
| Existing tests in the area stay green | `ctest -R '^(MidiClockTest\|MidiClockSourceTest\|MidiClockMenuTest\|ControlClockCommands)$'` and `ctest -R '^(MidiOutQueueTest\|ReversibilityContractTest\|ControlCommandsSnapshot\|agent_surface\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest)$'` from `build/tests` | 4/4 then 6/6 passed, 0 failed; `23-test-final2.log`, `24-supporting-final2.log` | 0 |
| Build | `cmake --build build --target zene MidiClockTest MidiClockSourceTest MidiClockMenuTest -- -j4` | linked clean (`22-build-final2.log`) | 0 |
| Gate 1 fork sources | `bash tests/fork-sources-gate.sh` | "every tracked source in scope is registered (1793 fork-NEW, 1212 inherited, 48 tooling)", 0 stale | 0 |
| Gate 9 all-sources recipe | `bash tests/all-sources-reproduce.sh` | REPRODUCES | 0 |
| Gate 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | "every change to upstream-inherited code ... is declared (1497 changed paths)" | 0 |
| Gate 3 no tautology | `bash tests/no-tautology-gate.sh` | no violation (incl. the new test file) | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 errors | 0 |
| Gate 4 complexity | `bash tests/complexity-gate.sh --check` | red on train 5 BY DESIGN; no function of mine is listed (the six REGRESSION lines are other lanes') | 1 |
| Gate 7 file length | `bash tests/file-length-gate.sh --check` | red on train 5 BY DESIGN; no file of mine is listed | 1 |
| Commands snapshot canary | `python3 tests/doc-figures-gate.py` | "snapshot measures 828 ids / 76 groups ... RESULT: PASS" - unchanged, no new command id | 0 |

What changed: the engine half (the follower's input entry point takes the source
the message arrived on; anything else is dropped and counted in `clock.get_state`'s
new `slave.ignored`; switching the source forgets the measurement; the ALSA
sequencer passes the sender's port name, a raw client passes an empty name which is
the truth about one device) and the surface half (Edit > MIDI Clock > Clock Source
lists what `midi.device_list` lists through the same registered `clock.slave_set`;
an unholdable name is refused `invalid_args`). File and line detail:
docs/050-features/Q050-026.md.

Rework r4 (Luna r2 verdict REWORK, r3 attempt reverted) addressed the two remaining
points:
(1) **The sequence-counter protocol is replaced by a real sequence lock whose
snapshot consistency the C++ memory model guarantees.** r2's bare acquire version
reads did not make the relaxed payload consistent - a reader could pair an even
version read with a code unit from the following write. `publishSource()` now
wraps its relaxed payload stores in `std::atomic_thread_fence(memory_order_release)`
(the odd marker ordered before the payload, the payload before the even marker) and
`acceptsSource()` wraps its relaxed payload loads in two
`std::atomic_thread_fence(memory_order_acquire)` calls (payload after the first
version read, the closing version read after the payload). Two equal even version
reads therefore prove the snapshot is one published name, never a mixture; the
reader stays bounded (four retries), lock-free and allocation-free on the MIDI
thread.
(2) **`concurrentReconfigurationNeverAdmitsAnotherPort` now proves writer-reader
overlap.** A publish counter is odd exactly while a `publishSource()` is in flight;
the reader counts a message only when its own read of that counter straddled a
publish (moved, or odd at both ends), and the slot does not stop until 20000 such
overlapped reads have been taken. The writer alternates `portTearOld()` ->
`portTearNew()`, a pair chosen so a torn read of the old length over the new units
spells the sender port (`portB()`), so a protocol that could tear would admit a
pulse and break the exact ignored count; the fenced sequence lock cannot, and the
count stays exact.
Rebuild, targeted tests, the pre-fix failure probe, all seven gates and both
ratchet listings were re-run (see the table); the worktree is left clean.

Honest limits, all stated in docs/050-features/Q050-026.md and
docs/KNOWN-LIMITATIONS.md: a client with ONE unnamed input (ALSA Raw-MIDI, JACK,
OSS, sndio, the dummy) cannot be narrowed and only follows "any input"; the
sequencer sender name is cached for the client's life; WinMM and CoreMIDI never
deliver the clock family in this tree; the control-socket transcript cannot inject
an incoming clock, so the drop half is proved by the QTest over the same entry
point; ThreadSanitizer does not run on this box (it crashes at thread creation), so
the publication is made consistent by the fenced sequence lock, exercised by the
overlap handshake and covered by the hosted TSan job, and is not measured locally.
