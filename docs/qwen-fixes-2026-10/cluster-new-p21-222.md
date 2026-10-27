Verdict: FALSE-POSITIVE

new-p21-222: `LinkAudioStream::addFromRing` ignores the return value of `m_rx.read`

Evidence: a short read cannot happen on any path that reaches `addFromRing`. The only caller is `receivePeriod`, which calls it at [src/core/LinkAudioStream.cpp:268](src/core/LinkAudioStream.cpp:268) only when `m_primed` is still true. Before that, [src/core/LinkAudioStream.cpp:255](src/core/LinkAudioStream.cpp:255) takes the ring fill (`fill = m_rx.fill()`), and [src/core/LinkAudioStream.cpp:263-266](src/core/LinkAudioStream.cpp:263) clears `m_primed` when `fill < period`. The trim at [src/core/LinkAudioStream.cpp:259](src/core/LinkAudioStream.cpp:259) leaves `fill == target`, and the clamp at [src/core/LinkAudioStream.cpp:254](src/core/LinkAudioStream.cpp:254) with `jitterBounds` ([src/core/LinkAudioStream.cpp:212-218](src/core/LinkAudioStream.cpp:212)) makes `target >= period` whenever `period <= capacity/4`. If `target < period`, the same `fill < period` gate at line 263 fires. So `addFromRing(frames, period)` runs only when the ring holds at least `period` frames.

Why the reads are then full:

- The consumer is the audio thread alone. `AudioRing::read` and `skip` are consumer-only ([include/LinkAudioStream.h:268](include/LinkAudioStream.h:268) names the producer as the I/O thread, the consumer as the audio thread).
- Between the snapshot at line 255 and the call at line 268 the audio thread does not call `skip` again, except in the trim (line 259), and that trim is accounted for above.
- The I/O thread only grows the ring (`m_rx.write` at [src/core/LinkAudioStream.cpp:380](src/core/LinkAudioStream.cpp:380), `writeSilence` at [src/core/LinkAudioStream.cpp:324](src/core/LinkAudioStream.cpp:324)). `AudioRing::fill` and `read` ([src/core/LinkAudioCodec.cpp:149-200](src/core/LinkAudioCodec.cpp:149)) take `n = min(frames, head - tail)`, so with `head - tail >= period` every chunk `n = min(period - done, ScratchFrames)` is read in full.
- The loop at [src/core/LinkAudioStream.cpp:199-209](src/core/LinkAudioStream.cpp:199) copies `m_scratch[0..n)` into the output after each `read`. The sample loop would use stale scratch only if `read` returned fewer than `n`, which the two points above rule out.

So the stale-scratch case in the finding is not reachable from the code as it stands. The function relies on a precondition (`fill >= period`) that its caller establishes and does not restate. That is a latent fragility, not a live bug, so I made no production change. A future change to the gate at lines 254-266 could expose it, and at that point the fix in the finding (use the returned count for both the sample loop and `done`) is the right one.

Not changed on purpose: no production code, no test. The procedure's RED/GREEN step applies to real bugs; a regression test for an unreachable path would be a test that passes with or without a fix. Existing coverage of this path is `LinkAudioStreamTest`, which passes unmodified (20/20, below).

| Evidence | Command | Result |
| --- | --- | --- |
| Test target build, first attempt (ccache could not write its temp dir under `zene-bunny-fix/dsh/home`; not a code error) | `cmake --build build --target LinkAudioStreamTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | EXIT=2 (`ccache: error: failed to create temporary file ... Permission denied`); no code compiled |
| Test target build, retry with the ccache temp dir set to a writable location | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new21 cmake --build build --target LinkAudioStreamTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` | EXIT=0; 0 `ccache: error` lines |
| Family ctest, baseline on unmodified code | `ctest -R '^LinkAudioStreamTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; 1/1 passed (6.15 sec) |
| Direct run of the test binary (executed-case count) | `./LinkAudioStreamTest > .../logs/test-3-verbose.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; `Totals: 20 passed, 0 failed, 0 skipped` |
| Caller and precondition audit | `rg -n 'addFromRing\|m_rx\.\|receivePeriod\(' src/core/LinkAudioStream.cpp > .../logs/audit-1.log 2>&1; echo EXIT=$?` | EXIT=0; the only `addFromRing` call is line 268 (its definition is line 197); `read` at line 202 is the only read; `skip` at lines 225, 237, 248, 259, 294 (all audio-thread side); `fill()` snapshots at lines 255 and 269; producer-side `writeSilence`/`write` at lines 324 and 380 |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-222.r1/logs/`.

Files committed in this change: this report only.
