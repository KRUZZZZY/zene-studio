Verdict: FALSE-POSITIVE

new-p23-64: FALSE-POSITIVE

Finding: "ConvolutionReverb assumes stereo impulse; mono files read channel 1 out of bounds" (plugins/ConvolutionReverb/ConvolutionReverb.cpp:195-196 on this tree).

## Why it is not real

The out-of-bounds read cannot happen. Every sample buffer the impulse path can receive is stored as stereo frames, and a mono source is already dual-mono by the time the reverb sees it.

1. **The frame type always has two channels.** `SampleFrame` stores its samples in `std::array<sample_t, DEFAULT_CHANNELS>` (include/SampleFrame.h:189), and `DEFAULT_CHANNELS` is `ch_cnt_t{2}` (include/lmms_constants.h:38). There is no one-channel frame, so `frames[i][1]` is always inside the frame.
2. **Decoders upmix mono to stereo.** The WAV/libsndfile path builds `SampleFrame{buf[i], buf[i]}` for one-channel files (src/core/SampleDecoder.cpp:152-155). The FLAC/sndfile path does the same (src/core/SampleDecoder.cpp:302-303). The Vorbis/OGG path duplicates the left sample when `numChannels == 1` (src/core/SampleDecoder.cpp:237-238 and :344). The result is a `std::vector<SampleFrame>`, so every decoded frame has a left and a right sample.
3. **The buffer contract says the same.** include/SampleBuffer.h:64-66: "The frames still carry two channels (a mono source is dual-mono)". `channels()` is metadata about the source layout; it does not change the frame storage. src/core/SampleBuffer.cpp:38-50 stores `std::vector<SampleFrame>` in every constructor.
4. **The resampler keeps two channels.** `resampleBuffer` builds `AudioResampler(..., 2)` (src/core/SampleBufferResample.cpp:69), so `atRate()` (plugins/ConvolutionReverb/ConvolutionReverb.cpp:103-114) returns the same stereo frame type, and the pass-through branch also returns a stereo buffer.
5. **`channelOf` only reads inside the frame.** It loops `i < min(limit, buffer.size())` and reads `frames[i][channel]` (plugins/ConvolutionReverb/ConvolutionReverb.cpp:117-123). The only callers pass channel `0` or `1` (lines 195-196). Both are in bounds for a two-sample frame.

So a mono WAV/FLAC/OGG impulse gives `left == right` after decoding. That is the dual-mono result the finding asks the fix to produce. The code already does it, in the decoder rather than in the reverb. Adding a channel-count check inside `loadImpulse` would be dead code: `SampleBuffer::channels()` can read 1 after `sample.edit_range` downmixes a clip (include/SampleBuffer.h:64-66), but the reverb reads the frames, which still hold two samples.

## Commands and results

I did not build or run a RED/GREEN test. The finding is a not-real verdict based on code, and procedure step 2 says not-real skips to the report. I read the code with the Read tool and searched it with `rg`.

| Evidence | Command | EXIT / result |
| --- | --- | --- |
| Confirm `SampleFrame` storage | `rg -n "m_samples" include/SampleFrame.h` ; `rg -n "std::array<sample_t, DEFAULT_CHANNELS>" include/SampleFrame.h` | read; line 189 |
| Confirm `DEFAULT_CHANNELS` | `rg -n "DEFAULT_CHANNELS =" include/*.h` | include/lmms_constants.h:38 = 2 |
| Confirm decoder upmix | Read src/core/SampleDecoder.cpp:140-170, 270-305, 320-345 | lines cited above |
| Gate: fork sources | `bash tests/fork-sources-gate.sh` | EXIT=0 |
| Gate: all sources reproduce | `bash tests/all-sources-reproduce.sh` | EXIT=0 |
| Gate: no upstream regression | `bash tests/no-upstream-regression-gate.sh` | EXIT=0 |
| Gate: check namespace | `tests/scripted/check-namespace` | EXIT=0 |
| Gate: no tautology | `bash tests/no-tautology-gate.sh` | EXIT=0 |
| Gate: doc figures | `python3 tests/doc-figures-gate.py` | EXIT=0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check` | EXIT=1 (red on the untouched base, expected on the train; nothing changed, so no item can be new) |
| Gate: file length (ratchet) | `bash tests/file-length-gate.sh --check` | EXIT=1 (same as above) |
| Full feature check | `python3 .../qwen/feature_check.py <worktree>` | EXIT=1 before any log: PermissionError creating `<worktree>-feature-check/` outside the write jail (the script's `logs.mkdir`); the gate commands above were run one by one with the same commands instead |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-64.r1/logs/` (001 to 009).

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-64.md` only. No product or test file was changed.

## Self-check

- Verdict evidence: each claim above is a file:line in the worktree, read directly. The only place a stale-artifact concern could matter is the decoder mono path; that is source, not a binary, so the tree is the proof.
- RED/GREEN (submit_check "regression test fails with the change reverted and passes with it"): not applicable. This is a not-real verdict, so there is no product change to revert for step 4b. No test was added: a regression test for this finding would pass on the current code and prove nothing about a bug. No test is claimed.
- Determinism: not applicable, no test added. The evidence is static: file:line reads of the code listed above, with no clock, sleep or race involved.
- Fix at the cause / callers and siblings: not applicable, no fix. The callers of `channelOf` (ConvolutionReverb.cpp:195-196) and the decoders (SampleDecoder.cpp:152-155, 237-238, 302-303, 344) were all checked; each keeps two channels in every frame.
- Collateral behavior: none. No product code changed; projects and saved data load exactly as before.
- Gate row FAIL in submit_check (`gates (feature_check.py)`): this is the environment, not the change. `feature_check.py` calls `logs.mkdir` on `<worktree>-feature-check/`, which is outside this worktree and outside the write jail, so it raises PermissionError before any gate runs (log 001). The six exit-code gates were run one by one with the same commands from the worktree and all exit 0 (logs 002-007, 010-011); the two ratchets exit 1 on the untouched base, the train's expected state (logs 008-009). The orchestrator should run `feature_check.py` outside the jail to confirm this row.
- Gates: six exit-code gates pass (EXIT=0). Two ratchets exit 1 on the untouched base, which is the train's expected state; the changed set is one doc file, so no ratchet item can be new in it.
- `feature_check.py` and `submit_check.py` cannot write their log folder (`<worktree>-feature-check/`) from inside the write jail. The gates were run directly; the orchestrator should run the combined check outside the jail.
- Collateral behavior: none. No code changed.
