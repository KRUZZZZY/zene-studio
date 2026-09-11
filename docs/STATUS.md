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
- **Gate 4 (complexity ratchet)** — **one genuine regression**: `LatencyCompensation::processPlanar`
  (CCN 11) — #605 PDC shipped both outside the gates *and* above the CCN 10 target; being refactored
  on `fix/latency-complexity`. Two gate defects found and fixed on 2026-09-11: the baseline was keyed
  by function line span (a *growing* function re-reported as a *new* one), and `--check` exited 0
  unconditionally. 807 functions scanned, 24 over target.
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

- A `main` that builds — the `build` workflow's runs 8-10 (2026-09-10) all failed; runs 11 and 12
  were superseded (cancelled) by the next push before finishing. The fixes for six of the failures
  are now **pushed** as `7f08809e4` (2026-09-11, rebased from `2dcec93b3` on `fix/ci-matrix`), and
  build run #13 is the first run that contains them. `checks` and `doxygen` pass.
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

## Where the documents are

- `README.md` — product-facing summary; its feature list is scoped by the caveats there and by
  this file.
- `tests/QA-GATES.md` — gate definitions, measurements, and the open gate defects.
- `DOCS-NAMING.md` — naming decision and the remaining wave-R rename checklist.
- `docs/phase-f/`, `doc/STEM-SPLIT.md` — **historical program artifacts** carried over from the
  pre-product branch stack; kept for provenance, not product documentation.

This file is the status of record. Update it when a claim above changes, and date the change.
