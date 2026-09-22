# ARCH-7 Stage 2 — written verdict: Tracktion Engine evaluation

Board card `plans/BACKLOG.md:1565-1572` (D17, `plans/MASTER-PLAN.md:211`). Stage 1 found a
legal path, so stage 2 runs: the technical verdict against the plan's own criteria (plan §10
ARCH-7, `…/inbox/zene-change-plan-2026-09-12-extracted.txt:311-314`; intake §2.8 and §4.3,
`git show 02885a1:NEXT-CHANGE-PLAN-INTAKE.md`). Lane `040/arch7-eval`, tip `ab9462d56`,
build-free lane — no configure/build/ctest was run (rule: this lane costs no disk).
Licence findings are in `docs/reports/ARCH-7-LICENCE-ANSWER.md`.

> **Verdict: DO NOT ADOPE Tracktion Engine as Zene's engine.** The evaluation is closed
> with this written verdict. Three independent grounds, each measured below: (1) the
> licence path that exists still puts **AGPLv3 into a GPL-2.0-or-later product** against
> this program's recorded precedent, and the *commercial* tiers do not combine with a GPL
> conveyance at all; (2) the plan's premise — Tracktion "already implements … most of the
> roadmap" — is **materially stale**: session view, comping, modulation, PDC and the
> routing graph are already in this tree in some state; (3) adoption would be a **replace,
> not a coexist**, and it contradicts the plan's own ARCH-1 ("staged replacement, not a
> rewrite"), which is the premise of ARCH-2..8.

## 1. What the plan asks of it, checked against the tree

Plan §10 ARCH-7: *"It already implements clips, a clip launcher, time-stretch, racks,
modifiers, and take comping, which is most of the roadmap. The trade is losing the LMMS
instruments and file compatibility, and depending on the JUCE licence stack, which changed
in JUCE 8."*

| plan claim | Tracktion side (fetched 2026-09-22) | Zene tree side (measured at `ab9462d56`) |
|---|---|---|
| clip launcher | in `FEATURES.md`: clip launching, scenes, follow actions, record-to-slot | session view **already in the tree** — `docs/FEATURE-LIST-0.3.0.md:98` (row 1, "partial": model + launch engine proved 6/11 ids; Follow Actions/Arrangement Record missing) behind `CMakeLists.txt:133` `WANT_SESSION_VIEW … ON` |
| take comping | not named as such in `FEATURES.md` (its audio list has no comping entry; comping is a Waveform-product claim) | **in the tree** — `docs/FEATURE-LIST-0.3.0.md:100` (row 3, `TakeLaneCompTest`, byte-identity proofs; stated limit: no playback path yet) |
| racks | `FEATURES.md`: "Rack patching environment for multi-track plugin buses" | racks exist in-tree (`docs/RACKS.md`, 428 lines; rack graph measured in FEATURE-LIST row 28's routing-graph row) |
| modifiers | `FEATURES.md`: LFO, envelope follower, breakpoint, step, random, MIDI tracker | modulation layer **in the tree** — `docs/FEATURE-LIST-0.3.0.md:126` (row 7, `ModulationLayerTest` proves 0 allocations over 64 blocks; LFO-source only, applied per block) |
| time-stretch | `FEATURES.md`: Elastique*/Rubber-band*/SoundTouch, *"Requires external licence"* | WSOLA pitch-preserving stretch **in the tree** — `docs/KNOWN-LIMITATIONS.md:1236`: drivable, not formant-preserving (renders 0.4992/0.2990 vs octave-up default) |
| (plan does not claim) PDC | `FEATURES.md`: "Perfect plugin delay compensation" | **in the tree** — `docs/FEATURE-LIST-0.3.0.md:184` (row 27, `PdcMixerTest`, `pdc.report` surface) |

**Symptom, not culprit:** the plan's "most of the roadmap" sentence was written against a
tree state that has since moved; five of its six named capabilities now have in-tree
counterparts with registered proofs. The *marginal* capability Tracktion would buy is
chiefly mature follow-actions/arrangement-record, formant-preserving stretch engines
(licence-encumbered), and Waveform-project interop — not the roadmap wholesale.

## 2. Fit with Zene's engine architecture

- **Language floor matches:** Tracktion README: *"N.B. Tracktion Engine requires C++20"*;
  this tree already builds C++20 (`src/CMakeLists.txt:25` `SET(CMAKE_CXX_STANDARD 20)`).
- **It is an engine + data model with no UI:** `FEATURES.md`: *"What Tracktion Engine
  doesn't provide is any kind of UI. You'll have to write code to display arrangements,
  tracks, clips, mixers…"*. Zene's interface layer is Qt (`CMakeLists.txt:144`
  `option(WANT_QT6 …)`). Coexistence therefore means JUCE (for the engine module) **and**
  Qt (for the UI) in one product — and per stage 1, JUCE in the product is the AGPLv3
  question, unavoidable because Tracktion *"is supplied as a `JUCE module`"* with JUCE as
  its submodule (README §Getting Started).
