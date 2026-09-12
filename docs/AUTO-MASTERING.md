# Auto-mastering, wave 1 — render once, branch many, measure every candidate

Lane: `post-alpha/auto-mastering` (worktree `zene-pa-mastering`), based on
`post-alpha/integration`. Task **#610**, "Auto-mastering wave 1: candidate-variant mastering
(objective gates + render-once/branch-many)". The product's own feasibility study
(`knowledge/bundles/general/auto-mastering-candidate-variants-feasibility.md`) is the brief this
implements and is quoted where it decides a question.

**Verdict.** Wave 1 is landed and driven end to end: one project render feeds five mastering
candidates, every candidate is scored with the merged BS.1770-4 meter (LUFS-I, short-term max,
true peak) against a named, cited target, and the candidate set is measurably distinct. **No
candidate is ranked, scored for preference, or called best** — there is no validated preference
scorer (the feasibility study's own finding: FAD/CLAP rank-correlate 0.14 against 0.62 for human
judgement, and cross-domain transfer of objective metrics was demonstrated for 1 of 13 metrics),
so the honest shape is N measured candidates and a user's choice.

The five numbers that carry the claim, all measured (not asserted):

| claim | measured |
|---|---|
| five candidates cost **one** project render | `project_renders=1 counted_renders=1` — counted inside `ProjectRenderer::run()` |
| every candidate hits its named target | worst loudness residual 0.10 LU, all within tolerance |
| every candidate respects its ceiling | `dBTP` = −1.00 / −2.00 dBTP exactly where the limiter engages |
| different settings ⇒ different measurements | every pair differs by ≥ 0.34 LU/dB on at least one metric |
| identical settings ⇒ identical output | control pair: `max|Δ| 0 LSB`, `ΔLUFS-I 0.000` |
| an unreachable target is reported as a warn | probe at −6 LUFS / −20 dBTP lands at −23.5 LUFS, verdict `warn` |

---

## 1. What landed

| File | What it is |
|---|---|
| `include/MasteringChain.h` / `src/core/MasteringChain.cpp` | the offline chain (measured loudness gain → optional dynamics → limiter → measured true-peak trim → bounded loudness correction) and the BS.1770-4 measurement it reports through (`MasteringMetrics`) |
| `include/MasteringJob.h` / `src/core/MasteringJob.cpp` | render-once/branch-many: one `ProjectRenderer` render, N in-memory branches, N files, N metric rows |
| `src/core/main.cpp` | a third render action, `zene master`, beside `render`/`rendertracks` |
| `include/ProjectRenderer.h` / `src/core/ProjectRenderer.cpp` | the render counter (`renderCount()`), which is what makes the render-once claim checkable |
| `tests/src/core/MasteringTest.cpp` + `MasteringTestSupport.h` | the whole path, end to end, on a real `.mmp` |
| `tools/auto-mastering-demo.py` | fixture generator, candidate checker, and an **independent** numpy BS.1770-4 implementation to check the printed numbers against |

### CLI

```
zene master <project> -o <dir> [-f wav] [-s samplerate] [-a]
```

`-o` is required (a candidate set is several files; scattering them next to the project is never
what the user meant). Wave 1 writes **wav only** and says so rather than pretending otherwise. It
prints one row per candidate — LUFS-I, short-term max, dBTP, the verdict against that candidate's
target — plus the source render's own row, the number of renders, and the sentence that no
candidate is preferred.

### The chain, and why each step is there

1. **Gain to target** — measured on the signal (`LufsMeter`), not predicted from its peak. A gain
   derived from anything else is a guess; the source render's own LUFS-I is the only honest basis.
2. **Optional dynamics** — a stereo-linked feed-forward compressor whose threshold is in dBFS of the
   *level-normalised* signal (the makeup gain runs first), so it engages on any material rather than
   on material that happens to sit at an absolute level.
3. **Second measured gain to the same target** (only when dynamics ran) — so a dynamics candidate
   differs from its sibling in *how* it reaches the target, not in where it lands.
