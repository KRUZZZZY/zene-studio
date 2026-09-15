# The golden-audio integration programme — the release's audio evidence

**What it is.** The verification programme `V0.3-ALPHA-PLAN.md` names (the other being the
real-time-safety one), landed as a registered ctest with a committed baseline. It answers one
question with a number instead of an opinion: *did this change alter what the engine
renders?* — and it answers it in the only form this tree's renderer allows.

**Why it is built the way it is.** Renders here are **not bit-reproducible run to run**
(`docs/RENDER-DETERMINISM.md`): before the `ProjectRenderer` fix, 6 of the 9 projects that
lane swept rendered to different bytes on every run of the *same* binary, and 2 still do. So:

* `sha256(a) == sha256(b)` is **not** a test — it fails on a correct build;
* a single before/after number (dBFS, RMS, LUFS) is **not** a test either — the recorded
  non-determinism moves no level (RENDER-DETERMINISM.md section 4), so a level-only check is
  blind to it, while a change a build really made can sit below one whole-file number;
* the test is **max |delta| in LSB and dBFS against the same-build run-to-run FLOOR**, with
  the floor **measured** per fixture and **recorded** (`docs/AUTO-MASTERING.md:251` states the
  rule; `docs/RACKS.md`, `docs/WARP.md` and `docs/STEM-EXPORT.md` each did it ad hoc, which is
  why it is one registered programme now).

## 1. The method

**The floor.** Render the same fixture N times in the same build into its own temp directory
and compare **every pair** — C(N,2) pairs, not one before/after pair — and take the worst
value of each term. `--write-record` measures it with N=5 (10 pairs); the ctest re-measures
with N=3 (3 pairs) and judges what it measured against what is recorded.

**The terms.** Every comparison is multi-term, because no single number answers the question:

| term | unit | what it catches |
| --- | --- | --- |
| max abs delta | LSB and dBFS | any sample-level change at all (1 LSB counts) |
| differing frames / samples | count | how widespread the change is, and where it starts |
| delta RMS | dBFS | the energy of the difference signal |
| level delta | dB | whether the loudness moved (the one term the recorded jitter cannot move) |
| envelope delta | dB | the worst per-window (`>= 50 ms`, `<= 128` windows) level difference |
| peak envelope delta | LSB | a shape change that preserves a window's energy |

**The tolerance model.** Each term's tolerance is **twice that term's own measured floor**,
floored at a stated resolution (1 LSB of a 16-bit render, 0.01 dB):

```
tol[lsb]         = max(2 * floor.max_delta_lsb,      1 LSB)
tol[db]          = max(2 * |floor.level_delta_db|,   0.01 dB)
tol[envelope_db] = max(2 * floor.envelope.max_delta_db, 0.01 dB)
```

The floor is the measurement; the **x2 is a declared policy choice**, not a measurement: with
N runs the observed spread is a left-censored estimate of the distribution's tail, and the
margin covers it. A verdict FAILS when *any* term exceeds its own tolerance.

