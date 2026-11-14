# SPEC-050: Zene Studio 0.5.0-beta

Written 2026-10-14 from the owner's grill record (`research/reviews/spec-050-grill-2026-10-14/DECISIONS.md`, Q1–Q19;
the owner confirmed it on 2026-10-14). For 0.5.0 this file supersedes `research/ableton-gap/SPEC-zene-studio.md` and
the parts of `plans/PLAN-050-CYCLE.md` it contradicts: the alpha gate, "P2/P3 optional", the Qwen verifier, the
command-contract freeze, the owner soak and real-project checklist, and "agents do not push". Everything else in the
cycle plan (inventory, milestones, model routing, the 2026-10-08/09 rulings) still stands. `AGENTS.md` stays binding.

## 1. What 0.5.0 is

**A complete DAW for musicians that AI agents can also drive** (Q2). The musician's workflow comes first; the agent
control surface (JSON-RPC over a unix socket, the MCP bridge) is the differentiator, not the headline.

It is the public launch: the owner starts the website and advertising with it (owner, 2026-10-08).

## 2. Versions (Q14)

Development identity update (owner-authorized Git audit, 2026-11-14): see
[VERSIONING-AND-MILESTONES.md](VERSIONING-AND-MILESTONES.md). Current development
is milestone 0.4 with separate feature-train and maintenance revisions. The beta
launch and 1.0 rules below remain unchanged; historical tags remain immutable.

- The first release is **`0.5.0-beta`**. Updates are `0.5.1-beta`, `0.5.2-beta`, … There is **no plain `0.5.0`**.
- Leaving beta is **`1.0.0`**, once iteration is down to either huge new features or tidy-up.
- Today the tree says `0.4.0-alpha` (`CMakeLists.txt:65-75`); the version bump is part of the release work.

## 3. Scope

- **All 476 queue units** (`plans/queue-050.jsonl`: P0 51, P1 129, P2 163, P3 133) ship in the beta, including OMF/AAF,
  the immersive mixer, video/SMPTE, project tabs, LV2 plug-in windows, ASIO, bundled ONNX Runtime and the Qt 6 default.
  No deferral to 0.5.x, no release date (owner, 2026-10-08).
- **Out:** Q050-236 offline speech-to-text ("no in-app models"), and everything in `plans/QUEUE-050-NOT-DOING.md`.
- **Behaviour rulings that stand:** the 2026-10-03 UX decisions (FL-style piano roll, trim on overlap, send flip,
  Workflows, one zoom plus accessible mode); linked MIDI clips share content, name and colour with per-instance start,
  length and loop plus Make Unique; a crossfade into a contained clip clamps at the edge (the V shape only with Alt held
  or `mode="container"`); loop recording uses the active-take model (all 2026-10-09).
- **Order:** bugs first, then features (owner, 2026-10-08). A feature merges when it works on a fundamental level; minor
  failures go to the bug queue, and the test stays red rather than being weakened (owner, 2026-10-12).

## 4. Platforms and compatibility

- **Linux, Windows and macOS on day one, all on Qt 6** (Q4).
- **Six installers** (Q13): Linux AppImage x86_64 and arm64; macOS dmg Intel and Apple Silicon; Windows x64 (MSVC) and
  arm64. The MinGW x64 build is dropped once nothing is found to depend on it. Today only the MSVC job is Qt 6
  (`.github/workflows/build.yml:1145,1184`), so moving the others to Qt 6 is release work.
- **Old projects open safely** (Q5): upstream LMMS `.mmp`/`.mmpz` projects and 0.4.x Zene projects open without losing
  tracks, clips, automation or routing. The opt-in v2 container is tested on its own and says plainly that older builds
  cannot open it.

## 5. Agent control surface (Q7, Q9)

- **No freeze at beta.** Commands may still change after `0.5.0-beta`.
- Every change is listed in the **release notes** and in the regenerated **commands snapshot** (from a live instance;
  canary figures per `tests/doc-figures-gate.py`). There is **no** period where an old name keeps working.
