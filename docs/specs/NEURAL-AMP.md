<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, NEURAL-AMP.md
    sha256   : f4c747a86beea66be15451f599ca4f1aefcf13679aeb98ac97136f1b0274a1ac
    bytes    : 32784
    why this file: the NeuralAmp design; cited by plugins/NeuralAmp/CMakeLists.txt, its rt_alloc_probe and models/README.md
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# NEURAL-AMP.md — NAM neural-amp plugin (task #577): build, reference match, CPU, audit

Lane: `feat/neural-amp`, worktree `lmms-nam/` (LMMS fork). Date: 2026-09-08/09.
Every number below comes from a run recorded in this session; the command that produced it is
shown with the output. Where something was **not** run, it is listed in §8.

## 0. Headline

| Item | Result |
|---|---|
| Plugin build (Release, Qt6) | **exit 0** in two independent build trees |
| Artifact | `build/plugins/libneuralamp.so` — 312,312 bytes, sha256 `a60c3f06…cd95` (byte-identical in `build-rel/`) |
| Reference match vs upstream | **correlation 1.000000000** (nano: max abs diff 4.97e-07; standard: 2.68e-07), both PASS |
| Release CPU, block 512 @48 kHz | nano **4.60 %** of one core (491 µs/block); standard **19.49 %** (2079 µs/block) |
| Debug CPU (`-O2 -g`, plugin's Debug config) | nano **4.26 %** (454 µs/block); standard **20.15 %** (2149 µs/block) |
| Unoptimised (`-O0`) | nano 63.9 %; standard 403.2 % |
| Upstream reference CPU (same input, `-O3`) | nano 3.06 %; standard 10.26 % (upstream render, block 64) |
| Spec gate T2 (<5 % single core) | nano **PASS**; standard **FAIL** (19.5 %) |
| Stretch gate (<1 % single core) | **FAIL for both — and for the upstream reference too** (see §4.5) |
| Audio-thread allocations | **0** across 512 consecutive blocks, 3 models × 3 block sizes |
| Licences | all components GPL-2.0+-compatible; Eigen MPL-2.0 text vendored; inventory in §6 |
| Commits | `5a76f75ae` (build/tests), `c8641b9b6` (licences), on top of inherited `f41be063e` |

## 1. What this is

Task #577 adds a neural amp (NAM) plugin to LMMS: a WaveNet A1 engine (`.nam` files) implemented
in-plugin under `plugins/NeuralAmp/`, with vendored Eigen/nlohmann-json via RTNeural, four
upstream example models, a headless verification harness and an RT-allocation probe.

The previous lane was killed by a provider billing outage (HTTP 402) after reaching reference
match; its work is committed as `f41be063e` on top of `4e31e46ea`. This lane inherited it,
fixed the build blocker, re-measured CPU in optimised builds, audited the audio path and
completed the licence/provenance record. The reference match itself was **not** re-derived from
scratch — it was **re-verified from a pristine upstream checkout** (§3).

## 2. Build evidence

### 2.1 Two independent trees

`build/` (existing tree, `CMAKE_BUILD_TYPE=Release`, `WANT_QT6=ON`) — forced recompile + relink
after the final edit, exit code captured unpiped:

```
$ touch plugins/NeuralAmp/nam/NamModel.cpp
$ cmake --build build -j4 --target neuralamp
FORCE_PLUGIN_BUILD_EXIT=0
[ 95%] Building CXX object plugins/NeuralAmp/CMakeFiles/neuralamp.dir/nam/NamModel.cpp.o
[ 95%] Linking CXX shared module ../libneuralamp.so
[100%] Built target neuralamp
```

`build-rel/` (configured from scratch on 2026-09-09 with
`cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON`) — full project build,
269 C++ translation units including the LMMS core, then the plugin:

```
BUILDREL_PLUGIN_EXIT=0
... [100%] Built target lmms
... [100%] Built target neuralamp
```

Earlier in the lane, the same tree also built the headless targets and the licence-only edits
did not disturb it (`FINAL_PLUGIN_BUILD_EXIT=0`, `FINAL_HARNESS_BUILD_EXIT=0`).

### 2.2 Artifact

```
$ ls -la build/plugins/libneuralamp.so
-rwxrwxr-x 1 kruzzzzy kruzzzzy 312312 Sep  9 00:29 build/plugins/libneuralamp.so

$ sha256sum build/plugins/libneuralamp.so build-rel/plugins/libneuralamp.so
a60c3f061192fe290cec75583414c23de7fe1d38688c83774fa882504392cd95  build/plugins/libneuralamp.so
a60c3f061192fe290cec75583414c23de7fe1d38688c83774fa882504392cd95  build-rel/plugins/libneuralamp.so
```

Two independently configured Release trees produce a **byte-identical** shared object.
Exported entry points and dependencies:

```
$ nm -D --defined-only build/plugins/libneuralamp.so | grep -E "lmms_plugin_main|plugin_descriptor"
000000000000dd70 T lmms_plugin_main
000000000003d3c0 D neuralamp_plugin_descriptor
$ ldd build/plugins/libneuralamp.so | grep -c "not found"
0
```

### 2.3 The build blocker that was fixed

`NeuralAmpControls.cpp:40/46` failed with *invalid use of incomplete type `QDomElement`*; the
WIP had the `#include <QDomElement>` fix staged. The remaining blocker was
`EMBEDDED_RESOURCES artwork.svg` in `BUILD_PLUGIN(neuralamp …)` referencing a file that has
never existed in this repository's history (`git log --all -- plugins/NeuralAmp/artwork.svg` is
empty). The descriptor uses the built-in LMMS plugin logo
(`data/themes/default/lmms-plugin-logo.svg`), so the dangling reference was removed rather than
adding a placeholder asset. See commit `5a76f75ae`.

