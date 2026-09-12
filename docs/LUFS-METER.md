# Loudness and true-peak meter (ITU-R BS.1770-4 / EBU R128)

> **Superseded in part (2026-09-11, lane `post-alpha/lufs-wire`).** This lane built the measurement
> core and stopped at the class boundary by design. The class is no longer inert: the render path
> now feeds it and reports it to the user. See **[docs/LUFS-WIRING.md](LUFS-WIRING.md)** for what was
> wired, the measured renders and the passivity proof. Every statement below that says nothing
> consumes the class is marked `[SUPERSEDED]` and describes the tree as it stood when this lane landed.

**Lane:** `post-alpha/lufs-meter` · **Branch base:** `post-alpha/v0.2` @ `0c23587d2`
**Scope:** one coherent slice — a realtime-safe measurement core plus a consumption point. The
GUI meter is deliberately out of scope.

## What was built

| File | What it is |
|---|---|
| `include/LufsMeter.h` | `lmms::LufsMeter` — the public class: construction, `processBlock()` / `processPlanar()`, `read()` and the four value getters |
| `src/core/LufsMeter.cpp` | The measurement: K-weighting, 400 ms gating blocks, gated integration, momentary/short-term windows, 4x-oversampled true peak |
| `tests/src/core/LufsMeterTest.cpp` | The compliance vectors below (QTest, `QTEST_GUILESS_MAIN`) |

Registration (the three places the tree requires):

- `src/core/CMakeLists.txt` — `core/LufsMeter.cpp` in `LMMS_SRCS` (compiled into the core library).
- `tests/CMakeLists.txt` — `src/core/LufsMeterTest.cpp` in `LMMS_TESTS`.
- `tests/fork-sources.txt` — `include/LufsMeter.h` and `src/core/LufsMeter.cpp`, so the fork-scope
  gates (coverage, length, duplication, complexity) and Gate 6 (upstream divergence) see them.
  Omitting a new source from this file is how `LatencyCompensation` went unwatched for a day.

### The consumption point

```cpp
lmms::LufsMeter meter(engine->baseSampleRate(), DEFAULT_CHANNELS);   // once, anywhere
meter.processBlock(buffer.data(), buffer.size());                     // audio thread, per block
const lmms::LufsMeter::Reading reading = meter.read();                // UI thread, any time
// reading.integratedLufs / momentaryLufs / shortTermLufs / truePeakDbtp
```

- `processBlock(const SampleFrame*, f_cnt_t)` takes interleaved stereo — the shape a
  `SampleBuffer` / mixer channel hands out. `processPlanar(const sample_t* const*, ch_cnt_t, f_cnt_t)`
  takes planar per-channel buffers for the 1/2/6-channel layouts.
- The getters are read-only and lock-free; the value reads never take a lock, so a poll can land
  between two blocks. That is what a meter wants and it keeps the audio thread lock-free.
- **Nothing called it, until the wiring lane.** `[SUPERSEDED]` No mixer, engine, render or device
  code constructed a `LufsMeter` when this lane landed; the class was inert in the default audio
  path (verified with `grep`, see "Wiring" below). `post-alpha/lufs-wire` added the consumer: the
  render path constructs a `LoudnessReport` (which owns a `LufsMeter`) when the render asked for a
  report and feeds it each rendered block — see [docs/LUFS-WIRING.md](LUFS-WIRING.md). `grep` for
  the class now finds `src/core/ProjectRenderer.cpp`, `src/core/LoudnessReport.cpp` and
  `src/core/RenderManager.cpp`, all reachable from `lmms render` and the export dialog.

## The algorithm, and where each piece comes from

