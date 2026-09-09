# Phase F — per-branch reviewability (board task #592)

Clone: `lmms-partd` (independent clone), branch `part-d-sidechain`.
Baseline for every figure below: **`7edfbbe45`** (tip of the Phase D branch *before* the Phase F
hardening commit; Phase F adds docs + tests only — see §5).

Agreed review budget (quoted): *"Phases A–E mirror attack-plan sub-PRs A–G …; each phase is one
reviewable upstream PR **< 800 new lines**."* — `MASTER-PLAN.md` §4.1 (line 192).
"New lines" is read as **insertions** (`git diff --shortstat` `+` count); deletions are reported
separately and not counted against the budget.

## 1. Measured diffstat per branch (the stacked set)

| # | Unit (branch / commit range) | Base | Commits | Files | +/− | Fits < 800 insertions? |
|---|---|---|---|---|---|---|
| 1 | `part-a-core-abstractions` (`4e677cb6c..part-a-core-abstractions`) | upstream `4e677cb6c` | 2 | 16 | **+3849 / −3** | **NO — 4.8×** |
| 2 | `part-b-engine-integration` (`part-a..part-b-engine-integration`) | `part-a-core-abstractions` | 2 | 30 | **+938 / −121** | **NO — 1.17×** (both commits individually YES) |
| 3 | Part C slice 1 (`part-b..08c5fe863`) | `part-b-engine-integration` | 2 | 74 | **+6033 / −84** | **NO — 7.5×** (commit 1 YES, commit 2 NO) |
| 4 | `part-d-sidechain` (`08c5fe863..7edfbbe45`) | `08c5fe863` | 11 | 15 | **+2633 / −21** | **NO — 3.3×** (9/11 commits YES; 2 WIP commits NO) |

No branch in the stacked set is under the 800-line budget as a single PR. The reviewable units that
*do* fit are individual commits (below), and the documented peel plans (§3).

## 2. Per-commit detail (the units that can actually be reviewed)

### Part A — `4e677cb6c..part-a-core-abstractions` (16 files, +3849/−3)
| Commit | Files | +/− | < 800? |
|---|---|---|---|
| `2999d13ff` Add AudioBus, AudioPorts and AudioPortsModel core abstractions | 14 | +3589/−3 | NO |
| `2a1ce4c7f` Add AudioPortsModel pin matrix and serialization tests | 2 | +260 | YES |

### Part B — `part-a..part-b-engine-integration` (30 files, +938/−121)
| Commit | Files | +/− | < 800? |
|---|---|---|---|
| `d5295871f` engine integration | 28 | +695/−121 | YES |
| `4ac5c3e38` AudioBusHandle wiring + tests | 2 | +243 | YES |

### Part C slice 1 — `part-b..08c5fe863` (74 files, +6033/−84)
| Commit | Files | +/− | < 800? |
|---|---|---|---|
| `8eaf7dcbe` WIP: Part C first slice — ports infra + plugin migration in progress | 18 | +507/−84 | YES |
| `08c5fe863` Tests: sample-exact behaviour-preservation harness for Part C effect migrations | 56 | +5526 | NO |

### Part D (this branch) — `08c5fe863..7edfbbe45` (15 files, +2633/−21)
| # | Commit | Files | +/− | < 800? |
|---|---|---|---|---|
| 1 | `24e06f944` | 3 | +300 | YES |
| 2 | `d7591d998` | 8 | +1060/−21 | NO |
| 3 | `7b3f08a60` | 3 | +834 | NO |
| 4 | `61e4898a1` | 2 | +10/−1 | YES |
| 5 | `fcde49d33` | 2 | +12 | YES |
| 6 | `df953f513` | 1 | +32 | YES |
| 7 | `a212d249d` | 1 | +4 | YES |
| 8 | `8dc0f6bc6` | 1 | +7/−1 | YES |
| 9 | `a543ea33b` | 1 | +2/−2 | YES |
| 10 | `4731e9ab8` | 2 | +143/−14 | YES |
| 11 | `7edfbbe45` | 2 | +247 | YES |

