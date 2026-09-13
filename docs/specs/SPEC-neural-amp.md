<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-neural-amp.md
    sha256   : a5a86245138673a85efc4373c828d5a6ee684be0d08d2f07de6e2c56146dae10
    bytes    : 5649
    why this file: the "specs/" citation class: cited by 3 places including plugins/NeuralAmp/CMakeLists.txt and plugins/NeuralAmp/tests/rt_alloc_probe.cpp
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: Neural Amp Simulator (NAM / RTNeural)

> **Program:** `lmms-fl-replacement-program` · mission `lmms-ai-dsp-mission` · **phase 2** (RNNoise = phase 1 skeleton)
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** findings-ai-dsp.md (verified license/latency facts), lmms-rnnoise/plugins/RnnoiseDenoiser/ (build precedent + WORKLOG), REPORT.md plan P6
> **Note:** future task — create via `ai_kos_task_create` with `program_slug=lmms-fl-replacement-program`, `project_slug=lmms-ai-dsp-mission` before implementation.

---

## 1. Scope & Goals

Load Neural Amp Modeler `.nam` captures and process guitar in real time as a native LMMS effect. Performance gate adopts the mixer spec's Decision D3 philosophy: **per-model** CPU metric — "<5% single-core per active instance at 48 kHz, measured against the RNNoise plugin as baseline," not per-channel.

Out of scope: capture creation/training (external tooling), amp *deck* UI (full control panels) — v1 is "load capture, play."

## 2. Component Decision

| Criterion | RTNeural | NeuralAudio (NAM core) |
|---|---|---|
| License | MIT | MIT |
| Formats | Dense/GRU/LSTM/Conv1D (its own JSON/jar format) | NAM `.nam` natively (A1/A2/WaveNet architectures) |
| Latency class | Real-time at 48 kHz (findings-ai-dsp.md) | Real-time; A2 ≈322 µs/block estimate |
| Maintenance | Active, generic | Active, NAM-specific |
| Fit | Generic neural DSP (broader future use) | Exact `.nam` compatibility out of the box |

**Decision: NeuralAudio core** — primary, because `.nam` capture compatibility is the product goal and A2 support (33% less CPU than A1, June 2026) is native. **RTNeural = fallback** if NeuralAudio's build integration (CMake, deps) fights LMMS's; both are MIT so no lock-in.

## 3. Plugin Architecture

Follows the RnnoiseDenoiser precedent exactly (lmms-rnnoise/plugins/RnnoiseDenoiser/): Effect subclass + Controls + ControlDialog, CMake via the BUILD_PLUGIN macro pattern, vendored third-party sources in-plugin.

- `NeuralAmpEffect` (PROPOSAL: plugins/NeuralAmp/): owns an inference core instance; processes per-period buffers; block size adapted to model requirement (NAM models define expected block; adapt via internal buffering like RNNoise's 480-frame accumulation — but sized per model)
- **Model loading:** file picker in ControlDialog → validate (header/magic + architecture tag) → load in GUI thread (heavy alloc) → atomically swap pointer into effect. **Graceful "model missing" state** (mission success criterion): plugin instantiates with a silent bypass and a UI notice if no model loads — never crashes
- **Cab/IR handling:** `.nam` captures may embed cab modeling (A2 "standard" includes it) — v1 processes capture as-is; separate IR loader is out of scope

## 4. License Matrix

| Component | License | Vendoring |
|---|---|---|
| NeuralAudio core | MIT | Can vendor (like RNNoise sources) |
| RTNeural (fallback) | MIT | Can vendor |
| `.nam` captures | Varies (per-capture) | **Never vendor** — load at runtime from user-provided files; document a curated-download page with per-capture licenses |

GPLv2 cleanliness: MIT code vendored + runtime-loaded models = clean (same argument as RNNoise, findings-ai-dsp.md).

## 5. Performance Budget

- Metric: per-instance CPU % at 48 kHz (D3-style), measured with `AudioEngineProfiler` in a controlled project
- Expectation from findings: A2 ≈322 µs/block → well under budget on desktop; A1-WaveNet models are the worst case — test with the heaviest public capture
- Budget line: >5% on the reference machine → document as "heavy model" with UI warning, not a blocker

## 6. UI

Knobs: Input gain, Output level, dry/wet. Display: model name, architecture (A1/A2), measured CPU%. Bypass toggle. All via AutomatableModel like RnnoiseDenoiserControls.

## 7. Test Plan

| # | Test | Pass criterion |
|---|---|---|
| T1 | Load a public A2 capture, process 60 s of guitar DI | Non-silent output, no NaNs (waveform inspect) |
| T2 | CPU measurement vs RNNoise baseline | <5% single-core reference |
| T3 | Heaviest public A1/WaveNet capture | Completes, CPU documented |
| T4 | No-model launch / corrupt file | Graceful bypass state, no crash (mission criterion) |
| T5 | Save/reload with model | Model path round-trips; re-loads or graceful-missing |

## 8. Phased Steps (with gates)

1. **G1** — Vendor NeuralAudio; headless CLI loads a `.nam` and renders a WAV. *Gate: output non-silent, matches reference player within tolerance.*
2. **G2** — Effect plugin skeleton (RnnoiseDenoiser pattern) with atomic model swap. *Gate: T1/T4.*
3. **G3** — UI + persistence + CPU instrumentation. *Gate: T2/T3/T5.*
4. **G4** — Upstream-PR-ready: license audit doc + WORKLOG.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Model format churn (A1→A2→future) | NeuralAudio core owns format support; validate + reject unknown versions gracefully |
| Low-end CPU | Per-model CPU display + documented heavy-model warning |
| Capture licenses | Runtime-load only; curated list with license metadata |

## 10. Open Questions

- **OQ-1:** Does NeuralAudio build cleanly against LMMS's CMake (no external heavy deps)? (G1 answers; fallback RTNeural)
- **OQ-2:** Sample-rate adaptation: models trained at 48 k vs project 44.1 k — resample audio or reject non-matching models? (G2 spike)

## 11. Sources

- findings-ai-dsp.md (RTNeural/NeuralAudio/NAM A2 facts, all license-verified)
- lmms-rnnoise/plugins/RnnoiseDenoiser/ (plugin pattern precedent, WORKLOG honest-reporting standard)
- REPORT.md plan P6; mixer/SPEC-dynamic-routing.md Decision D3 (metric philosophy)
