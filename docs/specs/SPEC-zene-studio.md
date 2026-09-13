<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, ableton-gap/SPEC-zene-studio.md
    sha256   : fabc0ae8bc97e1cd0257f6cd2a2b7201b0291aed69e13d02f9dba626e18474e7
    bytes    : 16725
    why this file: DOC-5 names it; cited by 9 product source files (A11-A16 are the control surface contract) and by tests/agent-surface-negative-control.md and tests/agent-surface-gate.py
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: Zene Studio — Complete DAW

> **Program:** `lmms-complete-daw-program` · **Repo:** `KRUZZZZY/zene-studio` (product; repo renamed 2026-09-09 — code/branding strings remain wave R)
> **Status:** Approved scope · **Version:** 1.0 · **Written:** 2026-09-09
> **Sources:** `ableton-gap/SCOPE-VERDICTS.md` (owner verdict: all 8 paradigms IN), `ableton-gap/B-PARADIGMS.md` (Live 12.4.5 paradigm truth, cited), `ableton-gap/A-INVENTORY.md` (42-chapter manual inventory), `REPORT.md` §3 P1–P7, `INTEGRATION.md` §5–6 (base tree provenance)
> **Product name:** **Zene Studio** (owner decision 2026-09-09). "Zene" = music; self-explanatory, easily searchable. The naming rationale lives in the KB doc (`lmms-fork-name-candidates`) only — not repeated elsewhere.

---

## 1. Mission

Ship **Zene Studio**: a complete DAW that keeps LMMS's engine strengths (multi-channel ports
architecture, VST3 + CLAP hosting, Patcher graph, Lua/WASM scripting, AI DSP) and adds the
eight structural paradigms that make Ableton Live *Live*:

1. Session View / clip launcher
2. Warp engine (time-stretch as a clip property)
3. Racks + Chain Selector + macros
4. Comping / take lanes
5. MPE
6. Modulation as a layer (relative modulators, no takeover)
7. Ableton Link / Link Audio
8. Tag / sound-similarity browser

**It is a derived product, not a Live clone and not an upstream patch set.** Explicitly out of
scope: `.als` import, Max for Live / `.amxd` runtime, Live device presets, Push/Move hardware
support. The goal is capability parity, verified by the milestones in PLAN.md.

## 2. Base and provenance

| Item | Value | Evidence |
|---|---|---|
| Base commit | `integration/all-verified` @ `416fb186b` → now product `main` `01148947e` | remote `main` tree hash equals locally built+tested tree (`58492409…`); ctest 17/17 |
| Already in base | Part A/B/C multi-channel ports (90/93 plugins migrated, 45 proven sample-exact), Part D sidechain, VST3 (13/13) + CLAP (13/13), Lua 21/21, WASM 9/9, RNNoise, NAM, stem split, slide notes, HiDPI, git-friendly .mmpz, recording prototype | INTEGRATION.md §2.2–6, PROGRAM-STATUS.md |
| Build system | CMake, `PROJECT(lmms)` @ CMakeLists.txt:23 — **rename to `zene` pending (wave R)** | verified in clone |
| Licence | GPL-2.0-or-later retained, upstream notices kept | SCOPE-VERDICTS §5 |

## 3. Architecture decisions (binding)