Build flags actually used (from `build/plugins/NeuralAmp/CMakeFiles/neuralamp.dir/flags.make`):

```
CXX_FLAGS = -fPIC -DPIC -O3 -DNDEBUG -std=gnu++20 ... -fvisibility=hidden ...
```

No `-march` is applied: LMMS adds `-march=x86-64-v2` only when `TARGET_UARCH=official`, and the
default is `none` — the configure log prints
`-- Setting target microarchitecture to 'none' since TARGET_UARCH was not set`. The plugin
CMakeLists pins `-O2` for Debug builds via
`TARGET_COMPILE_OPTIONS(neuralamp PRIVATE $<$<CONFIG:Debug>:-O2>)`, which is why the inherited
"unoptimised" figure was ~5 % rather than ~64 %.

## 3. Reference match (numerical correctness)

Method: a pristine worktree of upstream `sdatkinson/NeuralAmpModelerCore` at
`2563c0fd4cb1f9ce457d89a761738ea15097e1f3` (submodules `AudioDSPTools 0827c6c2`,
`eigen 6d829e76` / `bc3b3987`) is built with the upstream `render` tool; the same deterministic
input WAV (1,034,240 frames = 21.55 s @ 48 kHz) is rendered by upstream and by our harness, and
compared sample-by-sample. Reproduced end-to-end by
`plugins/NeuralAmp/tests/verify-reference.sh`:

```
== BossWN-nano
    max abs diff: 4.97326255e-07 (at sample 903383)
    correlation: 1.000000000
    reference match: PASS
== BossWN-standard
    max abs diff: 2.68220901e-07 (at sample 936853)
    correlation: 1.000000000
    reference match: PASS
VERIFYREF_EXIT=0
```

Independent NumPy cross-check (no harness code involved, `float32` WAVs):

```
== BossWN-nano ==
  max abs diff (out vs ref):  4.97326255e-07
  relative rms error:         8.50941455e-07 (-121.40 dB)
  correlation(out, ref):      1.000000000000
  verdict: PASS (rel rms < 1e-6, corr > 0.999999999)
== BossWN-standard ==
  max abs diff (out vs ref):  2.68220901e-07
  relative rms error:         2.63778284e-07 (-131.58 dB)
  correlation(out, ref):      1.000000000000
  verdict: PASS
```

Differences are at the float32 rounding floor (~1e-7); the engine is sample-exact against the
reference for these two models.

**Accepted limitation.** `A2.nam` (`SlimmableContainer`) is rejected by the loader with a clear
error rather than mis-loading:

```
LOAD FAILED: unsupported architecture 'SlimmableContainer' (this build supports WaveNet A1 only)
PROBE_EXIT=2        (rt_alloc_probe; nam_harness reproduces it: A2_EXIT=2)
```

Only WaveNet A1 is implemented. `wavenet_a1_standard.nam` (third model) is covered by the
allocation probe; the reference match above covers the two BossWN models.

**60-second run (the intent of spec T1).** 60.000 s of generated DI (5625 × 512 frames =
2,880,000 samples) on both A1 models — finite, non-silent output, and stable cost over a long
window:

```
== NeuralAmp harness ==
model: plugins/NeuralAmp/models/BossWN-nano.nam
architecture: WaveNet  version: 0.5.1
arrays: 2  weights: 842  receptive field: 4092 samples  trained sample rate: -1 Hz
block size: 512  blocks: 5625  warmup: 20  sample rate: 48000 Hz

(a) sanity
  output finite: yes (0 non-finite of 2880000)
  output non-zero: yes (peak 0.490519, rms 0.109501, non-zero 100.00%)
(b) cpu
  blocks: 5625 x 512 frames (60.000 s of audio @ 48000 Hz)
  cpu per block: 491.57 us (median 489.56, p95 511.04)
  realtime factor: 21.7x
  cpu of one core: 4.6084 %
result
  (a) finite/non-zero: PASS
  (b) cpu spec gate (<5% of one core @ 48000 Hz): PASS (4.6084 %)
      cpu stretch target (<1% of one core): FAIL
  (c) signal colouring (not a pass-through): PASS (corr(in,out)=0.0279, informational)
```

