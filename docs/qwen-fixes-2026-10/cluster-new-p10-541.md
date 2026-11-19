FIXED

# cluster new-p10-541: BitInvader clamps wavetable values on load

## Problem

A saved BitInvader wavetable row holding finite values outside [-1, 1] (for example `1e30`) was accepted into the graph unchanged. `waveform::decode` already rejects non-finite values and wrong-length rows, so only the value clamp was missing.

## Change

- `plugins/BitInvader/BitInvader.cpp:201-205`: after `shape.ok()` (line 199), each sample is clamped to [-1, 1] with `std::clamp` (line 204). The loop stops at `shape.count`. The complexity gate reports no new item in the changed files (limit CCN 10; `complexity` PASS in `gates-5.log`).
- `plugins/BitInvader/BitInvader.cpp:207-211`: the table is cleared and resized as before (`setLength(previousLength)`, `clear()`, then `setLength(shape.count)` or `setLength(sampleLength)`).
- `plugins/BitInvader/BitInvader.cpp:215-216`: a refused load still calls `waveform::reportRefused(...)`, and a `qWarning()` line now sits next to it.
- `src/core/WaveformData.cpp:60-91`: the existing decode checks that the clamp relies on:
  - strict base64 (63-67, status `Invalid`);
  - byte count not a multiple of `sizeof(float)` (70-74, `Misaligned`);
  - count outside `[max(minCount,1), maxCount]` (76-80, `WrongLength`);
  - any non-finite value (83-87, `NonFinite`);
  - success (88-90, `Ok`).
- `tests/src/plugins/BitInvaderLoadSettingsTest.cpp`: new test, 315 lines (limit 500).
- `tests/all-sources.txt`: one line added (see Commits).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-541.r1/logs/`.

| Step | EXIT | Key output | Log |
|---|---|---|---|
| test-2 (RED: clamp rows fail) | not recorded in log | `Totals: 13 passed, 3 failed`; `1 tests failed out of 1`; ctest `633 - BitInvaderLoadSettingsTest (Failed)`; `Errors while running CTest` | `test-2.log` |
| test-3 (GREEN) | not recorded in log | `100% tests passed out of 5`; `Total Test time (real) = 13.00 sec` | `test-3.log` |
| test-4a (fix reversed) | 8 | `Totals: 13 passed, 3 failed`; same three rows fail | `test-4a.log` |
| test-4b (fix restored) | 0 | `100% tests passed out of 1`; `Test #633: BitInvaderLoadSettingsTest ... Passed 0.71 sec` | `test-4b.log` |
| gates, first run | 1 | `fork-sources FAIL`, `all-sources-reproduce FAIL`; the other six gates PASS | `gates-5-first.log` |
| gates, after manifest fix | 0 | all 8 gates PASS, `RESULT: PASS` | `gates-5.log` |
| submit_check | 0 | every pre-check PASS (commits, clean tree, report, tests registered, gates); 5 `?` items answered in Self-check; `RESULT: PASS - submit` | `submit-5.log` |

The three failing rows in test-2 and test-4a are the clamp rows: `+1e30 clamps to +1`, `-1e30 clamps to -1`, and `mixed row clamps only the out-of-range value`. Each reports an accepted load, for example `table[37] is 1e+30, expected 1`.

Test-2 and test-3 logs contain no `EXIT=` line, so their exit codes are unverified. The log markers above are what was observed.

The first gate run failed because `tests/all-sources.txt` did not list `tests/src/plugins/BitInvaderLoadSettingsTest.cpp`. The gate's own "Regenerate with" command produced the same list plus that one line. The line was inserted after `BitInvaderAllocationTest.cpp`, and `diff` between the manifest entries and the regenerated list then showed no differences.

## Facts

(a) The non-finite and wrong-length rejection already existed in `waveform::decode` (`WaveformData.cpp:76-87`). Only the value clamp was missing. So NaN, Inf and wrong-length rows were already refused before this fix, and the RED rows are the three clamp rows.