| # | Decision | Rationale |
|---|---|---|
| A1 | **Clip model is new, not grafted onto `PatternClip`.** `SessionClip` (audio or MIDI) with its own loop region, launch state, and follow-action state. Session and Arrangement clips are distinct types on the same track; mutual exclusivity enforced at track level (Live's model). | Live's paradigm requires clips independent of timeline position; `PatternClip` semantics are timeline-bound |
| A2 | **Session transport = separate clock domain.** Session runs its own play position per playing clip, quantised by a global launch quantiser (bar/beat/none). No global timeline position during session playback. | enabling condition for Legato mode and scene sync; matches Live §7 |
| A3 | **Follow Actions evaluated by a Scheduler on the UI-side engine clock**, delivered to the audio path via the existing lock-free command-queue pattern (same invariant class as the Lua and mixer specs). Zero allocation in the audio callback. | realtime rule (AGENTS.md rule 4); Live's Follow Actions are quantise-and-fire, not sample-critical |
| A4 | **Warp engine = property of `AudioClip`, not of SampleBuffer.** Warp markers stored per clip; playback reads via a resampling/warping provider. Beats mode = transient-preserving granular (librubberband `Rolling`/transient map or self-built), Complex = Rubberband Pro-equivalent settings; Re-Pitch = varispeed. | B-PARADIGMS §2: modes are granular techniques; Rubberband is GPLv2-clean and already vetted in P1 research |
| A5 | **Racks are a new track-internal container** (`Rack` holding `Chain[]`), each chain a full device chain with key/velocity/chain-select zone filters. Macros = `FloatModel` aliases mapped to any AutomatableModel with Min/Max ranges. Reuses Patcher's DAG where chains are parallel. | Live racks §24: unlimited nested chains; our AudioPorts + Patcher infrastructure makes parallel chains cheap |
| A6 | **MPE = per-note event plumbing extension.** `Note` gains optional per-note pitch/slide/pressure streams (list of (offset, value) ramps), captured from MPE MIDI (MIDI 2.0 / MPE 1.0 note expression), stored in project XML as optional attributes/elements (backward compatible, slide-notes precedent). | builds on the shipped slide-note serialization pattern; verified backward-compat approach |
| A7 | **Modulation layer = relative sum at the model level.** Modulators output into a per-parameter modulation bus; final value = automation value + Σ modulations. No parameter takeover (Live 12 behaviour). Implemented as an `AutomatableModel` extension, not a per-device hack. | B-PARADIGMS §5: additive, performance-safe |
| A8 | **Link via libableton-link (official SDK)** — GPLv2+ licensed, compatible. Link session in a background thread; tempo/phase propagated into the engine clock at period boundaries. Link Audio (later) via the SDK's audio peers. | SDK is first-party, licence-clean, battle-tested |
| A9 | **Browser = metadata index (SQLite) + tag store + similarity vectors.** Similarity: local embeddings (CLAP/OpenVINO-free — reuse the bge-style encoder approach or a small ONNX audio encoder), stored per sample, cosine search. Models optional downloads (licence hygiene). | parity without cloud; reuses this machine's proven ONNX Runtime stack from stem-split |
| A10 | **Product naming:** repo renamed to `KRUZZZZY/zene-studio` 2026-09-09; code/project strings stay `lmms` until wave R. Wave R = CMake `project(zene)`, `PROJECT_AUTHOR/URL/DESCRIPTION`, desktop file, window titles, `--version` string, README. **The upstream-PR fork was deleted 2026-09-09** (its refs preserved in the product repo; trademark discipline: product must not ship as LMMS). | owner instruction 2026-09-09; licence discussion (standalone name is the zero-friction move) |

## 4. Paradigm specs (v0 slices, per SCOPE-VERDICTS)

### 4.1 Session View / clip launcher (W1)
- Grid UI: tracks = columns, scenes = rows; clip slots with distinct empty/filled rendering.
- Launch modes: **Trigger, Gate, Toggle, Repeat**; per-clip + global launch quantisation (None/1/2/4 bars).
- **Legato mode**: incoming clip inherits outgoing clip's play position.
- Scenes: launch button fires the row; per-scene tempo + time signature applied on launch; Capture-and-Insert-Scene.
- **Follow Actions** (10 types: No Action, Stop, Play Again, Previous, Next, First, Last, Any, Other, Jump) with Chance A/B weighting, Linked/Unlinked timing.
- **Arrangement Record**: session performance (launches, moves) recorded into the Arrangement as clips/automation; Back-to-Arrangement switch.
- Per-clip: loop region, gain, transpose/detune, RAM mode.
- Non-goals v0: Follow Actions on scenes, groove/persistence beyond above.

### 4.2 Warp engine (W2)
- Per-clip Warp switch + mode: **Beats, Tones, Texture, Re-Pitch, Complex** (Complex Pro-quality stretch acceptable via Rubberband quality settings).
- **Warp markers** (pin audio time ↔ timeline time), auto-transient detection for Beats, auto-warp long files.
- Clip tempo leader/follower: a leader clip drives project tempo.
- Slice-to-MIDI (cuts to a playable mapped instrument) — v1; v0 = warp + markers + leader.
- Non-goals: groove extraction, sample-file-embedded markers.

### 4.3 Racks + Chain Selector + macros (W3)
- `Rack` container per track: unlimited **parallel chains**, nesting supported.
- Zones per chain: **key zone, velocity zone, chain-select zone** (0–127 with crossfade ranges).
- **8 macros** (v0; 16 v1): map any AutomatableModel, Min/Max + inverted ranges.
- Drum Rack-style pad container: v1.
- Non-goals: macro variations, rack presets format (v1).

### 4.4 Comping / take lanes (W4)
- Parallel **take lanes** under a track; recording creates lanes automatically.
- **Region-level composite**: comp per region across lanes; audition per lane; main lane audible.
- Works for audio and MIDI (MIDI comp = clip-region selection).
- Non-goals: lane management polish (Delete-unused etc. v1).

### 4.5 MPE (W5a)
- Capture per-note **pitch, slide, pressure** (+ release velocity) from MPE controllers.
- Storage in project XML (backward compatible), editing in a per-note expression view, playback through per-note event path.
- Tuning systems: out of scope (upstream already has microtonality #5522).

### 4.6 Modulation as a layer (W5b)
- **Relative modulator envelopes** (volume/pan/sends/device params) layered over absolute automation.
- Modulator devices: **LFO, Shaper, Envelope Follower** in Modulation mode (base value stays adjustable — no takeover).
- Clip-level modulation envelopes: v1.

### 4.7 Link / Link Audio (W6)
- **Ableton Link**: tempo/phase sync across peers, join/leave at any time, transport via Start-Stop Sync.
- **Link Audio**: real-time audio streaming between peers (v1 within W6).
- Non-goals: Push/Move hardware, Tempo Follower.

### 4.8 Browser tags + similarity search (W7)
- Sidebar browser: Collections (colour labels), tags, filtered search, browser history.
- **Sound Similarity Search**: local ML embedding per sample; "find sounds like this".
- Auto-tagging for short samples: v1.
- Non-goals: cloud/Splice integration.

## 5. Cross-cutting requirements

- **Realtime safety:** no allocation, no locking, no unbounded growth on the audio thread (allocation-counter tests are the gold standard — copy the existing pattern from Part D/WASM lanes).
- **Behaviour-preservation:** existing 17 ctest suites must keep passing at every wave boundary; the integration tree is never broken.
- **Serialization:** every paradigm saves to XML with backward-compatible optional elements; old projects load unchanged and re-save byte-identically (slide-note precedent, `.mmpz` round-trip tests 15/15 as the harness pattern).
  - **Sharpened 2026-09-12** (a read-only design pass over the serializer challenged the wording, and it is right): the byte-identity promise holds for **a file written by this build** — a project that arrives from an OLDER version may legitimately differ on its first re-save, because `DataFile::upgrade()` (`src/core/DataFile.cpp:2087-2110`, triggered by `m_fileVersion < UPGRADE_METHODS.size()` at `:2205`) rewrites the root element. So the testable form of the promise is: *save → load → save is byte-identical; load(older) → save is content-preserving and the second save is then stable*. Compare with the repository's canonicaliser (`tools/mmpz-git`) rather than raw bytes when the question is "what changed musically". **Measured 2026-09-12 (see `SPEC-stable-ids.md`): two saves in the same session are byte-identical, but `save -> load -> save` differs at byte ~60 in root-attribute ORDER (the Qt6 hash-seeded churn `GIT-FRIENDLY-MMPZ.md` §7 recorded), so "byte-identical" must always mean "identical after canonicalisation", and a test that asserts raw bytes across a load will fail for a reason that has nothing to do with the change under test.** Two measurements behind the wording live in `GIT-FRIENDLY-MMPZ.md` §5/§7 (38/38 byte-identical **after canonicalisation**; two Qt6 saves of the same project differing at byte 61 raw) — **not re-verified here**, because the lane that produced them (`lmms-gitmmpz`) is not in this workspace, so the raw-byte half is UNVERIFIED in the current tree.
- **Licence:** each new dependency licence-audited before adoption (libableton-link GPLv2+, Rubberband GPLv2+, ONNX models as optional downloads). No GPL-3-only code. Upstream notices retained.
- **Testing:** every wave lands with (a) unit tests, (b) a milestone demo script in `tests/` proving the M# milestone from PLAN.md, (c) a `ctest` run from `build/tests` (0-tests = error, never a pass).
- **Versioning and release discipline (added 2026-09-11):** the product number, the control-socket protocol number and the project-format number are three separate versions, each with its own bump rule, in `VERSIONING.md` (SemVer 2.0.0 adapted pre-1.0: MINOR when capability is added, PATCH for fixes only, MAJOR reserved for the v1.0 format-stability promise). The next release is **0.2.0** — the agent control surface. The reported string must come from the tag and `CMakeLists.txt` must agree with it.
- **Agent operability (binding, added 2026-09-11):** every capability a user can drive must also be drivable by an agent, and that is part of the wave's Definition of Done — not a follow-up. Each user-facing action must (a) be reachable headless, with no display and no audio device; (b) be a registered command with a stable ID and a JSON schema; (c) have a test that drives it through the agent surface rather than through internal APIs. **A wave that ships user-facing capability without its command surface is not done.** Contract, per-wave command table and acceptance gates: `AGENT-TOOLING.md`; binding decisions in §7.

## 6. Explicit non-goals (whole program)

`.als` import · M4L/`.amxd` runtime · Live device presets · Push/Move hardware · video import/export · surround panning · freeze/bounce-in-place (roadmap items, later waves) · MIDI Tools transformations/generators (v2 candidate) · cloud services.

## 7. Agent operability — the AI-first control surface (binding)

The owner operates this product primarily through agents. A DAW whose features are reachable only by
mouse is half a product here, so the command surface is part of each feature, not an add-on.

| # | Decision | Rationale |
|---|---|---|
| A11 | **One action, one implementation.** Every user-facing action is a registered command (`zene.command`) with a stable ID, a JSON schema and a handler queued onto the UI thread. Menus, keyboard shortcuts, Lua bindings and the agent surface all call the same registry entry. | kills the drift class where a feature exists in the menu but not for an agent |
| A12 | **Transport is line-delimited JSON-RPC over a local UNIX socket**, opt-in by CLI flag/env, with a protocol-version handshake. The MCP server (`mcp-zene-control`) is a thin bridge **outside** the app; the DAW does not embed an MCP server. | keeps MCP lifecycle, JSON tooling and Python/Node dependencies out of the audio application; the bridge is replaceable and versionable on its own |
| A13 | **Headless parity.** Every command runs with no display and no audio device; a command that genuinely needs one declares `requires: {display\|device\|human}` and returns a typed refusal instead of failing obscurely. | an agent must be able to exercise the whole surface in CI |
| A14 | **Read-back and audio truth.** Every command family exposes `*.get_state` returning the same structure serialization uses, and the render/export family is available headlessly — so an agent verifies its own edits against both model state and rendered audio. | a state change is a claim; the render is the evidence |
| A15 | **Anti-drift gate.** An `agent_surface` ctest asserts, by reflection, that every registered menu/toolbar action has a command ID and that every declared command is reachable headless; a new action without a command fails the build. | makes the obligation mechanical rather than aspirational |
| A16 | **Every command is reversible. Destructive commands get no confirmation gate — they get an undo.** (Owner decision 2026-09-11: "don't have destructive commands need an explicit confirm, but find a way to make all commands reversible.") The registry records a transaction per mutating command — before-state plus the inverse operation — so `control.undo` / `control.redo` reverse it, and the same transaction backs the GUI's own undo history so an agent's edit and a user's edit are one history. File-level operations keep the previous revision (`project.save` over an existing file leaves a recoverable prior state); where a true inverse genuinely does not exist, the fallback is a bounded project snapshot and it is documented per command rather than assumed. | an agent driving a DAW without an undo is a data-loss machine; a confirmation prompt is a speed bump, an inverse is a guarantee. Design work for the general contract is boarded (task #623) |

Per-wave requirement: each wave in `PLAN-zene-studio.md` adds its own command group alongside its
paradigm (W1 `session.*`, W2 `warp.*`, W3 `rack.*`, W4 `comp.*`, W5 `note.expression.*` /
`modulator.*`, W6 `link.*`, W7 `browser.*`). The authoritative per-feature table, the current
gap list and the build ladder are in `AGENT-TOOLING.md`.