BossWN-standard likewise: `0 non-finite of 2880000`, peak 0.663697, rms 0.13966, 100 % non-zero,
2080.25 µs/block, **19.5023 %** of one core (realtime factor 5.1×), PASS on (a), FAIL on the
<5 % gate — the same verdict as the short runs.

## 4. CPU cost (re-measured in optimised builds)

### 4.1 Protocol

- Machine: Intel Core Ultra 7 255HX, 20 cores, Linux. Load average recorded per sweep
  (8.1 / 11.9 / 12.1 / 5.9–7.2 across four sweeps — the box also runs other lanes; thread CPU
  time is used precisely because it is robust to that).
- Measurement: `CLOCK_THREAD_CPUTIME_ID` around each `NamModel::process()` call only (model load
  and WAV I/O excluded), 20 warm-up blocks, 2000 blocks × 512 frames = 21.33 s of audio @ 48 kHz.
- 3 repetitions × 4 optimisation configurations × 2 models = 24 timed runs per sweep, four sweeps
  = 96 timed runs, plus block-size scaling runs, a 60 s run, and a like-for-like run at the
  upstream tool's block size.
- "cpu of one core %" = per-block CPU time ÷ block duration (10.667 ms at 512 frames/48 kHz).
- Harness source is the same code the plugin compiles (`nam/NamModel*.cpp`), built with the
  same `-O3 -DNDEBUG` flags.

### 4.2 Optimisation configurations (block 512)

Median of 3 reps from the final sweep (load average 5.9→7.2, every run exit 0); the spread
across all 12 reps per config is in §4.6.

| Config | BossWN-nano | BossWN-standard |
|---|---|---|
| `-O3 -DNDEBUG` (**shipped Release**) | **4.60 %** (491 µs/block) | **19.49 %** (2079 µs/block) |
| `-O2 -g` (**plugin's Debug config**) | 4.26 % (454 µs/block) | 20.15 % (2149 µs/block) |
| `-O0 -g` (unoptimised) | 63.89 % (6814 µs/block) | 403.25 % (43013 µs/block) |
| `-O3 -march=native` (not shipped) | 4.48 % (478 µs/block) | 15.58 % (1662 µs/block) |

Verbatim (`-O3`, final sweep 2026-09-09 00:30–00:36, load average 5.9→7.2, every run exit 0):

```
O3 BossWN-nano rep1 exit=0 cpu per block: 496.63 us cpu of one core: 4.6559 %
O3 BossWN-nano rep2 exit=0 cpu per block: 488.01 us cpu of one core: 4.5751 %
O3 BossWN-nano rep3 exit=0 cpu per block: 490.97 us cpu of one core: 4.6029 %
O3 BossWN-standard rep1 exit=0 cpu per block: 2083.16 us cpu of one core: 19.5296 %
O3 BossWN-standard rep2 exit=0 cpu per block: 2078.85 us cpu of one core: 19.4892 %
O3 BossWN-standard rep3 exit=0 cpu per block: 2076.62 us cpu of one core: 19.4683 %
```

Harness self-assessment on the committed tree — verbatim `result` blocks from the 2000-block runs
(`/tmp/nam2_final_BossWN-*.log`):

```
== BossWN-nano (2000 blocks, exit 0) ==
  (a) finite/non-zero: PASS
  (b) cpu spec gate (<5% of one core @ 48000 Hz): PASS (4.6243 %)
      cpu stretch target (<1% of one core): FAIL
  (c) signal colouring (not a pass-through): PASS (corr(in,out)=0.0281, informational)

== BossWN-standard (2000 blocks, exit 0) ==
  (a) finite/non-zero: PASS
  (b) cpu spec gate (<5% of one core @ 48000 Hz): FAIL (19.4637 %)
      cpu stretch target (<1% of one core): FAIL
  (c) signal colouring (not a pass-through): PASS (corr(in,out)=-0.0682, informational)
```

The Debug config (`-O2 -g`) is essentially the same as Release for nano and slightly worse for
standard; the shipped Release build is therefore not leaving meaningful CPU on the table for
this model pair. `-march=native` would buy ~20 % on standard but is not what ships.

### 4.3 Against the upstream reference implementation

Upstream `render` tool (`-O3`, same input, processes in 64-sample blocks — `tools/render.cpp`
uses `const int bufferSize = 64`):

```
UPSTREAM BossWN-nano rep1 user=0.66 s sys=0.00 s wall=0.67 s
UPSTREAM BossWN-nano rep2 user=0.65 s sys=0.00 s wall=0.66 s
UPSTREAM BossWN-nano rep3 user=0.66 s sys=0.00 s wall=0.66 s
UPSTREAM BossWN-standard rep1 user=2.19 s sys=0.00 s wall=2.20 s
UPSTREAM BossWN-standard rep2 user=2.22 s sys=0.00 s wall=2.23 s
UPSTREAM BossWN-standard rep3 user=2.21 s sys=0.00 s wall=2.21 s
```

For 21.55 s of audio: nano 3.06 % of one core (327 µs per 512-frame block equivalent),
standard 10.26 % (1094 µs). Like-for-like at upstream's own block size (64 frames, same total
audio, our engine): nano 5.38 % (71.7 µs/64-block) vs upstream 3.06 % (40.8 µs) — **1.76×**;
standard 20.41 % (271.8 µs) vs 10.26 % (136.6 µs) — **1.99×**. At 512-frame blocks the nano gap
narrows to ~1.5× because per-call overhead amortises.