- **Replace, not coexist:** Tracktion Engine ships its own Edit/Track/Clip document model,
  transport, undo and plugin graph. Accepting it replaces `AudioEngine`, the mixer/track/
  clip model, and the `.mmp`/`.mmpz` document path (`src/core/DataFile.cpp:288,290`). Two
  engines with two document models and two undo stacks cannot share one binary credibly;
  "coexist" would mean Zene becomes a Tracktion front-end.
- **It would strand the agent surface:** the control registry's `192 ids / 35 groups`
  (`docs/FEATURE-LIST-0.3.0.md:62`) and its reversibility/`true_inverse` checkpoints bind
  to this tree's model classes (`JournallingObject` checkpoints, per-row proofs through
  save/open round trips). A different document model re-binds or discards that machinery —
  the single largest body of proved work in 0.3.0.
- **It contradicts the plan's own premise:** plan §10 ARCH-1 is *"Staged replacement, not a
  rewrite — premise of ARCH-2..8"* (intake §2.8: *"compatible with BACKLOG.md's 'the
  inherited 227K lines' caution"*). Wholesale engine adoption is a rewrite of the core
  under a different flag.

## 3. What adopting would replace vs what it would not touch

- **Replaces:** engine core, document model + file format (losing `.mmpz`/`.mmp` — the
  plan names this itself), undo/journal base classes, transport/sequencer.
- **Does not provide / still ours to build:** UI (none — §2), LMMS instrument/effect
  plugins: `plugins/` holds **1,634 tracked files** including `ClapInstrument`,
  `Lv2Instrument` etc. (measured `git ls-files plugins | wc -l`), which either stay behind
  an adapter Tracktion does not have or are dropped; the agent/control registry; the Qt
  GUI. The plan's own trade sentence (*"losing the LMMS instruments and file
  compatibility"*) survives this evaluation unchanged.
