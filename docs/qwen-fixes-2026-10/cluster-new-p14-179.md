FIXED

# cluster new-p14-179: WinMM pitch bend packing

## Decision and evidence

The defect was present at p1. The WinMM output path packed every short message as `type+channel | param(0)<<8 | param(1)<<16`. For `MidiPitchBend`, the 14-bit value is carried in `param(0)` (via `pitchBend()`), so the wire message sent the wrong data bytes and dropped the high bits.

- Old packing, pre-p1 (`git show fa4a5f2e78:src/core/midi/MidiWinMM.cpp`, lines 57-59): `(event.type() + event.channel()) + ((event.param(0) & 0xff) << 8) + ((event.param(1) & 0xff) << 16)`.
- Current code: `src/core/midi/MidiWinMM.cpp:59` calls `winmmShortMessage( event )`. The helper lives in `include/MidiWinMMShortMessage.h:40-49`. Pitch bend is masked to 14 bits and split into low 7 bits (byte 1) and high 7 bits (byte 2). Every other message keeps the `& 0xFF` path for `param(0)` and `param(1)`.
- Input reconstruction (`src/core/midi/MidiWinMM.cpp:223`, `par1 + par2*128`) is the inverse of the same split, so the 14-bit value round-trips.
- Siblings that already split the value the same way: `src/core/midi/MidiEventToByteSeq.cpp:141-145` (`pitchBend() & 0x7f`, `>> 7`) and `src/core/midi/MidiClient.cpp:429-432` (`& 0x7F`, `>> 7 & 0x7F`).
- Call sites, proven by `rg -n 'winmmShortMessage\('`: one production call, `src/core/midi/MidiWinMM.cpp:59`. The other hits are the header definition, the ledger, and the test.

## Platform note

`LMMS_HAVE_WINMM` is off on this host, so the WinMM translation unit is empty and `processOutEvent` cannot be driven from a test here. This matches the MidiInputGate / Q050-488 precedent in `tests/upstream-modifications.txt:111-112`. The test therefore targets the exact expression `processOutEvent` uses (`winmmShortMessage`, from the header), plus a wiring assertion (`tests/src/core/MidiWinMMShortMessageTest.cpp:108-114`) that reads `src/core/midi/MidiWinMM.cpp` via `ZENE_SOURCE_DIR` and requires it to still contain `winmmShortMessage( event )`.

## Commands and exit codes

Run logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-179.r1/logs/`.

| step | exit |
|---|---|
| build-1 | 0 |
| commit-1 | 0 |
| build-2a-regen | 0 |
| build-2 | 0 |
| test-red-1 | 8 (4 passed, 4 failed) |
| build-3 | 0 |
| test-green-1 | 0 (3/3) |
| fix.patch reverse apply | 0 |
| build-4-red | 0 |
| test-red-2 | 8 (4 passed, 4 failed) |
| forward apply | 0 |
| build-5-green | 0 |
| test-green-2 | 0 (1/1) |

## Self-check

1. **Reversed-diff RED/GREEN.** Reverse-applying `fix.patch` restores the old packing. The build passes (build-4-red 0), and the test fails (test-red-2 8). Forward-applying the patch restores the fix; the build passes (build-5-green 0) and the test passes (test-green-2 0). The same red/green cycle happened before the first commit (test-red-1 8, test-green-1 0 at 3/3).
2. **Determinism.** The test uses literal expected values (`{0, 8191, 8193, 16383, 12345}` and the non-bend message literals) plus an exhaustive loop over `0..16383`. It has no timing, no threads, no sleeps and no randomness. `rg` for `thread|QTimer|sleep|chrono` returns no hits in the test file.
3. **Completeness.** There is a single production call site (`MidiWinMM.cpp:59`). The generic `0xFF` path in the helper is unchanged. Comments in the helper are updated to describe the pitch bend split.
4. **Collateral damage.** Non-bend messages are bit-identical: the generic branch keeps `(param & 0xFF)` for bytes 1 and 2, and the test checks the note-on, control-change and channel-pressure literals. No saved-data or undo path is touched. No range refusal is added: an out-of-range `pitchBend()` is masked to 14 bits, not refused. (This differs from `MidiEventToByteSeq`, which refuses via `isPitchBendValue`. Recorded in NOTES, not changed here.)
5. **Verdict.** FIXED, not ALREADY-FIXED. The defect was in the code at p1, as the pre-p1 packing above shows, and this cluster changes it.

## Other findings

- `src/core/midi/MidiWinMM.cpp:198` reads `const int par2 = ev >> 16;` without `& 0xff`, while the line above masks par1. If WinMM ever sets bits above the 24-bit short message, a note/CC/pitch-bend value is corrupted on input. Not fixed here: outside this finding's scope; listed for triage.