The engine is numerically exact but ~1.8–2.0× the reference implementation's CPU at equal block
size and flags. That was a real, documented gap; it is profiled and closed in **§10** (measured
1.79× / 2.12× speed-up, residual gap vs upstream 1.08× / 0.91×).

### 4.4 Block-size scaling

| Block | nano | standard |
|---|---|---|
| 512 | 4.60 % | 19.49 % |
| 1024 | 4.55 % | 19.88 % |
| 2048 | 4.55 % | 21.31 % |

(median of 3 reps per row; the 1024/2048 rows were measured at load ~13, the 512 row at load
5.9–7.2). nano is flat; standard rises ~9 % from 512 to 2048 frames, consistent with cache
pressure rather than per-call overhead. Larger host buffers do **not** make either model cheaper
per sample.

### 4.5 The "<1 % of one core" gate, reconciled

The gate originates in `findings-ai-dsp.md` §2.5, which converts a **hardware DSP** measurement
of "~322 µs per block (GP-200 DSP)" — for the A2-standard model — into "on a modern x86 or ARM
CPU, that's <1 % of one core at 48 kHz". The arithmetic does not survive the standard block size:

- At 512 frames/48 kHz a block lasts 10.667 ms. **322 µs = 3.02 % of one core**, not <1 %.
- <1 % requires blocks ≥ 32.2 ms (≥ 1546 frames), and §4.4 shows our per-sample cost is flat
  across 512–2048 frames, so no realistic host buffer size reaches it.
- Measured on this CPU, the **upstream reference implementation itself** costs 3.06 % (nano) and
  10.26 % (standard) at 48 kHz — it fails the <1 % gate too.

So the honest statement is: **<1 % is not achievable for WaveNet A1 at 48 kHz on this CPU by
either implementation**; the realistic operating points are ~4.6 % (nano, one instance) and
~19.5 % (standard) for ours, and ~3 %/~10 % for the reference. The spec's own §7 T2 gate
("<5 % single-core reference") is met by nano and not by standard. Both figures are reported by
the harness on every run (`<5 % spec gate`, `<1 % stretch target`) so the verdicts cannot drift.

### 4.6 Reproducibility / spread

96 timed runs across four sweeps at different machine loads; 12 reps per configuration.
Minimum / median / maximum:

| Config | nano min/med/max | standard min/med/max |
|---|---|---|
| `-O3` | 4.57 / 4.63 / 6.66 % | 19.47 / 19.52 / 21.63 % |
| `-O2 -g` | 4.25 / 4.27 / 4.95 % | 20.14 / 20.23 / 24.22 % |
| `-O0` | 63.68 / 63.97 / 70.78 % | 403.05 / 404.19 / 412.57 % |
| `-O3 -march=native` | 4.43 / 4.52 / 7.17 % | 15.51 / 15.58 / 16.77 % |

Higher values track machine load (the box runs other lanes; load 5.9–14.9 across the four
sweeps). The first sweep used the pre-fix harness binary, whose exit code was 1 because of the
invalid (c) correlation gate — the CPU timing happens before that check and is unaffected; the
other 9 reps per configuration exit 0. The minima are the best estimate of true cost, and the
medians sit within 2 % of them for both models. Raw logs: `/tmp/nam_cpu_sweep.log`,
`/tmp/nam_cpu_sweep_fixed.log`, `/tmp/nam_cpu_sweep_quiet.log`, `/tmp/nam_cpu_sweep_final.log`.

## 5. Audio-thread audit

### 5.1 Allocation probe (real run)

`plugins/NeuralAmp/tests/rt_alloc_probe.cpp` wraps `malloc/calloc/realloc/free/operator new/new[]`
via GNU `ld --wrap` and counts every heap operation during 512 consecutive `process()` calls:

```
########## BossWN-nano block=512 ##########
  malloc=0 calloc=0 realloc=0
  operator new=0 new[]=0 delete=0 delete[]=0
  free=105472 (null=105472 non-null=0)
RESULT: NO ALLOCATION IN PROCESS PATH (heap allocations during process(): 0)
PROBE_EXIT=0
```

