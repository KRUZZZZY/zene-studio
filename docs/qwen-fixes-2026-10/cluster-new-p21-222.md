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

## Self-check

- **Regression test fails with the product change reverted and passes with it.** No product change was made, so there is no RED/GREEN pair and no test was added. The procedure's step 4b applies to a real bug; this verdict is FALSE-POSITIVE, which the procedure resolves by skipping to the report. The existing family `LinkAudioStreamTest` passes unmodified: `./LinkAudioStreamTest` EXIT=0, `Totals: 20 passed, 0 failed` (logs `test-3-verbose.log`, `test-2.log`).
- **Deterministic.** Not applicable: no test was written. The existing family's timing-sensitive cases are outside this report's claim.
- **Fix at the cause and complete.** Not applicable: no fix. The caller checks were done instead: the only call to `addFromRing` is `src/core/LinkAudioStream.cpp:268`, guarded by the `fill < period` gate at line 263 and the trim at line 259 (see the evidence above). The audit output is in `audit-1.log`.
- **No collateral damage.** No code changed, so no saved data or caller behaviour changed.
- **Not-real evidence (file:line).** `src/core/LinkAudioStream.cpp:197-210` (`addFromRing`), `:255` (`fill` snapshot), `:259` (trim), `:263-266` (underrun gate), `:268` (sole call); `src/core/LinkAudioCodec.cpp:187-200` (`AudioRing::read` returns `min(frames, head - tail)`); `include/LinkAudioStream.h:268` (ring consumer is the audio thread).
- **Gates: `feature_check.py` could not run from this session.** The script creates `<worktree>-feature-check/` next to the worktree in `projects/zene-studio-program/`, which is outside the write-jail, and fails with `PermissionError` (`logs/gates-1.log`, EXIT=1). I did not work around that. I ran each gate command the script lists, from inside the worktree, with the same arguments (logs in `logs/gate-*.log`):

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1: 7 REGRESSION lines, none in a changed file |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1: 22 REGRESSION lines, none in a changed file |

  The two ratchets are red on the train on purpose. The branch changed only `docs/qwen-fixes-2026-10/cluster-new-p21-222.md` (`git diff --name-only 639e1d23e..HEAD`), so under the script's rule (fail only on a new item in a changed file) both rows would pass. The equivalent of the script's table is therefore: six simple gates PASS; complexity and file-length PASS by that rule. The script's own table was not produced. A fresh `feature_check.py` run from an unjailed shell is still needed to confirm it.
- **Completion check.** `submit_check.py` ran (`logs/submit-check-1.log`, EXIT=1). The FAIL for the gates row is the same sandbox limit above; the Self-check FAIL is closed by this section.
