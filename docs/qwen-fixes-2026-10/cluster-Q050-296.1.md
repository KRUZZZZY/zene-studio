IMPLEMENTED

# Cluster Q050-296.1: a 5.1 or 7.1 channel's speakers run through its devices

Branch `feat/050-Q050-296.1-oc`, feature record `docs/050-features/Q050-296.1.md`. Stage 1 of 4 of Q050-296
(post-fader inserts, sends, the mono/polarity/width/EQ stages and the meters stay on L/R).

## Acceptance

| Criterion | Test | Command | Exit (RED -> GREEN) |
|---|---|---|---|
| A distinct test signal on every 5.1/7.1 speaker is processed and appears on its matching output channel, without loss or reordering | `SurroundDevicePathTest::everySpeakerIsProcessedOnItsOwnOutput(5.1, 7.1)` and `WidePinRoutingGuiTest::aSurround51BusShowsItsSpeakers` | `cd build/tests && ctest -R '^(SurroundDevicePathTest\|WidePinRoutingGuiTest)$' --output-on-failure` | 8 -> 0 |
| Pins survive a save and load. | `SurroundDevicePathTest::pinsSurviveASaveAndLoad` | same | 8 -> 0 |
| One undo puts the device back on the pair. | `SurroundDevicePathTest::undoRestoresThePair` | same | 8 -> 0 |
| A stereo device without pins is compatible. | `SurroundDevicePathTest::aStereoDeviceWithoutPinsLeavesSpeakersUntouched` | same | 8 -> 0 |
| The wide mix allocates nothing. | `SurroundDevicePathTest::theWideInsertMixAllocatesNothing` | same | 8 -> 0 |
| An invalid format is refused. | `SurroundDevicePathTest::anInvalidFormatIsRefused` | same | 8 -> 0 |

RED: the product commit `5c7400499f`'s `src/` and `include/` hunks reverse-applied with
`git apply -R /tmp/q050-296.1-logs/fix.patch`; build exit 0 (`/tmp/q050-296.1-logs/008-build-red.log`), ctest
exit 8, 2 of 2 tests failed (`/tmp/q050-296.1-logs/009-red.log`). GREEN: hunks restored with
`git apply /tmp/q050-296.1-logs/fix.patch`; build exit 0 (`/tmp/q050-296.1-logs/010-build-green.log`), ctest
exit 0, 100% of 2 (`/tmp/q050-296.1-logs/011-green.log`). The failing RED assertions are the six-speaker bus
(`pair 0: bus channels 2, expected 6/8`) and the missing 5.1 Pin Connector (`connector != nullptr` false).

## Gates

`feature_check.py` on this tree: exit 0, all 8 gates PASS (`/tmp/q050-296.1-logs/013-gates.log`). The first run
(`012-gates.log`) failed only the complexity gate: `pinSpeakers` (test helper) was CCN 11 over the target, fixed by
splitting the per-pair check into `pinPair` and looping over the channels - no assertion or behaviour change.

`ReversibilityContractTest`: build exit 0 (`/tmp/q050-296.1-logs/014-build-rev.log`), ctest exit 0, 1 of 1
(`/tmp/q050-296.1-logs/015-rev.log`). No command metadata changed, so no snapshot regen and no A16 change.

## Commits

`git log --oneline 3ab01006f..HEAD` (the six commits of this branch; a plain `master..HEAD` also carries the
whole `050/m0-train-28` history, 8499 commits):

```
d79070ae6b test(mixer): keep pinSpeakers within the complexity gate (Q050-296.1)
923ffd008e docs(mixer): correct Q050-296.1 metadata and which layouts take the wide bus (Q050-296.1)
f3a22bf9b2 docs(mixer): 5.1/7.1 speakers through channel devices - manual and feature notes (Q050-296.1)
7b9d7fa87d test(gui): Pin Connector on a 5.1 bus reaches every speaker through port.set_pins (Q050-296.1)
5c7400499f feat(mixer): 5.1/7.1 speakers run through the channel's devices on the wide insert bus (Q050-296.1)
e5599dca9d test(mixer): 5.1/7.1 speakers through pinned channel devices to matching outputs, RED before the fix (Q050-296.1)
```

## Notes

- The run log folder `~/.local/share/zene-bunny-fix/qwen/runs/Q050-296.1.r1/logs/` is not writable, so the
  logs live in `/tmp/q050-296.1-logs`, `/tmp/Q050-296.1.r1-logs` and `/tmp/q050-296-p3-logs`.
- No new or changed commands, so no snapshot regen was needed; `ReversibilityContractTest` agrees.
- Post-fader inserts are out of scope (stage 1/4 of Q050-296).
- TSan was not run locally. The race reasoning: the wide insert pairs are grown only in
  `ChannelSurround::setFormat` on the control thread under the engine's change lock, and the audio path only
  reads them; the allocation probe test shows the wide master mix allocates nothing.

## Self-check

- [ ] Every acceptance criterion has a RED and a GREEN exit code.
- [ ] The gate table and the reversibility result name their log files.
- [ ] The commit list contains only this feature's commits.
- [ ] No command metadata changed, so no snapshot or A16 change.
- [ ] The tree is clean.
