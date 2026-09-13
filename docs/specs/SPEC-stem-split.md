<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-stem-split.md
    sha256   : 930492bd8551fff0dc4ab9e3bb9e2f3d238830ac6ec4f668b727893438403f87
    bytes    : 5689
    why this file: the "specs/" citation class: cited by include/StemSeparation/StemTypes.h and src/core/ExternalProcessStemSeparator.cpp
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: HTDemucs Offline Stem Split

> **Program:** `lmms-fl-replacement-program` · mission `lmms-ai-dsp-mission` · **phase 3**
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** findings-ai-dsp.md (verified model facts), REPORT.md plan P6, clone 4e677cb verification
> **Note:** future task — create via `ai_kos_task_create` before implementation. Real-time stem separation is EXPLICITLY OUT OF SCOPE (HTDemucs hybrid transformer requires ~7.8 s segment lookahead — findings-ai-dsp.md).

---

## 1. Scope & Goals

Right-click an audio (SampleClip) → "Split to stems" → background job runs HTDemucs via ONNX Runtime → 4 stems (drums/bass/vocals/other) land as new clips on new tracks. Explicitly **offline/background**; progress + cancel.

## 2. Current State in Code (verified vs clone 4e677cb)

| Component | Where | Relevance |
|---|---|---|
| Clip class | include/SampleClip.h:45 `class SampleClip : public Clip` | The target of the context action (REPORT.md "AudioClip" is stale naming) |
| Buffer attach | include/SampleClip.h:83 `setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)` | How stem clips get their audio |
| Buffer class | include/SampleBuffer.h | Immutable-shared buffer design → stems are new SampleBuffers, never mutate source |
| Decode path | src/core/SampleDecoder.cpp:30 (sndfile) | Feeds source audio to the job |
| No ONNX anywhere | CMakeLists.txt has no ONNX option (only WANT_WEAKJACK etc. at :86) | ONNX Runtime is a NEW dependency — this spec must define its integration policy |

## 3. Architecture

### 3.1 Background job manager (PROPOSAL: include/JobManager.h)
- Fixed worker pool (size = hardware concurrency − 1, min 1), MPSC job queue (pre-allocated), job = {source clip ref, model ref, project rate, cancel flag}
- Progress via atomic float + Qt signal marshaled to GUI thread; cancel = atomic flag checked between segment batches
- Jobs are project-scoped; saving/quitting with active job → prompt (v1: block save until finish or cancel)

### 3.2 ONNX Runtime integration
- **Dependency policy:** system ORT via pkg-config when available; vendored fallback build flag `WANT_STEM_SPLIT=ON` (default OFF upstream — heavy dep, opt-in like WANT_VST). Rationale: LMMS's CMake pattern (OPTION entries) supports this cleanly (CMakeLists.txt:86 precedent)
- Model file: HTDemucs ONNX, **fp16 default (166 MB)**; fp32 (316 MB) as quality option. **Optional download at first use** (mission rule: models never bundled) with SHA-256 checksum pinning per model version
- Inference: segments of HTDemucs-native length with overlap-add; source resampled to **44.1 kHz** (model native) via a resampler (libsamplerate or the existing resampling in SampleBuffer — verify at G1), stems resampled back to project rate

### 3.3 Output placement
4 stems → 4 new `SampleClip`s on a new muted-by-source track group placed adjacent to the source clip at identical TimePos (alignment is inherent — segments are time-indexed).

## 4. CPU / Memory Budget

| Item | Value (from findings-ai-dsp.md, verify at G1) |
|---|---|
| Model size | 166 MB fp16 / 316 MB fp32 |
| RAM during inference | ~1.5-2 GB working set (fp16, 3-min song) — document 8 GB floor |
| Wall time | RTF ~0.2 on Apple-class CPU; on typical Linux desktop assume 1-3× song duration — measure in G2 and document |
| ORT binary | +10-20 MB linked — reason for opt-in flag |

## 5. UX Flow

Progress dialog (per-stem progress + cancel). Partial results: stems land as they complete. Error states (mission graceful-degradation criterion): model missing → download prompt; ORT absent → feature hidden (build without WANT_STEM_SPLIT); OOM → clear error, no crash; cancel → partial stems kept with notice.

## 6. Test Plan

| # | Test | Pass criterion |
|---|---|---|
| T1 | 3-min song → 4 stems | 4 clips, each non-silent, sample-length within tolerance of source |
| T2 | Time alignment | Stem clip start == source start (sample-exact) |
| T3 | Cancel mid-job | Partial stems valid; no leak (valgrind/ASan) |
| T4 | Model checksum mismatch | Rejected with clear error |
| T5 | Project save/reload after split | Stems persist (SampleBuffers serialized) |

## 7. Phased Steps (with gates)

1. **G1** — Standalone CLI: ORT + HTDemucs ONNX splits a WAV on this machine. *Gate: T1 at CLI level; real wall-time measured.*
2. **G2** — JobManager + clip placement in-tree (behind WANT_STEM_SPLIT). *Gate: T1/T2 in-app.*
3. **G3** — UX: progress/cancel/download/checksums. *Gate: T3/T4.*
4. **G4** — Persistence + docs. *Gate: T5; license audit (ORT MIT, model MIT — document).*

## 8. Risks

| Risk | Mitigation |
|---|---|
| ORT binary bloat / license friction | Opt-in build flag; MIT is clean; document size |
| 8 GB machines OOM | fp16 default; document floor; error path tested |
| Model download trust | SHA-256 pinning + HTTPS-only + versioned URLs |
| Wall time surprises on desktop CPUs | G1 measures before UX promises; UI shows estimate from RTF |

## 9. Open Questions

- **OQ-1:** Which resampler (libsamplerate vs in-tree SampleBuffer path) — quality/perf decision at G1
- **OQ-2:** ORT threading inside LMMS worker pool — intra-op threads vs our pool ownership (avoid oversubscription; G2 decides)
- **OQ-3:** GPU/NPU path (DirectML/CoreML/CUDA) — defer entirely; CPU-only v1

## 10. Sources

- findings-ai-dsp.md (model sizes, RTF, 7.8 s lookahead, licenses — all verified)
- Clone 4e677cb: include/SampleClip.h:45,83; include/SampleBuffer.h; src/core/SampleDecoder.cpp:30; CMakeLists.txt:86 (OPTION precedent)
- REPORT.md plan P6; mixer spec §5 realtime invariants (job pool never touches audio thread)