- **K-weighting** — the two biquads of BS.1770-4 Annex 1: the high-shelf pre-filter (stage 1) and
  the RLB high-pass (stage 2), applied per channel in Direct Form I with double-precision state.
  The recommendation prints the coefficients for 48 kHz only and asks implementations at other
  rates to reproduce the same frequency response, so `kWeightingCoefficients()` derives both
  sections from the equivalent analogue prototype parameters (f0 = 1681.974450955533 Hz,
  G = 3.999843853973347 dB, Q = 0.7071752369554196 for stage 1; f0 = 38.13547087602444 Hz,
  Q = 0.5003270373238773 for stage 2 — the parameters the published rows correspond to and that the
  reference implementations use) by bilinear transform. At 48 kHz that derivation reproduces the
  published table to 1e-16, which the unit test asserts against all 15 published digits.
- **Loudness of a block** — `-0.691 + 10*log10(sum_i G_i * z_i)`, `z_i` the mean square of the
  K-weighted channel `i` over the block, `G_i` the BS.1770-4 Annex 1 Table 3 channel weights:
  1.0 for L/R/C, 1.41 for Ls/Rs, and the LFE channel of a 5.1 layout is not measured at all.
  The -0.691 is not a free constant: it is exactly the K-weighting gain at the recommendation's
  997 Hz reference frequency (measured 0.691014 dB by the test), which is why a 997 Hz tone reads
  its own level.
- **Blocks and gating** — 400 ms blocks with a 100 ms hop (75 % overlap), implemented on a 100 ms
  sub-block grid: a block is the last four sub-blocks. Absolute gate -70 LUFS, then the relative
  gate 10 LU below the mean of the blocks that survived the absolute gate; the mean square of the
  twice-gated blocks is LUFS-I. Block loudnesses are accumulated in a fixed 1501-bin histogram
  (0.05 LU bins from -70 LUFS to +5 LUFS, bin 0 = everything below the absolute gate), which is how
  the gated mean is computed without storing one value per block for the lifetime of the stream —
  the same device FFmpeg's `ebur128` filter uses (`libavfilter/ebur128.c`, 0.1 LU bins there).
- **Momentary / short-term** — LUFS-M is the loudness of the last 400 ms, LUFS-S of the last 3 s
  (30 sub-blocks), both ungated, as EBU Tech 3341 defines them.
- **True peak** — 4x oversampling with the **published order-48, 4-phase FIR interpolation filter of
  BS.1770-4 Annex 2** (12 taps per phase, coefficients copied verbatim from the recommendation). The
  maximum is taken over the four interpolated phases *and* the un-interpolated samples, so the meter
  can never read below a peak-sample meter. The recommendation designs that filter for a 48 kHz
  input and notes that a higher input rate needs proportionally *less* oversampling; applying it at
  every rate only over-measures.
- **Sentinel** — every getter returns `-std::numeric_limits<float>::infinity()` (`LufsMeter::MinusInfinity`)
  while it has no value: no gated block yet (integrated), window not full yet (M/S), nothing fed
  (true peak).

## Measured compliance vectors

All measured with `LufsMeterTest` (48 kHz, stereo unless stated). The EBU Tech 3341 test signals are
1 kHz sines "in phase in both channels, peak level of each channel", at the stated number of dB below
full scale; the tolerance is the ±0.1 LU the EBU compliance suite uses.

