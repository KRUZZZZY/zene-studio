# ARCH-7 Stage 1 — the licence answer: JUCE 8 and Tracktion Engine vs this GPLv2 tree

Board card `plans/BACKLOG.md:1565-1572` (owner decision `D17`, `plans/MASTER-PLAN.md:211`:
licence-first, then a written verdict). Lane `040/arch7-eval`, worktree `zene-arch7`,
tip `ab9462d56`. Evaluated 2026-09-22; every web source fetched 2026-09-22.

> **Stage-1 verdict: NOT FATAL — stage 2 proceeds.** Neither component is
> "GPL-3-only with no path beside this tree": Tracktion Engine is **GPLv3-or-commercial**
> and JUCE 8's copyleft tier is **AGPLv3-or-commercial** — note **not** GPL, which is the
> measured correction to the card's "(GPL/commercial)" framing. Both can legally sit beside
> this `GPL-2.0-or-later` code in one binary via the tree's own "or later" election
> (`LICENSE.txt:296-299`) combined with GPLv3 §13. **But** the JUCE 8 tier is AGPLv3, and
> this program has already recorded AGPL-3.0 as *a blocker* for a dependency
> (`docs/IMPORT-DETECTION.md:14`) — that collision, and the fact that JUCE's *commercial*
> tiers do not rescue a GPL-conveyed product (plain reading of JUCE 8 EULA §2.3), are the
> material findings stage 2 must weigh.

## 1. What this tree is, and the in-tree precedent

| claim | evidence |
|---|---|
| This tree is **GPL-2.0-or-later** | `LICENSE.txt:296-299`: *"either version 2 of the License, or / (at your option) any later version."* |
| "or later" already accepted as permitting GPL-3 deps | `research/plugin-hosting/VST3-LICENSING.md:20-22`: *"the 'or later' clause would even permit GPLv3-only dependencies if one were ever needed."* |
| No GPL-2.0-**only** code found that would break a v3 conveyance | measured 2026-09-22, unpiped: `grep -rniI -l "GPL-2.0-only\|GPLv2-only\|GPL version 2 only" src plugins include cmake tests` → EXIT=1 (0 files); per-file loop over every `src plugins include` file containing `version 2 of the License` checking for an `any later version` sibling → **0 files** lack it; `grep -rn "either version 2 of the License" src/3rdparty` → EXIT=1 (0 matches). Scope is exactly those trees; a full third-party licence audit is **unverified** (§7). |
| This program treats **AGPL-3.0 as a blocker** | `docs/IMPORT-DETECTION.md:14` and `include/ImportDetectionDsp.h:15`: *"aubio is GPL-2.0-or-later (compatible), Essentia is AGPL-3.0 (**a blocker**)"* — the AGPL verdict is the program's own recorded standard for dependencies, quoted from the import-detection decision. |

## 2. JUCE 8's licence stack

| source (fetched 2026-09-22) | says |
|---|---|
| `raw.githubusercontent.com/juce-framework/JUCE/8.0.0/LICENSE.md` | *"The JUCE Framework modules are dual-licensed under the **AGPLv3** and the commercial JUCE licence."* |
| `raw.githubusercontent.com/juce-framework/JUCE/8.0.8/LICENSE.md` | identical AGPLv3 + commercial wording, pointing at `juce.com/legal/juce-8-licence/` |
| `raw.githubusercontent.com/juce-framework/JUCE/7.0.12/LICENSE.md` | the *previous* stack: GPLv3 as the copyleft fallback (*"release your Applications under the GNU General Public License v.3"*) plus tiered commercial — JUCE Personal (< $50K, free), Indie (< $500K, $40/mo), Pro (no limit, $130/mo), Educational (free) — and an ISC carve-out for `juce_audio_basics`, `juce_audio_devices`, `juce_core`, `juce_events` |
| `juce.com/legal/juce-8-licence/` (EULA, page metadata modified 2025-07-17) | commercial tiers named **Starter, Indie, Pro** (§1.13: *"You may not use the Starter, Indie or Pro Licence Types simultaneously"*), revenue/funding limits, seats, minimum commitments; page self-describes as *"A previous version… The current licence is the JUCE 9 EULA."* |
| `raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md` | current (JUCE 9): still **AGPLv3 + commercial** (`juce.com/legal/juce-9-licence/`), plus an SPDX SBOM pointer (`JUCE.spdx.json`) |

