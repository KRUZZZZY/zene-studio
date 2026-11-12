# Cluster new-p23-156: MIDI byte encoder buffer check

**Verdict: FIXED**

`writeToByteSeq` checked its output buffer only with `Q_ASSERT(bufsize >= 3)`, which compiles out under `NDEBUG`. This tree is configured as RelWithDebInfo (`build/CMakeCache.txt:59`, `CMAKE_CXX_FLAGS_RELWITHDEBINFO` at line 94 uses `-DNDEBUG`), so a caller with a short buffer was written past its end. The status byte was also written before any check, even for a message that was then refused.

The fix builds the message in a local 3-byte array and copies it out only when the message's own length is non-zero and fits `bufsize`. Otherwise the function returns 0 and writes nothing.

## Evidence

- Original check: `src/core/midi/MidiEventToByteSeq.cpp` (before this commit) line 72, `Q_ASSERT(bufsize >= 3);`; the first write was `data[0] = ...` at line 75, before any check.
- Caller 1: `src/core/lv2/Lv2Proc.cpp:302` `auto buf = std::array<uint8_t, 4>{};`, and `:303` `writeToByteSeq(ev.ev, buf.data(), buf.size())`. Runs per period in the MIDI-in loop.
- Caller 2: `plugins/CarlaBase/CarlaMidiHandoff.h:120` `writeToByteSeq(event, native.data, sizeof(NativeMidiEvent::data))`, reached from `processPeriod` (`:83`, `:94`).
- No caller passes fewer than 3 bytes today.
- The regression test fails on the original code: `noteOnIntoTwoByteBufferIsRejectedAndNotWritten` returns 3 where 0 is expected; `messageIntoEmptyBufferIsRejectedAndNotWritten` returns 2 where 0 is expected (and writes the status byte into a 0-byte buffer).

### Why the fix checks the message length rather than a flat "bufsize < 3"

The finding suggested returning 0 when `bufsize < 3`. That would also refuse a two-byte program change or channel pressure written into a two-byte buffer, which the current code encodes correctly in release builds. The check that matters is whether the bytes actually written fit, so the fix checks that. Output for every message that fits is unchanged (the value and bytes match the original code); the only change is that a message which does not fit is refused.

### Header contract

`include/MidiEventToByteSeq.h` documented "at least 3 bytes"; it now documents `bufsize` bytes, and says a refused message writes nothing.

### Behaviour change

- Message fits (`size <= bufsize`, `size > 0`): same bytes and same return value as before.
- Message does not fit, or unhandled: returns 0 and writes nothing. Previously the status byte was written, and for a short buffer, bytes past `bufsize` were written. Both callers read the buffer only when the return value is non-zero, so the difference is not observable there.

## Realtime, undo, commands

- The encoder runs on the audio/render side (`Lv2Proc.cpp:303`, `CarlaMidiHandoff::stageMidiEvent` via `processPeriod`). The change adds a 3-byte stack array and a `std::memcpy` of at most 3 bytes. It does not allocate, free, lock, wait, log or emit a signal.
- No undo or control command is involved; no command was added.

## Files changed

- `src/core/midi/MidiEventToByteSeq.cpp`: local `msg[3]`, copy-out guarded by `size == 0 || bufsize < size`, `Q_ASSERT` removed, `<QtGlobal>` replaced by `<cstring>`.
- `include/MidiEventToByteSeq.h`: contract comment corrected.
- `tests/src/core/MidiEventToByteSeqTest.cpp`: three new cases (two refusal cases with sentinel bytes, one two-byte success case).
- `tests/upstream-modifications.txt`: entries for the header and the `.cpp` (both inherited LMMS files).
- `docs/qwen-fixes-2026-10/cluster-new-p23-156.md`: this report.

## Evidence table

Build used `CCACHE_DISABLE=1` (see environment note below). Test runs are from `build/tests`. Logs are in the run folder `logs/`.

| Step | Exact command | EXIT | Result / log |
| --- | --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target MidiEventToByteSeqTest -j4` | 0 | `build-2-red.log` |
| RED run | `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure` | 8 | 13 passed, 2 failed (`test-1-red.log`) |
| Fix applied, GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene MidiEventToByteSeqTest -j4` | 0 | `build-3-green.log` |
| GREEN run | `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure` | 0 | 100 % passed (`test-2-green.log`) |
| 4b: revert product hunks | `git diff -- src/core/midi/MidiEventToByteSeq.cpp include/MidiEventToByteSeq.h > fix.patch` ; `git apply -R fix.patch` | 0 ; 0 | `fix.patch` |
| 4b: rebuild reverted | `CCACHE_DISABLE=1 cmake --build build --target zene MidiEventToByteSeqTest -j4` | 0 | `build-5-revert.log` |
| 4b: RED with fix reverted | `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure` | 8 | 13 passed, 2 failed (`test-4-revert-red.log`) |
| 4b: re-apply | `git apply fix.patch` | 0 | `build-6-reapply.log` (rebuild exit 0) |
| 4b: GREEN with fix restored | `ctest -R '^MidiEventToByteSeqTest$' -V` | 0 | 15 passed, 0 failed (`test-9-final-midi.log`) |
| Affected family: MidiEventToByteSeqTest | same | 0 | 15 QTest cases, 0 failed |
| Affected family: CarlaMidiHandoffTest | `ctest -R '^CarlaMidiHandoffTest$' -V` | 0 (first run) / 8 (later) | 7/7 on the first green run (`test-3-family.log`); see flake below |
| Gates (wrapper) | `python3 -I gate_wrap.py <worktree>` (see note) | 0 | all PASS, `RESULT: PASS` (`gates-3.log`; re-run after the final comment-only edit: `gates-4-final.log`, EXIT 0) |
| Gates (direct) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `PermissionError` creating the sibling log directory `…/zene-qwen-new-p23-156-feature-check`, outside the write-jail (`gates-1.log`); no gate ran |

