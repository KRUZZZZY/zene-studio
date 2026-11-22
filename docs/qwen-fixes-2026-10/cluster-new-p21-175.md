FALSE-POSITIVE

# cluster new-p21-175: GigPlayer playImpl "spin forever" on consumed>0, generated=0

Verdict: the loop cannot spin forever, and the proposed fix (`break` when `framesToMix == 0`) would introduce a defect. No product code changed.

## Evidence (code)

* `plugins/GigPlayer/GigPlayer.cpp:1112-1148` is the loop. When the resampler reports `inputFramesUsed > 0` and `outputFramesGenerated == 0`, the no-progress `break` (line 1140) does not fire, but the iteration still makes progress: `m_sourceBufferView = m_sourceBufferView.subspan(inputFramesUsed)` (line 1145) shrinks the source view. When it is empty, the next iteration reloads `m_sourceBuffer` (lines 1114-1126), and `sample.pos` / the ADSR advance by `DEFAULT_BUFFER_SIZE` (256, `include/AudioEngine.h:76`) frames each time. Input is consumed on every pass, so the loop ends as soon as the resampler emits output, which it does after a bounded startup latency.
* `framesMixed < frames` guarantees `outFrames - outOffset > 0` in `gigplayerMixResampledBlock` (`GigPlayer.cpp:133`), so `framesToMix == 0` can only come from `outputFramesGenerated == 0`.
* `AudioResampler::process` throws on any libsamplerate error (`src/core/AudioResampler.cpp:477-480`), including an invalid ratio. The "no progress and no error" state is therefore the only zero-zero return, and it is already handled by the break at line 1140.
* The proposed fix is harmful. The `break` sits BEFORE `m_sourceBufferView.subspan(inputFramesUsed)` (line 1145), but the resampler has already consumed that input. Breaking on `framesToMix == 0` would end the period early with the rest of the block unrendered. The next period would then re-feed the same already-consumed frames, since the view was never advanced. With a start-up latency of N calls that means N silent periods plus duplicated input, which is an audible regression.

## Evidence (measured)

`logs/probe/p.cpp` is a libsamplerate probe. It feeds 256-frame blocks of constant input for 2000 calls per ratio and counts calls with consumed>0 and generated=0.

Commands (from `logs/build-1.log` and `logs/probe-out.log`):

* `g++ -o logs/probe/p logs/probe/p.cpp -lsamplerate` EXIT=0
* `logs/probe/p` EXIT=0

Output (type 2 = SRC_SINC_FASTEST, type 4 = SRC_LINEAR):

```
ratio 0.00391 type 2: firstOutputCall=19 zeroGenButConsumed=19 consumed=512000 gen=1981
ratio 0.01000 type 2: firstOutputCall=7  zeroGenButConsumed=7  consumed=512000 gen=5101
ratio 0.50000 type 2: firstOutputCall=0  zeroGenButConsumed=0  gen=255981
ratio 1.08840 type 2: firstOutputCall=0  zeroGenButConsumed=0  gen=512000
ratio 2.00000 type 2: firstOutputCall=0  zeroGenButConsumed=0  gen=512000
ratio 256.0   type 2: firstOutputCall=0  zeroGenButConsumed=0  gen=512000
```

(The remaining ratio and type rows in the log show the same pattern.) The consumed>0, generated=0 case exists, but it is a transient start-up latency that is followed by output. At the extreme 1/256 ratio it lasts 19 calls, and the render loop consumes input on each of them. This is not an endless loop.

## Test

No regression test was added: there is no defect to fail without a fix. The existing no-progress test (`PluginPortsMigrationTest::gigplayerNoProgressFallbackKeepsEarlierSamples`) covers the 0/0 branch and is unchanged. I did not run it this session.

## Not done

I did not run `feature_check.py`: no product file changed, so it had nothing to check. The `submit_check.py` result is in the final message.

## Other findings

None.

## Self-check

* Regression test fails without / passes with the change: not applicable. The verdict is FALSE-POSITIVE and no product code changed, so there is no red/green pair and no `proof.json` regression-test artifact. The manifest has a `code-reference` instead.
* Deterministic test: no test was added. The probe in `logs/probe/p.cpp` uses fixed constant input and fixed ratios, with no clock or sleeps.
* Fix at the cause and complete: nothing was changed. The only caller of the mixing helper is the render loop (`GigPlayer.cpp:1137`), and I read the whole loop (`GigPlayer.cpp:1112-1148`).
* No collateral damage: no product, save-format or command file was touched, so saved projects load as before.
* Verdict evidence: `plugins/GigPlayer/GigPlayer.cpp:1145` (the source view advances by `inputFramesUsed` on every pass), `GigPlayer.cpp:1140` (the existing 0/0 break), `src/core/AudioResampler.cpp:477-480` (libsamplerate errors throw), and the measured start-up latency in `logs/probe-out.log` (at most 19 calls at ratio 1/256, then output).
* Sibling commands (`app.*`, `arrangement.*`): not affected. The only file in this commit is this report. The test files `submit_check.py` lists (`tests/control-*.py`) come from the branch's merged history, not from this change.