**So the plan's premise is confirmed and sharpened** (plan §10: *"depending on the JUCE
licence stack, which changed in JUCE 8"*, extracted plan text lines 312-313): the change at
JUCE 8 was **GPLv3 → AGPLv3** for the open tier (measured by fetching the 7.0.12 vs
8.0.0/8.0.8 `LICENSE.md`), plus the Personal→Starter tier rename. The JUCE 7 ISC
core-module carve-out does **not** appear anywhere in the fetched 8.0.x `LICENSE.md`
(unverified: individual module headers or `JUCE.spdx.json` may still carry permissive terms
for subsets — not checked). Tier **prices** for Starter/Indie/Pro are not on the fetched
EULA page and are **unverified**.

## 3. Tracktion Engine's licensing

| claim | evidence (fetched 2026-09-22) |
|---|---|
| **GPLv3-or-commercial** | `raw.githubusercontent.com/Tracktion/tracktion_engine/develop/README.md` §License: *"Tracktion Engine is covered by a [GPL](https://www.gnu.org/licenses/gpl-3.0.en.html)/[Commercial license](https://www.tracktion.com/develop/tracktion-engine)"* — the GPL link targets GPLv3; an enterprise note adds *"Enterprise licensees, please check the terms of your license as it may not include v3."* Whether the open grant is v3-only or v3-or-later is **unverified** (README does not say "or later"). |
| **JUCE licence required separately** | same README: *"Although Tracktion Engine utilises JUCE, it is not part of JUCE nor owned by the same company… you must make sure you have an appropriate JUCE licence from juce.com when distributing Tracktion Engine based products."* Adopting Tracktion Engine therefore *imports the whole JUCE 8 question* — §2 is unavoidable, not optional. |
| Bundled third-party is permissive | same README inventory: rpmalloc (public domain), moodycamel ConcurrentQueue (BSL), choc (ISC), crill (BSL), expected (CC0), libsamplerate (BSD-2), rigtorp/MPMCQueue (MIT), magic_enum (MIT), farbot (MIT), doctest (MIT), signalsmith-stretch (MIT). |
| Dual licence confuses detectors | `api.github.com/repos/Tracktion/tracktion_engine` → `"spdx_id":"NOASSERTION"`; `raw …/tracktion_engine/master/LICENSE.txt` → 404 (the grant lives in the README). Repo active: `pushed_at 2026-09-21`, 1,445 stars, 35 open issues, default branch `develop`. |
| **Its stretch headline needs third-party licences** | `raw …/develop/FEATURES.md` (99 lines): time/pitch stretching *"(provided via Elastique\*, Rubber-band\* or SoundTouch)"* with the footnote *"\* Requires external licence and libraries from relevant companies"* — Elastique is proprietary and Rubber Band's licence is its own stack; both are **unverified** here beyond the footnote. |

## 4. Can either sit beside `GPL-2.0-or-later` in one binary?

1. **Tracktion Engine (GPLv3): yes.** The tree is `GPL-2.0-or-later`
   (`LICENSE.txt:296-299`); a distributor may convey under GPLv3, and GPLv3 code combines
   with GPLv3 code without further conditions. Same reasoning already accepted in-tree for
   a GPLv3 dependency (`VST3-LICENSING.md:22`). Tracktion's permissive bundled inventory
   (§3) adds no wall.
2. **JUCE 8 (AGPLv3): yes, but only by electing v3 and accepting §13's network clause.**
   GPLv3 §13 (fetched `gnu.org/licenses/gpl-3.0.txt`): *"you have permission to link or
   combine any covered work with a work licensed under version 3 of the GNU Affero General
   Public License into a single combined work, and to convey the resulting work… the special
   requirements of the GNU Affero General Public License, section 13, concerning interaction
   through a network will apply to the combination as such."* AGPLv3 §13 carries the mirror
   permission for GPLv3 works. Consequence for shipping Zene Studio: the conveyed binary
   becomes a GPLv3+AGPLv3 combination whose *combination* carries the **remote-network
   source-offer obligation** — dormant for a purely local desktop run, live if a modified
   build's control socket / MCP surface is exposed to remote users. This is an analysis from
   the licence text, **not legal advice; the interpretation is unverified** (§7).