4. **Limiter at the ceiling, then a measured true-peak trim** — inter-sample peaks survive a
   sample-domain limiter, so the ceiling is enforced against the *measured* true peak. The 4×
   interpolator is linear, so one measured trim is exact.
5. **Bounded correction loop** (up to 5 passes, damping 0.7, total ±12 dB) — limiting removes energy
   the first gain cannot predict, so the chain re-measures and adds only part of the residual it
   still sees. Every pass *ends* with the limiter and the trim, so the ceiling holds after the last
   operation whatever the last gain did. Where a target cannot be reached at that ceiling on that
   material, the loop stops short and **the residual is what the report shows** — it is never
   quietly declared reached.

### Threading (the realtime rule)

| entry point | thread |
|---|---|
| `zene master` dispatch, `MasteringJob::run()`, `MasteringChain::process()` | the CLI thread. Blocking, allocates, does floating-point work proportional to the signal |
| `RenderManager::renderProject()`, `ProjectRenderer::run()` | `ProjectRenderer`'s own `QThread` (unchanged); `MasteringJob` drives it through its own `QEventLoop` instead of quitting the application |
| `AudioEngine::renderNextPeriod()` (the audio callback) | **untouched by this lane.** No mastering code is reachable from it; the chain is offline-only and the render counter is a plain atomic increment |

---

## 2. The render-once proof (counted, not asserted)

`ProjectRenderer::run()` increments a process-wide atomic counter — a two-line instrumentation
addition with no effect on what a render produces. `MasteringJob` records the counter before and
after its one render, and `MasteringTest::oneRenderFeedsEveryCandidate` reports both:

```
MASTERING_EVIDENCE candidates=5 project_renders=1 counted_renders=1
MASTERING_EVIDENCE source (one render)      LUFS-I   -21.27  ST-max   -21.03  dBTP    -7.31  crest  13.96
```

Five candidates, one render, and the count comes from the render machinery rather than from the
job's own bookkeeping. The same file prints from the CLI:

```
Auto-mastering: 5 candidates from 1 project render
one render: /tmp/masterdemo/candidates/00_source-mix.wav
```

The single render is kept beside the candidates as `00_source-mix.wav` (32-bit float, an
intermediate, not a deliverable) so "the render happened once" is a file a reader can look at.

---

## 3. The candidate set, and the numbers it produces

Fixture: `tools/auto-mastering-demo.py make` writes a three-track `.mmp` (three sample tracks, two
bars at 120 bpm, transient-heavy so the limiter has real work). Project length 6 s; 264 448 frames
at 44.1 kHz.

```
candidate                  target       LUFS-I   ST-max     dBTP  verdict
source (no mastering)      -            -21.28   -21.03    -7.31  -
streaming-14               streaming-14   -14.10   -13.85    -1.00  pass
streaming-16               streaming-16   -16.00   -15.75    -2.03  pass
streaming-14-ceiling-2     streaming-14-ceiling-2   -14.09   -13.87    -2.00  pass
streaming-16-dynamics      streaming-16   -16.04   -15.82    -1.00  pass
ebu-r128                   ebu-r128     -23.00   -22.75    -9.03  pass
```

`zene master` exit code 0. The `ctest` run reports the same numbers from the same fixture shape
(§5), and the independent implementation below agrees with them (§4).

### The distinction test, with the numbers

Every pair of candidates whose settings differ must differ in a measurement; a candidate set that
measures identically would be a bug, not a feature. Measured pairs (LU = LUFS-I, ST = short-term
max):

