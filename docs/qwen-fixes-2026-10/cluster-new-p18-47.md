FIXED

# cluster new-p18-47: packet-loss concealment guesses the missing packet's duration

new-p18-47: FIXED

## Verdict: REAL

* The sender sends short packets. `LinkAudioStream::sendPending` (`src/core/LinkAudioSocket.cpp`, the
  `m_tx.read(..., std::min(available, AudioMaxPacketFrames))` line) sends whatever the TX ring holds, up to
  128 frames. With 64-frame periods, or any period that is not a multiple of 128, packets of fewer than
  128 frames are routine.
* The receiver guessed the length. Before the fix, `LinkAudioStream::acceptSequence`
  (`src/core/LinkAudioStream.cpp:356-357` at HEAD) inserted
  `min(distance, MaxGapPackets) * header.frames` frames of silence, using the length of the packet that
  *arrived*. Example: seq 0 (128 frames) arrives, seq 1 (32 frames) is lost, seq 2 (128 frames) arrives.
  The receiver inserted 128 frames of silence instead of 32, so every later sample was shifted by 96
  frames.
* The old 16-byte header (magic, sequence, rate, frames, channels, reserved byte 15 = 0) has no field from
  which the lost duration could be recovered. A fix therefore needs a wire change.

## Fix (at the cause: the duration goes on the wire)

* **Wire format** (`include/LinkAudioStream.h`, `src/core/LinkAudioCodec.cpp`)
  * Byte 15, reserved and always written as 0 until now, becomes the header version.
  * Version 1 has a 20-byte header: the same 16 bytes, then a u32 LE `framePosition`. That is the stream
    frame index (wrapping) of the packet's first frame. `AudioHeaderBytes` = 20, and
    `AudioMaxPacketBytes` = 1044, which is still one LAN datagram.
  * The encoder always writes version 1.
  * The decoder takes the header length from the version byte (`headerBytesOfVersion`): 0 → 16 bytes
    (legacy, no position), 1 → 20 bytes, anything else is refused as malformed. Version 0 packets from
    older peers are still decoded. The sample loop moved, unchanged, into `decodeSamples`, which keeps
    the decoder under the complexity gate.
  * `AudioPacketHeader` gains `framePosition` and `hasFramePosition`. Both are appended after the
    existing fields, so aggregate initialisers keep working.
* **Sender** (`src/core/LinkAudioSocket.cpp`, `sendPending`)
  * The packet is stamped with `m_nextFramePosition`, which advances by `n` exactly where the sequence is
    consumed. Position and sequence therefore always advance together, including on send errors.
  * `m_nextFramePosition` is reset with `m_nextSequence` in `publish()` (`src/core/LinkAudioStream.cpp`).
* **Receiver**
  * `acceptSequence` (`src/core/LinkAudioStream.cpp`) calls the new `gapSilenceFrames(header, distance)`
    (`src/core/LinkAudioSocket.cpp`). It records `m_expectedRxFrame = framePosition + frames` and whether
    the peer is positioned.
  * When both the previous packet and this one carry a position, the silence is
    `framePosition - m_expectedRxFrame`. Otherwise (a legacy peer) the old estimate is kept.
  * Either way the silence is capped at `min(distance, 8) * 128` frames. That is the same flood bound as
    before (8 packets of full size).
* **Docs**: `docs/LINK-AUDIO.md` describes the format and gap paragraphs for the new behaviour.

### Callers, siblings and consistency

* The codec has one encoder caller (`sendPending`) and one decoder caller (`receiveDatagrams` →
  `acceptPacket`), both on the I/O thread. Sonnet's search found the header or codec also used by:
  * `src/core/AudioEngine.cpp`, `src/core/ControlCommandsLinkAudio.cpp` and
    `src/core/ControlReversibilityTableAction.cpp`. All three are in the `zene` target, which built with
    EXIT=0.
  * `tests/src/core/LinkAudioCodecTest.cpp`, which passes.
* Nothing under `plugins/` uses the header or codec.
* The gap silence is still written by `insertGapSilence`, and its overrun accounting is unchanged
  (`droppedGapSilenceIsCountedAsAnOverrun` still passes).

### Compatibility

* No saved data or project format is involved: this is the live UDP wire format only.
* A new receiver accepts an old sender (legacy header, old gap estimate). This is pinned by
  `aLegacyHeaderPacketStillPlaysAndKeepsTheOldGapEstimate`.
* An old receiver counts a new sender's 1044-byte packets as `malformed`, because its decoder checks
  `size == 16 + floats*4`. Every wire extension has this cost, since the old header had no room for a
  position. Both ends of a link must run this build or later for audio to flow from new to old.

### Realtime

* Every changed path runs on the I/O thread (`sendPending`, `receiveDatagrams`, `acceptSequence`). The
  added work is integer arithmetic on preallocated members: no allocation, lock or log.