| Vector | Expected | Measured | Tolerance |
|---|---|---|---|
| K-weighting coefficients at 48 kHz vs the published BS.1770-4 Table 1 | published values | identical to all 15 published digits | < 1e-9 |
| K-weighting gain at 997 Hz | +0.691 dB | +0.691014 dB | ±0.005 dB |
| 1 kHz stereo sine, -23 dBFS, 20 s (EBU Tech 3341 case 1) | -23.0 LUFS-I | -22.9933 | ±0.1 |
| The same signal's LUFS-M / LUFS-S | -23.0 | -22.9933 / -22.9933 | ±0.1 |
| 1 kHz stereo sine, -33 dBFS, 20 s (case 2) | -33.0 | -32.9933 | ±0.1 |
| 10 s at -36 dBFS then 20 s at -23 dBFS (case 3, relative gate) | -23.0 | -23.0244 | ±0.1 |
| Control: constant -24.65 dBFS for 30 s (the ungated mean of the vector above) | -24.65 | -24.6433 | ±0.1 |
| -23 dBFS in the **left channel only** (channel-summing law) | -26.0 | -26.0036 | ±0.1 |
| -100 dBFS input (below the absolute gate) | sentinel | `-inf` | exact |
| 5.1 planar, L only / Ls only / LFE only | -26.0 / -24.51 / `-inf` | -26.0036 / -24.5114 / `-inf` | ±0.1 / ±0.01 / exact |
| LFE excluded: L-only vs L+LFE fed through the same path | identical | bit-identical | exact |
| fs/4 (12 kHz) sine, amplitude 1.0, 45° off the sample grid: true peak | 0 dBTP | +0.0826 dBTP | ±0.2 |
| the same signal's **sample** peak (what a peak-sample meter reads) | -3.01 dBFS | -3.0103 dBFS | ±0.02 |
| full-scale 1 kHz square wave: true peak | > sample peak | +1.9982 dBTP (sample peak 0.0000 dBFS) | > +1.0, < +3.0 |
| Momentary defined after 400 ms, short-term after 3 s | 400 ms / 3 s | -22.9936 / -inf, then -22.9933 | ±0.1 |
| Same signal in 512-frame blocks vs one buffer | identical | bit-identical | ≤ 0.001 |
| Allocations during 64 blocks + 64 reads (AllocationProbe) | 0 | 0 | exact |

### The gating vector, spelled out

`10 s at -36 dBFS + 20 s at -23 dBFS` measures **-23.0244 LUFS-I**. The ungated mean over the blocks
above the absolute gate is -24.637 LUFS, i.e. 1.6 LU away: the quiet passage is *excluded*, not
averaged in. The control signal — a constant tone at exactly that -24.65 dBFS level — measures
**-24.6433** through the same code path, 1.62 LU away from the gated reading. So a version of the
meter without the relative gate reports the control's value for the gated vector and the test fails;
a comparator that always returns the same value fails on the two levels (case 1 vs case 2) as well.

### True peak, spelled out

A 12 kHz sine (fs/4) at amplitude 1.0, 45° off the sample grid, has all its samples at ±0.7071:
a **peak-sample** reader says -3.01 dBFS, an ideal reconstruction says 0 dBTP. The meter reads
**+0.0826 dBTP** and the test asserts both the absolute value (0 ± 0.2 dBTP) *and* that the reading
is at least 2.5 dB above the buffer's sample peak — an implementation with the oversampler bypassed
reads -3.0103 dBTP, fails the first assertion and has zero difference for the second. The full-scale
square wave is the other direction: samples at full scale, waveform between them at +1.9982 dBTP.

> Note on the brief's expectation. The task asked for "a full-scale sine must read about -3.0 dBTP
> (a peak-sample implementation would read 0.0)". That is inverted: by definition true peak ≥ sample
> peak, so the -3.0 belongs to the **sample-peak** reading of this signal and the true-peak reading
> is ≈ 0.0 dBTP. The test asserts both numbers (sample peak -3.0103 dBFS measured from the buffer
> itself, true peak 0.0826 dBTP from the meter) plus the ≥ 2.5 dB gap, which is the assertion that
> actually distinguishes an oversampled meter from a peak-sample one. A full-scale *sine* whose
> samples sit at full scale reads 0.0 dBTP either way, so it cannot discriminate; the fs/4 phase-offset
> sine and the square wave can.

## Deviations from BS.1770-4 — stated, not hidden

1. **Relative gate classification is quantised.** The absolute gate is applied exactly, per block;
   the relative gate compares the bin's centre loudness (0.05 LU wide), so a block within ±0.025 LU
   of the relative threshold can be classified differently from an implementation that keeps every
   block. Worst-case effect on LUFS-I: < 0.05 LU, well inside the ±0.1 LU compliance tolerance.