| pair | ΔLUFS-I | ΔST-max | ΔdBTP |
|---|---|---|---|
| streaming-14 vs streaming-16 | 1.90 | 1.90 | 1.04 |
| streaming-14 vs streaming-14-ceiling-2 | 0.00 | 0.01 | **1.00** |
| streaming-14 vs streaming-16-dynamics | 1.94 | 1.94 | 0.00 |
| streaming-14 vs ebu-r128 | 8.90 | 8.90 | 8.04 |
| streaming-16 vs streaming-14-ceiling-2 | 1.91 | 1.88 | 0.04 |
| streaming-16 vs streaming-16-dynamics | 0.04 | 0.04 | **1.04** |
| streaming-16 vs ebu-r128 | 7.00 | 7.00 | 7.00 |
| streaming-14-ceiling-2 vs streaming-16-dynamics | 1.94 | 1.93 | 1.00 |
| streaming-14-ceiling-2 vs ebu-r128 | 8.91 | 8.88 | 7.04 |
| streaming-16-dynamics vs ebu-r128 | 6.96 | 6.96 | 8.04 |

Every pair clears the gate (≥ 0.25 of the metric's unit; the meter's own tolerance is ~0.1 LU).
Tightest margins are the true-peak axis: the ceiling is where two same-target candidates separate,
by exactly the ceiling difference (1.00 dB) where the limiter engages.

**The control.** Two candidates with identical settings, same run: `max|delta| 0 LSB`,
`ΔLUFS-I 0.000`, `ΔdBTP 0.000`. Identical settings produce identical bytes — which is what says the
differences above come from the settings and not from the render.

### A measured finding, and a candidate set changed because of it

The first version of the set had a `streaming-14-dynamics` candidate (the −14 target with the
dynamics stage on). Measured against its own sibling: **ΔLUFS-I 0.00, ΔST-max 0.02, ΔdBTP 0.00** —
indistinguishable. At −14 LUFS this fixture's peaks sit on the limiter, the limiter decides the
output, and a glue compressor in front of it changes nothing the meter can see: the compressor's
loudness reduction is undone by the re-normalisation and the limiter re-imposes the peaks.

That is not a bug to hide, it is a measurement, and it says what the dynamics stage can and cannot
do at a given target on given material. The candidate moved to the −16 LUFS target, where the
limiter stays out of the way, and there it *is* visible: `ΔdBTP 1.04` against its identical-target
sibling (crest 15.04 vs 13.96 — with a 10 ms attack the transients pass while the sustained bed is
compressed and then re-levelled, so the master uses more of the ceiling at the same loudness). The
old behaviour and the reason for the change are recorded in the code comment beside the candidate
list, not only here.

---

## 4. The targets, and why those

| target | integrated | tolerance | ceiling | source |
|---|---|---|---|---|
| `streaming-14` | −14 LUFS-I | ±1.0 LU | −1 dBTP | −14 LUFS is the figure Spotify documents for normalisation; −1 dBTP is the ceiling its delivery guidance gives. **No service publishes a tolerance**, so this target states ±1.0 LU as the lane's choice and says so in the code and in the row it prints |
| `streaming-16` | −16 LUFS-I | ±1.0 LU | −1 dBTP | a second streaming point, exactly 2 LU from `streaming-14`. Its job is to make target loudness a *dimension* of the candidate set; it is labelled as such rather than attributed to a service |
| `streaming-14-ceiling-2` | −14 LUFS-I | ±1.0 LU | −2 dBTP | a lane-chosen variant that moves only the ceiling, so the ceiling is visible as an axis. The −2 dB headroom figure is this lane's, not a service's, and is labelled so |
| `ebu-r128` | −23 LUFS-I | **±0.5 LU** | −1 dBTP | EBU R 128 programme loudness, measured per ITU-R BS.1770-4. Both the target and the tolerance are the published ones |

Two standards, one of them (EBU R 128) with a published tolerance that is used verbatim; the
streaming figure is a service convention, not a standard, and is labelled as a convention wherever
it appears. Nothing was invented and no target is claimed to be "the right" master loudness.

**Verdicts** (per candidate, printed): `loudness` = achieved LUFS-I inside the target's tolerance;
`true-peak` = measured dBTP ≤ ceiling (+0.05 dB float slack, not a tolerance on the standard);
`st-flag` = the loudest 3 s window more than 5 LU above the programme target — **explicitly a
lane-defined level-consistency flag, never a verdict**, because no standard publishes a short-term
limit for music. Where a target is unreachable, the loudness verdict is a warn:

```
MASTERING_EVIDENCE unreachable probe: target -6.0, LUFS-I -23.51, residual -17.51, lufs warn
```

### Independent measurement (the strongest check in this lane)

`tools/auto-mastering-demo.py check --lufs` measures every output with a **second** BS.1770-4
implementation — numpy/scipy, the recommendation's own K-weighting prototype parameters and its
Annex 2 true-peak coefficients, exact block list (no histogram), sharing no code with the C++ meter
or with the chain:

| file | C++ meter LUFS-I | numpy LUFS-I | Δ | C++ dBTP | numpy dBTP | Δ |
|---|---|---|---|---|---|---|
| `00_source-mix.wav` | −21.28 | −21.33 | 0.05 | −7.31 | −7.31 | 0.00 |
| `01_streaming-14.wav` | −14.10 | −14.14 | 0.04 | −1.00 | −1.00 | 0.00 |
| `02_streaming-16.wav` | −16.00 | −16.05 | 0.05 | −2.03 | −2.03 | 0.00 |
| `03_streaming-14-ceiling-2.wav` | −14.09 | −14.14 | 0.05 | −2.00 | −2.00 | 0.00 |
| `04_streaming-16-dynamics.wav` | −16.04 | −16.08 | 0.04 | −1.00 | −1.00 | 0.00 |
| `05_ebu-r128.wav` | −23.00 | −23.05 | 0.05 | −9.03 | −9.03 | 0.00 |

Every file agrees to ≤ 0.05 LU and ≤ 0.01 dBTP — inside the meter's own documented deviation (the
0.05 LU relative-gate quantisation in `docs/LUFS-METER.md`), and on a *render*, not on a
synthesized vector, so it also says the measurement survives the render path and the 16-bit output.

---

## 5. Determinism: what it can honestly mean here

**The audio is not bit-reproducible, and this lane did not change that.** Renders in this tree are
not reproducible run to run — independently confirmed three times already, upstream-inherited, with
the mechanism traced to `ProjectRenderer`'s render-start offset jitter at period boundaries. This
lane's own measurements reproduce that:

* **in-process** (one live process, three ordinary renders of the same project): run-to-run floor
  `max|Δ| 11 525 LSB` (0.022 dB) in one run, `0 LSB` in another — the same class of jitter the stem
  lane measured at up to 26 204 LSB;
* **cross-process** (`zene render` three times, fresh processes, one render each): `max|Δ| 0 LSB`,
  i.e. byte-identical in this run.

So the honest statements are:

1. **The measurements are deterministic.** `candidatesWithIdenticalSettingsMeasureIdentically`
   processes the same source with the same settings and gets `max|Δ| 0 LSB` and `ΔLUFS-I 0.000`;
   the chain contains no RNG, no time input, no thread-dependent order, and the meter's readings are
   a pure function of the samples fed. Two runs of `zene master` on the same project agree to the
   meter's tolerance (≤ 0.05 LU), and the residual scatter you see between runs comes from the
   *render*, not from the chain.
2. **The candidate set is deterministic.** The same project and the same candidate list produce the
   same candidate *settings* and the same file set, on every run.
3. **The audio is not bit-reproducible**, so no claim of byte-equality between two runs of a master
   may be made — and the render-once design *neutralises* the effect within one run: all N
   candidates branch off one render, so the render's jitter cannot move one candidate's metrics
   relative to a sibling's. That is a real benefit of the design and the reason the distinction
   test is stable even though the render is not.
4. **The meter's own tolerance is the floor for every claim here.** Tolerances tighter than ~0.1 LU
   (for loudness) or ~0.1 dB (for true peak) are below what the meter can distinguish and are not
   claimed.

---

## 6. Behaviour preservation — an ordinary render is unchanged

Rule: never sha256; use max |Δ| in LSB/dB against a same-build run-to-run floor, with a control.