Same result for `BossWN-standard`, `wavenet_a1_standard`, and for blocks 64 and 1024 — 5 positive
runs, 0 allocations. The `free` calls are all null-pointer frees from Eigen's internal cleanup
paths (no heap blocks were ever allocated on this path).

### 5.2 Static audit of the process path

- `NamModel::process()`/`processImpl()` are `noexcept`; no locks, no file/console I/O, no
  `std::function` on the audio path (`grep` over `NamModel.{h,cpp}` finds no mutex/lock/atomic;
  the atomics live in `NeuralAmpEffect` for the model swap). Scratch buffers are sized once in
  the constructor (`kMaxBlock` = 8192 frames × 2 floats); the only `std::vector` member
  (`m_prewarmBuffer`) is used by `prewarm()` on the loader thread, not by `process()`.
- Model publication uses an atomic pointer swap with an `audioActive` guard; the audio thread
  never blocks on the loader. Prewarm runs on the loader thread, not the audio thread.
- LMMS's per-buffer size is chunked to `kMaxBlock` if a host buffer exceeds 8192 frames.
- Known assumptions/risks (not exercised here): the swap guard assumes a single audio thread
  (true for LMMS); `setModelPath()` joins any in-flight load on the calling (GUI) thread, so a
  slow model load can stall the UI; the engine assumes the host sample rate matches the model's
  trained rate (no resampling, no warning); denormal behaviour under sustained silence was not
  measured.

## 6. Licence inventory

Rule applied: **retain upstream notices**; all components must be GPL-2.0+-compatible. The full
inventory is in `plugins/NeuralAmp/LICENSE-NOTICE.md`; summary:

| Component | Where | Licence | Notice status |
|---|---|---|---|
| RTNeural | `plugins/NeuralAmp/rtneural/` | **BSD-3-Clause** (was misstated as MIT) | `rtneural/LICENSE` present; corrected in NOTICE + CMake comment |
| Eigen | `rtneural/modules/Eigen/` | **MPL-2.0** | **newly vendored** as `COPYING.MPL2` (373 lines, mozilla.org text) |
| nlohmann/json | `rtneural/modules/json/json.hpp` | MIT | `SPDX-License-Identifier: MIT` in the header (single-header distribution, no separate file) |
| Lua / LuaBridge | `plugins/NeuralAmp/lua/` | MIT | `COPYRIGHT` with MIT text; `README.lmms` records URL + SHA-256 + licence |
| Models (4 `.nam`) | `plugins/NeuralAmp/models/` | MIT (upstream repos) | provenance table + SHA-256 in `models/README.md` |

- Eigen's MPL-2.0 notice previously existed **only inside the source headers**; the text is now
  vendored at `rtneural/modules/Eigen/COPYING.MPL2`. MPL-2.0 is GPL-compatible here (Eigen is not
  marked "Incompatible With Secondary Licenses").
- Model provenance re-verified on 2026-09-08 by re-downloading each file from the URL recorded in
  `models/README.md` and comparing SHA-256 (`/tmp/nam-prov/verify.log`, 4/4 MATCH, `curl_exit=0`):
  `A2.nam 2d2d7445…`, `wavenet_a1_standard.nam ceb53469…`, `BossWN-nano.nam 747bd1d2…`,
  `BossWN-standard.nam 0474d8e1…`. Upstream licence files read directly:
  `NeuralAudio LICENSE` = MIT, `NeuralAmpModelerCore LICENSE` = MIT, `RTNeural LICENSE` =
  BSD 3-Clause.

## 7. Reproducing

```
# build (Release, Qt6)
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON
cmake --build build-rel -j4 --target neuralamp nam_harness rt_alloc_probe

# correctness vs upstream (fresh worktree @ 2563c0f + submodules)
bash plugins/NeuralAmp/tests/verify-reference.sh        # VERIFYREF_EXIT=0

# CPU
build-rel/plugins/nam_harness plugins/NeuralAmp/models/BossWN-nano.nam \
    --blocks 2000 --warmup 20 --block 512

# 60 s stability run (T1 intent)
build-rel/plugins/nam_harness plugins/NeuralAmp/models/BossWN-nano.nam \
    --blocks 5625 --warmup 20 --block 512

# audio-thread allocations
build-rel/plugins/rt_alloc_probe plugins/NeuralAmp/models/BossWN-nano.nam --blocks 512

# LMMS core regression suite (8 tests)
cmake --build build -j4 --target ArrayVectorTest AudioBufferTest AutomatableModelTest \
    MathTest ProjectVersionTest RelativePathsTest TimelineTest AutomationTrackTest
cd build/tests && ctest          # 100% tests passed, 0 tests failed out of 8
```

## 8. Not verified (explicit)

1. **In-LMMS instantiation / DAW end-to-end** — the `.so` builds and exports `lmms_plugin_main`,
   but it was not loaded into a running LMMS GUI (headless environment). No audio was played
   through the plugin in a host.
2. **T5 save/reload round-trip** — the persistence path exists (`NeuralAmpControls` writes/reads
   the model path in the project XML) but no GUI project save/load test was run.