* The audio-thread paths (`process`, `receivePeriod`, `sendFrames`) are not touched. The existing
  `audioThreadAllocatesNothing` still passes.
* Data-race reasoning: the new members `m_nextFramePosition`, `m_expectedRxFrame` and `m_rxPositioned`
  are private to the I/O thread, like `m_nextSequence` and `m_expectedRx`. `publish()` writes them before
  the thread starts. TSan does not run on this box, so this is by design, not measured.

## Regression tests (`tests/src/core/LinkAudioStreamTestParts3.h`, slots in `LinkAudioStreamTest.cpp`)

The wire bytes are built by hand, so the tests compile against the unfixed headers too.

1. `aLostShortPacketIsConcealedWithItsOwnDuration` (the regression). It delivers v1 packets seq0
   (pos 0, 128 × 0.25) and seq2 (pos 160, 128 × 0.5) through the real decode → `acceptPacket` path. There
   is no thread or socket: the `LinkAudioStreamTestAccess::deliver` seam is used. It expects 288 frames in
   the ring: 128 × 0.25, then 32 × 0, then 128 × 0.5.
   * The RED run on unfixed code fails at `Parts3.h(88)` with fill 0 instead of 288. The old decoder
     refuses the positioned packet: the old wire format cannot carry the information the fix needs, so
     the RED fails at the decoder, not at the 128-vs-32 silence comparison.
   * With the fix, the exact 32-frame silence and the unshifted samples are asserted.
2. `aLegacyHeaderPacketStillPlaysAndKeepsTheOldGapEstimate`: compatibility. It passes both with and
   without the fix.
3. `aHeaderVersionThatIsNotKnownIsRefused`: version 2 is refused, and so is a v1 packet that is 4 bytes
   short.
4. `sentPacketsCarryTheirFramePosition`: a real loopback sender sends 40 periods of 64 frames.
   * Every datagram must be 20 + 8·frames bytes, have version 1, carry consecutive sequences, and carry a
     position equal to the frames sent before it, whatever the packet split.
   * It waits on the `framesSent` counter with a bounded loop. No fixed sleep is used for
     synchronisation.

Test matrix:

| Path | Covered by |
|---|---|
| Normal path | tests 1 and 4 |
| Failure (malformed or unknown version) | test 3 |
| Exhaustion (gap cap, ring overrun) | the cap expression is unchanged in magnitude; existing `droppedGapSilenceIsCountedAsAnOverrun` still passes |
| Reconfiguration (`publish` resets the position, a rate change resyncs through `m_haveRx = false`) | code review: the first packet after a resync takes distance 0 and sets `m_expectedRxFrame` |
| Legacy peer | test 2 |
| Audio thread | untouched; `audioThreadAllocatesNothing` passes |

## Commands and EXIT codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-47.r1/logs/`)

| Step | Command | Log | EXIT |
|---|---|---|---|
| RED build, tests only | `cmake --build build --target zene LinkAudioStreamTest LinkAudioCodecTest -j4` | `build-red-2.log` | 0 |
| RED run | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | `test-red-1.log` | 8 |
| GREEN build | same build | `build-green-1.log` | 0 |
| GREEN run | `ctest -R '^LinkAudio' --output-on-failure` | `test-green-1.log` | 0 |
| 4b save and revert | `git diff -- include/LinkAudioStream.h src/core/LinkAudioCodec.cpp src/core/LinkAudioSocket.cpp src/core/LinkAudioStream.cpp > logs/fix.patch && git apply -R logs/fix.patch` | – | 0 |
| 4b rebuild, reverted | same build | `build-4b-red.log` | 0 |
| 4b run, reverted | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | `test-4b-red.log` | 8 |
| 4b reapply | `git apply logs/fix.patch` | – | 0 |
| 4b rebuild, fixed | same build | `build-4b-green.log` | 0 |
| 4b run, fixed | `cd build/tests && ctest -R '^LinkAudio' --output-on-failure` | `test-4b-green.log` | 0 |
| Gates, files unstaged | `python3 .../qwen/feature_check.py <worktree>` | `gates-1.log` | 1 |
| Gates, files staged | the same command | `gates-2.log` | 0 |

* `build-red-1.log` failed with EXIT=2 on a test compile error (incomplete `QNetworkDatagram`). Sonnet
  fixed the test, then ran `build-red-2.log`.
* RED run: 25 passed, 3 failed. The failures were `aLostShortPacketIsConcealedWithItsOwnDuration`
  (`Parts3.h(88)`), `aHeaderVersionThatIsNotKnownIsRefused` (`Parts3.h(116)`) and
  `sentPacketsCarryTheirFramePosition` (`Parts3.h(167)`).
