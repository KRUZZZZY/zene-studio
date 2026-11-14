# Development versions and milestones

Adopted 2026-11-14 after the owner requested a Git-based milestone and train review.
This supersedes development-build numbering in earlier plans. The 0.5 beta scope,
release gates and project/API compatibility policies in SPEC-050 remain binding.

Current development milestone: **0.4 — Integrated DAW alpha**.
Current promoted development identity: **0.4.21.1-alpha**, feature train **21.1**,
internal ref `050/m0-train-30`, commit `40b4322860e9c2133f412a4b921b63b057dd710c`.
This identity records integration progress; it is not a claim of green hosted CI,
a completed beta milestone, or a newly published downloadable release.

## What Git establishes

These are Zene's alpha tags, distinct from inherited LMMS tags such as `v0.4.15`
and `v1.3.0-alpha.1`. Existing tags and version strings in historical binaries
are preserved.

| Date | Zene milestone / checkpoint | Evidence and meaning |
|---|---|---|
| 2026-09-11 | 0.1: first installable alpha | `v0.1.0-alpha`, `0c23587d25`; its release notes describe the first public alpha. |
| 2026-09-12 | 0.2: product identity and capability accounting | `v0.2.0-alpha`, `b099fd6cb4`; first release under the product's own name, with build/test evidence requirements. |
| 2026-09-13 | 0.2 maintenance | `v0.2.1-alpha`, `16423db905`; platform/test fixes, not a new product milestone. |
| 2026-09-21 | 0.3: engine and agent control | `v0.3.0-alpha`, `2a2d8e1ff7`; backend/control-socket/MCP release. Historical defects remain documented in BUGS_FOUND.md. |
| 2026-10-01 | 0.4: integrated DAW alpha | `v0.4.0-alpha`, `5d627f0cab`; engine workflows exposed through the interface, contract/safety work and architecture integration. |
| 2026-10-05–07 | Current cycle's baseline | `0.5.0/ui-train` at `6d1a1143d0`, then `050/features-2026-10-06` at `ec338df42f`; UI shell/note-editor/visual work and 14 feature lanes form the baseline before numbered trains. |
| 2026-10-25 | Interim downloadable checkpoint | `v0.4.1-alpha`, `4a406d8bb5`; release notes explicitly identify it as train 16 plus a release bump, not a new milestone. |
| 2026-11-14 | Continuing 0.4 development | Thirty promoted internal train refs reachable from train 30: 21 feature-bearing batches and nine maintenance batches. Train 31 is an unpromoted attempt and receives no promoted version. |

The one-time audit walks each train's first-parent delta from the previous
promoted train and verifies ancestry to the current promoted tip. A feature batch
contains a merged `feat/050-*` lane or `Q050-*` queue delivery, including completion
and documentation units. This is an integration classification, not a new claim
that every feature in a batch is complete or that a reverted change is available.
The pre-train 14-lane baseline is not counted again. Abandoned `-attempt-*`, `-ci*`
and other work branches are not releases.

Audit and runtime source of truth:
`~/.local/share/zene-bunny-fix/qwen/TRAIN-VERSIONS.json`. Each record pins a full
commit SHA; historical bootstrap records also contain the feature merge subjects
and hashes that justify their classification. `bootstrap_train_versions.py`
reproduces the audit and refuses to overwrite an existing ledger.

## Numbering from now on

The four visible components are **major.milestone.feature-train.repair**,
followed by the maturity stage. The package-compatible form uses three numeric
components and a prerelease revision: `0.4.21-alpha.1`.

- New accepted feature-queue work in a validated promotion advances the feature
  train and resets its repair revision: `0.4.21.1-alpha` → `0.4.22.0-alpha`.
- Bugs, CI portability, maintenance and repair-only promotions advance the repair
  revision: `0.4.21.1-alpha` → `0.4.21.2-alpha`.
- Attempts and failed CI runs consume neither a feature number nor a repair number.
- Milestones change on product scope and readiness, never after an arbitrary
  number of batches. They require an explicit policy change; automation only
  advances the feature/repair components.
- Internal `050/m0-train-N` refs remain unique integration IDs. Renaming them would
  break queued repairs, logs, evidence paths and independent review references.