(b) Deviations from Luna's checkpoint, and why (reasons as given in the packet; the commits named below exist):
- The legacy `count == sampleLength` path is kept. Writers `55045b7017`, `922d2c5f38`, `ee7175be75` and `GraphWaveformLoadTest` depend on it.
- `waveform::decode` and `reportRefused` are kept. `GraphWaveformLoadTest` locks them.
- A rejected load keeps the pre-load table. `GraphWaveformLoadTest.cpp:88` lists BitInvader's row; `:315-319` asserts that a damaged attribute does not change the graph.
- `setLength` is called as before.
- `qWarning` was added next to `reportRefused` (`BitInvader.cpp:216`).

(c) The fresh-worktree instruction was not followed. `git switch` and worktree creation are forbidden in this session, so the work stayed in the existing worktree. `6d1a1143d0` is an ancestor of HEAD (`git merge-base --is-ancestor 6d1a1143d0 HEAD`, EXIT=0).

(d) The normalize factor is a private member (`m_normalizeFactor`, `BitInvader.h:115`). The test checks it through a render with normalize on.

(e) This change does not touch the audio thread. `loadSettings` runs on the load thread, and `Song::collectError` and `qWarning` also run there.

(f) TSan was not run.

## Commits

- `752d172101` `test(bitinvader): register loadSettings test in all-sources manifest` (one line in `tests/all-sources.txt`).
- Earlier, already on the branch: `08f5cfb2ee` (test), `3aed8c4adf` (fix).
- This report commit.

## Self-check

1. **The regression test fails with the change reverted and passes with it.** Yes. With the fix reversed (test-4a), EXIT=8 and the three clamp rows fail (`Totals: 13 passed, 3 failed`). With the fix restored (test-4b), EXIT=0 and `100% tests passed out of 1`. The test-2 RED run shows the same three rows failing; its exit code is not recorded in its log.

2. **The test is deterministic.** Yes. A grep of `tests/src/plugins/BitInvaderLoadSettingsTest.cpp` for `sleep`, `chrono`, `QTime`, `QElapsed`, `QTimer`, `std::thread`, `rand`, `time(` and `wait` finds nothing. The seam is the injected payload: the rows in `loadSettingsShape_data` (line 193) are passed to `loadCandidate` (line 109), which sets them as the `sampleShape` attribute of a `QDomDocument` and calls `loadSettings` directly. I did not read `playPeriod()` (line 156) in full.

3. **The fix is at the cause and complete for its callers.** Partly. The clamp sits in `BitInvader::loadSettings` only (`plugins/BitInvader/BitInvader.cpp:201-205`). The decode checks it relies on are unchanged: `WaveformData.cpp:83-87` rejects non-finite values, and nothing in decode bounds the range. Read this session, not run:
   - **BitInvader install paths.** The only `setSamples` call in `plugins/BitInvader` is `BitInvader.cpp:210`, the clamped path. The view's draw and drag edits go through `setSampleAt` → `drawSampleAt` (`Graph.cpp:530-532`, `:770-778`, called from `Graph.cpp:272` and `:305`), which clamp x and val to the model's min/max. That range is -1..1 (`BitInvader.cpp:135`; stored at `Graph.cpp:488-493`). The Sine, Triangle, Saw, Square and Noise buttons write `m_samples[i]` directly (`Graph.cpp:563-615`), but their generators stay in [-1, 1] (`include/Oscillator.h:118-145`, `:167-170`; triangle, saw and square assume `absFraction` returns [0, 1), which I did not read). Smooth (`Graph.cpp:646-667`) writes weighted averages of in-range values, so it stays in range. Normalize (`BitInvader.cpp:248-258`) reads `m_graph.samples()`.
   - **Load from file can install out-of-range or non-finite values. NEEDS-CONTEXT.** `graphModel::setWaveToUser` (`Graph.cpp:622-636`) writes `Oscillator::userWaveSample` into `m_samples` with no clamp and no `drawSampleAt`. `userWaveSample` (`include/Oscillator.h:172-180`) returns a `std::lerp` of raw buffer frames. The libsndfile read at `src/core/SampleDecoder.cpp:286-294` has no clamp or finite check in the lines I read, and I did not trace `SampleBuffer::fromFile` up to it. So a float file with |x| > 1 or NaN can reach the BitInvader wavetable and `normalize()`. Not reproduced, not fixed here. The fix needs a decision: clamp in `setWaveToUser`, or also in the file path.
   - **Sibling loaders.** All seven loaders named in `tests/upstream-modifications.txt` call `waveform::loadGraph` (`WaveformData.cpp:123-130`), which copies through `graphModel::setSamples(const float*, int)` (`Graph.cpp:547-555`). That copy does not clamp, and none of the seven loaders clamp. Details are under Other findings.

