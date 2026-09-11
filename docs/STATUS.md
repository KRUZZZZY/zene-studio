# Zene Studio — status

**Verified 2026-09-11** against `main` @ `b61e14c75` for the measurements, and `4f1acd5e6` (the
docs commit that carries this text) for the tree; counts that advance with history are given at
both. Every line is traceable: code facts
cite a path, measurements cite the command and the date they were taken, and CI state cites
`gh run list --repo KRUZZZZY/zene-studio`. Where this file and a measurement disagree, the
measurement wins — re-run it. Gate definitions and their numbers live in
[`tests/QA-GATES.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/tests/QA-GATES.md).

## Have (in `main`, with tests)

| Area | State | Where |
|---|---|---|
| Multi-channel port model | `AudioBus`, `AudioPorts`, `AudioPortsModel`; unbounded mixer | `src/core/AudioBus.cpp`, `include/AudioPorts.h` |
| Routing DAG | `RoutingNode`/`RoutingGraph` present and unit-tested — **but nothing in the app instantiates them** (see Disarmed) | `src/core/RoutingGraph.cpp`, `tests/src/core/RoutingGraphTest.cpp` |
| Plugin delay compensation | Landed #605 (2026-09-10): chain/graph latency + summing-point alignment; zero-latency graphs bit-identical | `src/core/LatencyCompensation.cpp`, `src/core/Mixer.cpp`; `tests/src/core/PdcMixerTest.cpp` |
| VST3 hosting | **Effects only** | `plugins/Vst3Effect/` |
| CLAP hosting | **Effects only**; legacy single-buffer bridge routed through `AudioPortsRouter` (#607) | `plugins/ClapEffect/` |
| Recording | Two-track capture **prototype**; lock-free SPSC ring buffer | `src/core/audio/MultiTrackRecorder.cpp`, `include/RecordRingBuffer.h` |
| AI DSP | RNNoise denoiser, NAM neural amp, offline HTDemucs stem separation | `plugins/RnnoiseDenoiser/`, `plugins/NeuralAmp/`, `src/core/OnnxRuntimeStemSeparator.cpp` |
| Scripting | Lua 5.4 with instruction budget; wasmtime DSP sandbox | `src/core/ScriptEngine.cpp`, `src/wasm/`, `plugins/WasmEffect/` |
| Editing / UI | Slide notes; HiDPI scaling | `tests/src/core/SlideNotesTest.cpp`, `include/DpiHelper.h` |
| Tooling | `mmpz-git` (filters, semantic diff, merge driver); 8-gate suite incl. mutation harness | `tools/mmpz-git/`, `tests/` |
| Part C migration | 90/93 plugin files across 43 dirs; 40 plugins proven sample-exact (program measurement, 2026-09-09) | `projects/lmms-fl-research/PART-C-MIGRATION.md` |
| Inherited from LMMS | Piano Roll, Song Editor, Beat/Bassline, mixer, 15+ synths, SF2, VST2 (Vestige), LADSPA, LV2, MIDI I/O | — |

## Started, not landed

- **Session View data layer** — `SessionModel` / `ClipSlot` / `Scene`, versioned
  `<session version="1">` XML behind `WANT_SESSION_VIEW` (default **OFF**). Branch
  `feat/session-view-model`, PR #5 / task #594, 1677 insertions across 15 files. **Not on
  `main`; no UI.**
- **Coverage** — 47 of the 97 in-scope files measured on 2026-09-09: **76.07%**
  (2661/3498 lines), below the adopted 85% aspiration. `plugins/NeuralAmp` (158 lines) and
  `plugins/RnnoiseDenoiser` (57 lines) sit at **0% with no registered tests**;
  `src/gui/PinConnector.cpp` (269 lines) is at 0% with an evidenced exclusion.
- **PDC scope** — `include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp`
  shipped on 2026-09-10 **outside `tests/fork-sources.txt`**, so no gate watched them. Added
  to the scope file on 2026-09-11; no coverage run has been taken on the new scope yet.

## Have, but disarmed

- **`RoutingGraph`** — tested, but no GUI or audio-path code includes it.
- **Quality gates** — `.github/workflows/quality-gates.yml` is **dispatch-only**: nothing runs
  on push or PR, so a green check here proves nothing about gate state. Last dispatch run
  (#2, `main`) **failed**.
- **Gate 6 (upstream divergence)** — **PASSES since 2026-09-11, under a rewritten rule**: divergence
  in inherited code is allowed when it is *declared* with a reason in `tests/upstream-modifications.txt`
  (the divergence ledger) and is a violation when it is not. The ledger declares 10 files (#605 PDC
  plus two compile-only CI fixes); a blank reason is refused with exit 2. The old blanket ban was red
  from the first behavioural change — i.e. not a gate — and outlawed exactly the mixer work PDC needs.
- **Gate 2 (coverage ratchet)** — no entry floor: a zero-line file is banked at 100.00%, and a
  new file enters the baseline at its measured coverage, including 0%.
- **Gate 4 (complexity ratchet)** — **green since 2026-09-11**: `LatencyCompensation::processPlanar`
  (CCN 11, #605 PDC) was refactored to CCN 10 in `d9d5deee2` — the duplicated ring wrap-around read
  became the private helper `readWrapped()`, verified by a Debug build plus 24/24 `ctest`. Two gate
  defects were fixed the same day: the baseline was keyed by function line span (a *growing* function
  re-reported as a *new* one) and `--check` exited 0 unconditionally. 808 functions scanned, 23 over
  target, all grandfathered.
- **Gate 7 (file-length ratchet)** — **green after a recorded re-anchor** (2026-09-11): two
  grandfathered files had grown (`ScriptBindings.cpp` 1216 → 1217, `ScriptEngine.cpp` 908 → 910)
  before the ratchet could fail anywhere; `--reanchor "reason"` is now the only way to move a
  baseline. 8 files over 500 lines.
- **Gate 5 (mutation)** — scoped to one translation unit, `src/core/RoutingGraph.cpp` — a file
  the application never calls.
- **Patcher** — the node-graph engine is in the tree and unit-tested, but no GUI or audio-path
  code instantiates it, so patching is not available in the build.

## Don't have

**Blocking**

- A `main` that builds — **as of run #21 (2026-09-11) 6 of 7 jobs are green** (linux-x86_64,
  linux-arm64, macos-arm64, macos-x86_64, mingw64, windows-arm64); msvc-x64 is the last one running.
  The road there: runs 8-10 failed; runs 11-13 were cancelled by the per-ref concurrency group;
  run #14 was the first to contain the earlier fixes and all seven still failed, for four causes
  read from its job logs (a Qt-only link in `synthetic_audio_plugin`, a bare `__attribute__` under
  MSVC, and two vendored-code warnings — RNNoise, Eigen — promoted by `-DUSE_WERROR`). Fixed, then
  #19 went 6/7 with msvc-x64 failing on a *third* vendored RNNoise diagnostic, which is why the
  vendored C now builds as its own `SYSTEM`-flagged target rather than one suppression at a time.
  Read the live state from `gh run list --repo KRUZZZZY/zene-studio` rather than from this file.
- Local reproduction of the CI matrix.
- Any installable release (no GitHub releases exist).

**Core DAW** — clip editing (trim, slip, fades, crossfades, clip gain); take lanes, comping,
punch in/out; arbitrary input count and input monitoring; instrument hosting (VST3/CLAP are
effects only); out-of-process plugin hosting; plugin scanning/caching/blacklisting; multicore
graph scheduling; automation modes and sample-accurate automation; LUFS metering, freeze,
bounce-in-place; stem export.

**Workflow** — warp engine; Session View UI, clip launch, scenes, follow actions; racks,
chains, macros; browser with audition and drag-and-drop; MIDI learn, controller surfaces,
Ableton Link; groove pool, scale awareness, note probability.

**Polish** — modern stock devices; factory content; design system; autosave recovery; crash
reporter.

## Feature backlog — everything still to be added

Assembled 2026-09-11 from this file's own gap audit plus the board (`lmms-complete-daw-program`
program). `[#N]` = already boarded as a task; a bare item = no task yet. Statuses are the board's.

**Committed roadmap (boarded)**
- Session View: launch scheduler + quantisation, audio-thread safe `[#595]` · Follow Actions +
  Arrangement Record `[#596]` · grid UI (slot launch, scene column, drag-drop) `[#598]` · and the
  clip/scene data layer itself is in progress on PR #5 `[#594]`
- Warp engine — clip tempo independence (markers, modes, tempo leader/follower) `[#597]`
- Racks — parallel chains, Chain Selector, macros, key/velocity zones `[#599]`
- Comping — parallel take lanes + non-destructive composite `[#600]`
- MPE — per-note pitch/slide/pressure capture, storage, editing `[#601]`
- Modulation layer — relative modulator envelopes + LFO/Shaper/EnvFollower devices `[#602]`
- Ableton Link — tempo/phase sync, then Link Audio transport `[#603]`
- Browser — tag/metadata search + ML sound-similarity search `[#604]`
- Auto-mastering wave 1 — candidate-variant mastering with objective gates `[#610]`
- Engineering, not features: green build matrix `[#609]`, AudioBuffer SharedMemory pointer defect `[#608]`

**Documented gaps with no task yet**
- Audio editing: clip trim, slip, fades, crossfades, clip gain
- Recording depth: punch in/out, arbitrary input count, input monitoring
- Plugin hosting: **instrument** hosting (VST3/CLAP are effects-only), out-of-process hosting,
  plugin scanning / caching / blacklisting
- Engine: multicore graph scheduling, automation modes, sample-accurate automation
- Metering & rendering: LUFS metering, freeze, bounce-in-place, stem export
- Workflow: browser audition + drag-and-drop, MIDI learn, controller surfaces, groove pool,
  scale awareness, note probability
- Polish: modern stock devices, factory content, design system, autosave recovery, crash reporter
- Auto-mastering waves 2–3 (reference-matching arm; a learned ranker only if pick-logs justify it)

**Blocking the first release**
- An installable release (none exists) and local reproduction of the CI matrix
- Part C's last 4 plugin dirs (Vestige, ZynAddSubFx, VstBase, VstEffect) `[#589, blocked]` — needs an
  owner decision on the remote-process `process()` rewrite

**Built but disarmed — the feature exists only when wired**
- Patcher: `RoutingGraph` is in the tree and unit-tested, but no GUI or audio-path code instantiates
  it, so patching is unavailable in the build
- Quality gates run `workflow_dispatch`-only, so nothing enforces them on push or PR
- Gate 2 has no coverage floor; Gate 5's mutation sweep covers one translation unit

**Explicit non-goals** (from `ableton-gap/SPEC-zene-studio.md` §6 — deliberately not on any list):
`.als` import · M4L/`.amxd` runtime · Live device presets · Push/Move hardware · video import/export ·
surround panning · MIDI Tools transformations (v2 candidate) · cloud services. Freeze/bounce-in-place
is named there as a later-wave roadmap item rather than a non-goal.

### Review pass — 2026-09-11 (after the feature-list critique)

**Sequencing defect, now fixed.** Five boarded features depended on work that had no task: comping needs
take lanes, which need clip editing, punch in/out, arbitrary input count and input monitoring; warp needs a
non-destructive clip model to attach markers to; racks needs `RoutingGraph` instantiated in the audio path
(still disarmed, below); the Session View grid's drag-drop needs the browser's drag-drop half, which stayed
unboarded while the browser task boarded only tag search and ML similarity. Boarded the fix as **#611
clip-and-capture wave (xl, yellow)** — it lands *before* #597, #598 and #600. The four editing/recording
items listed as gaps above are therefore owned by #611, not gaps.

**Differentiators, now boarded.** They had no board presence at all while auto-mastering was added ahead of
them, despite being cheaper and being the things the commercial DAWs will never build: **#612** mmpz-git
depth (three-way merge of concurrent track edits, musical conflict presentation, large assets, audible-diff
CLI, CI render recipes) · **#613** Lua API stabilisation (versioning + compatibility policy, script-defined
devices, console, package format, generated docs) · **#614** documented WASM effect ABI for third parties
(conformance suite, an example built from the docs alone, distribution).

**#608 recategorised.** It is not "engineering beside the build matrix": an `AudioBuffer` over
`SharedMemoryResource` carries a cross-process pointer table, so its failure mode is memory corruption in
the audio path. Risk raised mid → high; it outranks feature work.

**Verification debt — this list under-counted it.** Also missing:
- `run-all-gates.sh` treats SKIP as pass (`record()` only fails on `FAIL`), so a skipped gate is invisible
- nothing says plainly "this new source is not in `tests/fork-sources.txt`": Gate 6 surfaced
  `LatencyCompensation.{h,cpp}` only as an *undeclared upstream change* — the wrong diagnosis for a file
  that is this repo's own code (they were registered on 2026-09-11; 99 files in scope). A gate that names
  the omission would have caught it in minutes instead of a day
- NeuralAmp and RnnoiseDenoiser at 0.00% with no registered test file
- Gate 6's window covers 11 commits of the 133 the base names (an older "5 of 133" figure is stale)
- `coverage-gate.sh` banks a zero-instrumented-line file as 100% — no entry floor

**Product-side gaps also missing from the group above**: waveform rendering + peak cache; undo depth and
drag coalescing; plugin state save/restore; tempo automation and time-signature changes; MIDI clock/MTC;
dithering and sample-rate-conversion quality; recording crash recovery; real-time-safety verification;
golden-audio integration tests; soak testing; keyboard navigation and accessibility.

**Two claims in the critique that did not survive checking**: `LatencyCompensation.{h,cpp}` *are* in
`tests/fork-sources.txt` (registered 2026-09-11), and Part C is **90/93** in three documents here
(`PROGRAM-STATUS.md` ×2, `ableton-gap/SPEC-zene-studio.md`) — the 89/93 figure is unreconciled and needs a
recount before either number is quoted.

## Where the documents are

- `README.md` — product-facing summary; its feature list is scoped by the caveats there and by
  this file.
- `tests/QA-GATES.md` — gate definitions, measurements, and the open gate defects.
- `DOCS-NAMING.md` — naming decision and the remaining wave-R rename checklist.
- `docs/phase-f/`, `doc/STEM-SPLIT.md` — **historical program artifacts** carried over from the
  pre-product branch stack; kept for provenance, not product documentation.

This file is the status of record. Update it when a claim above changes, and date the change.
