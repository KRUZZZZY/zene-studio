# The render is not bit-reproducible — measured, explained, mostly fixed

Lane `post-alpha/render-determinism` (worktree `zene-pa-determinism`), base `post-alpha/v0.2`
(the renderer is identical on every post-alpha branch). Measured with `build/lmms` configured
exactly as `tools/local-ci.sh` does (the `linux-x86_64` job's `CMAKE_OPTS`, `-DWANT_QT6=ON`,
RelWithDebInfo) on a 20-core Linux box, one render per process unless stated. Binary sha256
`77b56da09b23fda4ec124c266609b5f8b54ce1b86ffc57296eab0db6ccb23ff0` before the fix and
`67e952c8b9fec766798ad3ecf85e049960effcc250d6a4a25b1dab934cc69e87` after.

## Verdict

* **The defect is real and large.** 6 of the 9 projects this repo bundles rendered to different
  bytes on every run of the same binary; a 7th does so intermittently. The worst differs on
  98.3 % of its frames, and the smallest by one LSB on 63 frames of 1.4 M. Loudness never moves,
  so no level/RMS/LUFS measurement can see it — only a sample-level comparison can (§4).
* **The dominant mechanism is the offline renderer's worker pool**, and it is now fixed. Three
  configurations of the same binary on `DirtyLove.mmpz`: pooled/unpinned gave three different
  files; pooled pinned to one CPU gave one file four times over; pool disabled gave **the same
  file** four times over, byte for byte equal to the pinned runs (§6).
* **The fix**: `ProjectRenderer` renders its export on its own thread, so an export has no
  scheduling decision in it. 5/5 subsequent renders of that project are byte-identical and equal
  to the single-threaded answer; **7 of 9 bundled projects are now bit-reproducible** (§7).
* **2 of 9 are not, and this lane proves the fix is not at fault for them**: for both,
  one-CPU pinning, ASLR disabled, `MALLOC_PERTURB_`, a fixed `rand()`, and a frozen wall clock
  *each still produce different files*, and for one of them bypassing all twelve effect chains
  changes nothing. The mechanisms are in the instruments, they are named with file:line in §10,
  and they need their own lane.
* **It is still upstream-inherited.** Stock LMMS 1.3.0-alpha.2 renders the same four demos
  non-deterministically (§2.3). That is why this is a fix worth making here, not a reason to
  leave it: it corrupts our own evidence base.
* **The "byte-identical render" instruction that nearly every lane brief in this program carried
  is invalid for any pre-fix build.** Behaviour-preservation has to be stated as a measured delta
  against a control render, or against a build with this fix. The `lmms-lab` MCP skill's
  determinism workflow ("compare `data_sha256` across two renders") should say so.

---

## 1. Definitions

* "Non-deterministic" means: same binary, same project, same flags, **one render per process**
  (which is how `ProjectRenderer` is always used), different output bytes.
* All figures are 16-bit PCM WAV at 44100 Hz. **LSB** = one unit of that, 1/32768 of full scale
  (−90.3 dBFS). "frames differing" counts a frame when *any* channel differs at all — 1-LSB
  differences count, deliberately.
* An engine period is `framesPerPeriod` = 256 frames (`include/AudioEngine.h:54`), which is what
  the period histogram in `tools/render-determinism-compare.py` buckets on.

## 2. What was already known — five independent confirmations

Five different lanes, five different projects and flag sets, all before this lane started.

1. **Stem export** (`zene-pa-stems/docs/STEM-EXPORT.md` §4.4): three runs of the *base* binary on
   one project gave three sha256s (`60a48df3…`, `481d0b4e…`, `dbfa6f28…`); the worst pair differed
   on 2 704 frames with a ~408-frame dropout, max 6 547 LSB, −0.066 dB. Inside a single process
   the same class was larger (26 204 frames), and one episode was a phase change inside the
   sample-playback window (frames 1 792–26 460 of one stem, 6 673 LSB peak, 3.88 % of samples)
   with the energy still matching to 0.018 dB.
