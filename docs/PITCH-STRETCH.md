# Pitch-preserving time stretch — what landed, and the numbers that prove it (#652)

Lane: `030/pitch-stretch` (worktree `zene-030/wstretch`), based on `release/0.3.0` @ `f611c888b`.
Feature-list row **30** (section 5), audit §6.1.

**Verdict.** The warp path has a second render mode and it is not resampling in disguise. A 2× warped
clip still lasts half as long, and its **pitch is measured where it was**: on a two-tone fixture
(440 Hz + 660 Hz), through the real clip path, the resampling mode renders `0.0000 / 0.0000 / 0.5000 /
0.3000` at 440 / 660 / 880 / 1320 Hz and the new mode renders `0.4992 / 0.2990 / 0.0001 / 0.0001` —
the tones stay put and no energy appears an octave up. `docs/WARP.md` §3's "the stretch is resampling,
so it changes pitch" is now a statement about the *default* mode, not about the engine.

The mechanism is WSOLA (`AudioStretcher`, a new class), the selection is a clip field with a command
(`warp.stretch`), and the proof is two registered ctests that measure the pitch rather than assert it.

---

## 1. What the DSP does, and why it is not resampling

`include/AudioStretcher.h`, `src/core/AudioStretcher.cpp` (new, ~300 lines together).

A resampler has one degree of freedom: it reads the source faster or slower, so the waveform's own
period — the pitch — moves with the rate, and that is exactly the trade `AudioResampler` makes today.
WSOLA decouples the two:

1. the output is built from **grains** of `grainFrames` (default 1024, 23 ms at 44.1 kHz), mixed in at
   50 % overlap with a periodic Hann window — which is exactly constant-overlap-add, so a stretch of 1
   rebuilds the waveform rather than modulating it;
2. a hop advances the **output** by the synthesis hop (`grain/2`) and the **source** by
   `synthesisHop × speed`, so the two rates are independent — this is what makes the clip last as long
   as the mapping says;
3. before a grain is mixed in, its start is searched over ±`searchRadius` source frames (default 128)
   for the offset whose first half best continues what has already been written
   (`Σ(candidate × carry) / √Σ(candidate²)`), because two grains out of phase by half a period cancel
   instead of adding.