- The known-by-design contracts in `AGENTS.md` (provenance journal, undo coalescing, async `mastering.run` ACK, boolean
  refusals, first-save `reversible:false`, empty tempo maps, fx-id renumbering, `id:-1` replies) are unchanged.

## 6. Privacy (Q12)

Release builds compile telemetry out (`ZENE_TELEMETRY=OFF`): no prompt, no network code, crash reports stay local.
Consent already defaults off and no endpoint ships (`src/core/Telemetry.cpp:159-163`).

## 7. How quality is assured

### 7.1 Every feature lane: two verifiers before merge (Q16)

- **Code verifier, GPT-6 Luna:** smells, the realtime rule (no allocation or locking on audio paths), Qt 5/MSVC
  portability, weakened tests, licensing.
- **Feature verifier, Claude Haiku 5.5:** matches this spec and its queue unit, works the way a musician expects, no
  bloat or scope creep.

This replaces the single review pass. The merge rule is unchanged.

### 7.2 Bug claims (Q6)

Luna finds; Haiku reproduces every claim on a clean copy before it counts. This replaces the Qwen verifier.

### 7.3 Usage sweep (Q8, Q10, Q17–Q19)

There is no owner soak and no owner real-project checklist: an agent with good rules does it, and more thoroughly.

- **When:** after the features are in (Q18). Luna code sweeps keep running alongside feature work meanwhile.
- **How:** Haiku plays the real app as a musician; Luna reproduces each claim on a clean copy; findings are deduplicated
  into the bug queue. It repeats until the stopping rule says stop (§8).
- **Scenarios:** generated by the agent from the 476 units' acceptance text plus everyday workflows: making a beat,
  recording vocals, comping, a mix with sends and automation, stem export, reopening the next day. Each scenario lists
  its steps and what "correct" looks like. No owner review is needed.
- **The agent's sign-off includes:**
  - a 24-hour soak on this machine's real audio (PipeWire/JACK) covering playback, multitrack recording, save/reopen,
    render and plug-ins, with drop and queue counters logged;
  - open → save → reopen → render on the 28 upstream demos, the shorties and `~/Music/hardstyle-150bpm-fminor/`.

## 8. Definition of done for the beta (Q3)

All of these, on one frozen commit:

1. All 476 units merged and fundamentally working.
2. Zero open S1 and zero open beta-blocker bugs.
3. The stopping rule (`tools/luna-patrol/stopping.py`, owner 2026-10-09) says the sweeps can stop.
4. The full bar is green (clean build, full ctest from `build/tests`, every gate with coverage, unpiped exit codes),
   except for a **known-failures ledger** of non-core items. Each ledger entry is shown on the known-limitations page.
5. Hosted CI is green on the `ci/*` push of that commit (agents may push the newest line to `ci/*` on `product` after a
   secret scan; never `main`, a tag, `origin` or upstream).

## 9. Launch documentation (Q15)

README, a getting-started guide, a feature reference generated from the build, the known-limitations page, release
notes, and an agent-control guide. The full user manual waits for 1.0.0.

## 10. Superseded rulings

| Old | Replaced by |
| --- | --- |
| `PLAN-050-CYCLE.md:83` freeze the command contract at beta | §5 (Q7, Q9) |
| `PLAN-050-CYCLE.md:86` Qwen as the second-vendor verifier | §7.2, §7.3 (Q6, Q17) |
| `PLAN-050-CYCLE.md:87-88` owner soak and owner real-project checklist | §7.3 (Q8, Q10, Q17) |
| `PLAN-050-CYCLE.md` alpha gate; "P2/P3 breadth remains optional" | §3 (owner, 2026-10-08) |
| `PLAN-050-CYCLE.md` "Agents do not push" | §8.5 (owner, 2026-10-08) |
| `SPEC-zene-studio.md` §6 non-goals (video, surround) | §3 |