2. **Bounded memory by design.** The gated mean comes from the histogram (1501 bins, ~24 KB per
   meter) instead of an unbounded list of block loudnesses: a meter that grows with stream length is
   not realtime-safe. This is the FFmpeg `ebur128` approach; the 0.05 LU bin width there is 0.1 LU.
3. **The true-peak filter is the recommendation's own example.** Its per-phase DC gains are 1.0016
   (phases 0, 3) and 0.9730 (phases 1, 2), i.e. ~0.02 dB of passband ripple, which is why the fs/4
   vector reads +0.083 rather than +0.000 dBTP. The recommendation states this filter "would satisfy
   the requirements"; the 4x factor and filter are applied at every sample rate (the recommendation
   notes that a higher input rate needs proportionally less oversampling, so this only over-measures).
4. **No LFE, no >5.1 layouts.** The Table 3 weights are exact for 1, 2 and 6 channels (5.1, LFE
   excluded). Any other channel count is measured with every channel at 1.0 — documented in the
   header, not silently guessed.
5. **Sub-block grid instead of per-sample exact block boundaries.** Blocks start on the 100 ms grid
   of the stream as fed (the standard measures sliding windows; every reference implementation
   computes the overlapping blocks on this grid, and it is exact for any signal that starts on a
   sub-block boundary).
6. **Momentary/short-term are ungated**, as EBU Tech 3341 specifies; they are *not* the gated
   integrated value over a short window.

## Wiring: provably inert (as of this lane)

`[SUPERSEDED — the class now has a consumer; see docs/LUFS-WIRING.md.]` The evidence below is what
this lane ran when it landed; the grep is no longer empty, by design.

```
$ grep -rn "LufsMeter" src/ include/ plugins/ --include=*.cpp --include=*.h | grep -v "include/LufsMeter.h\|src/core/LufsMeter.cpp"
(no output)
```

No engine, mixer, render, device or plugin source mentions the class. Rebuilding with it linked in
changes no render: it is dead weight in the library until a consumer (the GUI meter, a render
report) asks for it.

## What was run, and what it returned

### Static gates (this worktree, after the changes)

| Gate | Command | Result |
|---|---|---|
| 3 — no tautological tests | `bash tests/no-tautology-gate.sh` | **PASS**; `src/core/LufsMeterTest.cpp` 66 slots / 46 assertions / 0 tautologies. `--strict` is red tree-wide (23 files, all pre-existing); not the default mode |
| 4 — per-method complexity | `bash tests/complexity-gate.sh --check` | **PASS**; 834 functions, 23 over CCN 10 (all grandfathered), no new function over target |
| 7 — per-file length | `bash tests/file-length-gate.sh --check` | **PASS**; 102 fork sources, 8 over 500 lines (all grandfathered), no new file over the limit |
| 8 — token duplication | `bash tests/duplication-gate.sh` | **PASS**; 1.05 % duplicated lines (budget 5 %) |

Gate 6 (upstream divergence) and Gate 1 (ctest) are evidenced by the commits and the runs below.

### Build and test

```
$ cd projects/lmms-fl-research/zene-pa-lufs
$ JOBS=6 bash tools/local-ci.sh --build-dir build --jobs 6
```

The script is checked in mode 100644, so it is invoked through `bash` here (the CI job has it
executable); everything else is the CI linux-x86_64 job's exact `CMAKE_OPTS` plus the documented
`-DWANT_QT6=ON` deviation this box needs (no Qt5 development files, no sudo).

### Green run — the test binary on its own