3. **T1 as written** (A2 capture) — A2/`SlimmableContainer` is unsupported; the *intent* of T1
   (60 s of DI, non-silent, finite output) is checked in generated mode on the A1 models, but the
   literal A2 criterion is not met. This is the accepted documented limitation.
4. **A2 port** — not implemented.
5. **Non-Linux builds** (macOS/Windows) — not built.
6. **Denormal / sustained-silence behaviour** — not measured.
7. **Multiple instances / real-time load in a DAW** — not measured; figures are per instance on
   an otherwise-busy machine.
8. **Sample-rate mismatch** — no resampling and no warning if the host runs at a rate other than
   the model's trained rate; not tested.
9. ~~**Cause of the ~1.8–2.0× CPU gap vs upstream** — measured, not profiled.~~ **Resolved in
   §10**: profiled per stage (tanh activation dominated at 47–62 %), fixed by using the same
   libmvec kernels upstream compiles against.
10. **ctest integration** — the NAM harness/probe are `EXCLUDE_FROM_ALL` targets, not registered
    with ctest; they are run manually (commands in §7). The LMMS core suite itself passes
    (`100% tests passed, 0 tests failed out of 8`, 3.79 s), so the plugin changes cause no
    regression in the existing tests.

## 9. Commits

| Commit | Contents |
|---|---|
| `4e31e46ea` | base |
| `f41be063e` | inherited WIP (engine, loader, plugin skeleton, models) |
| `5a76f75ae` | build fix (dangling `artwork.svg`), tests CMake targets, harness (c)-gate fix, `verify-reference.sh`, `.gitignore` |
| `c8641b9b6` | vendored Eigen `COPYING.MPL2`, corrected licence inventory |
| `cf9f48715` | stage-level CPU profile of the WaveNet engine (`NamProfile.h`, `nam_profile` target) — task #590 |
| `bcc5ef293` | vectorised tanh via the libmvec kernels the reference uses (`NamTanh.h`, loader warm-up, CMake) — task #590 |

All in `lmms-nam` on branch `feat/neural-amp`; working tree clean. This report itself is tracked
in the project repo (`git log -- NEURAL-AMP.md` there: first carried by `5c6ef6b`, then follow-up
commits adding the 60 s run, ctest result and final CPU sweep).

No pushes were made. Build logs: `/tmp/nam2_build_plugin.log`, `/tmp/nam2_buildrel_plugin.log`,
`/tmp/nam2_force_plugin_build.log`; CPU sweeps: `/tmp/nam_cpu_sweep.log`,
`/tmp/nam_cpu_sweep_fixed.log`, `/tmp/nam_cpu_sweep_quiet.log`, `/tmp/nam_cpu_block64.log`,
`/tmp/nam_cpu_blockscale.log`; reference chain: `/tmp/nam2_verifyref.log`,
`/tmp/nam2_numpy_verify.log`, `/tmp/nam2_upstream_time.log`; probe: `/tmp/nam2_probe.log`.

## 10. CPU optimization (task #590) — the gap profiled and closed

Status: **fixed and re-verified** (commits `cf9f48715`, `bcc5ef293` on `feat/neural-amp`).
This section supersedes the "measured, not profiled" caveat in §4.3 and §8 item 9.

### 10.1 Profile (real run, per stage)

`perf` is not usable in this container, so the engine was instrumented instead:
`plugins/NeuralAmp/nam/NamProfile.h` defines `NAM_PROF_TIC/TOC` hooks that expand to
nothing unless `NAM_PROFILE_LAYERS` is defined; the new `nam_profile` target builds the
engine with them enabled and times each stage with `CLOCK_THREAD_CPUTIME_ID`
(2000 × 512-frame blocks @ 48 kHz, 20 warm-up). The hooks are compiled out of the plugin
and `nam_harness` builds (verified: 0 profile symbols in `libneuralamp.so`, 0 in
`nam_harness`, 1 in `nam_profile`), so shipped code is unchanged.

BEFORE the change (scalar `std::tanh`), µs/block and share of the accounted slots:

| stage | nano µs/blk | nano % | standard µs/blk | standard % |
|---|---|---|---|---|
| **tanh** | **266.2** | **47.2** | **1508.7** | **61.9** |
| conv GEMMs (K taps) | 115.1 | 20.4 | 497.4 | 20.4 |
| 1x1 + bias + residual | 63.5 | 11.3 | 220.7 | 9.1 |
| history shift+append | 45.5 | 8.1 | 55.1 | 2.3 |
| mixin GEMM | 31.5 | 5.6 | 53.5 | 2.2 |
| conv bias | 17.0 | 3.0 | 26.5 | 1.1 |
| z = conv + mixin | 7.5 | 1.3 | 25.9 | 1.1 |
| head += z | 7.8 | 1.4 | 23.2 | 1.0 |
| rechannel / head rechannel / head init / copies / scale | ≤ 3.8 each | ≤ 0.7 each | ≤ 12.3 each | ≤ 0.5 each |
| SUM of slots | 563.8 | 100 | 2437.6 | 100 |
| measured process total | 603.3 | | 2479.8 | |