4. **No collateral damage.** Partly. The clamp is the identity on [-1, 1], so in-range saved values load unchanged, and the legacy `count == sampleLength` path is kept. With the fix in place, test-3 passed 5 of 5 tests: WaveformDataTest (#140), GraphWaveformLoadTest (#141), BitInvaderAllocationTest (#632), BitInvaderLoadSettingsTest (#633) and ControlGraphWaveformLoad (#856). The full suite was not run, so other callers have no evidence either way. The clamp adds no limit that other callers could hit.

5. **Verdict ALREADY-FIXED or FALSE-POSITIVE.** Not applicable. The verdict is FIXED.

## Other findings

- `tests/fork-sources.txt` cannot be regenerated by its header command. Its header notes that the recipe does not parse: its second awk line ends in a stray continuation, and bash reports a syntax error at line 5 of the recipe, already at HEAD. The fork-sources gate's log reports only `tests/all-sources.txt`, and the gate passes after the all-sources fix without re-deriving `fork-sources.txt`. `tests/all-sources.txt` is the list that is re-derived and reproduces.
- `tests/fork-sources.txt` lists `BitInvaderLoadSettingsTest.cpp` (line 3978), added in `08f5cfb2ee`.
- Writes to the run directory root failed with `read-only file system`. The `logs/` subdirectory is writable.
- **Sibling loaders have the same gap (not fixed; this job is BitInvader only).** `loadGraph` copies values without a range clamp, and each graph's contract is its constructor range. Loader, then model range, then consumer:
  - WaveShaper: `WaveShaperControls.cpp:77`, model 0..1 at `:48`. The consumer at `WaveShaper.cpp:77` clamps output only when clip is on (`:99-101`).
  - DynamicsProcessor: `DynamicsProcessorControls.cpp:86`, model 0..1 at `:50`. Consumer read at `DynamicsProcessor.cpp:112`.
  - MultitapEcho: `MultitapEchoControls.cpp:89-90`, models -60..0 at `:47` and 0..3 at `:48`. Consumer reads at `:124` and `:142`.
  - Watsyn: `Watsyn.cpp:587-590`, model `a1_graph` -1..1 at `Watsyn.cpp:399` and `Watsyn.h:276`. Consumer not traced.
  - Xpressive: `Xpressive.cpp:182-184`, model -1..1 at `:91`. Consumer not traced.
  - Vibed: `Vibed.cpp:202`, models -1..1 at `:125`. Consumer not traced.
  - FreeBoy: `FreeBoy.cpp:431`, model 0..15 at `:327`. The consumer at `:542-546` does `floor(wpm[i * 2]) << 4` with no range check, so an out-of-range or NaN value gives a wrong or undefined integer.
- `graphModel::setSamples(const float*)` (`Graph.cpp:538-543`) has no clamp and copies `length()` values without checking the source. MultitapEcho calls it at `MultitapEchoControls.cpp:104` and `:118`. It is not on a BitInvader path.