3. **The commercial route does not rescue a GPL product (plain reading).** JUCE 8 EULA
   §2.3 (`juce.com/legal/juce-8-licence/`) undertakes *"not to do anything that could cause
   or result in the Framework being subject to any open source licence… that requires… the
   Framework or other software combined or distributed with the Framework be: 2.3.1.
   disclosed or distributed in source code form…"* — conveying a binary that combines
   commercially-licensed JUCE with this GPL code does exactly that. So Starter/Indie/Pro
   buys JUCE freedom only for a **proprietary** product; it is not a permissive path for
   Zene Studio. Reading of clause text, **unverified as legal interpretation**; JUCE's own
   LICENSE.md frames the copyleft tier as the route for GPL applications (JUCE 7 wording,
   §2 above).
4. **Proprietary relicensing is not available to us at all** — the inherited code is
   GPL-2.0-or-later and not ours to relicense; no path analysis needed.

**FATAL test (D17 wording):** no component is GPL-3-only *and* pathless — Tracktion GPLv3
passes via "or later", JUCE 8 AGPLv3 passes via GPLv3 §13. → **not fatal, stage 2 runs.**
The honest caveat the owner should see before stage 2's recommendation is consumed: under
this program's own recorded standard (`docs/IMPORT-DETECTION.md:14`, AGPL-3.0 = blocker)
the JUCE 8 open tier is *policy*-blocked, and §3 above shows the paid tier is not a way
around it for a GPL conveyance. The remaining adoption-enabling options are therefore only
"owner explicitly accepts AGPL §13 obligations on the shipped combination" or "change the
product's licence identity" — both owner decisions, not engineering ones.

## 5. What this means for shipping Zene Studio

- **Keep the status quo (stage 2's recommendation if the evidence holds):** no JUCE, no
  AGPL, no §13 network clause enters the product; licence posture unchanged.
- **Adopt with JUCE 8/Tracktion open tiers:** legal (§4.1-4.2), but every shipped binary
  carries AGPLv3 for the JUCE portion plus the §13 network-source condition on the
  combination, and the tree would gain its first AGPL dependency against the recorded
  Essentia precedent — an explicit owner decision.
- **Adopt with paid licences:** not combinable with a GPL conveyance on the plain reading
  of EULA §2.3 (§4.3); unavailable without abandoning this tree's licence identity, which
  the inherited code forbids (§4.4).

## 6. Sources (all fetched 2026-09-22)

- JUCE: `https://raw.githubusercontent.com/juce-framework/JUCE/8.0.0/LICENSE.md` ·
  `…/8.0.8/LICENSE.md` · `…/7.0.12/LICENSE.md` · `…/master/LICENSE.md` ·
  `https://juce.com/legal/juce-8-licence/` (tier names, §2.2/2.3 text)
- Tracktion: `https://raw.githubusercontent.com/Tracktion/tracktion_engine/develop/README.md` ·
  `…/develop/FEATURES.md` · `https://api.github.com/repos/Tracktion/tracktion_engine` ·
  `https://www.tracktion.com/develop/tracktion-engine` · (`…/master/LICENSE.txt` → 404)
- Copyleft text: `https://www.gnu.org/licenses/gpl-3.0.txt` (§13) ·
  `https://www.gnu.org/licenses/agpl-3.0.txt` (§13)
- In-tree: `LICENSE.txt:296-299` · `research/plugin-hosting/VST3-LICENSING.md:19-22,41-48` ·
  `docs/IMPORT-DETECTION.md:14` · `include/ImportDetectionDsp.h:15` ·
  `plans/BACKLOG.md:1565-1572` · `plans/MASTER-PLAN.md:211` · plan §10 ARCH-7
  (`…/inbox/zene-change-plan-2026-09-12-extracted.txt:311-314`) · intake §2.8/§4.3
  (`NEXT-CHANGE-PLAN-INTAKE.md`, absorbed per `history/README.md`, recovered with
  `git show 02885a1:NEXT-CHANGE-PLAN-INTAKE.md`)

## 7. `unverified:` in this document

No build was run (build-free lane) — nothing here is compiler-checked; a full third-party
licence audit of `src/3rdparty` beyond the two greps in §1; whether JUCE 8 keeps permissive
terms for any core module outside `LICENSE.md` (e.g. `JUCE.spdx.json`); Tracktion's open
grant being v3-only vs v3-or-later; Starter/Indie/Pro and Tracktion commercial **prices**;
Elastique and Rubber Band licences beyond the FEATURES footnote; the legal interpretation
of JUCE EULA §2.3 (§4.3) and of AGPL §13's reach over a local-vs-remote control socket
(§4.2); JUCE 9 EULA differences from JUCE 8 beyond the page's self-description.