**The golden term.** Committed renders are refused by `tests/evidence-gate.sh` (a WAV beside a
test is a run's output), so the golden reference this programme ships is a **measurement**:
`tests/golden-audio-record.tsv` carries, per (fixture, path), the measured floor, the golden
render's per-window RMS and peak envelopes, the whole-file numbers and the build's sha256.
A later build's render is compared against that record at the record's own tolerance.

## 2. The fixtures

| fixture | what it is | paths |
| --- | --- | --- |
| `socket-1track-2clips` | built through the commands an agent has: one instrument track (TripleOscillator) on the shipped default template, two one-bar clips, one note each | `render.render`, `render.stems`, `bounce.in_place` |
| `bundled-Root84-TrancyLoop` | `data/projects/shorties/Root84-TrancyLoop.mmpz` — the bundled project `docs/RENDER-DETERMINISM.md` records as **still not bit-reproducible** | `render.render` |

Two fixtures on purpose: a tolerance model demonstrated only on a bit-reproducible fixture has
not been demonstrated. The second one is where the recorded non-determinism actually lives.

## 3. The measured floors (2026-09-15, `--write-record --runs 5`)

Build: `build/zene`, RelWithDebInfo, Qt6, `-DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON`
(the `linux-x86_64` job's `CMAKE_OPTS` minus `-DUSE_WERROR=ON`, minus the VST3/CLAP hosting
flags — no fixture here loads a hosted plugin), binary sha256
`fd10f160212777089ce7c22ebc7cf739cdae7ac0bef5598ec94f3e8ac879f9a8`, 20-core Linux box,
one render per process.

| fixture | path | floor max abs delta | frames differing | level delta | envelope delta | byte-identical pairs |
| --- | --- | --- | --- | --- | --- | --- |
| `socket-1track-2clips` | `render.render` | **0.000 LSB** | 0 | 0.000000 dB | 0.000000 dB | **10 of 10** |
| `socket-1track-2clips` | `render.stems` | **0.000 LSB** | 0 | 0.000000 dB | 0.000000 dB | **10 of 10** |
| `socket-1track-2clips` | `bounce.in_place` | **0.000 LSB** | 0 | 0.000000 dB | 0.000000 dB | **10 of 10** |
| `bundled-Root84-TrancyLoop` | `render.render` | **13 275 LSB** (−7.85 dBFS) | **461 812 of 477 440 (96.7 %)** | 0.002253 dB | 1.564567 dB | **0 of 10** |

Read the two halves together: **three of the four measured paths are bit-reproducible on this
build** (the `ProjectRenderer` fix holds for a socket-built session), and the bundled project
that is recorded as non-reproducible still is — 96.7 % of its frames differ, up to 40.5 % of
full scale (13 275 LSB), while its loudness moves by 0.0023 dB. That last figure is the whole argument for
this programme: a level check would call those renders identical.

The verify run re-measures the floors (`--runs 3`, 3 pairs) and requires each to be within
**twice the recorded floor**:

```
  floor vs record      : measured 0.000 LSB (-inf dBFS), recorded 0.000000 LSB on build fd10f16021277708
  floor vs record      : measured 15529.000 LSB (-6.49 dBFS), recorded 13275.000000 LSB on build fd10f16021277708
```

## 4. The registered proof

`tests/CMakeLists.txt` registers two ctests:

* `GoldenAudioSelfTest` (`tests/golden_audio_selftest.py`) — the **instrument's own control**,
  on synthesised WAVs whose difference is known exactly: two identical files, one sample moved
  by one LSB, a stated −0.5 dB gain, two silent files, a three-render floor, stereo
  frame-versus-sample counting. No binary, no socket; runs in 0.30 s on every platform.
* `ControlGoldenAudio` (`tests/control-golden-audio.py`) — the programme itself, driven end to
  end over `--control-socket` against the real binary: builds the fixtures, renders, measures,
  judges against the record, applies the negative control and runs the bound sweep.

```
1/2 Test #133: GoldenAudioSelfTest ..............   Passed    0.30 sec
2/2 Test #160: ControlGoldenAudio ...............   Passed   23.43 sec
100% tests passed, 0 tests failed out of 2
```

The self-test's own control (15 checks, exit 0) includes the one that matters here:

```
  PASS THE NEGATIVE CONTROL: a deliberate -0.5 dB gain FAILS the comparison   tolerance 1.000 LSB, measured 917 LSB
  PASS ... and a 1-LSB change is NOT distinguished (the resolution floor)     tolerance 1.000 LSB, measured 1.000 LSB
  PASS three renders 3 LSB apart measure a 3 LSB floor                        3.000 LSB over 3 pairs
  PASS ... so the tolerance is twice it (6 LSB): the model, not a magic number 6.000 LSB
  PASS a change below the floor is NOT caught - the programme's stated bound
```

## 5. The negative control — the comparison is PROVED able to fail

A comparison that cannot fail is not evidence. In the socket-driven run, `mixer.set_volume`
moves a fader by a **stated** amount — a deliberate gain change made by the product, not by
the harness — and the re-render must FAIL. It does, on all three headline paths:

```
=== socket-1track-2clips / render: NEGATIVE CONTROL, fader ch-1 1.000000 -> 0.944061 (-0.5000 dB) ===
  measured (render.wav)        : 1622.000 LSB max |delta|, 151690 differing frames, level +0.500013 dB, envelope 0.500024 dB
    max |delta|     +1622.000 LSB            limit 1            FAIL
    max |delta|     -26.11 dBFS              limit -90.309      FAIL
    level delta     +0.500013 dB             limit 0.01         FAIL
    envelope delta  0.500024 dB              limit 0.01         FAIL
```

and on the fixture that jitters, where the sample term alone could NOT have caught it — the
level term does, which is what the multi-term design is for:

```
=== bundled-Root84-TrancyLoop / render: NEGATIVE CONTROL, fader ch-1 1.000000 -> 0.944061 (-0.5000 dB) ===
  measured (render.wav)        : 15114.000 LSB max |delta|, 477145 differing frames, level +0.494061 dB, envelope 2.194825 dB
    max |delta|     +15114.000 LSB           limit 31058        ok
    max |delta|     -6.72 dBFS               limit -0.465529    ok
    level delta     +0.494061 dB             limit 0.01         FAIL
    envelope delta  2.194825 dB              limit 3.72814      ok
```

The record's own golden term fails the same control render on both fixtures, so the baseline
comparison has teeth too, not just the floor-derived one.

## 6. THE BOUND — what this programme cannot distinguish (measured, not asserted)

The bound sweep moves the same fader by a range of stated dB values and reports, for each,
whether the verdict caught it. This is the honest limit of the programme's claim.

`socket-1track-2clips` / `render.render` — tolerance 1.000 LSB / 0.01 dB:

```
  delta dB    max |delta|   level dB       verdict
  -0.0001     1.000         0.000101       NOT caught
  -0.0010     4.000         0.001000       caught
  -0.0100     34.000        0.010001       caught
  -0.1000     332.000       0.100003       caught
  -0.5000     1622.000      0.500013       caught
  -2.0000     5964.000      2.000062       caught
```

`bundled-Root84-TrancyLoop` / `render.render` — tolerance 31 058 LSB / 0.01 dB (twice the floor
that run measured, 15 529 LSB):

```
  delta dB    max |delta|   level dB       verdict
  -0.0001     13589.000     0.004300       NOT caught
  -0.0010     14207.000     -0.000043      NOT caught
  -0.0100     14118.000     0.008875       NOT caught
  -0.1000     14208.000     0.100349       caught
  -0.5000     14945.000     0.492114       caught
  -2.0000     15588.000     1.992888       caught
  caught            : -0.1000 dB (14208 LSB), -0.5000 dB (14945 LSB), -2.0000 dB (15588 LSB)
  NOT distinguished : -0.0001 dB, -0.0010 dB, -0.0100 dB
```

**The bound, stated plainly:**

1. **Below one LSB nothing is distinguishable, by construction.** A 16-bit render cannot carry
   a smaller difference than one LSB (−90.31 dBFS), so a change that alters no sample value is
   invisible to every term. On the reproducible fixture the smallest caught change is
   **−0.001 dB** (4 LSB of sample delta); −0.0001 dB (1 LSB) is not caught and is not
   distinguishable from the floor.
2. **On a fixture whose floor is large, the sample term stops discriminating.** 96.7 % of the
   bundled fixture's frames differ between two runs of the same build, so its sample tolerance
   is 26 550-31 058 LSB — 81-95 % of full scale, so that term alone can only catch a
   near-full-scale difference. A gain change of ±0.01 dB there moves samples *less*
   than the jitter: it is caught only by the **level** term, at 0.01 dB, which is exactly why
   the recorded jitter ("loudness never moves") does not disable the programme. The measured
   bound on that fixture is **−0.1 dB**.
3. **It is not a cross-build, cross-machine or cross-optimisation promise.** The floor is a
   property of *this build on this box*. A different compiler, CPU or `TARGET_UARCH` changes
   the last bits; the comparison is meaningful within a build, and across builds only at the
   tolerance the record's floor supports.
4. **It cannot tell you *which* change moved the render.** It answers "this render moved
   beyond the floor", with the terms and the first differing frame; attributing the difference
   to a commit is the reader's work (RENDER-DETERMINISM.md's `tools/render-determinism-compare.py`
   is the tool for that half).