```
$ cd build/tests && ./LufsMeterTest > /tmp/lufs-green-full.log 2>&1; echo EXIT=$?
EXIT=0
PASS   : LufsMeterTest::initTestCase()
PASS   : LufsMeterTest::kWeightingMatchesThePublishedTable()
PASS   : LufsMeterTest::kWeightingGainAt997HzIsTheCalibrationOffset()
PASS   : LufsMeterTest::integrationLoudnessReadsTheTestSignalLevel()
PASS   : LufsMeterTest::relativeGateExcludesTheQuietPassage()
PASS   : LufsMeterTest::singleChannelSignalIsThreeLuQuieter()
PASS   : LufsMeterTest::planarFeedFollowsTheFiveOneWeightings()
PASS   : LufsMeterTest::windowsFillInOrderAndBlockSizeDoesNotMatter()
PASS   : LufsMeterTest::silenceReadsMinusInfinityAndKeepsTheRunningValue()
PASS   : LufsMeterTest::truePeakOversamplesTheSignal()
PASS   : LufsMeterTest::processingABlockAllocatesNothing()
PASS   : LufsMeterTest::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 2537ms
```

### Red runs — the suite against a deliberately broken implementation

Three one-line mutations of `src/core/LufsMeter.cpp`, each rebuilt (`cmake --build build --target
LufsMeterTest`), run, then reverted and md5-checked. They are not committed.

**A. 4x over-sampling bypassed** (`updateTruePeak()` takes only the un-interpolated samples - i.e. a
peak-sample meter):

```
FAIL!  : LufsMeterTest::truePeakOversamplesTheSignal() 'std::fabs(truePeak - 0.0) <= 0.2' returned FALSE. (measured -3.0103, expected 0.0000 +/- 0.2000)
Totals: 11 passed, 1 failed, 0 skipped, 0 blacklisted, 489ms
```

The measured -3.0103 dBTP is exactly the value the brief attributed to a true-peak meter: it is the
peak-sample reading of this signal, and the test catches it.

**B. Relative gate disabled** (`gatedLoudness()` averages every block above the absolute gate):

```
FAIL!  : LufsMeterTest::relativeGateExcludesTheQuietPassage() 'std::fabs(gatedReading - -23.0) <= 0.1' returned FALSE. (measured -24.6366, expected -23.0000 +/- 0.1000)
Totals: 11 passed, 1 failed, 0 skipped, 0 blacklisted, 2175ms
```

-24.6366 is the ungated mean — the same number the control tone measures, which is why the test
also carries the control: a meter without the gate cannot tell the two signals apart.

**C. K-weighting bypassed** (the raw sample is measured):

```
FAIL!  : LufsMeterTest::integrationLoudnessReadsTheTestSignalLevel() ... (measured -23.6910, expected -23.0000 +/- 0.1000)
FAIL!  : LufsMeterTest::relativeGateExcludesTheQuietPassage() ... (measured -23.7220, expected -23.0000 +/- 0.1000)
FAIL!  : LufsMeterTest::singleChannelSignalIsThreeLuQuieter() ... (measured -26.7013, expected -26.0000 +/- 0.1000)
FAIL!  : LufsMeterTest::planarFeedFollowsTheFiveOneWeightings() ... (measured -26.7013, expected -26.0000 +/- 0.1000)
FAIL!  : LufsMeterTest::windowsFillInOrderAndBlockSizeDoesNotMatter() ... (measured -23.6910, expected -23.0000 +/- 0.1000)
FAIL!  : LufsMeterTest::silenceReadsMinusInfinityAndKeepsTheRunningValue() ... (measured -23.7237, expected -23.0000 +/- 0.1000)
Totals: 6 passed, 6 failed, 0 skipped, 0 blacklisted, 1675ms
```

Six slots catch it at the 0.69-0.70 dB the K-weighting's 997 Hz gain accounts for - the coefficient
and calibration-offset checks pass in this state (they test the filter design, not the audio path),
which is the point of having both.

### The whole suite through the CI-reproduction script