**Cross-process** (`tools/auto-mastering-demo.py compare`, 16-bit renders of the same project):

```
plain-a.wav vs plain-b.wav: common frames 264448, max|delta| 0 LSB (-inf dB), level delta 0.0000 dB
plain-a.wav vs plain-c.wav: common frames 264448, max|delta| 0 LSB (-inf dB), level delta 0.0000 dB
```

`plain-a`/`plain-b` are ordinary `zene render` runs; `plain-c` runs *after* a `zene master` on the
same project in the same directory. All three are byte-identical in this run and all three are the
same length (264 448 frames), so the mastering path leaves the ordinary render exactly as it was.

**In-process** (`MasteringTest::masteringLeavesAnOrdinaryRenderAlone`), where the renderer's own
jitter is larger:

```
MASTERING_EVIDENCE ordinary render: frames 264448, floor max|delta| 11525 LSB (0.0277 dB), after-mastering max|delta| 0 LSB (0.0000 dB)
```

Three ordinary renders around a mastering job: the same frame count, the after-mastering delta
*inside* the run-to-run floor, and both level deltas ≤ 0.5 dB (the test's gate, printed with the
numbers). The test also asserts the frame count directly — a state leak that changed the export
length or level would fail it.

**Sensitivity control (the rule that makes the gate meaningful):** the plain renders are compared
*against each other* first. If the floor is 0 LSB, a 0 LSB after-delta proves nothing on its own;
that is why the in-process gate is stated against the *measured* floor of the same build in the
same run and the numbers are printed rather than a PASS/FAIL. A failure mode this catches: any
mastering state that leaked into the export length, the audio device, or the mixer would move the
level by ≥ 3 dB, not by 0.03 dB.

---

## 7. Run log — unpiped exit codes

```bash
cd projects/lmms-fl-research/zene-pa-mastering
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```

```
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 32

deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
local-ci: overall exit=0 (0 = every executed step passed)
```