The instrumented totals run above the production harness figures (603 vs 493 µs for nano,
2480 vs 2080 µs for standard) — that is the clock-read overhead of the hooks plus machine
load; the *shares* are what matter. The top cost is the tanh activation by a wide margin:
47.2 % (nano) and 61.9 % (standard).

AFTER (this change):

| stage | nano µs/blk | nano % | standard µs/blk | standard % |
|---|---|---|---|---|
| tanh | 31.1 | 9.4 | 110.3 | 10.7 |
| conv GEMMs (K taps) | 116.6 | 35.2 | 495.9 | 47.9 |
| 1x1 + bias + residual | 62.1 | 18.8 | 219.4 | 21.2 |
| history shift+append | 46.1 | 13.9 | 55.6 | 5.4 |
| mixin GEMM | 32.1 | 9.7 | 52.7 | 5.1 |
| SUM of slots | 331.3 | 100 | 1034.3 | 100 |
| measured process total | 372.6 | | 1075.5 | |

Every other stage is unchanged within run-to-run noise (conv GEMMs 115.1→116.6,
history 45.5→46.1, 1x1 63.5→62.1). The tanh stage itself got 8.6× faster (nano) and
13.7× (standard).

### 10.2 Where the gap came from (upstream comparison)

Upstream's own flags (`/tmp/nam-core-build/tools/CMakeFiles/benchmodel.dir/flags.make`):

```
CXX_FLAGS = -O3 -DNDEBUG -std=c++20 -flto=auto -fno-fat-lto-objects -fvisibility=hidden
            -Wall -Wextra -Wpedantic -Wstrict-aliasing -Wunreachable-code -Weffc++
            -Wno-unused-parameter -Ofast
```

`-Ofast` lets GCC vectorise `ActivationTanh::apply()`'s plain `std::tanh` loop into libmvec
calls; the upstream binary imports them directly:

```
$ nm -D /tmp/nam-core-build/tools/benchmodel | grep tanhf
                 U tanhf@GLIBC_2.2.5
                 U _ZGVbN4v_tanhf@GLIBC_2.35
$ ldd /tmp/nam-core-build/tools/benchmodel | grep mvec
        libmvec.so.1 => /lib/x86_64-linux-gnu/libmvec.so.1
```

Our Release build is `-O3` without `-ffast-math`, so the same loop stayed scalar
(~10–12 ns/element). That is the whole gap: with tanh excluded the remaining stages already
cost about the same as the reference engine's (see the totals in §10.4).

One correction to §4.3 worth recording: the upstream `benchmodel` tool enables an
*approximate* tanh by default (`Fast tanh: enabled`, `NAM/activations.h::fast_tanh`, a
rational approximation) and only uses the accurate activation with `--no-fast-tanh`. The
`render` tool — which generated the reference WAVs and the §4.3 figures — has no such flag
and always uses the accurate activation, so the §4.3 baseline was fair. The like-for-like
numbers below use the accurate path.

### 10.3 The change

- New `plugins/NeuralAmp/nam/NamTanh.h`: computes tanh with the same libmvec kernels the
  reference uses (`_ZGVdN8v_tanhf`, 8-wide, when the CPU has AVX2+FMA; `_ZGVbN4v_tanhf`,
  4-wide, otherwise; scalar tail), resolved once via `dlopen()`/`dlsym()`.
- `NamModel::applyTanh()` calls `tanhInPlace()` over the same contiguous element range, in
  the same element order as the scalar loop it replaces. No other code path changes.
- RT safety: `tanhdetail::warmUp()` runs in `NamModel::loadFromFile()` on the loader thread
  (before `prewarm()`), so the audio thread only reads an already-initialised function
  pointer. The `dlopen` handle is intentionally never closed.
- Fallbacks: without libmvec, or with `-DNAM_TANH_SCALAR`, the previous scalar loop is
  compiled verbatim.
- `${CMAKE_DL_LIBS}` added to the affected targets (portable no-op on glibc).

### 10.4 Before/after CPU (shipped methodology)

Protocol unchanged from §4.1: `-O3 -DNDEBUG`, `CLOCK_THREAD_CPUTIME_ID` around `process()`
only, 20 warm-up blocks, 2000 × 512 frames @ 48 kHz, median of 3. The "before" column was
measured with the same tree compiled `-DNAM_TANH_SCALAR` (interleaved A/B, same machine,
same session); it reproduces the §4.2 numbers (4.60 % / 19.49 %).

| model | before µs/blk | before % core | after µs/blk | after % core | speed-up |
|---|---|---|---|---|---|
| BossWN-nano | 492.8 | 4.62 % | 276.0 | 2.59 % | **1.79×** |
| BossWN-standard | 2079.6 | 19.50 % | 982.6 | 9.21 % | **2.12×** |