- The ledger, control-room status, promotion reports, hosted-CI monitor metadata,
  launcher comments and installed `train-version.json` use this identity. Existing
  compiled binaries retain their embedded historical release/build version; the
  install manifest records that separately as `binary_version`. No binary is
  relabelled as rebuilt, and no public release tag is created by this policy.

Examples at adoption:

| Internal train | Development identity | Classification |
|---|---|---|
| 26 | 0.4.19.0-alpha | Feature batch |
| 27 | 0.4.19.1-alpha | Maintenance |
| 28 | 0.4.20.0-alpha | Feature batch |
| 29 | 0.4.21.0-alpha | Feature batch |
| 30 | 0.4.21.1-alpha | Maintenance |
| Next validated repair-only train | 0.4.21.2-alpha | Pending; not yet promoted |
| Next validated feature batch | 0.4.22.0-alpha | Pending; not yet promoted |

## Remaining milestones

The October cycle plan's M0–M6 are workstream checkpoints, not app minor-version
numbers. Several run concurrently; an `m0` branch prefix is historical and does
not mean feature development has never started.

| Checkpoint | Meaning | Current assessment |
|---|---|---|
| M0 | Baseline consolidation, accepted fixes and rebasing | Many trains integrated; maintenance continues. This audit does not issue a new closure certificate. |
| M1 | Blockers, P0 workflows and dependencies | Feature deliveries exist; do not infer complete P0 coverage from train count. |
| M2 | P1 workflows and their prerequisites | Ongoing alongside the feature queues. |
| M3 | P2/P3 breadth | Required by the later SPEC-050 decision, which supersedes the old cycle plan's optional designation; explicit exclusions still apply. |
| M4 | Scope freeze and release documentation | Requires the chosen beta scope and documentation to agree on a pinned build. |
| M5 | Stabilisation and usage validation | Terra sweeps, independent fixes and the current SPEC-050 quality requirements must pass. Older owner-soak proposals are not reinstated by this document. |
| M6 | Cross-platform release candidate | Hosted build matrix and required checks must pass; currently blocked by build/portability failures and pending repair reconciliation. |
| 0.5 beta | Complete musician-facing DAW and agent interface | Remains the public-launch target in SPEC-050; no beta designation while these gates are unmet. |
| 1.0 | Exit beta | Retains SPEC-050's criterion: remaining iteration is major additions or tidy-up, with release requirements satisfied. |

Immediate milestone: **a cross-platform validated 0.4 development checkpoint**.
Reconcile the queued CI repairs, promote through the unchanged local suite/gates
and Terra policy, then obtain successful hosted build/test evidence on the exact
promoted commit. Only then report that checkpoint as green.

## GitHub recovery records

The owner authorized automatic GitHub backups on 2026-11-14. The dedicated
`ci/train-records` branch in `KRUZZZZY/zene-studio` preserves all promoted train
identities, first-parent change lists, CI links/results, local build/test/gate
evidence, repair metadata and replayable repair patches. It also contains recovery
copies of the versioning and maintenance automation. Source code remains on its
existing `ci/*` branches, linked by immutable SHA in each record.

The backup branch has an independent root and no workflow definitions: updating
records does not rerun the product build matrix. Its history is append-only;
updates use ordinary fast-forward pushes, never force pushes. Old snapshots remain
available through Git history. Existing unpromoted attempts receive no release
identity and do not replace the promoted records.

The owner explicitly approved ongoing publication on 2026-11-14.
`zene-train-backup.timer` refreshes records five minutes after the previous run
finishes, and each promotion requests an immediate refresh. CI and repair state
updates are picked up by the timer. Poll timestamps do not create new commits.
Failures are recorded in `qwen/TRAIN-BACKUP-STATUS.json` and retried by the timer;
they do not alter the release gates. Secrets stop publication. Evidence is scanned
before compression; the staged commit is scanned again before push. Only the
identified artifact hashes and public signing-key fingerprints have narrow
scanner exceptions. No credentials, provider configuration or agent conversations
are selected for backup.

To recover, clone the product repo, fetch `ci/train-records`, consult its README and
`versions.json`, and select a pinned source SHA. Decompress evidence with gzip and
verify the uncompressed hashes in `evidence-index.json`. Repair patches are recovery
material; reconcile their recorded base/source SHAs before replaying them or
restoring queue state. Restore automation files selectively rather than replacing
a running workspace wholesale.