- **Partial-reuse path (the honest middle):** Tracktion's algorithms/docs as *reference*
  (e.g. follow-action semantics for row 1's gap) carries no linking and no licence import.
  Adopting its **build** carries the full stack.

## 4. Maintenance burden and size/weight

- **Upstream is alive but closed to drive-by contribution:** `api.github.com/repos/Tracktion/tracktion_engine`
  (fetched 2026-09-22): `pushed_at 2026-09-21`, CI + codecov badges on `develop`, 1,445
  stars, 35 open issues. README §Contributing: *"We don't accept third party GitHub pull
  requests directly due to copyright restrictions"* — a fork-and-carry burden for any
  fix we need, routed through the JUCE Forum.
- **Licence-stack volatility is itself a maintenance fact:** the open tier moved GPLv3 →
  AGPLv3 between JUCE 7.0.12 and 8.0.0 (stage 1 §2, both `LICENSE.md` fetched), and JUCE 9
  already carries a new EULA. Two open-tier changes in two release cycles is the exact
  risk the card called *"the JUCE licence stack, which changed in JUCE 8"* — it did change,
  and it kept changing.
- **Size:** Tracktion's own headline is *"115,000 lines of code"*
  (`tracktion.com/develop/tracktion-engine`, fetched 2026-09-22), on top of a JUCE
  dependency; GitHub reports repo `size: 1597670` KB (history included, submodule not).
  For scale, this tree's `src/core/*.cpp` alone measure 125,658 lines and `include/*.h`
  71,824 lines (measured `grep -c ""`), total 6,781 tracked files — Tracktion is not a
  small library grafted onto a small app; it is a second engine of comparable weight to
  the first.

## 5. Licence ground, restated from stage 1

The only open path conveys a GPLv3+AGPLv3 combination with AGPL §13's network-source
condition attached to the combination (stage 1 §4.2), against the recorded precedent
*"Essentia is AGPL-3.0 (**a blocker**)"* (`docs/IMPORT-DETECTION.md:14`). The paid path
(JUCE Starter/Indie/Pro + Tracktion commercial) does not exist for a GPL-conveyed product
on the plain reading of EULA §2.3 (stage 1 §4.3), and relicensing this tree is impossible
(§4.4). One narrow alternative exists and was **not** exercised: pinning JUCE 7 (whose
open tier was GPLv3) with Tracktion — unverified whether current `develop` still builds
against JUCE 7 (a `juce_compat` CI badge exists, its matrix unread), and it inherits an
EOL framework, so it does not change the verdict.

## 6. Recommendation and conditions for revisiting

**Do not adopt.** Close ARCH-7 with this verdict. Revisit only if either: (a) the owner
explicitly accepts AGPLv3 obligations in shipped binaries (an owner decision that would
also revisit `docs/IMPORT-DETECTION.md`'s blocker), or (b) a future Tracktion/JUCE release
restores a GPLv3-or-permissive open tier (checkable in minutes by fetching the then-current
`LICENSE.md`). Meanwhile the specific gaps Tracktion does expose — Follow Actions and
Arrangement Record (`docs/FEATURE-LIST-0.3.0.md:98`) — stay where the register already
has them, as in-tree work.

## 7. `could not verify` (honest list)

- **Nothing was compiled.** Build-free lane: Tracktion Engine and JUCE were never
  configured, built, linked or run against this tree — no binary size, no build time, no
  compiler-warning surface, no runtime behaviour of any kind.
- No measurement of Tracktion's real-time allocation behaviour vs this workspace's
  realtime rule (allocation counters), no latency/PDC comparison against `PdcMixerTest`
  levels of proof — both need a build.
- No source-level review of Tracktion beyond its README/FEATURES/site; internal API shape,
  thread model and undo design are unexamined.
- Elastique/Rubber Band licence terms beyond the `FEATURES.md` footnote; Waveform project
  format's own licence; `juce_compat` CI matrix (JUCE 7 pin feasibility).
- Whether an adapter could keep the 1,634 plugin files working on a Tracktion graph —
  asserted as a cost, never tested.
- Prices for JUCE Starter/Indie/Pro and Tracktion commercial tiers (stage 1 §7).

## 8. Sources

In-tree citations are `file:line` at `ab9462d56` as listed above; web sources are those in
`docs/reports/ARCH-7-LICENCE-ANSWER.md` §6 plus
`https://www.tracktion.com/develop/tracktion-engine` (115,000 lines, fetched 2026-09-22).
Plan/intake sources: plan §10 ARCH-7 (`…/inbox/zene-change-plan-2026-09-12-extracted.txt:311-314`),
intake §2.8 and §4.3 (`git show 02885a1:NEXT-CHANGE-PLAN-INTAKE.md`), card
`plans/BACKLOG.md:1565-1572`, decision `plans/MASTER-PLAN.md:211`.