### Pre-existing flake: `CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree`

Under load this test fails the assertion at `tests/src/plugins/CarlaMidiHandoffTest.cpp:494`: `processing did not overlap MIDI producer activity`. This assertion checks thread-scheduling overlap, not encoded bytes. The other six Carla cases pass in every run.

Samples taken on this machine (load average 2–3.5 from other lanes):

- Unmodified code: 1 of 5 runs failed (`test-7-carla-baseline-*.log`); then 15 of 20 runs failed in a later window (`BASE_FAILS_OF_20=15`).
- With the fix: the first family run passed (`test-3-family.log`); later runs failed in `test-5-reapply-green.log`, two of three reruns (`test-6-carla-rerun-*.log`), three of eight (`test-8-carla-fix-*.log`) and the final run (`test-10-final-carla.log`); 19 of 20 failed in the later window (`FIX_FAILS_OF_20=19`), against the baseline's 15 of 20 in that same window.

The fix's effect is a few nanoseconds of copy on the render side, and the baseline fails at the same magnitude in the same window. I did not find a deterministic cause in this test and did not change it (it must not be weakened). This needs a separate fix to the test's overlap logic, under a separate cluster.

## Environment notes (not product changes)

- `cmake --build` with ccache failed with `ccache: error: failed to create temporary file … Permission denied` under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (`build-1-red.log`). Builds were rerun with `CCACHE_DISABLE=1`; no source or build configuration was changed.
- The gate script writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail. `gate_wrap.py` (in the run folder) runs the unmodified `feature_check.py` source with only that log path replaced by a path in the run folder. Baselines (`ROOT`) and gate limits are unchanged.

## Self-check

1. **RED fails with the product change reverted, GREEN passes with it.** Yes: the reverted tree gives EXIT 8 with the two new refusal cases failing (`test-4-revert-red.log`); with the fix restored it gives EXIT 0, 15/15 (`test-9-final-midi.log`).
2. **Acceptance (the finding's contract: no write past `bufsize` in release builds, and a short buffer is refused).** Proven by the two refusal cases; the two-byte success case shows fitting messages still encode.
3. **Undo / commands.** Not applicable: no command added or changed; the encoder is not on the undo path.
4. **Nothing new on the audio thread.** The added code is a 3-byte stack array and a `std::memcpy` of at most 3 bytes, inside `writeToByteSeq` (`src/core/midi/MidiEventToByteSeq.cpp`), called from `Lv2Proc.cpp:303` and `CarlaMidiHandoff.h:120`. No allocation, lock, log, wait, I/O or signal.
5. **Old projects and saved data unchanged.** The encoder's output for any message that fits is the same bytes and the same return value. Saved data does not contain this output, and the only callers are the LV2 MIDI-in loop and the Carla handoff.
6. **Portability.** Only standard C++ (`std::memcpy` from `<cstring>`). `QtGlobal` is no longer included, as `Q_ASSERT` was its only use in this file.
7. **Sibling commands.** None: no `ControlCommands*` or `ControlReversibilityTable` file is changed.
8. **Gates.** All PASS through the wrapper (see the table). The direct run of `feature_check.py` could not create its sibling log directory in this sandbox (EXIT 1, `gates-1.log`), so the gate result above is from the wrapper.
9. **Open item.** The Carla stress-test flake above is pre-existing and is not part of this fix.

### Answers to the completion-check ASK items

- **Regression test fails with the product change reverted, passes with it.** Yes. Reverted: `git apply -R fix.patch` EXIT 0, build EXIT 0, `ctest -R '^MidiEventToByteSeqTest$'` EXIT 8 with 2 failing cases. Restored: `git apply fix.patch` EXIT 0, build EXIT 0, EXIT 0 with 15/15 (table above).
- **Test is deterministic.** No wall-clock time, sleeps, threads or races. The three new cases call `writeToByteSeq` with literal `MidiEvent` values and a fixed-size byte array pre-filled with the sentinel `0xAA`; the outcome is a return value and byte values, checked with `QCOMPARE`.
- **Fix is at the cause and complete.** The cause is the unchecked `data[...]` writes in `writeToByteSeq`. Every write in the function now goes to the local `msg[3]` (grep: no `data[` remains in `src/core/midi/MidiEventToByteSeq.cpp`), and `data` is written once, after the size check. Callers checked: `src/core/lv2/Lv2Proc.cpp:302-303` (4-byte buffer) and `plugins/CarlaBase/CarlaMidiHandoff.h:120` (`sizeof(NativeMidiEvent::data)`, after `memset`). Both read the buffer only when the return value is non-zero. The header contract (`include/MidiEventToByteSeq.h`) now matches the code.
- **No collateral damage.** A check that would hit legitimate callers was avoided on purpose: the fix refuses only a message that does not fit, not every buffer under 3 bytes (a two-byte program change into a two-byte buffer still encodes; covered by `programChangeFitsTwoByteBuffer`). Saved projects do not contain this output.
- **Verdict is FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE evidence item does not apply.
- **Gates row FAIL in `submit_check.py`.** That check runs `feature_check.py` directly, which fails with the `PermissionError` on its sibling log directory (`gates-1.log`, EXIT 1). The same gate code run through the run-folder wrapper passes (`gates-3.log`, `gates-4-final.log`, EXIT 0, `RESULT: PASS`). I could not make the direct run pass from inside the write-jail, so the row remains FAIL. This is an environment limit, not a gate failure.