`build/tests/ctest.log` includes `MasteringTest` (8 slots over 6 cases, 0 failed) beside the 31
pre-existing tests; ctest prints a passing test's output only with `-V`, so the `MASTERING_EVIDENCE`
lines in this document come from running `./MasteringTest -v1` directly from `build/tests`.

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ./MasteringTest -v1
Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 13361ms
```

### Static gates

| gate | command | exit | result |
|---|---|---|---|
| 9 — fork-sources registration | `bash tests/fork-sources-gate.sh` | **0** | PASS; 1120 tracked sources, 132 fork-NEW, 996 inherited, 0 stale |
| 6 — upstream divergence | `bash tests/no-upstream-regression-gate.sh` | **0** | PASS; every change to inherited code declared (88 files in the ledger) |
| all | `bash tests/run-all-gates.sh` | **3** | PASS-WITH-SKIPS: gate 2 (coverage) not run — it needs `--with-coverage`, a whole-tree instrumented rebuild this box's disk and the concurrent lanes make expensive. 8 of 9 gates ran and passed |

Gate 4 (complexity) was red on the first run — three functions in `tools/auto-mastering-demo.py`
were over CCN 10 (`read_wav` 15, `check` 15, `integrated_lufs` 11). They were split into named
helpers (chunk parsing, block loudness, relative gate, the per-file table, the level-order check)
and the gate is green with no baseline entry for any of them.

### Touch list and ledgers

* **Fork-new (registered in `tests/fork-sources.txt`):** `include/MasteringChain.h`,
  `include/MasteringJob.h`, `src/core/MasteringChain.cpp`, `src/core/MasteringJob.cpp`,
  `tests/src/core/MasteringTest.cpp`, `tests/src/core/MasteringTestSupport.h`,
  `tools/auto-mastering-demo.py`.
* **Upstream-inherited, changed (declared in `tests/upstream-modifications.txt` with its reason):**
  `include/ProjectRenderer.h`, `src/core/ProjectRenderer.cpp` (the render counter),
  `src/core/main.cpp` (the `master` action, its help text, the `-o` guard, the report printer, and
  the rename of `scriptExitCode` → `headlessExitCode` because the variable now carries this action's
  exit code too). `src/core/CMakeLists.txt` and `tests/CMakeLists.txt` are build config and are
  allowed by the gate without a ledger entry.
* **No new third-party dependency.** The chain and the job are Qt plus `libsndfile`, which the tree
  already links for every render. The tool's numpy/scipy are *developer* dependencies of one
  script, not of the product.

---

## 8. What is NOT done

* **No preference scorer, no ranking, no "best".** By construction and by the feasibility study's
  finding. The API exposes `reports()` — measurements — and nothing that orders them. A ranker is a
  later wave and is gated on real user pick-logs, which do not exist yet.
* **No pick-log.** The feasibility study's step 5 ("candidate UI: level-matched A/B + visible
  metrics + commit winner + log the choice") needs a UI and a decision record; wave 1 delivers the
  measurement half only.
* **No reference-matching arm.** Matching a candidate to a reference track (the one route the study
  calls "solved by construction — taste outsourced to the user's reference") is not implemented.
  Neither is a Matchering sidecar nor an FFmpeg `loudnorm` QC export.
* **No GUI beyond reachability.** `zene master` is the CLI; there is no Export-dialog mode, no
  candidate list panel, no A/B player. The measurement engine and the job manager are what a UI
  would sit on.
* **No level-matched A/B.** The candidates differ in loudness by design (that is the target axis),
  and choosing between them without level matching is the loudness confound the study warns about.
  Nothing in this lane presents them as comparable-by-ear.
* **wav only.** `-f flac|ogg|mp3` is refused with a message rather than half-implemented.
* **No per-candidate parallel processing.** Candidates are processed serially on the CLI thread.
  The render is what is expensive and that already happens once.
* **Not measured: cost.** No benchmark of `MasteringChain::process()` was run, and no claim about
  its speed is made. The renders in this lane are tiny (6 s of audio, 0.05–0.2 s per render).
* **The fixture is not music.** Three synthesized tracks (a sustained 55 Hz tone, two decaying
  burst trains) give the chain material with a realistic *crest factor* to work on, but nothing here
  says how the chain behaves on a real mix, and no listening test of any kind was performed — by
  construction, since no quality claim is being made.
* **The dynamics stage's effect is material- and target-dependent** and this lane says so with
  numbers (§3) instead of claiming a general behaviour.

### Proposed wording for `docs/KNOWN-LIMITATIONS.md` (another lane owns that file)

> **Auto-mastering (wave 1)** — `zene master` renders the mix once and writes N measured
> candidates. It does not rank them and does not claim a best: no validated preference scorer
> exists for master variants of one song. Candidate loudness/true-peak verdicts are against named,
> cited targets (EBU R 128 with its published ±0.5 LU; a −14 LUFS streaming *convention* with a
> tolerance this project chose and states). Renders in this tree are not bit-reproducible run to
> run, so two runs of the same master are equal only to the meter's tolerance (≤ 0.05 LU / 0.01 dB),
> never byte-for-byte; within one run all candidates branch off the same render, so their metrics
> are comparable with each other. `zene master` writes wav only.

---

## 9. Reproduction

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-mastering
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4          # configure + build + ctest

# the suite, with its evidence lines
cd build/tests && QT_QPA_PLATFORM=offscreen ./MasteringTest -v1

# end to end, on a generated project: project in, 5 mastered outputs out
python3 tools/auto-mastering-demo.py make /tmp/masterdemo
cd /tmp/masterdemo
$BUILD/zene master demo.mmp -o candidates -f wav -s 44100          # prints the metrics table
$BUILD/zene render demo.mmp -o plain-a.wav -f wav -s 44100
python3 <repo>/tools/auto-mastering-demo.py check candidates --lufs  # + the independent BS.1770-4
python3 <repo>/tools/auto-mastering-demo.py compare plain-a.wav plain-c.wav
```

`00_source-mix.wav` in the candidate directory is the single project render, kept as evidence.
