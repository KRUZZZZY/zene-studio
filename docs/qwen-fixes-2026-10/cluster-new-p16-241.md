FIXED

Cluster new-p16-241: “A zero sample rate leaves the delay ring unusable” in `plugins/Delay/StereoDelay.cpp`.

Branch: `integration/new-p16-241-c6ee4fc7bc` (base `c6ee4fc7bc`). This review follow-up applies the explicit refusal contract required by the earlier QA reviews; it supersedes the submitted `NEEDS-CONTEXT` self-check.

## Findings

- `setSampleRate` now returns for nonpositive rates before freeing or replacing the current ring. This preserves a live delay’s ring and audible tail for zero or negative reconfiguration.
- The constructor’s initial `maxTime * sampleRate` uses `std::int64_t`, and only a positive result fitting `int` is narrowed. A refused initial rate leaves the delay empty; `tick` passes samples through while no ring exists.
- A later valid sample rate initializes a full-duration ring when there was no prior ring to scale. Oversized rings (`rate * maxTime >= 2^31`) are refused before the existing ring is touched.
- With a positive sample rate, zero or negative `maxTime` still follows the established one-frame clamp.

## Regression coverage

`tests/src/core/StereoDelay.cpp` covers:

- A zero-rate constructor starts empty and passes samples through; a later valid rate restores the full delay duration.
- Zero and negative reconfiguration preserve the live ring length, delay length, and stored samples.
- Constructor overflow and oversized reconfiguration are refused safely.
- The existing zero/negative `maxTime` clamp and ordinary sample-rate reconfiguration remain covered.

The focused `StereoDelay` test failed with the product hunks reversed and passed after restoring them. The earlier artifact at `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-241.r4/negctl` independently proves the constructor-overflow and out-of-range regressions also fail with the original product code and pass with the submitted fix.

## Verification

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-241.r4/logs`.

| Command | Exit | Evidence |
| --- | ---: | --- |
| `CCACHE_DISABLE=1 cmake --build build --target zene StereoDelay DelayControlsTest DelayLfoAutomationTest MonoDelayTest partc_ref_delay -j12` | 0 | `review-build-restored.log` |
| From `build/tests`: `ctest -R '^(StereoDelay|DelayControlsTest|DelayLfoAutomationTest|MonoDelayTest)$' --output-on-failure` | 0, 4/4 | `review-test-restored.log` |
| Reversed only the QA product hunks, rebuilt `zene` and `StereoDelay` | 0 | `review-build-reversed.log` |
| From `build/tests`: `ctest -R '^StereoDelay$' --output-on-failure` with product hunks reversed | 8; two nonpositive-rate regressions failed | `review-test-reversed.log` |
| `git checkout -- plugins/Delay/StereoDelay.cpp`, rebuilt the focused targets, and reran the focused CTests | 0 | `review-build-restored.log`, `review-test-restored.log` |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p16-241-c6ee4fc7bc` | 0 | `review-feature-check-final2.log` (all gates pass; no new complexity or file-length items) |

The header is unchanged. No save/load fields or control commands were added. `tick` adds no allocation, locking, waiting, I/O, or logging.
