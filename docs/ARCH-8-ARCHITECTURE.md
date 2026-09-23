# ARCH-8 — Zene Studio architecture write-up (closing the 0.4.0 architecture wave)

The architecture of the tree as it stands at the three-stream acceptance
(`576fd8c68`), written for whoever builds the next wave. The named decisions
are the owner's (D12–D17, recorded in `plans/MASTER-PLAN.md` §3).

## 1. Layer map

```
UI (B-desk)         FocusDesk / FocusDeskActions / FocusDeskPane / Workspaces
                    | dispatches registry-first (`mod:` seam), availability IS the
                    v register — never "hidden because the menu says so"
Control surface     ControlRegistry — 350 ids / 56 groups at the final tip
                    ProjectJournal: every mutating verb = one checkpoint; A16
                    transactions (true_inverse bookkeeping) + reversibility tables
                    (Action / Passive / SessionView families)
                    |
Engine              Song / Track / Clip / TakeLane / SessionModel
                    AudioEngine: 2 render stages + AudioEngineWorkerThread job
                    queue; play-handle lifecycle (see §3)
                    RoutingGraph + the multicore scheduling frontier (Slice 0+1)
DSP / instruments   InstrumentTrack (+ SessionTuning: MTS-ESP + Scala),
                    SampleBuffer ops (#706), ScriptClock (#708), Feedback
                    submode (#709), Mixer (unbounded channels), plugins
Document model      ARCH-4: ONE counter (`ProjectIds`), revisions keep-3 +
                    `.bak` + git:<sha> slots, `z:provenance` journal (S6),
                    top-level namespaced sections: `<z:lanes>` (S7),
                    `<z:scenes>` (S8), `<session>` = the upconverter (S8/S10)
```

## 2. The document model's invariants (ARCH-4, the wave's core)

1. **New state lives in top-level namespaced sections** — never as a child of
   an existing element (an older build materialises an unrecognised `<track>`
   child as a phantom Clip — RISK 1; the negative-control tests in
   `LanesUpconvertTest`/`ScenesUpconvertTest` are the tripwires).
2. **Preservation before claiming** (S1): an unrecognised top-level element is
   preserved verbatim and re-emitted (`UnclaimedElements` +
   `Song::restoreNamedSection`'s claim-or-preserve branch); a section is claimed
   only in the exact shape the writer produces.
3. **Byte identity for non-users**: a project that uses none of lanes/scenes/
   warp re-saves byte-identically (the additive rule). Instance/build/journal/
   layout metadata is excluded from content equality by construction
   (`writer`, `creatorversion`, `<z:provenance>`, GUI view sections, window
   geometry — each exclusion measured before it was made).
4. **One counter, one `next-id`** (`ProjectIds`): load placeholders, kept-id
   reporting (`noteLoadAssignment`), document floors (`observe` at id-write —
   including engine-born mixer channel/vcagroup ids, the churn bug S7's claim-3
   caught), and save-time `next()` — the invariant "a file never carries an id
   at/above its own next-id".
5. **Upconverters are readers**: `<session>` (legacy) → `<z:scenes>`+lanes
   (S8); journal/checkpoint forms ride along unchanged (S7's 4-slot container
   form stays in place).

## 3. Play-handle lifecycle (the wave's hardest bug)

`AudioEngine::addPlayHandle` stages a handle in `m_newPlayHandles`; the render
merges it into `m_playHandles`. REMOVAL must therefore sweep both lists — the
staging sweep (in-place, head-safe via `setFirst`, note-handled only: a staged
`InstrumentPlayHandle` is owned by its instrument's create/delete bookkeeping).
`~InstrumentTrack` → `silenceAllNotes(true)` is the teardown entry. This seam
was a use-after-free (a staged note rendering through its freed track) — fixed
and proved 8/8 in `SessionTuningTest`; the same class is behind stale-handle
crashes on repeated `project.open` (the owner's legacy-load crashes).

## 4. The verification architecture

Eleven text gates (file-length/complexity/duplication ratchets, fork-sources G9,
upstream-regression G6 with `tests/upstream-modifications.txt` rows extended in
place, evidence-class G11, unregistered-tests, mutation, coverage, honesty,
release gates), the legacy corpus (`verification/arch4-s0-gate.py` + the 42
fixtures), registered proofs per feature (the scope contract's four parts),
`commands_snapshot.json` REGEN-ONCE from a live instance + its drift check,
and the A16/transaction honesty dump. Measured constants move only by re-
measurement with a recorded act (`docs/709-logs/709-baseline-rotate.md` is the
worked example, twice).

## 5. Decisions and frontiers

- D12–D15: the release ladder (0.3.0 engine-and-agent → 0.4.0 architectural
  remainder → UI last; 0.5.0 scrapped; 0.6.0 retired).
- D16: ARCH-5 keep-and-own (GATE-3 kept).
- D17: ARCH-7 licence-first-then-verdict — Tracktion/JUCE8: **DO NOT ADOPT**
  (GPLv2-2.0+ incompatible without a commercial licence).
- Frontiers for the next wave: multicore S2+ (schedule → execution), the
  RT-safety rules (no allocation/locking/unbounded growth on audio-thread
  paths — the allocation-counter test pattern is the gold standard), the
  destructive-editor slices beyond #706's slice 1, and the DAW-as-clock MTC
  surface (`clock.*` exists; wire the hardware paths).
