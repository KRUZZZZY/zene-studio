Verdict: FIXED

Cluster new-p23-141 (Zyn setBufferSize accepts invalid sizes). The defect is real but latent: `setBufferSize()` stores any `int`, and a non-positive value breaks the synth's invariants. No current caller passes one, so the fix is defensive hardening at the function that takes the value. The finding's wording "the oversized-period check becomes ineffective" is not what happens, and is corrected below.

## What the code does (before the fix)

- `setBufferSize()` stored the value unchecked: `m_transportSize = bufferSize` and `synth->buffersize = std::min( SynthBlockSize, bufferSize )` ([LocalZynAddSubFx.cpp:123-131](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:123)). `synth->alias()` then sets `buffersize_f = buffersize` ([globals.h:238-244](plugins/ZynAddSubFx/zynaddsubfx/src/globals.h:238)), so a zero makes `buffersize_f` zero.
- A zero `buffersize_f` reaches note-on DSP math: `ADnote` divides by it ([ADnote.cpp:178](plugins/ZynAddSubFx/zynaddsubfx/src/Synth/ADnote.cpp:178), `samplerate_f / buffersize_f`). Note-ons with offset 0 are applied at once, outside `process()` ([LocalZynAddSubFx.cpp:232-253](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:232)).
- With `m_transportSize <= 0`, the oversized-period guard in `process()` fires for every period with frames > 0 and renders silence ([LocalZynAddSubFx.cpp:312-318](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:312)). That is over-restrictive, not "ineffective". For a negative size and a zero-frame period, the `assert` at [LocalZynAddSubFx.cpp:314](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:314) would fire in a debug build.
- The render path never asks Zyn for an invalid frame count: `renderFrames()` returns early when `frames <= 0` ([LocalZynAddSubFx.cpp:360-363](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:360)), and the guard keeps `out.frames()` within `m_transportSize`. Had it not, `Master::GetAudioOutSamples` with `synth->buffersize == 0` would loop forever (`smps` never shrinks; [Master.cpp:498-518](plugins/ZynAddSubFx/zynaddsubfx/src/Misc/Master.cpp:498)). The guard is what keeps that unreachable.

## Reachability (why it is latent)

- Local path: `ZynAddSubFx.cpp:656` passes `Engine::audioEngine()->framesPerPeriod()`. That value is set once in the constructor as `std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`, where `m_framesPerAudioBuffer` is clamped to `[MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE]` ([AudioEngine.cpp:474-479](src/core/AudioEngine.cpp:474), [AudioEngine.h:74-75](include/AudioEngine.h:74)). `MINIMUM_BUFFER_SIZE` is 32, so the value is never below 32.
- Remote path: `RemoteZynAddSubFx.cpp:81` passes `bufferSize()`, which comes from `IdBufferSizeInformation` ([RemotePluginClient.h:272-278](include/RemotePluginClient.h:272)). The host sends its own `framesPerPeriod`, so the same bound applies.
- No other caller of `setBufferSize` exists in the product (`rg` over `plugins/ZynAddSubFx` and the rest of the tree, excluding the vendored synth).

## The fix

[LocalZynAddSubFx.cpp](plugins/ZynAddSubFx/LocalZynAddSubFx.cpp): `setBufferSize()` returns early when `bufferSize < 1`, so the last valid size stays in force. I chose rejection over clamping to 1: a clamp to 1 would leave `m_transportSize = 1`, and every real period would be silenced by the guard.

Both callers go through this one function, so there is no sibling path to keep in step. `setSampleRate()` has the same shape (`samplerate_f` divides in LFO and Envelope) but is a different finding; I did not change it.

## Regression

[tests/src/plugins/ZynNoteOffTest.cpp](tests/src/plugins/ZynNoteOffTest.cpp): `nonPositiveBufferSizeIsRefusedAndTheLastValidSizeStays()` sets 128, then 0 and -4, and checks `m_transportSize` stays 128 (observed through a test-side accessor; no product seam). Deterministic: no timing, no audio thread. It does not check `synth->buffersize_f` directly, because the synth global is not visible to this test target; the transport-size assertion is the one that fails.

Registration: `tests/upstream-modifications.txt` gets one line for `LocalZynAddSubFx.cpp`. The test file is already in `tests/fork-sources.txt`.

## Commands and exit codes

