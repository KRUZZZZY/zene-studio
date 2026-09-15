# `docs/specs/` — the specifications this repository's code and tests cite

**DOC-5.** First pass 2026-09-13 (seven snapshots, ten names recorded as "cited but not
committed"). **Second pass 2026-09-15, after the owner answered the question that pass left
open — option 1a: COMMIT the cited specification files into the product repo, and fix every
citation that points at a file in neither repo.** This file is the measured list and the
record of what was done to each row; `tools/doc5-citations.py` produces it.

```
tools/doc5-citations.py --specs            # the table, as TSV with the citing files
tools/doc5-citations.py --specs --appendix # the same as the markdown table
tools/doc5-citations.py --verify-snapshots # every pinned snapshot against its header and source
```

## The convention, stated once

- **These are pinned snapshots, not live documents.** The program workspace
  (`~/Documents/AI_KOS_PROJECT/projects/lmms-fl-research`, its `ableton-gap/`, `mixer/`,
  `plugin-hosting/`, `specs/` and root) is where these documents are edited. A snapshot is
  refreshed by a deliberate commit that updates the header hash in the same commit as the copy —
  never by editing this copy in place while the workspace copy says something else.
  `tools/doc5-citations.py --verify-snapshots` is the check; it exits non-zero if any header,
  body or workspace copy disagrees.
- **Citations are by bare name.** The three most-cited specifications alone are named by 102
  files under the code scopes — `SPEC-stable-ids.md` by 44, `AGENT-TOOLING.md` by 38,
  `SPEC-zene-studio.md` by 20 (measured:
  `for n in SPEC-stable-ids.md AGENT-TOOLING.md SPEC-zene-studio.md; do git grep -lF "$n" -- src include plugins modules tests recording tools cmake | wc -l; done`).
  Rewriting them all to add `docs/specs/` would be churn across code this release must not
  disturb, so the name is the citation and this directory is the resolution point:
  `git grep -F SPEC-zene-studio.md` finds the file here. Where a citation does carry a path, it
  now names `docs/specs/` — that is the 2026-09-15 repair below.
- **`SPEC-stable-ids.md` is the exception**: it was written for this repository by the 0.3.0
  stable-ids lane, lives here, and is edited here. It has no snapshot header because it has no
  workspace source.

## Committed here — 18 pinned snapshots and one live document

`bytes` and `sha256` are of the **source bytes** (the file after this header); the header of
each file carries the same two values, and `--verify-snapshots` re-checks them. `code`/`docs`
are the number of citing files of each class, measured by the sweep on the committed tree.

| file | source (program workspace) | bytes | sha256 | code | docs |
|---|---|---:|---|---:|---:|
| `A16-STATUS-MEASURED.md` | `ableton-gap/A16-STATUS-MEASURED.md` | 7,516 | `4631af8220ebb137` | 6 | 9 |
| `AGENT-SURFACE-INVENTORY.md` | `AGENT-SURFACE-INVENTORY.md` | 78,465 | `319c6350edbf2b49` | 0 | 5 |
| `AGENT-TOOLING.md` | `ableton-gap/AGENT-TOOLING.md` | 32,394 | `c71ffea13b7634c4` | 26 | 16 |
| `GIT-FRIENDLY-MMPZ.md` | `GIT-FRIENDLY-MMPZ.md` | 40,213 | `51e9402c9e385dd9` | 5 | 3 |
| `NEURAL-AMP.md` | `NEURAL-AMP.md` | 32,784 | `f4c747a86beea66b` | 2 | 2 |
| `PART-D-SIDECHAIN.md` | `PART-D-SIDECHAIN.md` | 21,273 | `8d8c7e75d97b962a` | 3 | 2 |
| `PATCHER-MVP.md` | `PATCHER-MVP.md` | 13,786 | `91fb1d5014ae7f3a` | 2 | 3 |
| `RECORDING-PROTOTYPE.md` | `RECORDING-PROTOTYPE.md` | 43,569 | `1c2bf51384aa6945` | 2 | 5 |
| `SPEC-dynamic-routing.md` | `mixer/SPEC-dynamic-routing.md` | 44,148 | `9fa32752d146f4fa` | 5 | 7 |
| `SPEC-lua-api-v0.md` | `specs/SPEC-lua-api-v0.md` | 7,937 | `8ffcec1cd6dd5059` | 8 | 8 |
| `SPEC-neural-amp.md` | `specs/SPEC-neural-amp.md` | 5,649 | `a5a86245138673a8` | 3 | 2 |
| `SPEC-slide-notes.md` | `specs/SPEC-slide-notes.md` | 7,059 | `d958a1b35c1aa327` | 1 | 1 |
| `SPEC-stable-ids.md` | **live document** — lives here | 4,575 | — | 40 | 7 |
| `SPEC-stem-split.md` | `specs/SPEC-stem-split.md` | 5,689 | `930492bd8551fff0` | 5 | 4 |
| `SPEC-two-track-recording.md` | `specs/SPEC-two-track-recording.md` | 7,371 | `a12afbd648c36417` | 0 | 3 |
| `SPEC-wasm-sandbox.md` | `specs/SPEC-wasm-sandbox.md` | 5,285 | `cd073153065a65f8` | 7 | 3 |
| `SPEC-zene-studio.md` | `ableton-gap/SPEC-zene-studio.md` | 16,725 | `fabc0ae8bc97e1cd` | 15 | 24 |
| `VST3-LICENSING.md` | `plugin-hosting/VST3-LICENSING.md` | 5,001 | `631a72f01a684e8b` | 1 | 1 |
| `WASM-SANDBOX.md` | `WASM-SANDBOX.md` | 34,079 | `1d233d09442ce2a1` | 3 | 2 |

The eleven added on 2026-09-15 are everything the first pass recorded as "cited, not
committed" and had not meanwhile been committed: `A16-STATUS-MEASURED.md`,
`GIT-FRIENDLY-MMPZ.md`, `NEURAL-AMP.md`, `PART-D-SIDECHAIN.md`, `PATCHER-MVP.md`,
`RECORDING-PROTOTYPE.md`, `SPEC-dynamic-routing.md`, `SPEC-slide-notes.md`,
`SPEC-two-track-recording.md`, `VST3-LICENSING.md`, `WASM-SANDBOX.md`. `SPEC-stable-ids.md`
from that list was committed by the 0.3.0 stable-ids lane in the meantime; `AGENTS.md` is not
here (see below).

## Cited, resolvable in the program workspace, deliberately not committed

Each name below is tracked in the program workspace repo, so it is **not** a dangling citation:
the owner's rule repairs citations that point at a file in **neither** repo. These are program
governance, plans, audits and findings — not product specifications — and folding them into the
product repository is a decision larger than DOC-5 carries.

| cited name | where it lives | code | docs | why it is not here |
|---|---|---:|---:|---|
| `AGENTS.md` | workspace root | 10 | 12 | program governance (lane rules, board, verified facts), not a product spec |
| `BACKLOG.md` | workspace root | 2 | 11 | the owner-gated backlog register |
| `findings-ai-dsp.md` | workspace root | 3 | 3 | findings, not a specification; its facts are already carried by `SPEC-neural-amp.md` and `SPEC-stem-split.md` |
| `feedback/grade-B-recording.md` | workspace `feedback/` | 3 | 3 | an independent audit report |
| `ableton-gap/PLAN-zene-studio.md` | workspace `ableton-gap/` | 1 | 2 | the wave plan (cited by `SPEC-zene-studio.md` as `PLAN.md`) |
| `PLANNED-WORK-MASTER-LIST-2026-09-13.md`, `V0.3-ALPHA-PLAN.md`, `WAVE-1-BRIEFS.md`, `NEXT-0.3.0-AGENT-PROMPT.md`, `HANDOFF-0.2.0-RELEASE.md`, `MCP-ZENE-CONTROL.md`, `projects/lmms-fl-research/START-HERE.md`, `projects/lmms-fl-research/STATUS-CORRECTION-2026-09-13.md` | workspace root / `projects/` | 1 each | 0–4 | program handoffs, plans and lane briefs |
| `SPEC-zene-ui-v0.md` | workspace `specs/` | 0 | 0 | **cited by nothing in this tree** — measured, not assumed; the same sweep that found the others does not find it |
| `SPEC-slide-notes.md` | — | 1 | 1 | committed above |

`AGENT-SURFACE-INVENTORY.md`, `SPEC-zene-ui-v0.md` and the workspace `specs/` copies are the
reason this table is measured rather than guessed: a name in the workspace is not automatically
cited, and a cited name is not automatically in the workspace.

## Cited and committed in the product repo, but on another branch

`product@` in the sweep: the file exists in this repository — just not on this branch. The merge
train brings these in; they are not dangling and nothing was changed for them.

| cited name | branch that has it | code | docs |
|---|---|---:|---:|
| `docs/FEATURE-LIST-0.3.0.md` (and bare `FEATURE-LIST-0.3.0.md`) | `030/audit`, `release/0.3.0` | 56 | 6 |
| `docs/CONTROL-SURFACE-FUZZ.md` | `audit/control-surface-fuzz` | 1 | 2 |
| `docs/CI-FIX-AUDIT.md` | `audit/ci-fixes` | 0 | 1 |

The 2026-09-13 pass flagged the second of these as "no such file exists in the tree; either it
is committed or those two citations are re-pointed". The sweep answers it: it **is** committed,
on `audit/control-surface-fuzz`. No repair was owed.

## Cited, and in neither repo — what was done about each

| cited name | cite class | what was done |
|---|---|---|
| `docs/CHAIN-PRESETS.md` | code (1) | re-pointed to `docs/KNOWN-LIMITATIONS.md`, which states the finding as the shipped limitation. The design document was never written. |
| `docs/CONTROLLER-SURFACES.md` | code (1) | re-pointed to `docs/KNOWN-LIMITATIONS.md`, which states feature row 19's shipped limits. Never written. |
| `tests/reference/ORIGIN.md` | harness (1) | re-pointed to `tests/reference/ORIGIN.tsv` — the file exists, under `.tsv`. |
| `tests/control-verb-inverses.md` | code (1) | the sentence that named it was rewritten: the negative control needs a rebuilt engine, so this test cannot exercise it. Neither that procedure nor the quoted `LANE-STATE.md` entry exists in this repo (see the hotspot note in the lane report — `LANE-STATE.md` at the root is another lane's file). |
| `docs/KNOWN-LIMITATIONS-v0.2.0-alpha.md` | docs (2) | **not repaired, on purpose**: both citing documents state that the path does not exist (`docs/INDEPENDENT-NOTES-READ.md:8`, `docs/AUDIT-FIX-PASS.md:178`). The name is the finding. |
| `PROTOTYPE-NOTES.md` | frozen text | inside `docs/specs/SPEC-two-track-recording.md:86` — the workspace document's own gate description, frozen with the snapshot. |
| `control.md`, `Home.md`, `LICENSE.md`, `TAG.md`, `VERSION.md`, `build/zene-body.md`, `PLAN.md` | — | a line wrap, a file-name crumb list, the Ableton Link library's own licence file, and three shell expansions/redirections. Named in `tools/doc5-citations.py`'s `FALSE_POSITIVES` with the evidence for each so the sweep cannot be read as citing them. |

## What the repair changed, and what is frozen

Two path classes named a location no branch has, because a citation was copied from the program
workspace:

- **`specs/…`** (15 live citations) — the workspace directory, never this repo's. All now name
  `docs/specs/`. `docs/STATUS.md`'s "The `specs/` citations" open item is closed in the same
  commit that closes it.
- **`ableton-gap/…`** (11 live citations, 7 of them in code and 4 citing it by line number) —
  now `docs/specs/`.
- `docs/STEM-SPLIT.md` (3 citations) → `doc/STEM-SPLIT.md`, which is where the file is.

Left as they are, each for a stated reason:

- **The pinned snapshots.** Their `source :` header names the workspace path by design. A
  snapshot's own text (e.g. `AGENT-SURFACE-INVENTORY.md`'s table naming
  `ableton-gap/SPEC-zene-studio.md:69`) is the workspace document's text, frozen with it.
- **Shipped and historical records**: `docs/RELEASE-NOTES-v0.2.1-alpha.md`,
  `docs/RELEASE-NOTES-v0.2.0-alpha.md`, `docs/INDEPENDENT-NOTES-READ.md`,
  `docs/RELEASE-DOCS-FINAL-PASS.md`, `docs/AUDIT-FIX-PASS.md`. Rewriting them would make the
  record disagree with the release it describes.
- **`modules/wasm/*.wat` and `modules/wasm/demo/gain_clip.c`** (4 citations of
  `specs/SPEC-wasm-sandbox.md`): their bytes are the compiled ABI fixture the sandbox test
  loads, so re-pointing a comment would change the artefact.
- **`src/3rdparty/lua/README.lmms` and `src/3rdparty/luabridge/README.lmms`** (2 citations of
  `specs/SPEC-lua-api-v0.md`): re-pointed first, then reverted on measurement. Gate 6 classifies a
  changed file under `src/3rdparty/` that is in no scope list as "undeclared change to
  upstream-inherited code", and `README.lmms` is not a source extension, so it can be registered
  in no scope list and the ledger's own policy forbids an entry for it (the file does not exist at
  the fork point, so a ledger entry would be a false statement about it — the same classifier
  defect Gate 6's own comment records for non-source files under `tools/`). The two citations
  therefore keep the workspace path, and that is a recorded decision rather than an oversight.
- **The derived command snapshot**: `tools/mcp-zene-control/zene_control/commands_snapshot.json`
  carries one `specs/SPEC-lua-api-v0.md` inside a command description string copied from
  `src/core/ControlCommandsScript.cpp`. That file is DERIVED, never hand-edited, and is
  regenerated from a live instance at a merge (`snapshot_commands.py --socket`).
- **Frozen merge logs** under `tests/integration-logs-*/` and `tests/reference/` — records of
  runs, counted by the sweep and never repaired.

## Re-measuring

```
tools/doc5-citations.py --specs --appendix     # this list, as markdown
tools/doc5-citations.py                        # every row, with the citing files
tools/doc5-citations.py --verify-snapshots     # 18 snapshots, 0 disagree (2026-09-15)
```

The tool resolves each name against this repo's HEAD, against **every branch** of this repo
(`git log --all --diff-filter=A -- <name> '*/<name>'` — the second pathspec matters: `*name`
would also match `docs_KNOWN-LIMITATIONS.md` for `KNOWN-LIMITATIONS.md`), against the workspace
repo's tracked files, and finally against the filesystem. A row with no home in any of those is
the only kind DOC-5 repairs.