The two over-budget commits are the WIP implementation commit (`d7591d998`) and its WIP test commit
(`7b3f08a60`) — the same shape as Part C commit 2. The nine remaining commits are small (1–2 files
each) fixes.

## 3. Documented peel plans (what would bring a unit under budget)

| Unit | Plan (source) | Peelable sub-units |
|---|---|---|
| Part A | `PART-A-EXTRACTION.md` §7; `BACKLOG.md` row A1/A2 | A1 = core model + bus + pin connector + tests (+3849, 16 files); A2 = `AudioPorts.h` + `PluginAudioPorts.h` (+1322) can be peeled with no code change → first PR +2527/−3; A3 = `AudioBusHandle.*` + `AudioPlugin.h` (+450/−43) must land with Part B. Smallest compiling unit is still ~3× the budget — owner decision required. |
| Part B | branch is two commits; each already < 800 | submit as two stacked PRs (695, 243). |
| Part C | Part C defers the remaining 147 files / ~36k lines to slice 2 (`PART-C-MIGRATION.md`) | split commit 2 (the +5526 harness) into per-plugin-test PRs; commit 1 (+507/−84) is under budget as-is. |
| Part D | this document | split into D1 = harness + implementation (`24e06f944` + `d7591d998`), D2 = tests (`7b3f08a60` + fixes), D3 = perf bench. D1 remains over budget (+1360); a model/serialization split (Mixer.h/.cpp) from the routing-engine split (MixerRoute.cpp) would be the next lever. |

## 4. Exact commands (reproduce every figure)

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-partd
git diff --shortstat 4e677cb6c..part-a-core-abstractions
git diff --shortstat part-a-core-abstractions..part-b-engine-integration
git diff --shortstat part-b-engine-integration..08c5fe863
git diff --shortstat 08c5fe863..7edfbbe45
# per-commit:
for h in $(git rev-list --reverse 08c5fe863..7edfbbe45); do \
  echo "$h | $(git diff --shortstat $h^..$h)"; done
```

## 5. Phase F delta (this task)

Phase F adds no engine code. It adds: the 100+ channel scale test
(`tests/src/core/PhaseFChannelScaleTest.cpp`), the AudioBus regression test
(`tests/src/core/AudioBusTest.cpp`), the `AudioBus.cpp` two-line fix (its own commit `9327c350f`), the
`tests/CMakeLists.txt` registrations, and these three documents under `docs/phase-f/`.
Re-measure the branch total with:

```bash
git diff --shortstat 7edfbbe45..part-d-sidechain
```

## 6. Submission order (INTEGRATION.md)

`INTEGRATION.md` §3, verbatim:

1. `part-a-core-abstractions` (foundation; everything multi-channel needs it)
2. `part-b-engine-integration` (already stacked on Part A)
3. `feat/slide-notes` — rebase onto the stack; resolve the `NotePlayHandle` overlap; re-run suite
4. `feat/neural-amp`, `feat/patcher-mvp`, `feat/two-track-recording` — independent; order by review
   appetite
5. `feat/rnnoise-denoiser`, `feat/hidpi-scaling` — independent of everything above; can go first

Mixer-stack order derived from `INTEGRATION.md` §2.3 (bridge conflict resolution): **Part A → Part B
→ Part C → VST3 (rebased) → CLAP**. Part D (`part-d-sidechain`) stacks **after Part C slice 1** and is
independent of VST3/CLAP. Rule after every stack step (`INTEGRATION.md` §4): `cmake --build build -j8`
exit 0 **and** `cd build/tests && ctest --output-on-failure` passes (0 tests in `build/` is an error).

## 7. Verdict for the "PR mergable" criterion

- **Local proxy (measured):** the stack builds and passes the full suite (see
  `CRITERIA-TO-EVIDENCE.md`); per-branch diffstats above.
- **NOT PROVEN:** reviewer sign-off ("confirmed by reviewers, e.g. sakertooth / JohannesLorenz").
  No PR may be opened by this program (`AGENTS.md` rule 1: owner-gated, never push), so no reviewer
  has seen the branches. The table in §1 is the evidence that can exist locally; the size finding
  (no branch under budget) is itself part of the reviewability answer and must go to the owner
  before submission.