2. **Clip model**: 53 314 differing samples after best alignment, a start offset that jitters by
   up to one frame, differences concentrated at period boundaries, attributed to `ProjectRenderer`.
3. **Alpha baseline** (`zene-studio-alpha-first-test-baseline-2026-09-11`): **4 of 7 bundled demos
   differ every run, in stock LMMS 1.3.0-alpha.2 as well** — `DirtyLove.mmpz`,
   `Root84-TrancyLoop.mmpz`, `Skiessi-222.mmpz`, `Surrender-Main.mmpz` unstable in both trees,
   `Crunk(Demo).mmp` stable in both (18/18 bit-identical upstream). For one project the files were
   bit-identical for 1 716 851 frames (35.77 s) and then both channels diverged together:
   911 951 frames differing by >0.1, max 1.06, loudness unchanged.
4. **RNNoise denoiser** (`plugins/RnnoiseDenoiser/RUNTIME-TEST.md` §9.2–9.3): repeat renders
   differ only in the first ~70 ms, and three-render data-chunk sha256s split
   (`C_1=C_2≠C_3`, `D_1=D_2≠D_3` for no-plugin projects, two plugin projects bit-identical). That
   lane correctly refused to blame its plugin.
5. **Two lanes in flight while this was written**: a routing-graph lane rejected its `.mmp`-level
   A/B render because two same-binary renders differed in 48.5 M of 72 M payload bytes, and a
   MIDI-depth lane measured the difference collapsing to 1 ulp on 1.6 % of samples when the render
   was pinned to one CPU.

## 3. Reproduction recipe

No patch, no instrumentation, no special build:

```sh
cd projects/lmms-fl-research/zene-pa-determinism
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4      # configure + build + ctest
D=data/projects/shorties/DirtyLove.mmpz
for i in 1 2 3; do
  QT_QPA_PLATFORM=offscreen ./build/lmms render "$D" -o /tmp/r$i.wav -f wav -s 44100
  echo "run$i EXIT=$?"
done
sha256sum /tmp/r*.wav              # three different hashes on an unfixed build
python3 tools/render-determinism-compare.py /tmp/r1.wav /tmp/r2.wav /tmp/r3.wav
```

The whole sweep, per-project table and pairwise detail included, is one command:

```sh
bash tools/render-determinism-probe.sh --binary build/lmms --runs 3     # the 9 bundled projects
bash tools/render-determinism-probe.sh --all --runs 3                   # + 38 upstream demos (hours)
```

It renders into a fresh `mktemp -d` unless `--out` is given, keeps every per-render log and WAV,
and writes `summary.tsv`. **Three settings must stay fixed or the numbers move**: `-s 44100`
(the CLI default; the alpha baseline used 48000), `-f wav` without `-a` (16-bit PCM — `-a` writes
float32 and the LSB figures change), and **one render per process** (renders inside one process
share engine state and differ differently — the stem lane measured 26 204 frames that way).

Two instrumented variants are supported directly and are what made §6 and §10 possible:

```sh
taskset -c 0 ./build/lmms render …

gcc -shared -fPIC -O2 -o /tmp/ncpu.so tools/ncpu-shim.c -ldl
ZENE_FAKE_NCPU=1 LD_PRELOAD=/tmp/ncpu.so ./build/lmms render …   # no workers at all
bash tools/render-determinism-probe.sh --ld-preload /tmp/ncpu.so # the sweep under the shim
```

`tools/ncpu-shim.c` exists because `AudioEngine`'s pool is sized `QThread::idealThreadCount() - 1`
(`src/core/AudioEngine.cpp:85`) and nothing else moves it: Qt 6 asks the kernel for the **affinity
mask**, so interposing `sysconf` alone changes nothing (verified — it moved
`python3 -c 'os.cpu_count()'` to 3 and left `QThread::idealThreadCount()` at 20). The shim also
interposes `sched_getaffinity`, and then Qt reports the forced count.

## 4. The sweep — which projects are unstable, and by how much

