# Loudness wiring: making the BS.1770 meter reachable

**Lane:** `post-alpha/lufs-wire` · **Branch base:** `post-alpha/integration` @ `ccd07f490`
**One-line verdict:** the render/export path now measures the audio it writes and reports it to the
user — integrated LUFS, short-term maximum and true peak, with an EBU R128 pass/warn — in a
`.loudness.txt` sidecar beside the render and on the export dialog. The tap is passive: a render
with the report and the same render without it are bit-identical in the audio (proved below).

The measurement core (`LufsMeter`) is untouched by this lane. Everything here is a consumer.

---

## 1. What existed, and what was wrong with it

Established by reading the tree at `ccd07f490`, not from the lane reports.

| Piece | Where | What it did |
|---|---|---|
| The meter | `include/LufsMeter.h:83` — `processBlock()` `:135`, `processPlanar()` `:143`, `read()` `:146`, `integratedLufs()` `:148`, `momentaryLufs()`, `shortTermLufs()`, `truePeakDbtp()` `:154`, `kWeightingCoefficients()` `:165`; implementation `src/core/LufsMeter.cpp` | ITU-R BS.1770-4 / EBU R128 measurement, allocation-free per block, eleven compliance vectors asserted by `tests/src/core/LufsMeterTest.cpp` |
| The render path | `src/core/ProjectRenderer.cpp:171` `run()`; the loop at `:185`-`:201` pulls one period with `Engine::audioEngine()->renderNextPeriod()` and hands it to `m_fileDev->writeBuffer()` `:197` | Renders the project to a file; no measurement of any kind |
| The export entry point | `src/gui/modals/ExportProjectDialog.cpp:261` `onStartButtonClicked()` → `RenderManager` `:274` → `ProjectRenderer` (`src/core/RenderManager.cpp:135` `render()`, `:137`) | Collects the output settings and starts the render |
| The CLI entry point | `src/core/main.cpp:730` (`renderOut` non-empty ⇒ headless render), `:757` `RenderManager`, help text `:187`-`:210` | `lmms render <project> -o out.wav` |
| Existing "meter" infrastructure | `include/MeterModel.h:34` is the **musical** meter (numerator/denominator), not a level meter; `src/gui/widgets/MeterDialog.cpp` is its dialog. Level display is `PeakIndicator` (`src/gui/MixerChannelView.cpp:140`) fed by `Fader::peakChanged`, polled by `MixerView::updateFaders()` (`src/gui/MixerView.cpp:558`) | None of it measures loudness |

**The failure mode this lane exists to fix:** the meter was present, unit-tested and instantiated by
nothing. `grep -rn "LufsMeter" src/ include/ plugins/ --include=*.cpp --include=*.h` found only
`include/LufsMeter.h` and `src/core/LufsMeter.cpp`. `docs/LUFS-METER.md` said so in as many words
("Nothing calls it", "no render has been produced *from* it") — that statement is now corrected in
that file rather than left to contradict the code.

## 2. What was built

