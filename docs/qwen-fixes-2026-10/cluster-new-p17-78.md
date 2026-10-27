# FIXED - cluster new-p17-78: audio packets with a zero sample rate are accepted

Finding (Luna patrol, "BUG 10"): `src/core/LinkAudioCodec.cpp:68`. Encoding and decoding validated packet size and frame
count but accepted `sampleRate == 0`.

Verdict: the finding is REAL, but its downstream claim does not hold on this branch. Before the fix the codec reported a
zero-rate packet as well formed, so the receiver counted it as a rate mismatch instead of a malformed packet. No
conversion consumed the zero rate: the only reader of a packet's `header.sampleRate` is `acceptPacket`, which drops it.

## Evidence (file:line, current tree)

| Claim | Evidence |
| --- | --- |
| The finding's line 68 is the encoder's byte count; the check it refers to is HEAD:69 | `git show HEAD:src/core/LinkAudioCodec.cpp` lines 68-69 (`bytes = AudioHeaderBytes + floats * 4`, then the frame check without a rate test) |
| Decoder accepted a zero rate before the fix | HEAD:89-92 (magic, channels, frames, size; no rate test) and HEAD:98 (`header.sampleRate = get32(data + 8)`) |
| Fix at the cause, one shared rule | `src/core/LinkAudioCodec.cpp:56-58` `isValidPacketShape(frames, sampleRate)` (`frames` in 1..128 and `sampleRate != 0`) |
| Encoder uses it | `src/core/LinkAudioCodec.cpp:75` |
| Decoder reads the rate, then checks it before any output is written | `src/core/LinkAudioCodec.cpp:98-99`; `header.sampleRate` is assigned only on success at `:105` |
| Receiver already refused a zero rate, as a mismatch | `src/core/LinkAudioStream.cpp:364-368` (`rate == 0 \|\| header.sampleRate != rate` -> `m_rateMismatch`). No other reader of the packet rate exists (grep `sampleRate` in `src/core/LinkAudio*.cpp`: `LinkAudioSocket.cpp:205` is the producer, `LinkAudioStream.cpp:364` the only packet consumer) |
| Sender never encodes a zero rate | `src/core/LinkAudioSocket.cpp:197-198` returns while `rate == 0`, before the encode at `:207` |
| Callers that decode or encode | `src/core/LinkAudioSocket.cpp:176` (decode, I/O thread), `:207` (encode, I/O thread); tests `tests/src/core/LinkAudioStreamTest.cpp:146,152,178,181` and `tests/src/core/LinkAudioStreamTestSupport.h:89,93,179` pass nonzero rates (48000, 44100, the test rates); no caller passes a zero rate |
| Documented contract | `include/LinkAudioStream.h:92-98` (comments updated); `docs/LINK-AUDIO.md:29-32` (a zero-rate packet is malformed) |

## What changed

* `src/core/LinkAudioCodec.cpp`: one helper `isValidPacketShape` (`:56-58`) used by `encodeAudioPacket` (`:75`) and
  `decodeAudioPacket` (`:98-99`). A zero rate is now refused in both directions. Frame limits are unchanged.
  Moving the frame and rate checks into the helper also brought `decodeAudioPacket` back under the complexity gate
  (inline, it measured CCN 11; the gate passes now - run 14).
* `include/LinkAudioStream.h`: the two contract comments now name the zero-rate rule (`:92-98`). Comment only.
* `tests/src/core/LinkAudioCodecTest.cpp` (new file): two slots, `zeroSampleRateIsNotEncoded` and
  `zeroSampleRateIsNotDecoded`. Each has a positive control (a real rate encodes / decodes), so the failure can only come
  from the zero rate. The decoder test builds the zero-rate packet by zeroing bytes 8-11 of a valid packet, so it does not
  depend on the encoder. No clock, sleep, thread or socket. The slots live in a new file because `LinkAudioStreamTest.cpp`
  was at 495 lines and two slots pushed it to 522, over the 500-line gate.
* Registration: `tests/CMakeLists.txt` (one `LIST(APPEND LMMS_TESTS ...)` line), `tests/fork-sources.txt` and
  `tests/all-sources.txt` (one line each, byte order), `tests/upstream-modifications.txt` (reason for the inherited
  `tests/CMakeLists.txt` edit).
* `docs/LINK-AUDIO.md`: one sentence stating that a zero-rate packet is malformed.

## Behaviour change (the only one)