Pre-fix binary `77b56da0…`, 3 renders per project, one render per process. "differing" and
"max |Δ|" are the worst of the three pairs. Raw tables: `/tmp/det-probe-before-clean/summary.tsv`
and `<project>/pairs.tsv` (reproduce with §3's sweep command).

| project | verdict | distinct sha256 | frames differing | of frames | max │Δ│ | max Δ dBFS | level Δ dB | first differing frame |
|---|---|---|---|---|---|---|---|---|
| `demos/StrictProduction-DearJonDoe.mmp` | **UNSTABLE** | 3/3 | 7 883 555 | 9 002 240 (87.6 %) | 4 616 LSB | −17.02 | −3.0e-5 | 243 961 |
| `shorties/Crunk(Demo).mmp` | stable | 1/3 | 0 | — | 0 | −inf | 0 | — |
| `shorties/DirtyLove.mmpz` | **UNSTABLE** | 3/3 | 2 317 068 | 4 066 048 (57.0 %) | 34 074 LSB | +0.34 | +0.018 | 959 452 |
| `shorties/Root84-TrancyLoop.mmpz` | **UNSTABLE** | 3/3 | 469 286 | 477 440 (**98.3 %**) | 13 758 LSB | −7.54 | +0.0049 | 512 |
| `shorties/Skiessi-222.mmpz` | **UNSTABLE** | 3/3 | 63 | 1 375 744 (0.005 %) | **1 LSB** | −90.31 | −2.4e-9 | 471 604 |
| `shorties/Surrender-Main.mmpz` | **UNSTABLE** | 3/3 | 78 185 | 666 112 (11.7 %) | 299 LSB | −40.80 | −6.8e-5 | 13 211 |
| `shorties/sv-DnB-Startup.mmpz` | stable | 1/3 | 0 | — | 0 | −inf | 0 | — |
| `shorties/sv-Trance-Startup.mmpz` | stable | 1/3 | 0 | — | 0 | −inf | 0 | — |
| `tutorials/editing_note_volumes.mmp` | stable* | 1/3 | 0 | — | 0 | −inf | 0 | — |

\* `editing_note_volumes` is **intermittent**: this sweep found it stable, two earlier sweeps of
the same binary found it 2/3 and 3/3 distinct (differing 23 753 frames, max 336 LSB, and 47 548
frames, max 421 LSB). Both of those ran while a second probe instance was rendering on the same
box, so its instability — like everything here — looks load-sensitive. It is counted as unstable
in the summary above (`7 of 9`) and as stable in the clean run; the honest statement is
"unstable when the machine is busy".

Reading it:

* **The magnitude tracks the processing, not the length.** `Skiessi-222` differs by one LSB on 63
  frames — the raw arithmetic signature. `Root84-TrancyLoop` differs on 98 % of frames by up to
  13 758 LSB. `StrictProduction-DearJonDoe` is the 87 %-of-frames / 67 %-of-payload-bytes case the
  routing-graph lane saw independently.
* **Loudness is unchanged everywhere** (|level Δ| ≤ 0.018 dB), which is why this survived a
  release: every level-based test passes.
* **The stable projects are the short, single-instrument, effect-free ones** (`Crunk(Demo)`,
  `sv-DnB-Startup`, `sv-Trance-Startup`) plus, in the clean run, the tutorial.
* **The one-frame start-offset story does not describe these files**: in all nine the best
  alignment lag is **0**, and aligning reduces nothing. The clip-model lane's ≤1-frame jitter is
  real but is a different, smaller effect.

## 5. What the difference is made of (`DirtyLove`, run 1 vs run 2, pre-fix)

* Identical for the first 959 452 frames (21.756 s).
* The first differences are **exactly 1 LSB** (89 249 frames differ by exactly 1 LSB).
* From frame 1 435 339 (32.55 s) the difference becomes large and *intermittent per second* —
  some seconds are bit-identical, others differ by up to 30 871 LSB; 1 250 583 frames differ by
  more than 1 000 LSB; 9 855 of 15 883 periods are dirty, and the worst period is dirty on all
  256 frames.
* Same length, same RMS to 0.006 dB, correlation 1 at lag 0 — aligned, not shifted.

A one-ULP seed amplified wherever a non-linear chain runs is the signature of arithmetic whose
*order* changed. It is also why the effect-free projects are the stable ones.

## 6. Mechanism: the worker pool, measured three ways

Each period's work is spread over a pool: play handles
(`AudioEngine::renderStageInstruments`, `src/core/AudioEngine.cpp:260`), audio-bus effects
(`renderStageEffects`, `:271`) and mixer channels (`Mixer::masterMix`, `src/core/Mixer.cpp:1414`)
all go through `AudioEngineWorkerThread::startAndWaitForJobs()`, which wakes every worker and
processes the remainder inline. The pool is `QThread::idealThreadCount() - 1`
(`src/core/AudioEngine.cpp:85`) — 19 threads here. Which thread takes which job is decided by the
scheduler at that instant.

Same binary, same project (`DirtyLove.mmpz`), same flags, one render per process:

| configuration | runs | distinct sha256 | bytes |
|---|---|---|---|
| pooled, unpinned (**shipped behaviour**) | 3 | **3** | three different files |
| pooled, `taskset -c 0` (one CPU, 19 workers awake) | 4 | **1** | `fed03af8e71b23ec…` |
| pool disabled (`ZENE_FAKE_NCPU=1` → 0 workers) | 4 | **1** | `fed03af8e71b23ec…` — same file |

Two conclusions, both load-bearing:

1. **It takes genuinely concurrent execution, not just arbitrary ordering.** With 19 workers on
   one CPU the jobs still interleave in an arbitrary order and the output is stable; with 19
   workers on 20 CPUs it is not.
2. **The single-threaded result is well defined** — the no-worker and one-CPU renders are
   byte-identical. That is what makes a fix possible instead of a mitigation.

Checked and cleared, so the next reader does not repeat it: `MixerChannel::m_dependenciesMet` is
`std::atomic_size_t` here and upstream (`include/Mixer.h:152`), so the dependency counter is not
lost-update-racy; denormals are disabled on the engine thread (`src/core/main.cpp:364`) *and* on
every worker (`src/core/AudioEngineWorkerThread.cpp:161`); and each `AudioBusHandle::doProcessing`
and `MixerChannel::doProcessing` writes its own buffer and reads senders in a fixed route order.
**The racy object itself is not identified** — see §10.

## 7. The fix

**`ProjectRenderer::run()` now renders its export on one thread.**

| file | change |
|---|---|
| `include/AudioEngineWorkerThread.h` | `setDeterministicProcessing()` / `deterministicProcessing()`, documented as export-only |
| `src/core/AudioEngineWorkerThread.cpp` | the atomic behind it, plus the `startAndWaitForJobs()` branch that drains the queue on the calling thread without waking the pool |
| `src/core/ProjectRenderer.cpp` | `DeterministicRenderScope` holds the switch for the whole of `run()`, restored on every exit including abort |

Scope: live playback keeps the pool (the switch defaults to `false`; nothing on the audio-device
path sets it). The setting is process-global on purpose — one export runs at a time in this
design, and the alternative is a lock on the audio thread's hot path. No plugin was touched and
no realtime property of the audio callback was changed.

### Before/after proof

Same project, same flags, same machine, unpinned:

| | renders | distinct sha256 | result |
|---|---|---|---|
| pre-fix binary `77b56da0…` | 3 | 3 | three different files |
| pre-fix binary, pool disabled | 4 | 1 | `fed03af8e71b23ec…` (the single-threaded answer) |
| **post-fix binary `67e952c8…`** | **5** | **1** | **`fed03af8e71b23ec…` — the single-threaded answer** |

And the sweep re-run on the fixed binary (`/tmp/det-probe-after/summary.tsv`), 3 renders each:

| project | pre-fix | post-fix |
|---|---|---|
| `Crunk(Demo)`, `sv-DnB-Startup`, `sv-Trance-Startup` | stable | stable |
| `DirtyLove` | 3/3 distinct, 57 % of frames | **stable (3/3 identical)** |
| `Skiessi-222` | 3/3 distinct | **stable** |
| `Surrender-Main` | 3/3 distinct, 11.7 % | **stable** |
| `editing_note_volumes` | 3/3 distinct | **stable** |
| `Root84-TrancyLoop` | 3/3 distinct, 98.3 % | still 3/3 distinct (445 106 frames) — §10 |
| `StrictProduction-DearJonDoe` | 3/3 distinct, 87.6 % | still 3/3 distinct (7 884 145 frames) — §10 |

The change is complete for the render path: `startAndWaitForJobs()` has exactly four callers —
`AudioEngine.cpp:129` (shutdown), `:260`, `:271` and `Mixer.cpp:1414` — all on the render thread,
and nothing else wakes `queueReadyWaitCond`. With the switch on, the pool is never signalled.

### Cost

Per-project render time for `DirtyLove.mmpz` (4 066 048 frames), read from the engine's own
`PerfLog` line, measured under heavy sibling-lane load:

| configuration | wall clock | user CPU |
|---|---|---|
| pooled (pre-fix, in-tree) | 15.4 – 17.8 s | 7.2 – 7.6 s |
| one CPU (pinned, pooled) | 9.2 – 14.7 s | 4.0 – 4.2 s |
| single-threaded (the fix) | 12.5 – 15.7 s | 5.0 – 6.7 s |

Not a slowdown, and *less* total CPU: `JobQueue::wait()` busy-waits
(`src/core/AudioEngineWorkerThread.cpp:99`) and 19 woken threads per stage per period spend CPU
there on a render that is not CPU-bound. Caveat: ranges under load, not a benchmark.

### The test can fail (the red half of the pair)

`tests/src/core/RenderJobQueueTest.cpp`, with only the inline branch in `startAndWaitForJobs()`
removed and everything else identical:

```
$ QT_QPA_PLATFORM=offscreen ./RenderJobQueueTest ; echo EXIT=$?
PASS   : RenderJobQueueTest::initTestCase()
PASS   : RenderJobQueueTest::deterministicProcessingIsOffByDefaultAndResettable()
PASS   : RenderJobQueueTest::inlineModeRunsEveryJobOnceOnTheCallingThread()
FAIL!  : RenderJobQueueTest::inlineModeDrainsAStaticQueueOnTheCallingThread()
         'job->runner == expected' returned FALSE. (a job ran on a thread other than the expected one)
FAIL!  : RenderJobQueueTest::inlineModeNeverLetsThePoolTakeAJob()
         'job->runner == expected' returned FALSE. (a job ran on a thread other than the expected one)
PASS   : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce()
PASS   : RenderJobQueueTest::jobsThatDoNotRequireProcessingAreNotRun()
PASS   : RenderJobQueueTest::cleanupTestCase()
Totals: 6 passed, 2 failed, 0 skipped, 0 blacklisted, 91ms
EXIT=2
```

With the branch restored: `Totals: 8 passed, 0 failed`, `EXIT=0`. The decisive case starts a real
worker thread and hands it real work to steal, so this is red/green rather than a test that
asserts nothing. No render-level assertion is left in ctest on purpose: rendering a bundled
project twice costs about a minute on each of seven CI jobs, and
`tools/render-determinism-probe.sh` reports a project as stable **only** for byte-identical
sha256s — so a future "fix" that puts a tolerance in the comparison changes the probe's verdict
instead of hiding behind it.

## 8. Also in this lane: `TwoTrackRecordingHarness`'s fixed `/tmp` path

`tests/src/core/TwoTrackRecordingHarness.cpp` defaulted its output directory to the fixed
`/tmp/lmms-recording-harness`, and the ctest entry point passes no argument
(`add_test(NAME TwoTrackRecordingHarness COMMAND TwoTrackRecordingHarness)`,
`tests/CMakeLists.txt:64`). Two lanes running their suites at the same time therefore wrote the
same two WAVs and the same `harness-digests.txt`: whichever process is mid-write when the other
reopens the file makes that other run fail a frame-count or digest check that no code change can
explain. A sibling lane hit exactly that and diagnosed it as this path.

Fixed in its own commit: a per-run directory — `TMPDIR`/`TEMP`/`TMP`, or
`std::filesystem::temp_directory_path()` where that is usable (the availability-annotated helpers
are still avoided on Apple, the same reason `create_directories` is not called there) — plus a
monotonic-clock and `std::random_device` suffix. An explicit `argv[1]` is still honoured verbatim
and the digest assertion is untouched.

| | runs | failures |
|---|---|---|
| pre-fix default path, 3 rounds × 8 concurrent (lockstep) | 24 | 0 |
| pre-fix default path, 4 rounds × 12 concurrent (staggered 0–2.5 s) | 48 | **0** |
| post-fix default path, 4 rounds × 12 concurrent (staggered 0–2.5 s) | 48 | 0, and **48 distinct output directories** |

Honest reading: **this lane could not reproduce the spurious failure** in 72 concurrent runs, so
the evidence for the defect is the construction of the race (one shared path, plus the sibling
lane's observation), not a red run of my own. The evidence for the fix is positive and cheap to
check — 48 concurrent runs, 48 distinct directories — and it cannot make the harness weaker.

## 9. The two projects the fix does not cover, and why the fix is not at fault

`Root84-TrancyLoop.mmpz` and `StrictProduction-DearJonDoe.mmp` still render differently under the
fixed binary. Six falsification experiments, all post-fix, all with `Root84` unless noted; each
one above produces *different* files, which is what rules the hypothesis out:

| experiment | result | what it rules out |
|---|---|---|
| pinned to one CPU (`taskset -c 0`) | 2/2 distinct (Root84), 3/3 distinct (StrictProduction) | scheduling / CPU migration |
| ASLR disabled (`setarch $(uname -m) -R`) | 3/3 distinct | address-space layout, pointer-ordered iteration |
| `MALLOC_PERTURB_=42` twice, then `=99` | all distinct | uninitialised heap from `malloc` |
| fixed `rand()`/`srand()` via `LD_PRELOAD` | 2/2 distinct (Root84), 3/3 (StrictProduction) | the C RNG, including SWH `vynil`'s click generator (`plugins/LadspaEffect/swh/ladspa/vynil_1905.c:390,538`, seeded by `srand(getpid() + time(0))` at `src/core/main.cpp:362`) |
| wall clock frozen (`time`/`gettimeofday`/`CLOCK_REALTIME`) | 2/2 distinct | a clock read in the audio path |
| all 12 effect chains bypassed (`on="0"`, `fxchain enabled="0"`) | 2/2 distinct | the effect chains for this project |

Then a **mute bisection** on `Root84` (14 tracks): with only the 8 `AudioFileProcessor` tracks
audible it is **stable**; with only the 3 synth tracks (`lb302` ×2, `kicker` ×1) audible it is
**2/2 distinct**. So for that project the source is in the synths, not in sample playback and not
in the effects.

Candidates with file:line, neither confirmed:

* **`Oscillator::init()`** (`src/core/Oscillator.cpp:302-319`) builds its wavetables in **four
  threads** (`:306-313`), and the FFT thread shares `s_sampleBuffer`/`s_specBuf` while the three
  simple generators write their own tables. Its FFT plans are created with **`FFTW_MEASURE`**
  (`:226-227`), which is documented to choose a plan *by timing* the alternatives — a plan choice
  that is not reproducible run to run, and which every FFT-derived wavetable (`MoogSaw`,
  `Exponential`, and the runtime `generateAntiAliasUserWaveTable` path at `:191-208`) inherits.
  `plugins/Lb302` and `plugins/Kicker/KickerOsc.h` are the two synths involved here; whether they
  reach an FFT-derived shape was not established.
* `StrictProduction-DearJonDoe` is 9 `AudioFileProcessor` tracks and no synths at all, so it
  cannot share that explanation — something in the sample-playback path has its own residual
  variation. Both projects are also the two with the largest track counts.

This lane stops at "not the renderer, not the six things above, and for `Root84` it is the synth
tracks" because the remaining work is a different investigation with a different owner. The
recipe and instruments in §3 reproduce all of it, and the falsification table above is the
starting point.

## 10. Limits — what this does not claim

* **The racy object in the engine is not identified.** The fix removes the concurrency, not the
  race; whatever two concurrently-running jobs share is still there for live playback to trip. It
  has not been observed to affect live playback, but nobody has measured that either.
* **2 of 9 bundled projects are still not reproducible** (§9) — a plugin/synthesis-side
  non-determinism that survives six falsification experiments. A user with a project like those
  still gets different bytes per export.
* **All numbers are this box**: one 20-core Linux machine, the `linux-x86_64` configuration, under
  sibling-lane load. macOS, MSVC, mingw and ARM were not run; the mechanism is
  platform-independent but the numbers there are unknown.
* **The upstream demo library under `data/projects/demos` was only sampled**, not swept — 38
  projects with renders of minutes each (`--all` does it; hours). The routing-graph lane's
  48.5 M/72 M-byte case is the evidence this class is not limited to the nine projects here.
* **Only the 16-bit WAV path was measured.** FLAC/OGG/MP3 export uses the same renderer; float32
  (`-a`) was not measured.
* **The one-frame start-offset jitter is not explained** — it is not what these nine projects do
  (§4), and the fix removes the most likely cause, but no episode was isolated.
* A MIDI-depth lane measured a **residual 1-ulp difference on 1.6 % of samples with the render
  pinned to one CPU**; this lane's pinned pre-fix runs were bit-identical (4/4). One of the two is
  project-specific (their project may carry a §9-class source). Unresolved here; check it first if
  a non-reproducibility report survives this fix.

## 11. Proposed `docs/KNOWN-LIMITATIONS.md` wording

Another lane owns that file, so this is text to apply, not an edit:

> ### Renders are reproducible — with two caveats
>
> Exporting a project to a file is reproducible: two exports of the same project by the same build
> are byte-identical. That was not true before this release. The offline renderer spread each
> period's work over a thread pool whose thread-to-job assignment depends on the machine and the
> moment, and on a project whose processing amplifies a last-bit arithmetic difference — which
> means any project with an effect chain or a synth — two exports differed, sometimes across most
> of the file, with identical loudness. Exports now render on a single thread, which is also no
> slower.
>
> What this does **not** give you:
>
> * **Bit-identity across builds, machines or platforms.** A different compiler, optimisation
>   level, CPU or plugin set changes the last bits, and non-linear effects can turn that into an
>   audible difference. Compare renders from the same build, or with a tolerance that you choose.
> * **Reproducibility for every project.** Some projects contain a component that is itself
>   non-deterministic — a synthesis path or effect that varies from run to run independently of how
>   the render is scheduled. Two of the bundled demo projects are in that class and still differ
>   between exports. If your exports must be identical, check the project rather than assuming.
> * **Reproducible live playback.** Playing the project in the application still uses the audio
>   engine's worker threads and real-time scheduling. It sounds the same in level, but two live
>   passes are not sample-identical and nothing promises they will be.
> * **A fixed seed for anything random in your project.** A plugin that seeds its own noise from
>   the clock or the process will still differ between exports; that is the plugin's behaviour, not
>   the renderer's.
> * **Any guarantee for recordings made from live input.** This covers the offline render only.

## 12. Run log — commands and unpiped exit codes

Baseline (pre-fix source, before any change in this lane):

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0   (build/configure.log)
build EXIT=0       (build/build.log)
ctest EXIT=0       (build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 25
local-ci: overall exit=0
deviation: Qt5 development files not found: added -DWANT_QT6=ON
```

After the fix (full rebuild, then ctest from `build/tests`):

```
$ cmake --build build -j4 ; echo EXIT=$?
EXIT=0                                     # post-fix binary sha256 67e952c8…; the same bytes as the
                                           # earlier post-fix build of the same source (two builds, one hash)
$ cd build/tests && QT_QPA_PLATFORM=offscreen ctest -j2 ; echo EXIT=$?
EXIT=0
100% tests passed, 0 tests failed out of 26  # 26 = the 25 above + RenderJobQueueTest
26/26 Test #16: RenderJobQueueTest ............... Passed    0.36 sec
19/26 Test #23: TwoTrackRecordingHarness ......... Passed    2.18 sec
Total Test time (real) =  30.76 sec
```

`ctest` must be run from `<build>/tests`; the top-level build directory has no
`CTestTestfile.cmake` and reports 0 tests, which is an error here, not a pass.

Sweeps, with their own exit codes (all unpiped):

```
$ bash tools/render-determinism-probe.sh --binary build/lmms --runs 3 --out /tmp/det-probe-before-clean
PROBE-BEFORE EXIT=0        # 27 renders, 0 render failures, binary sha unchanged across the sweep
$ bash tools/render-determinism-probe.sh --binary build/lmms --runs 3 --out /tmp/det-probe-after
PROBE-AFTER  EXIT=0
```

### Gates

`bash tests/run-all-gates.sh` was **not** run in full, and the honest reason is Gate 5: the mutation
gate mutates product sources and rebuilds in the same `build/` directory (and a previous lane found
it can leave a mutant behind), which would have invalidated the sweeps running against that binary.
It should be run on this branch after the sweeps; nothing in this lane's changes is a mutation-gate
target beyond the new test file. The gates that were run, unpiped:

| gate | command | exit |
|---|---|---|
| 1 unit tests | `cmake --build build -j4` + `ctest` from `build/tests` | 0 (26/26) |
| 2 coverage | `tests/coverage-gate.sh` | **not run** (needs `tests/run-coverage.sh`, a separate instrumented build) |
| 3 no tautological tests | `bash tests/no-tautology-gate.sh` | 0 (`PASS`) |
| 4 per-method complexity | `bash tests/complexity-gate.sh --check` | 0 (`PASS`) — after splitting `diff_stats` in the new tool, which the gate caught at CCN 11 |
| 5 mutation | `bash tests/mutation-gate.sh` | **not run** — see above |
| 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | 0 (`PASS: every change to upstream-inherited code … is declared (31 files in the ledger)`) |
| 7 file length | `bash tests/file-length-gate.sh --check` | 0 (`PASS`) |
| 8 duplication | `bash tests/duplication-gate.sh` | 0 (`PASS: duplicated lines 1.08 % (budget 5 %)`) |
| 9 fork-sources registration | `bash tests/fork-sources-gate.sh` | **does not exist on this branch** — it was added by the lane descended from `post-alpha/gate-debt`, and this tree is from `post-alpha/v0.2`. Registration was done anyway (§12 below), because that gate reads the same ledger. |

### Registered in this lane

| file | ledger |
|---|---|
| `include/AudioEngineWorkerThread.h` | `tests/upstream-modifications.txt` (inherited: the export-only switch) |
| `src/core/AudioEngineWorkerThread.cpp` | `tests/upstream-modifications.txt` (inherited: the inline branch) |
| `src/core/ProjectRenderer.cpp` | `tests/upstream-modifications.txt` (inherited: holds the switch for an export) |
| `tests/src/core/RenderJobQueueTest.cpp` | new test (no ledger entry needed under `tests/**`) |
| `tools/ncpu-shim.c` | `tests/fork-sources.txt` (measurement instrument) |
| `tools/render-determinism-compare.py` | `tests/fork-sources.txt` (the instrument this report uses) |
| `tools/render-determinism-probe.sh` | `tests/fork-sources.txt` (the reproduction recipe) |