Three reps per cell (raw `SUMMARY` lines in `/tmp/nam590_final_matrix.log`):
nano after 276.02 / 275.85 / 276.04 µs (median 276.02 = 2.59 %),
nano before 494.27 / 492.21 / 492.84 µs (median 492.84 = 4.62 %);
standard after 982.63 / 977.95 / 983.13 µs (median 982.63 = 9.21 %),
standard before 2078.55 / 2079.64 / 2080.02 µs (median 2079.64 = 19.50 %).

Against upstream, measured with the *same* CPU-time method by a driver linked against the
objects already in `/tmp/nam-core-build` (upstream engine is double-precision;
`--no-fast-tanh` for the accurate path), 512-frame blocks, median of 3:

| model | upstream (accurate) | ours before | ours after |
|---|---|---|---|
| BossWN-nano | 255.8 µs (2.40 %) | 492.8 µs — 1.93× | 276.0 µs — 1.08× |
| BossWN-standard | 1072.1 µs (10.05 %) | 2079.6 µs — 1.94× | 982.6 µs — 0.92× |

(Upstream's fast-tanh mode, for reference: 235.1 µs nano / 980.2 µs standard.)

At upstream's own block size (64 frames, 16000 blocks, CPU time, median of 3):
nano 72.5 → 43.8 µs vs upstream 39.5 µs (1.84× → 1.11×);
standard 273.0 → 129.3 µs vs upstream 134.2 µs (2.03× → 0.96×).

### 10.5 The reference match is preserved (and improves)

`nam_harness --wav-run … --compare-ref …`, block 512, final binaries:

```
BossWN-nano      max abs diff 5.96046448e-08  rms 4.21788382e-10  correlation 1.000000000  PASS
BossWN-standard  max abs diff 3.10130417e-07  rms 3.44056275e-08  correlation 1.000000000  PASS
```

Before the change (scalar engine, same harness):

```
BossWN-nano      max abs diff 4.97326255e-07  rms 9.33603527e-08  correlation 1.000000000  PASS
BossWN-standard  max abs diff 2.68220901e-07  rms 3.69131212e-08  correlation 1.000000000  PASS
```

Correlation is 1.000000000 in all four runs; nano's max abs difference against the reference
*falls* by 8.3× (4.97e-07 → 5.96e-08) because libmvec tanh is the same kernel family the
reference is compiled against. The scalar tail path (block 511: 4 × 511 = 2044 ≡ 4 mod 8)
was exercised explicitly — correlation 1.000000000, PASS for both the libmvec and the scalar
builds.

`rt_alloc_probe` on the new binary: `malloc=0 calloc=0 realloc=0 operator new=0 new[]=0 …`,
`RESULT: NO ALLOCATION IN PROCESS PATH`. LMMS core suite from `build/tests`:
`100% tests passed, 0 tests failed out of 8` (3.80 s).

### 10.6 What remains unverified

1. **The 4-wide SSE2 path (`_ZGVbN4v_tanhf`) is resolved but not exercised on this host** —
   the CPU has AVX2+FMA, so the 8-wide kernel is selected (`nam590_kernel_probe`:
   `v8: yes, v4: yes`). Only the scalar fallback was exercised (via `-DNAM_TANH_SCALAR`).
2. **Non-x86 / no-libmvec platforms** — the fallback compiles and is the pre-change code
   path, but was not built or run on such a platform.
3. **Bit-exactness is not claimed** — libmvec tanh is within ~2 ulp of glibc scalar `tanhf`,
   so the plugin's output changes at the 1e-8…1e-7 level vs the pre-change engine (it moves
   *closer* to the reference render). The end-to-end reference match is the verified
   criterion.
4. **The per-stage upstream comparison is inferred from totals**, not profiled on their
   side (their tools do not expose stage timing).
5. **In-LMMS / DAW behaviour, multiple instances, denormal/sustained-silence behaviour** —
   unchanged from §8; still unverified.
6. **The <1 % stretch target is still not met** (nano 2.59 %, standard 9.21 %) — and it is
   not met by the upstream reference either (2.40 % / 10.05 % at 512 frames). See §4.5.
7. **Load conditions** — the box runs other lanes; CPU-time medians drift a few percent
   between sweeps (per-run spread is recorded above and in §4.6).

Artifacts: `nam_profile`, `nam_harness`, `rt_alloc_probe` targets in `lmms-nam`;
`/tmp/nam590_upstream_bench.cpp` (upstream driver), `/tmp/nam590_tanh_probe.cpp` (kernel
accuracy/speed), `/tmp/nam590_kernel_probe.cpp` (kernel selection); logs
`/tmp/nam590_build_*.log`. Commits `cf9f48715` (profile tooling) and `bcc5ef293` (the
optimisation) are local to `feat/neural-amp` — **no push**.