A received zero-rate packet is now counted as `malformed` instead of `rate_mismatch`. It is still dropped and never reaches
the ring. Valid packets, the wire format and every nonzero rate are unchanged. No supported-rate list was added:
there is none in the codebase, and rejecting other rates would be a change outside this bug.

## Realtime, undo, commands

* Encode and decode run on the socket I/O thread (`LinkAudioSocket.cpp:176, :207`), not on the audio thread. The change is
  one comparison: no allocation, lock, log or wait.
* No command added, no undo step touched.

## Commands run and EXIT codes

Build tree: `build/` in this worktree. Logs: the run folder `logs/`.

| # | Command (cwd) | EXIT | Result |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target LinkAudioStreamTest -j4 > logs/build-red-1.log 2>&1` | 2 | ccache could not write its temp file: `CCACHE_TEMPDIR` in the ccache config points outside the write-jail. Environment problem, not code. |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-tmp-new-p17-78 cmake --build build --target LinkAudioStreamTest -j4 > logs/build-red-2.log 2>&1` | 0 | Built with the RED test (first version, slots in `LinkAudioStreamTest.cpp`). |
| 3 | RED: `cd build/tests && ctest -R LinkAudioStreamTest --output-on-failure > logs/test-red-1.log 2>&1` | 8 | `FAIL! zeroSampleRateIsNotEncoded`: encode returned 1040, expected 0. `FAIL! zeroSampleRateIsNotDecoded`: decode returned TRUE, expected FALSE. Totals 21 passed, 2 failed. |
| 4 | `git diff -- src/core/LinkAudioCodec.cpp include/LinkAudioStream.h > logs/fix.patch` then the fix applied (first version) | 0 | Fix in tree. |
| 5 | GREEN: `ctest -R LinkAudioStreamTest --output-on-failure > logs/test-green-1.log 2>&1` | 0 | 100% passed (1 of 1). |
| 6 | Proof 1: `git apply -R logs/fix.patch`; `cmake --build build --target zene LinkAudioStreamTest -j4`; `ctest -R LinkAudioStreamTest` | 0 / 0 / 8 | With the fix reverted, both slots FAIL (same lines as #3). |
| 7 | Proof 1, restore: `git apply logs/fix.patch`; rebuild; `ctest -R LinkAudioStreamTest` | 0 / 0 / 8 | Totals 23 passed, 0 failed, then a SEGFAULT at teardown (flake, see below). |
| 8 | Final version: `git apply -R logs/fix.patch` (final patch); `cmake --build build --target zene LinkAudioCodecTest -j4`; `cd build/tests && ctest -R "^LinkAudioCodecTest$"` | 0 / 0 / 8 | RED on the final code: `zeroSampleRateIsNotEncoded` and `zeroSampleRateIsNotDecoded` FAIL; Totals 2 passed (init/cleanup), 2 failed. |
| 9 | Final version, restore: `git apply logs/fix.patch`; rebuild; `ctest -R "^LinkAudioCodecTest$"` | 0 / 0 / 0 | GREEN. The re-applied diff is byte-identical to `logs/fix.patch` (`cmp`). |
| 10 | Family: `cd build/tests && ctest -R "^(LinkAudioCodecTest\|LinkAudioStreamTest)$" --output-on-failure` | 0 | 100% passed (2 of 2): LinkAudioStreamTest 6.68 s, LinkAudioCodecTest 0.05 s. |
| 11 | Direct: `./LinkAudioCodecTest` (in `build/tests`) | 0 | PASS: `zeroSampleRateIsNotEncoded`, `zeroSampleRateIsNotDecoded` (plus QTest's init/cleanup entries). Totals 4 passed, 0 failed. |
| 12 | Direct: `./LinkAudioStreamTest` (in `build/tests`) | 0 | Totals 21 passed, 0 failed. |
| 13 | Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Crashed before any gate ran: `PermissionError` creating the sibling folder `zene-qwen-new-p17-78-feature-check` (outside the write-jail). Not a gate result. |
| 14 | Gates, same logic: `python3 -I /tmp/new-p17-78-gates.py <worktree> logs/gates-4` (a copy of `feature_check.py`'s main, logs redirected into the run folder, same gate lists and same baseline JSON `qwen/gate-baselines/c6ee4fc7b...json`) | 0 | RESULT PASS (table below). |

Gate table (run 14, final tree): fork-sources PASS (0); all-sources-reproduce PASS (0); no-upstream-regression PASS (0);
check-namespace PASS (0); no-tautology PASS (0); doc-figures PASS (0); complexity PASS (no new item in changed files);
file-length PASS (no new item in changed files). Both earlier FAILs were mine and are fixed: complexity
(`decodeAudioPacket` CCN 11, now below 10 after the helper), file-length (`LinkAudioStreamTest.cpp` 522 lines, now the
test lives in its own file). fork-sources and all-sources-reproduce failed until the new file was staged, because the
manifest recipe derives its list from `git ls-files`; re-deriving it fixed both.

## Flake found while testing (pre-existing, not fixed here)

`LinkAudioStreamTest` sometimes segfaults after its last test has passed (exit 139, or ctest `SEGFAULT`). It also happens
with the fix reverted, so the codec change is not the cause.

| Build | Runs | Segfaults |
| --- | --- | --- |
| Pristine (fix reverted), direct binary | 10 + 25 | 1 + 8 = 9 (26%) |
| Fixed, direct binary | 10 + 15 | 3 + 3 = 6 (24%) |
| Fixed, under ctest (earlier version of the same fix) | 5 | 2 |

Backtrace (gdb, `ZENE_CONTROL_CHECK_RESULTS=1 gdb -batch -ex run -ex 'bt 12' ./LinkAudioStreamTest`, log
`logs/gdb-1.log`): the crash is on the `lmms::AudioDummy` thread, in `AudioEngine::renderNextPeriod` ->
`AudioEngineWorkerThread::startAndWaitForJobs` -> `drainDeferredPlayHandleEdits` -> `QArrayDataPointer::reallocateAndGrow`.
That is the audio engine still rendering while the test process tears down. No link codec frame is on the stack.
The log shows `Totals: 23 passed` before the crash. `docs/qwen-fixes-2026-10/cluster-new-p15-97-port-port-port-port-port-port.md`
recorded a segfault in the same test. This is an audio-engine teardown race. It is out of scope for this bug and I did not
change it. Consequence for the gates: `LinkAudioStreamTest` is flaky at about 25% on every build here.

## Self-check

ASK items (non-feature form), answered with the evidence above:

1. **Regression fails with the fix reverted and passes with it.** Yes: runs 3, 6, 8 (RED, EXIT 8) and 5, 7, 9 (GREEN).
   Final code: run 8 (RED, EXIT 8, both slots FAIL), run 9 (GREEN, EXIT 0). The product diff re-applied is identical
   (`cmp` against `logs/fix.patch`).
2. **Deterministic.** Yes. The codec functions are pure. The tests use no clock, sleep, thread, socket or random input.
   No seam is needed.
3. **Fix at the cause, complete.** Encoder and decoder share `isValidPacketShape` (`LinkAudioCodec.cpp:56-58, :75, :99`).
   Every caller was checked (above): the sender refuses to encode a zero rate before the call (`LinkAudioSocket.cpp:197-198`),
   and the receiver's refusal at `LinkAudioStream.cpp:364` stays as a second line of defence. No sibling needs the same
   change: the only other packet-rate handling is the producer side (`LinkAudioSocket.cpp:205`).
4. **No collateral damage.** Old data: none. The wire format is persisted nowhere, and valid packets are byte-identical.
   The frame limit is unchanged (1..128, as before). The only observable change is the counter described above
   (`rate_mismatch` -> `malformed`, for zero-rate packets only). No new limit was added for other rates. Nothing in the
   tests or callers sends a zero rate, so no legitimate caller is affected.
5. **Realtime, undo, commands.** Encode/decode run on the I/O thread (`LinkAudioSocket.cpp:176, :207`), not the audio thread.
   The added work is one comparison. No allocation, lock, log or wait is added. No command, no undo step.
6. **Registration and gates.** Gate table above, all PASS (run 14, EXIT 0). The official `feature_check.py` could not run
   here (write-jail blocks its sibling log folder, EXIT 1; run 13). `submit_check.py` is reported below.
7. **Portability (Qt 5.15 / MSVC / macOS).** Only standard C++ (`std::uint32_t`, `std::size_t`); no Qt API added.
8. **Scope.** The downstream claim in the finding (conversion of a zero rate) is not reached on this branch, see Evidence.
   The fix changes only the codec's contract. The teardown flake is out of scope and is reported, not fixed.

## Files committed

`include/LinkAudioStream.h`, `src/core/LinkAudioCodec.cpp`, `tests/src/core/LinkAudioCodecTest.cpp` (new),
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`,
`docs/LINK-AUDIO.md`, `docs/qwen-fixes-2026-10/cluster-new-p17-78.md`.

The test file was not in any `LinkAudioStreamTest.cpp` change: that file is unmodified (`git diff` empty).