Step 3 is the whole difference, and it is measured rather than argued: with `searchRadius = 0` the same
class is a plain overlap-add, and on this fixture it **cancels the tones almost to silence**
(0.0285 of the input's 0.5 at 440 Hz) because the grains land out of phase. With the search on, the same
signal comes back at 0.4992. A resampler has nothing to align to; that is why its pitch has to move.

Two details that matter for the render path:

* the analysis cursor is a `double`, so a fractional hop (a stretch that is not an integer ratio, or a
  source whose rate differs from the project's) carries its fraction instead of rounding it away every
  hop — reads between frames are linearly interpolated, the same converter class as the engine's
  default `Mode::Linear`;
* everything is a fixed-size member array sized once in `prepare()`. `process()` allocates nothing,
  locks nothing, grows nothing — measured below — so it runs on the audio thread.

---

## 2. How a clip selects it

| Where | What |
|---|---|
| `include/SampleClip.h` | `enum class WarpStretchMode { Resample = 0, PreservePitch = 1 }`, the `m_stretchMode` member (**default `Resample`**) and `warpStretchMode()` / `setWarpStretchMode()` |
| `src/core/SampleClip.cpp` | the mode is the `stretch="wsola"` attribute of the **same `<warp>` element** #597 already owns; written only when it is not the default, so a clip that never asks serialises byte for byte as before |
| `include/SamplePlayHandle.h`, `src/core/SamplePlayHandle.cpp` | the handle snapshots the mode (`m_preservePitch`) with the window and the warp, and renders through `renderPreservingPitch()` when the mode is `PreservePitch` **and** the clip is not `rendersLinearly()` |

Three consequences, all deliberate:

* **The default is unchanged.** Every project written before this feature renders through the resampler
  with a ratio of exactly 1.0 in the new code path's terms, i.e. as it always did. There is no new
  `<warp>` element, no new attribute and no `UPGRADE_METHODS` entry for a clip that does not ask.
* **A clip with no rate change is never routed through the stretcher.** `rendersLinearly()` (no markers
  and the default tempo mode) means there is nothing to preserve the pitch across, so the historical
  path stays the historical path even if the mode was somehow set — and the command *refuses* the
  meaningless case rather than promising a pitch the next playback pass would not deliver.
* **The two modes agree on everything except the waveform.** The stretcher consumes
  `1 / (converterRatio × sampleRateRatio × freqRatio)` source frames per output frame — the reciprocal
  of the ratio the resample path hands `AudioResampler` — so both modes put the same source frames on
  the same timeline and produce the same number of output frames. Measured: 44100 rendered frames for a
  2-second source under a 2× warp, in both modes.

---

## 3. The agent surface (command ids and A16)

| id | mutating | args | result | A16 |
|---|---|---|---|---|
| `warp.stretch` | **yes** | `clip` (string, required), `mode` (`"resample"` \| `"preserve_pitch"`, required) | the whole warp state (below) + `previous_mode`, `changed` | `true_inverse` — `ControlReversibilityTable.cpp`; the mechanism is the clip's own journal checkpoint, because the mode is the `stretch` attribute of the clip's `<warp>` element, and the recorded inverse is `warp.stretch` re-issued with the before-state's own mode |
| `warp.list` | no | `clip` | + `stretch`, `stretch_algorithm` (`"wsola"`), `renders_linearly` | `not_mutating` (unchanged row) |

`warp.stretch` is registered in the `warp` group beside `add` / `move` / `remove` / `set`
(`src/core/ControlCommandsWarpEdit.cpp`), headless-safe (empty `requires`), and it **refuses**:

* a `mode` the engine does not know — `invalid_args`;
* `preserve_pitch` on a clip that renders linearly — `refused`, with the reason and the next step
  ("author the warp first, or declare the clip's own source tempo").

Over the socket:

```
{"id":"warp.stretch","args":{"clip":"clip-3","mode":"preserve_pitch"}}
-> {"ok":true,"result":{"clip":"clip-3","stretch":"preserve_pitch","stretch_algorithm":"wsola",
                        "renders_linearly":false,"previous_mode":"resample","changed":true, ...}}
```

and one `control.undo` takes it back (measured in the clip-path test below).

**Not done on this surface, on purpose:** the stretcher's parameters (`grainFrames`, `searchRadius`)
are **not** exposed as command arguments. They are the quality/complexity dial, they default to the
values measured here, and a per-clip parameter pair would need a schema, a persistence field and a
re-render story that this slice does not have.

---

## 4. The proofs, with the numbers

Both tests are registered in `tests/CMakeLists.txt` and both are in `tests/fork-sources.txt`.
Runs below are from this worktree's `build/` (RelWithDebInfo, `-DWANT_QT6=ON`, `-DWANT_VST3=OFF`,
`-DWANT_CLAP=OFF`, `-DWANT_WASM=OFF`, `-DWANT_STEM_SPLIT=OFF`), unpiped.

### 4.1 The DSP, on the same input through both paths — `AudioStretcherTest`

```
$ QT_QPA_PLATFORM=offscreen ./AudioStretcherTest ; echo EXIT=$?      # EXIT=0, 9 passed / 0 failed
STRETCH_EVIDENCE input: 440 / 660 / 880 / 1320      0.5000  0.3000  0.0000  0.0000
STRETCH_EVIDENCE resampled 2x: 440 / 660 / 880 / 1320  0.0000  0.0000  0.5000  0.3000
STRETCH_EVIDENCE stretched 2x: 440 / 660 / 880 / 1320  0.4992  0.2990  0.0001  0.0001
STRETCH_EVIDENCE measured frequency, resampled 2x   879.9900
STRETCH_EVIDENCE measured frequency, stretched 2x   439.9536
STRETCH_EVIDENCE rms input / resampled / stretched  0.4123  0.4123  0.4123
STRETCH_EVIDENCE aligned: 440 / 660                  0.4992  0.2990
STRETCH_EVIDENCE overlap-add with no search: 440 / 660  0.0285  0.0060
STRETCH_EVIDENCE allocations in 2000 x 512-frame render calls   0
STRETCH_EVIDENCE searchRadius / render ms / x-realtime   32  15.0973  66.2370
STRETCH_EVIDENCE searchRadius / render ms / x-realtime   64  29.6770  33.6961
STRETCH_EVIDENCE searchRadius / render ms / x-realtime  128  58.7295  17.0272
```

* **Two independent measurements of the pitch.** The amplitudes are Goertzel bins (a single-bin DFT,
  no new dependency); the frequencies are zero-crossing periods interpolated between the two samples
  each crossing falls between. The resample control reads **880 Hz**; the stretcher reads **440 Hz**;
  the amplitudes say the same thing from the other side, and the two rows are the *same input*.
* **The level and the length do not move either** — `rms 0.4123 / 0.4123 / 0.4123` and the same frame
  count — so "the pitch did not move" cannot be true because "nothing happened".
* **The search is what does it** (0.4992 with it, 0.0285 without), and that is the row that makes the
  cost claim below a claim about a mechanism rather than about a parameter.
* **Streaming = one call, byte for byte**, at 64/512/1024/4096-frame periods (asserted, 0 differing
  frames each).
* **I8**: 0 allocations across 2000 × 512-frame render calls, with `AllocationProbe`.

### 4.2 The clip path — `SampleClipStretchTest`

The fixture is the same two-tone source, a `SampleTrack` + `SampleClip` whose 2 s are pinned to 96
ticks by two warp markers (a 2× warp), rendered period by period through
`SamplePlayHandle(clip, clip->sampleWindow())` — the constructor `SampleTrack::play` uses.

```
$ QT_QPA_PLATFORM=offscreen ./SampleClipStretchTest ; echo EXIT=$?    # EXIT=0, 7 passed / 0 failed
CLIP_STRETCH_EVIDENCE rendered frames                          44100
CLIP_STRETCH_EVIDENCE resample: 440 / 660 / 880 / 1320   0.0000  0.0000  0.5000  0.3000
CLIP_STRETCH_EVIDENCE preserve: 440 / 660 / 880 / 1320   0.4992  0.2990  0.0001  0.0001
CLIP_STRETCH_EVIDENCE linear clip, both modes: differing frames   0 of 88200
CLIP_STRETCH_EVIDENCE allocations over 16 stretched periods       0
```

| acceptance item | measured |
|---|---|
| the path can select it | `clip->setWarpStretchMode(PreservePitch)` and `warp.stretch` both change what the *next playback pass* renders (same clip, same markers, only the mode differs between the two measurement rows) |
| pitch preserved | 440 Hz and 660 Hz stay at 0.4992 / 0.2990 of their input amplitudes, with 0.0001 at 880 / 1320 |
| distinguished from plain resampling ON THE SAME INPUT | the resample row on the same clip, same markers, measures 0.5000 / 0.3000 **at 880 / 1320** with 0.0000 at 440 / 660 |
| behaviour preserved | a clip with no rate change renders **0 differing frames of 88200** whichever mode it carries |
| persisted | the mode round-trips through the clip's `<warp>` element, and a file with no `stretch` attribute loads as `resample` |
| drivable + reversible | `warp.stretch` reports the state, carries a `true_inverse` contract row, records an inverse that names the command and the previous mode, and `control.undo` restores it |
| realtime | 0 allocations over 16 stretched periods |

### 4.3 ctest, from `build/tests`

```
$ ctest -R "AudioStretcherTest|SampleClipStretchTest" --output-on-failure
1/2 Test   #8: AudioStretcherTest ........   Passed   1.77 sec
2/2 Test #103: SampleClipStretchTest .....   Passed   1.51 sec
```

### 4.4 The quality/complexity trade, stated

The cost of the stretch is the alignment search, and the search is linear in `searchRadius`. Two
measurements, both on this box, and **labelled with the fixture they used** — the render times come
from the registered test (two-tone fixture, 1 s of output audio, 44.1 kHz stereo,
`grainFrames = 1024`, RelWithDebInfo build), and the fixture column says what was measured there:

| `searchRadius` | render time for 1 s of output | × realtime | what the test measured at that setting |
|---|---|---|---|
| 32 | 15.1 ms | 66× | timing only |
| 64 | 29.7 ms | 34× | timing only |
| **128 (default)** | **58.7 ms** | **17×** | the 440 Hz component of the two-tone fixture: **0.4992** of the input's 0.5000, with 0.0001 at 880 Hz |
| 0 (plain overlap-add) | not measured in the test — 0.4 ms in a standalone `-O2` harness | — | the 440 Hz component collapses to **0.0285** of 0.5000 (the tones cancel) |

A second, independent sweep (standalone `-O2` harness, a single 440 Hz sine, RMS relative to the
input's) shows where the reach starts to bite: `searchRadius` 128 → **1.000**, 64 → **1.000**,
32 → **0.971**, 0 → **0.972** (that last row is the same collapse from a different fixture).

Read as a trade: the default costs **58.7 ms of CPU per second of stretched audio — 17× faster than
realtime, i.e. ~6 % of one core while a clip is being stretched** (one clip, one core; the lanes were
building alongside these runs, so treat the absolute times as an order of magnitude, not a benchmark).
Halving the radius halves the cost and halves the alignment reach (±128 frames is 2.9 ms at 44.1 kHz,
i.e. periods down to ~345 Hz); `searchRadius = 0` is a plain overlap-add, 100× cheaper, and it cancels
the tones. The parameters are compile-time defaults because they are not exposed on the surface (§3),
so a caller cannot buy the cheaper row — this table is what a later slice would expose as a dial.

The render path pays this **per clip being stretched**, on the audio thread, and only for a clip that
asked for the mode: a project with no warp, or with warps in the default mode, is unchanged and pays
nothing — one bool test per period.

---

## 5. What is NOT done, and what it costs a user

* **No UI, and no gesture**: nothing in `src/gui/` was touched. A user cannot set the stretch mode from
  the application; the mode is set by editing the project file or through `warp.stretch` on the control
  surface. One line in `docs/KNOWN-LIMITATIONS.md` and in the release notes says so.
* **No formant handling.** WSOLA preserves the waveform's period, not a vowel's formants: a large
  stretch of a voice will sound "chipmunky" in the opposite direction from a resampler — the formants
  move *with* the pitch instead of staying put. Pitch-preserving is not formant-preserving.
* **The alignment reach is ±`searchRadius` source frames** (±128 = 2.9 ms at 44.1 kHz). Content whose
  period is longer than that — **below ~345 Hz** at the default — cannot be aligned and is stretched as
  plain overlap-add. Raising the radius raises both the reach and the cost linearly.
* **Transients are not detected**: a grain straddling an onset smears it over up to one grain
  (23 ms). No transient preservation, no phase-locked transient mode.
* **Sample-rate conversion on this path is linear interpolation** between source frames (the engine's
  default `Linear` class), not the resampler's sinc filters. A source at a rate other than the
  project's is converted inside the stretch lattice; the `SincBest` export quality does **not** reach
  this path.
* **`speed == 1.0` is not a bit-identical copy** (grains are re-windowed and re-aligned). The render
  path therefore bypasses the stretcher entirely for a clip with no rate change, and a caller that
  routes one through it anyway gets a re-windowed render, not the input.
* **One rate per audio period**, inherited from `Sample::play`: the stretch rate is chosen from the
  source frame the period starts on and held for that period, so a rate *change* takes effect at the
  next period boundary (≤ 1024 frames ≈ 23 ms at 44.1 kHz). A two-marker set covering a whole clip —
  what the measurements above use — is exact everywhere, because the rate never changes.
* **The A16/undo story is the clip checkpoint**, not a signature: `control.undo` restores the mode
  through the Clip's journal step, and the recorded inverse (`warp.stretch` with the previous mode) is
  what a reader re-issues by hand. There is no redo half of its own.
* **Not measured here**: how the stretch sounds on real music. The fixture is synthetic (two tones)
  on purpose — it is the input on which "the pitch moved" and "the pitch did not move" are both
  measurable numbers. Nothing in this lane listened to anything.

---

---

## 5b. What is RED on this branch, and what was not run

* **`ControlCommandsSnapshot` is RED, with exactly one finding**: `warp.stretch` is registered by this
  branch's binary (228 live ids) and missing from `tools/mcp-zene-control/zene_control/commands_snapshot.json`
  (227 ids). Measured: `1 command id(s) this binary registers are NOT in the snapshot`. That file is
  regenerated **from a live instance** (`python3 tools/mcp-zene-control/snapshot_commands.py --socket
  <sock>`), never by hand, and every lane of this wave adds commands - so regenerating it here would be
  a merge conflict rather than a fix. **The merge tip regenerates it once.** Nothing else about the
  snapshot drifted: the test's other three comparisons report `0 missing, 0 extra`.
* **`ReversibilityContractTest` was RED and is GREEN after the histogram update** (§4.5 of the ledger:
  row 228, +1 `true_inverse`). It is listed here because the fix is a *count* the test itself asks to be
  updated with the table, and the merge tip must re-count it again.
* **Not run at all**: the coverage, mutation and file-length gates, the full ctest suite (only the
  targets this lane needed were built), and CI. `tests/run-all-gates.sh` was not run.
* **The socket transcript IS GREEN** and committed: `tests/control-pitch-stretch-transcript.py` drives
  the real binary headless through the shared harness and every check held (20 PASS / 0 FAIL, exit 0) —
  including the typed refusal, the switch, the read-back, the A16 record with its re-issuable inverse
  and `control.undo` restoring the previous mode. The raw log is committed as
  `docs/PITCH-STRETCH-TRANSCRIPT.md`.
* **`agent_surface` (the junk-argument sweep over every registered id, `warp.stretch` included) is
  GREEN** on this branch's binary, and so are `ControlWarpCommandsTest` (the pre-existing warp group's
  own contract test, 1.41 s) and `WarpMarkersTest`.

## 6. Reproduction

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wstretch
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON \
  -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF
cmake --build build -j2 --target zene AudioStretcherTest SampleClipStretchTest
cd build/tests
ctest -R "AudioStretcherTest|SampleClipStretchTest" --output-on-failure
QT_QPA_PLATFORM=offscreen ./AudioStretcherTest -v1     # the EVIDENCE lines
QT_QPA_PLATFORM=offscreen ./SampleClipStretchTest -v1
```

Raw logs for this lane: `/home/kruzzzzy/zene-030-wstretch-logs/`.