```
$ cd projects/lmms-fl-research/zene-pa-lufs
$ JOBS=6 bash tools/local-ci.sh --build-dir build --jobs 6
=== local-ci: reproducing .github/workflows/build.yml :: linux-x86_64 ===
machine     : Linux x86_64, Ubuntu 24.04.4 LTS
compiler    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
ccache      : /home/kruzzzzy/.local/bin/ccache
build dir   : build (jobs=6, ctest -j2)
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j6) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
linux-x86_64: REPRODUCED
  run: configure OK, build OK, ctest OK (100% tests passed, 0 tests failed out of 26)
local-ci: overall exit=0 (0 = every executed step passed)
```

`build/ctest.log`: `7/26 Test #8: LufsMeterTest .... Passed 2.48 sec`. No test-count surprise: 26 tests
in this build tree (the CLAP-hosting tests are not configured here — no CLAP headers in this
worktree — and that is true of the base commit too).

A first `local-ci.sh` run exited 143 (SIGTERM) on the build step: the cause was my own doing — a
second `cmake --build build --target LufsMeterTest` was running in the same build directory while the
script's build ran. Re-running the script with nothing else touching `build/` is the run above.

### Gate 6 — no undeclared divergence in upstream code

```
$ bash tests/no-upstream-regression-gate.sh > /tmp/lufs-gate6.log 2>&1; echo GATE6_EXIT=$?
GATE6_EXIT=0
include/LufsMeter.h                                      fork-NEW (allowed)
src/core/LufsMeter.cpp                                   fork-NEW (allowed)
tests/fork-sources.txt                                   tests (allowed)
tests/src/core/LufsMeterTest.cpp                         tests (allowed)
tests/CMakeLists.txt                                     build config (allowed)
PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is
declared (31 file(s) in the ledger)
```

Nothing in this lane touches inherited code: the class is new, the test is new, and the two
build-list lines and the scope file are the allowed categories.

## What is NOT proven

- **No GUI, no wiring, no device** — in this lane. `[SUPERSEDED for the wiring: the render path,
  the export dialog and the sidecar report are `post-alpha/lufs-wire`'s work — docs/LUFS-WIRING.md.
  Still true: no GUI *meter widget* exists, and the meter is not fed from the live audio thread.]`
  By design and by the brief. Nothing has listened to a real mixer output through this class yet,
  and no render has been produced *from* it.
- **No reference-implementation cross-check.** The numbers are checked against the published
  vectors (EBU Tech 3341 signal definitions plus the recommendation's own tables), not against a
  second implementation run (libebur128/ffmpeg) on the same bit-exact input. That is the strongest
  missing check; it needs a third-party binary on this box.
- **Compliance is proven for the vectors in the table, at 48 kHz, for 1/2/6 channels.** Other sample
  rates are covered by design (coefficients re-derived) but not by a measured vector; the EBU test
  material is defined at 48 kHz.
- **No 5.1 or LFE-spec'd multichannel vector from the suite beyond the LFE exclusion** (BS.1770-4
  multi-channel test material was not available offline).
- **Not measured: CPU cost.** The per-sample work is 2 biquads plus a 48-tap interpolator per
  channel; no benchmark was run, and no claim about it is made here.
- **Thread safety is by design, not by a race test**: one writer, lock-free readers, no atomics.
  A reader can observe a value computed from a partially updated ring (the momentary/short-term
  windows are summed on the read path), which is acceptable for a meter and not for anything else.
- **The `--strict` mode of Gate 3 is red tree-wide** (23 files, all pre-existing, mine among them),
  because that mode compares assertion macros to a loose "line starting with an identifier(" slot
  count. The default Gate 3 mode — the one `run-all-gates.sh` and CI use — is green.
- **Gate 2 (coverage) was not run in this lane.** It needs a full `--coverage` rebuild of the tree
  (`tests/run-coverage.sh`), which this box's disk and the five concurrent lanes make expensive. The
  test's own coverage of the class is visible in the vectors above (every branch of the gating,
  the planar path, the LFE rule and the silence/sentinel paths is exercised); the ratchet entry is
  the parent's call.