Environment note: the first build failed with `EXIT=2` because ccache could not write its temp file under `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/` (outside the write-jail). Every later build ran with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp`, which changes no source or config.

| Step | Command (from the worktree root, logs in `LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-141.r1/logs`) | Result |
| --- | --- | --- |
| Build, first attempt (ccache permission failure, not code) | `cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-1.log 2>&1; echo EXIT=$?` | EXIT=2 (`build-1.log`) |
| RED build, second attempt | `CCACHE_TEMPDIR=... cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-2.log 2>&1; echo EXIT=$?` | EXIT=0 |
| **RED test** (no product fix present) | `cd build/tests && ctest -R '^ZynNoteOffTest$' --output-on-failure > $LOGS/test-2-red.log 2>&1; echo EXIT=$?` | **EXIT=8**, `FAIL! ... nonPositiveBufferSizeIsRefusedAndTheLastValidSizeStays()`, `Actual (synth.transportSize()): 0`, `Expected (128): 128`, `Loc: [ZynNoteOffTest.cpp(62)]` |
| Fix applied; GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-3.log 2>&1; echo EXIT=$?` | EXIT=0 |
| **GREEN test** | `ctest -R '^ZynNoteOffTest$' --output-on-failure > $LOGS/test-3-green.log 2>&1; echo EXIT=$?` | **EXIT=0**, 1/1 passed |
| Instrument test target rebuilt (links the changed core) | `cmake --build build --target ZynAddSubFxInstrumentTest -j4 > $LOGS/build-4.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Product target rebuilt | `cmake --build build --target zene -j4 > $LOGS/build-5-zene.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Separate-process test target (not built by the earlier targeted build; without it ctest reports "Not Run") | `cmake --build build --target ZynSeparateProcessTest -j4 > $LOGS/build-6.log 2>&1; echo EXIT=$?` | EXIT=0 |
| **Family** | `cd build/tests && ctest -R 'Zyn' --output-on-failure > $LOGS/test-5-family.log 2>&1; echo EXIT=$?` | **EXIT=0**, 3/3 passed: ZynSeparateProcessTest, ZynNoteOffTest, ZynAddSubFxInstrumentTest |
| Step 4b: save the product diff | `git diff -- plugins/ZynAddSubFx/LocalZynAddSubFx.cpp > $LOGS/fix.patch; echo EXIT=$?` | EXIT=0 |
| Step 4b: revert the product fix | `git apply -R $LOGS/fix.patch; echo EXIT=$?` | EXIT=0 |
| Step 4b: rebuild without the fix | `cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-7-reverted.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Step 4b: **test without the fix must FAIL** | `ctest -R '^ZynNoteOffTest$' --output-on-failure > $LOGS/test-6-red-4b.log 2>&1; echo EXIT=$?` | **EXIT=8**, same `Actual 0 / Expected 128` assertion |
| Step 4b: reapply the fix | `git apply $LOGS/fix.patch; echo EXIT=$?` | EXIT=0 |
| Step 4b: rebuild with the fix | `cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-8-reapplied.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Step 4b: **test with the fix must PASS** | `ctest -R '^ZynNoteOffTest$' --output-on-failure > $LOGS/test-7-green-4b.log 2>&1; echo EXIT=$?` | **EXIT=0**, 1/1 passed |
| Comment-only edit to the test header; rebuild and rerun | `cmake --build build --target ZynNoteOffTest -j4 > $LOGS/build-9-comment.log 2>&1` then `ctest -R '^ZynNoteOffTest$' ...> $LOGS/test-8-green-final.log` | build EXIT=0; test EXIT=0 |
| Gates, first attempt | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $LOGS/gates-1.log 2>&1; echo EXIT=$?` | EXIT=1 before any gate ran: `PermissionError` creating `<worktree-parent>/zene-qwen-new-p23-141-feature-check`, outside the write-jail (`gates-1.log`) |
| Gates, rerun | An unchanged copy of `feature_check.py` in `$LOGS` with only two paths changed: its log folder (now `$LOGS/gates`) and its data root (`ROOT`, so `FIX-BASE` and the cached base listing `gate-baselines/639e1d23e...json` are read from the shared location). Gate logic and limits are unchanged. `python3 -I $LOGS/feature_check_logdir.py <worktree> > $LOGS/gates-2.log 2>&1; echo EXIT=$?` | **EXIT=0**, `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files") |

Family test count: 3 executed, 3 passed (ZynSeparateProcessTest, ZynNoteOffTest, ZynAddSubFxInstrumentTest). Zero-tests check: passed.

## Files committed

- `plugins/ZynAddSubFx/LocalZynAddSubFx.cpp` (product fix)
- `tests/src/plugins/ZynNoteOffTest.cpp` (regression and header line)
- `tests/upstream-modifications.txt` (one registration line)
- `docs/qwen-fixes-2026-10/cluster-new-p23-141.md` (this report)

## Not checked

- `ZynAddSubFx.cpp:656` and the remote path were read, not exercised with a live host. No note-on with a non-positive size was run, because the only producer is the guard added here.
- `setSampleRate()` is out of scope for this finding and was not examined further.