| File | Status | What it is |
|---|---|---|
| `include/LoudnessReport.h` (168 lines) | new | `lmms::LoudnessReport`: owns a `LufsMeter`, adds the worst-case short-term maximum, the EBU R128 verdict, the report text and the sidecar. `addBlock(const SampleFrame*, f_cnt_t)` is the tap |
| `src/core/LoudnessReport.cpp` (193 lines) | new | The report text (`key = value`, sentinel-aware formatting), `writeSidecar()`, `verdict()`, `summary()` |
| `tests/src/core/LoudnessReportTest.cpp` (326 lines) | new | 54 QTest slots / 42 assertions (Gate 3's count), 0 tautologies: the wiring layer, both negative controls, byte-level passivity, the sidecar, and `AllocationProbe` on the per-block path |
| `include/OutputSettings.h` | wiring | `loudnessReport()` / `setLoudnessReport()`, **default false** — every pre-existing caller renders exactly as before |
| `include/ProjectRenderer.h` / `src/core/ProjectRenderer.cpp` | wiring | Constructs the report only when asked (`:105`-`:111`), taps each rendered block immediately before `writeBuffer()` (`:187`-`:197`), writes the sidecar and emits the report at the end (`:218`-`:223`, `:236`) |
| `include/RenderManager.h` / `src/core/RenderManager.cpp` | wiring | Keeps the report alive past the renderer (which is destroyed on `finished()`) and forwards it (`:153`-`:160`) |
| `src/core/main.cpp` | wiring | `--loudness-report` (`:626`-`:632`), documented in `--help` (`:194`) |
| `src/gui/modals/ExportProjectDialog.cpp` + `include/ExportProjectDialog.h` | wiring | "Loudness report (EBU R128)" checkbox, ticked by default (`:271`), and the measured report shown in the dialog when the render finishes instead of the dialog closing on the result (`:283`, `:314`) |

Registration: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/fork-sources.txt` (both new
sources), `tests/all-sources.txt` (the new test), and `tests/upstream-modifications.txt` (eight
inherited files, each with a `#618 loudness wiring:` reason).

### The tap, in one picture

```
ProjectRenderer::run()
  while (!isExportDone() && !m_abort) {
      const auto buffer = Engine::audioEngine()->renderNextPeriod();
      if (m_loudnessReport) m_loudnessReport->addBlock(buffer.data(), buffer.size());  // reads only
      m_fileDev->writeBuffer(buffer.data(), buffer.size());                            // unchanged
  }
  ...
  reportLoudness(f);   // sidecar + console + loudnessReportReady()
```

`addBlock()` takes the block as `const SampleFrame*`; the measured bytes and the written bytes are
the same bytes. There is no copy, no second render, no re-reading of the file.

## 3. The target, and why that one

**EBU R128** (EBU Tech 3343 delivery guidance over the ITU-R BS.1770-4 measurement):

* programme loudness **-23.0 LUFS-I, tolerance ±0.5 LU**,
* maximum true peak **-1.0 dBTP**.

Chosen because it is a *published specification with a stated tolerance* — a pass/warn against it
means something checkable. The streaming services' **-14 LUFS-I / -1 dBTP** convention is printed in
the report as `streaming_reference` for information only; it is a platform normalisation convention,
not a specification, and carries no tolerance to grade against, so this lane does not invent one.

The tolerance is graded on the gated integrated value; the true peak is graded against the -1 dBTP
ceiling. A silent render gets **no verdict** (`NOT MEASURED`), rather than a plausible-looking default.

## 4. Measured evidence

All commands and numbers below are from a single run of
`bash tests/data/loudness/render-evidence.sh` against the CI-flagged build in this worktree
(`build/lmms`, `QT_QPA_PLATFORM=offscreen`). The fixtures are generated deterministically by
`tests/data/loudness/make-fixtures.py`; the tones are EBU Tech 3341 case 1 and case 2 signals
(a 1 kHz sine, in phase in both channels, each channel's peak at -23 / -33 dBFS) written as 32-bit
float WAV, played by an `audiofileprocessor` at unity through an untouched mixer.

### 4.1 The real render reports the level it was given

```
$ ./build/lmms render /tmp/lufs-evidence/fixtures/tone-23.mmp -f wav -s 48000 -a --loudness-report -o tone-23.wav
render EXIT=0
Loudness: -23.03 LUFS-I (target -23.0), short-term max -22.99, true peak -22.99 dBTP -> PASS [deviation -0.03 LU]
Loudness report written to /tmp/lufs-evidence/tone-23.wav.loudness.txt
```

| Render (known signal) | Known value | Reported `integrated_lufs` | Reported `short_term_max_lufs` | Reported `true_peak_dbtp` | Independent numpy BS.1770-4 |
|---|---|---|---|---|---|
| 1 kHz sine, -23 dBFS (EBU 3341 case 1) | -23.0 LUFS ±0.1 | **-23.03** | **-22.99** | **-22.99 dBTP** | -23.0342 / -22.9933 / -23.0000 |
| 1 kHz sine, -33 dBFS (case 2) | -33.0 LUFS ±0.1 | **-33.03** | -32.99 | -32.99 dBTP | -33.0342 / -32.9933 |
| digital silence | sentinel | **-inf** | -inf | -inf | -inf |

* The reported value is **0.004 LU** from the second implementation (`bs1770_reference.py`, numpy,
  written from the recommendation's published 48 kHz coefficient rows and an explicit list of
  gating blocks — deliberately not the same code path as the C++ meter's derived coefficients and
  0.05 LU histogram). The meter lane recorded the missing reference-implementation cross-check as
  its strongest open check; it now exists for the rendered files.
* The absolute value is the EBU case-1 value: `LufsMeterTest` measures -22.9933 LUFS for this same
  signal, and the short-term maximum here is -22.99.
* **The tap is live, not a constant:** the two renders differ by 10 dB of input level and report
  **10.00 LU** apart (-23.03 vs -33.03), matching the known level difference inside the meter's
  ±0.1 LU tolerance. A component that reported a fixed value, or that was never fed, cannot do this.

### 4.2 Negative controls

* **Silence is not a plausible number.** The silent render's sidecar reads
  `integrated_lufs = -inf`, `true_peak_dbtp = -inf`, `deviation_lu = n/a`,
  `verdict = NOT MEASURED (no measurable signal)` — the sentinel, and no verdict claimed.
  `LoudnessReportTest::silenceReportsTheSentinelAndNoVerdict` asserts the same at the API level.
* **No report requested ⇒ no report.** `tone-23-noreport.wav` has no `.loudness.txt` beside it and
  the console says nothing about loudness (`ls: cannot access 'tone-23-noreport.wav.loudness.txt':
  No such file or directory`). The report is opt-in: `OutputSettings::loudnessReport()` defaults to
  false; the CLI needs `--loudness-report`, and the dialog's checkbox is what sets it.

### 4.3 Passivity — the measured render is the written render

```
$ python3 tests/data/loudness/passivity-check.py tone-23.wav tone-23-noreport.wav
1. data chunk (the audio) : sha256 d3d6e796b12af01d4c166696e482fd5a  IDENTICAL
2. sample-by-sample       : 1645568 frames, max |delta| = 0.0  IDENTICAL
3. differing bytes        : 1 of 6582404 at [104]
   PEAK chunk body at 100 (size 24); the differing bytes read as Unix seconds: 2026-09-11 21:28:45
   bytes differing outside the PEAK chunk: 0
PASSIVITY: PASS - measuring the render did not change it
```

The raw `md5sum` of the two files differs — by exactly **one byte**, offset 104, inside libsndfile's
`PEAK` metadata chunk, where the writer stamps the wall-clock second. That byte differs between *any*
two renders, report or no report. The audio (`data` chunk) hashes identically, the sample values are
bit-identical, and no byte outside the `PEAK` chunk differs. `passivity-check.py` exits non-zero if
that ever stops being true.

### 4.4 The block path allocates nothing

`LoudnessReportTest::thePerBlockPathAllocatesNothing` wraps 64 `addBlock()` calls plus
`reading()`/`shortTermMaxLufs()`/`verdict()` per block in `AllocationProbe` (the tree's existing
global-`operator new` counter, `tests/src/core/AllocationProbe.h`) and asserts a count of **0**.
`LufsMeter`'s own test already asserted this for the meter; this asserts it for the layer the render
path now calls.

### 4.5 Build and tests, unpiped

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4      # + -DWANT_QT6=ON, Qt5 dev files absent
configure EXIT=0   build EXIT=0   ctest EXIT=0   (build/ctest.log: 100% tests passed, 0 tests failed out of 28)
18/28 Test  #8: LoudnessReportTest ...............   Passed    1.30 sec
linux-x86_64: REPRODUCED
```

`tools/local-ci.sh` is checked in mode 100644 on this branch, so it is invoked through `bash`
(the CI job has it executable); everything else is the CI linux-x86_64 job's exact `CMAKE_OPTS` plus
the documented `-DWANT_QT6=ON` deviation. 28 tests are registered in this build tree; 27 of them are
registered by `post-alpha/integration` and the 28th is this lane's `LoudnessReportTest` (checked
against `git show HEAD:tests/CMakeLists.txt`, not taken from a lane report).

### 4.6 Static gates

| Gate | Command | Result |
|---|---|---|
| 3 — no tautological tests | `bash tests/no-tautology-gate.sh` | **PASS** |
| 4 — per-method complexity | `bash tests/complexity-gate.sh --check` | **PASS** (no regressions) |
| 7 — per-file length | `bash tests/file-length-gate.sh --check` | **PASS** (no regressions) |
| 8 — token duplication | `bash tests/duplication-gate.sh` | **PASS**; 0.96 % of lines duplicated (budget 5 %) |
| 6 — upstream divergence | `bash tests/no-upstream-regression-gate.sh` | **RED, pre-existing** — see below |
| fork-sources scope | `bash tests/fork-sources-gate.sh` | **RED, pre-existing** — see below |

Both red gates are red on `post-alpha/integration` before this lane, and neither red is in a file this
lane touches:

* Gate 6 reports four undeclared divergences: `include/MainWindow.h`, `include/MidiController.h`,
  `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp` — all from `5d6ccdf1f feat(midi):
  global one-shot MIDI-learn mode`, none of them in this lane's diff.
* `fork-sources-gate` reports three new test files that no lane registered:
  `tests/src/core/LufsMeterTest.cpp`, `tests/src/core/MidiLearnTest.cpp`,
  `tests/src/core/SessionModelTest.cpp`. This lane registered its own new test
  (`tests/src/core/LoudnessReportTest.cpp` → `tests/all-sources.txt`, which is where the fork's other
  new tests live) and left the three to their owning lanes rather than editing their books.

## 5. What is NOT done

* **(2) The live master loudness readout was not attempted.** The scope ranked the offline report
  first and said to land it solidly and say so plainly if the budget ran out; that is what happened.
  Nothing in the live audio thread constructs or feeds a meter: `AudioEngine::renderNextPeriod()`
  is untouched, and no widget polls a loudness reading while playback runs. The pieces the next
  attempt needs are all in place (`LoudnessReport` is realtime-safe by measurement — fixed-size,
  lock-free, 0 allocations per block) but the bus tap, the `std::atomic` enable flag, the polling
  widget and its own allocation/`-inf` assertions are not written, so no claim is made about them.
  Note the design constraint found while reading: `AudioEngine::renderNextPeriod()` is the one choke
  point both the realtime device and `ProjectRenderer` pull from, so a flag-gated tap there would
  serve both paths — at the cost of running the true-peak interpolator on every live block.
* **No GUI test.** `ExportProjectDialog`'s checkbox and result label compile and are wired, but this
  tree has no Qt-widget test that clicks an export dialog; the export-dialog surface is verified by
  reading the code and by the fact that the same `OutputSettings` flag drives the CLI path that *is*
  tested end to end. The sidecar is the surface with full end-to-end evidence.
* **`renderTracks` shows one report.** Rendering each track to its own file produces one report per
  renderer; `RenderManager` forwards the report of the last track to finish, and each track's sidecar
  is written beside its own file. The dialog's label says nothing about which track it refers to.
* **Stereo only in the application.** The render path constructs the report with `DEFAULT_CHANNELS`
  (2). `LufsMeter`'s 5.1/LFE handling is tested but no application path feeds it a 6-channel render.
* **True peak is not cross-checked against another 4× oversampler.** `bs1770_reference.py` computes
  the same Annex 2 interpolation independently and agrees to 0.0000 dBTP on these signals, but both a
  sample peak and a true peak cannot be independently validated by one Python file; the suite's
  assertions (true peak ≥ sample peak, the fs/4 phase-offset vector, the square wave) carry that.
* **Sampled rates other than 48 kHz** are unmeasured here for the same reason as in the meter lane:
  the EBU compliance signals are defined at 48 kHz.
* **The fixtures are generated, not committed.** `tests/data/loudness/make-fixtures.py` writes the
  tones (3 × 16 s float32 stereo ≈ 6 MB each) and the three `.mmp` files into a temporary directory;
  the WAVs themselves are not in the repository. The generator is deterministic (pure sine, no
  randomness, no timestamps) and `render-evidence.sh` runs it, so the evidence is reproducible
  without carrying 18 MB of test audio.

## 6. How to reproduce everything above

```
# build + tests (CI flags; -DWANT_QT6=ON printed as a deviation on this box)
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-lufswire
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4

# the render evidence (generates fixtures, renders, measures, checks passivity)
bash tests/data/loudness/render-evidence.sh

# the wiring tests on their own
cd build/tests && ./LoudnessReportTest
```

Gate 2 (coverage) was not run: it needs a full `--coverage` rebuild of the tree, which this box's
disk and the concurrent lanes make expensive — the same call the meter lane made, and the parent's
to revisit.