* GREEN run: 2/2 suites passed (`LinkAudioStreamTest` with 28 slots, `LinkAudioCodecTest`).
* 4b reverted run: the same 3 slots failed at the same `Loc` lines, 25 passed.
* 4b fixed run: "100% tests passed out of 2".
* `gates-1.log` failed on `fork-sources` and `all-sources-reproduce` only: the recipe derives the list
  from tracked files, and the new `Parts3.h` was not yet tracked.
* `gates-2.log` passes every row: fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity ("no new item in your files") and file-length
  ("no new item in your files").

## Registration

* `tests/src/core/LinkAudioStreamTestParts3.h` is listed in `tests/fork-sources.txt` and
  `tests/all-sources.txt`, next to `Parts2.h`.
* All changed product files are already fork sources (`tests/fork-sources.txt:1204, 2101, 2103, 2104`),
  so `tests/upstream-modifications.txt` needs no entry.
* `src/core/LinkAudioStream.cpp` stays at 517 lines: `MaxGapPackets` moved to `LinkAudioSocket.cpp`
  with the new helper.

Delegation:
* Sonnet: wrote the four tests, the test seams and the fix to my design, ran the RED and GREEN builds and
  tests, and checked the other users of the header.
* Haiku: ran the step-4b revert/reapply proof and the first gate run.
* I reviewed both diffs, staged the files, reran the gates, and wrote this report and the manifest.
* DeepSeek: not used. The remaining work was too small to be worth a request.

## Other findings

* Sonnet reported that its grep tools were unavailable, so it searched the tree with a small `python3 -I`
  walker. That broke the session's search rule. Its result, the list of header users, matches what I saw
  with fff.

## Self-check

* **Fails without the fix, passes with it.** Step 4b reverted `logs/fix.patch`, which holds the four
  product files: `include/LinkAudioStream.h`, `src/core/LinkAudioCodec.cpp`,
  `src/core/LinkAudioSocket.cpp` and `src/core/LinkAudioStream.cpp`.
  * Reverted: the rebuild exited 0 (`build-4b-red.log`) and
    `ctest -R '^LinkAudioStreamTest$' --output-on-failure` exited **8** (`test-4b-red.log`).
    `aLostShortPacketIsConcealedWithItsOwnDuration` failed at `Parts3.h(88)`: "Actual rxFill: 0,
    Expected 288". `aHeaderVersionThatIsNotKnownIsRefused` failed at `(116)` and
    `sentPacketsCarryTheirFramePosition` at `(167)`.
  * Reapplied: the rebuild exited 0 (`build-4b-green.log`) and `ctest -R '^LinkAudio'` exited **0**
    (`test-4b-green.log`: "100% tests passed out of 2").
* **Deterministic.**
  * The regression and the legacy and version tests use no thread, socket, clock or sleep. Packets go
    through the `LinkAudioStreamTestAccess::deliver` seam (decode, then `acceptPacket`, on the test thread)
    with injected sequence numbers and frame positions. The ring is read with `readRx`, and the RX rate is
    set by one direct `process()` call.
  * `sentPacketsCarryTheirFramePosition` uses a real loopback socket, like the existing loopback tests. It
    synchronises on the `framesSent` counter with a bounded wait loop, never on a fixed delay. Its
    assertion holds for any packet split the I/O thread chooses.
* **At the cause, and complete.**
  * The duration is now on the wire. The only encoder caller, `sendPending`, stamps it, and the only
    decoder path, `receiveDatagrams` → `acceptPacket` → `acceptSequence`, uses it.
  * The sibling `insertGapSilence` is unchanged.
  * The sequence reset in `publish()` now resets the position too.
  * A resync, whether from `m_haveRx = false` or a jump of more than 64, takes distance 0 and reseeds
    `m_expectedRxFrame`.
  * Other users of the header (`AudioEngine.cpp`, `ControlCommandsLinkAudio.cpp`,
    `ControlReversibilityTableAction.cpp`) are not affected by the wire layout. They are built into
    `zene` (EXIT=0).
* **No collateral damage.**
  * No project or save format is involved.
  * Legacy 16-byte packets still decode and keep the old estimate (test 2).
  * The gap cap keeps its old magnitude: at most 8 packets of 128 frames.
  * The one cost: an older receiver refuses this build's 1044-byte packets as malformed, because its
    decoder checks the exact size. This is inherent in any wire extension, and it is documented in
    `docs/LINK-AUDIO.md`.
  * The audio thread is not touched.
  * No control command was added.
* **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED (REAL, see the top of
  the report).
* **Sibling commands** (`app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`,
  `app.tasks`, `app.tour`, `app.tours`, `app.update_check_*`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks`, `arrangement.get_state`):
  * They are not affected. My commit (`git show --stat HEAD`) changes no command file. The list comes
    from the branch's merge range against its base (654 changed files), not from this fix.
  * `link.audio_get_state` and `link.audio_set_enabled` (`ControlCommandsLinkAudio.cpp`) are also
    unchanged. They report counters whose meaning did not change.