5. **It says nothing about a module that is itself non-deterministic.** A fixture whose floor
   is set by an instrument's own randomness can be "within its floor" while the audio is
   genuinely different run to run; the floor, not the verdict, is what that case tells you
   (the same wording `docs/KNOWN-LIMITATIONS.md` already uses for the WASM render verdict).
6. **The golden term is a measurement, not bytes.** It compares level, envelope and peak
   envelopes. A change confined inside one window's RMS, or one that preserves energy while
   changing shape (other than at a window peak), is not visible to the golden term — only to a
   sample-level comparison against a *second render*, which the floor phase performs on every
   run.

## 7. Re-measuring, re-recording, and how not to cheat

```bash
# the deep floor measurement + the committed record (5 runs per path, 10 pairs)
QT_QPA_PLATFORM=offscreen python3 tests/control-golden-audio.py build/zene --write-record --runs 5
# the ctest's own path: 3 runs per path, judged against the record
QT_QPA_PLATFORM=offscreen python3 tests/control-golden-audio.py build/zene --runs 3
# the instrument alone (no build, no socket)
python3 tests/golden_audio_selftest.py
# re-derive any number in the record by hand
python3 tests/golden_audio_record.py compare a.wav b.wav
python3 tests/golden_audio_record.py floor a.wav b.wav c.wav
python3 tests/golden_audio_record.py record
```

**A lane that rewrites `tests/golden-audio-record.tsv` to make itself green has disabled this
programme.** The record IS the baseline; a floor moved to fit a result is a deleted test. The
file says so in its own header, for the next reader.

## 8. What is not claimed

* The bound above is not a general statement about audibility; it is what *this* programme,
  with *this* tolerance model, distinguishes on *these* fixtures.
* Only the 16-bit WAV path is measured (the export presets can change bit depth and rate; the
  measurement core reads PCM 8/16/24/32 and IEEE float 32/64, but no fixture here uses them).
* `render.stems` on the sound fixture exports every unmuted track of the session; the
  programme measures **the fixture's own stem** (the `<index>_<name>.wav` carrying the fixture
  track's name — the contract `tests/control-stem-export-verb.py` reads), not the whole set.
* The fixture's fader is the **master** channel: a headless instance's mixer holds only the
  master (the `MixerView` is what creates the rest), so a track added over the socket sums into
  it. The control's own check — the deliberate gain change is CAUGHT — is what proves the fader
  is in the fixture's path, and it is re-proved on every run.
* **UI absence — one line: this programme is a test, not a surface.** It registers **no**
  command group (no `golden.*` id exists), nothing under `src/gui/` reaches it, and its
  release evidence is the two ctests above plus the committed record. It is not drivable from
  the interface because it is not part of the product's surface at all.
