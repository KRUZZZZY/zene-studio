<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-wasm-sandbox.md
    sha256   : cd073153065a65f8352e7616c5a0b33e96add6b2609e4fc683a9b8b99cd46924
    bytes    : 5285
    why this file: the "specs/" citation class: cited by 7 places including modules/wasm/*.wat, plugins/WasmEffect/WasmEffect.cpp and tests/src/wasm/WasmSandboxTest.cpp
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: WASM DSP sandbox v0

> **Task:** AI-KOS task #582 (program `lmms-fl-replacement-program`, mission `lmms-collab-scripting-mission`)
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** `findings-collab-scripting.md` §4.2–4.3 + recommendation 7–8, `REPORT.md` §7.4,
> and parent-verified environment facts (see §8).

---

## 1. Goals & Non-Goals

**Goal:** run user DSP in a memory-isolated sandbox such that a malicious or buggy module **cannot
crash LMMS**, cannot read host memory, and cannot spin forever — with the audio path unaffected.

**Non-goals (v0):** no WASI filesystem/network access, no host-pointer passing, no real-time script
execution on the audio thread, no component model / WIT world beyond the narrow audio ABI, no GUI.

## 2. Runtime decision

| Criterion | wasmtime (C API) | wasm3 |
|---|---|---|
| Footprint | ~2–5 MB runtime | smallest (interpreter) |
| Fuel metering | **first-class** (`wasmtime_fuel_*`) | not built in — needs a custom instruction counter |
| Memory isolation | full, audited | full (interpreter) |
| Toolchain to obtain | prebuilt C-API release archive — **no Rust needed** | C, builds with cmake/gcc |
| WASI | yes (`wasi.h`) | partial |

**Decision: wasmtime C API** — the task's acceptance explicitly requires *fuel metering* to abort a
runaway module, which wasmtime provides directly. Obtain it from the official prebuilt C-API
release archive (verified available; the machine has **no** Rust toolchain). **Fallback:** wasm3 if
the archive cannot be fetched, in which case a watchdog/instruction-counter runaway guard must be
implemented and documented as the substitute for fuel.

## 3. Audio ABI (the narrow contract)

A module exports exactly one entry point and reads/writes only through linear memory:

```
;; host -> module
process(in_ptr: i32, out_ptr: i32, frames: i32, sample_rate: f32) -> i32   ;; 0 = ok
;; module -> host (imports, all pure/no-alloc)
host_get_param(index: i32) -> f32
host_log(ptr: i32, len: i32) -> ()
host_get_transport_state() -> i32   ;; 0 = stopped, 1 = playing
```

- `in_ptr`/`out_ptr` are **offsets into the module's own linear memory** — the host copies audio in
  and out across the boundary; no host pointer ever crosses.
- The module declares its I/O channel count and latency at instantiation via exported globals.
- v0 is **planar, mono or stereo**; multi-channel rides on the Part A/B `AudioPorts` transport later.

## 4. Threading

- Modules are **never executed on the audio thread**. `WasmEffect::processImpl()` enqueues a block
  descriptor onto a pre-allocated SPSC command queue; a dedicated **worker thread** runs the module
  and publishes the result.
- Same invariant class as the Lua API (spec §4): pre-allocated queue, no allocation/locking on the
  audio-thread path, overflow = logged + dropped, never a block.
- If a module misses its deadline, the effect passes audio through dry and counts the miss; it never
  stalls the engine.

## 5. Sandbox

- Linear memory only; the host passes offsets, never pointers.
- **Fuel:** every instantiation gets a fuel budget per `process()` call; exhaustion traps the module
  and surfaces a clear error (the runaway test asserts this).
- No WASI preopens; `wasi.h` is linked but the filesystem/network surfaces are not wired.
- A trap in the module is caught at the embedder boundary — the DAW continues, the effect is marked
  corrupted, and the error is reported to the user.

## 6. Phases & gates

| Phase | Deliverable | Gate |
|---|---|---|
| **G1** | wasmtime C API embedded; a `hello.wasm` module instantiated and called | builds; the call's output is observed |
| **G2** | `WasmEffect : Effect` runs a demo DSP module over a block | a 48-frame block produces the expected output |
| **G3** | fuel metering + crash isolation | infinite-loop module traps; a module that aborts does **not** crash LMMS |
| **G4** | demo module (Rust/C) + docs + size measurement | the demo processes audio with zero host-memory access; runtime size measured |

## 7. Test plan

- Unit: module load/reject, fuel exhaustion, trap handling, param mapping.
- Integration: `WasmEffect` in a headless render producing measurably altered audio.
- Negative: a module that tries to read outside its memory (traps), a module that loops (fuel),
  a module that aborts (isolated).

## 8. Environment reality (parent-verified 2026-09-08)

- **No WASM runtime is installed**: `wasmtime`, `wasm3`, `wasmer` all absent; **no `cargo`/Rust**;
  Python `wasmtime` not installed.
- Therefore the runtime must be **obtained**; keep the dependency **optional** behind
  `find_package`/`FetchContent` so a clean `cmake -B build -DWANT_QT6=ON` still exits 0 without it.
- If the runtime cannot be obtained at all, the exact command output is the acceptable deliverable.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Runtime download unavailable | feature-gate the whole effect; the build must still configure without it |
| Fuel not enough (module does legal but heavy work) | per-call budget + deadline miss → dry pass-through, counted |
| ABI churn | v0 ABI is frozen in §3; additions are new exports, never signature changes |
| Users expect WASI | documented as out of scope for v0; WASI is a v1 decision |
