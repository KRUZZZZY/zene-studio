# Proof debt — board card #681 (lane `030/proof-debt`, wave 9)

**Branch `030/proof-debt`**, base `a36e8b0d4`, commits `b62296d17` (the six rows) and
`e2b79033a` (the two row-parse fixes the first measurement caught). Worktree
`zene-030/wproof`. Files changed: `docs/FEATURE-LIST-0.3.0.md` only — no test, gate,
baseline, manifest or workflow step was touched, and no build tree was created.

## 1. The list is 6 rows, not 12 — the drift, with the commit that proves it

The card title says *12 THIN-100 rows*. That was true at the 2026-09-15 snapshot:
`git show 78b789f:V0.3-FEATURE-TRACKER.tsv` reads **THIN-100 = 1, 8, 63, 12, 13, 17, 20,
23, 68, 40, 47, 85** (12 rows). Six of those graduated before this lane ran — rows **1, 12,
13, 20, 23, 68** are `ADDED` in today's measurement (the audit folds named their proofs:
`c967f8f24` folded rows 60/61/67/68 for instance). The fresh run re-derived here reads
exactly the other six:

```
rows 8, 63, 17, 40, 47, 85   ->   THIN-100, 2/4 probes, proof "no proof named in the list (not measurable)"
```

which matches the dispatch's known members (8 MPE, 17 MIDI learn, 40 autosave).

## 2. What the missing probe was, per row

A THIN-100 row is 100 % over **two** measurable parts (`ids`, `limits`); its `engine` and
`proof` parts are unmeasurable because of the **row's own text** — the tracker derives every
probe from the row (a file token, a dotted id, a CamelCase test token). So the debt was not
a missing artefact in the tree; it was a **registration the row did not name**. Each row was
re-measured against this tree at `a36e8b0d4`, and its proof artefact verified in
`tests/CMakeLists.txt` and on disk before it was named:

| # | row | proof named (all registered ctests) | what the proof actually does | state before → after |
|---|---|---|---|---|
| 8 | MPE capture, storage and edit (#601) | `MpeNoteStorageTest`, `MpeExpressionTest`, `MpePlaybackTest`, `ControlNoteExpressionCommandsTest` | per-note fields on the `Note` + serialization; per-channel capture and pitch math; pressure and timbre reaching the instrument (task #649); `note.expression_set`/`get`/`clear` over the surface with a `MidiClip` checkpoint as inverse | `partial` → **in the tree** |
| 17 | MIDI learn | `MidiLearnTest`, `MidiLearnThreadTest`, `ControlSocketIntegration` | the global MIDI-learn mode headless (synthetic CC binds the focused control, binding survives a project round trip, learn-off ignores the CC); the arm path across threads; `midi.learn_toggle` over the wire | `partial` → **in the tree** |
| 40 | Autosave / project recovery | `ProjectRecoveryTest`, `ReversibilityUndoTest`, `RevisionTimelineTest` | the recovery decision (fresh / stale / other-project / missing / empty) + identity sidecar; `project.restore_revision` invoked and its typed failure beyond the rotation; the restore path through `control.undo`'s own mechanism | `partial` → **in the tree** |
| 47 | The control surface itself | `ControlRegistryTest`, `ControlSocketIntegration` (the `agent_surface` ctest named in the row stands) | every required command registered, `describeAll` carries schemas and `requires`, the typed refusals, the transaction a mutating command leaves; the socket end to end | `in the tree` (proof added) |
| 63 | `automation.record_mode_set` | `ControlAutomationScriptTest`, `AutomationModesTest`, `ControlAutomationModesTest` | the group's contract test (drives the id), and the no-destruction property repeated through the command surface | **`to build` → `in the tree`** |
| 85 | `telemetry.consent_set` | `ControlRegistryTest` | the telemetry slots that refuse the missing id's existence, assert no reachable `telemetry.*` command is mutating, and assert the only consent verb is `requires: display, human` | `to build`, **closed by recorded decision** — no probe invented |

Three of the six rows were **stale**, and the correction is the finding:

- **row 63** said the record-mode verb was not among the `automation.*` ids. It is:
  `src/core/ControlCommandsAutomation.cpp:340` registers `automation.record_mode_set`
  (track / parameter / `on` or `off`, `mutating`, result schema reporting `mode_before` and
  `changed`), its A16 row is `true_inverse`
  (`src/core/ControlReversibilityTableAutomationModes.cpp:101`), and row 10's dependency has
  landed (`docs/KNOWN-LIMITATIONS.md:431` documents the modes as drivable and proved).
- **row 8** said "only the pitch axis reaches playback — drivable but inert". Task #649
  lifted that: `docs/KNOWN-LIMITATIONS.md:280-283` states all three axes reach playback,
  and `MpePlaybackTest` is the proof.
- **row 40** said it was "referenced by no behavioural test". Three registered ctests
  reference it.

**Row 85 is the one row the list can supply no probe for**, and that is the finding: the id
does not exist and is not going to — `telemetry.consent` **is** the consent verb and declares
`requires: display, human`, so no automated caller can consent and a second agent-reachable
setter would turn telemetry on on the user's behalf (`docs/TELEMETRY-V1.md` §2.6). The
decision is already written (`docs/KNOWN-LIMITATIONS.md`, "There is no `telemetry.consent_set`"
and `docs/RELEASE-NOTES-v0.3.0-alpha.md`, "closed by decision, not by a second id") and pinned
by `ControlRegistryTest`, which is what the row now names. The row keeps its `to build` state
and the tracker prints it in its own *"the probes reach 100 %; the list does not say in the
tree"* section — the honest signal, not a hidden one.

## 3. Measurements — the commands and their exit codes

The tracker's list source is **`030/audit`'s copy of `docs/FEATURE-LIST-0.3.0.md`**, not the
release tree's, so a row edit on a lane branch cannot change the canonical run's probe set
until the audit fold brings it across (`c967f8f24` did exactly that for three earlier merge
lanes). Both readings are given, so the fold can be checked afterwards.

**(a) the canonical command from the card** — list `030/audit @ c967f8f24`, tree
`zene-030/wproof`:

```
python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/scripts/zene-feature-tracker.py \
  --release-wt /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wproof
EXIT=0
```

| run | at | ADDED | THIN-100 | IN-PROGRESS | NOT-STARTED |
|---|---|---|---|---|---|
| before | `a36e8b0d4` | 38 | **6** | 4 | 41 |
| after | `e2b79033a` | 38 | **6** | 4 | 41 |

The row table is **byte-identical** between the two runs (`diff` of the two outputs, empty) —
the six rows still read `THIN-100 2/4`, and **nothing regressed**. That is the honest result
of the canonical command: it cannot see this branch's rows yet.

**(b) the same instrument with this branch's list** (`--audit-wt` the same worktree,
`ZENE_AUDIT_BRANCH=030/proof-debt`; the before row uses `ZENE_AUDIT_BRANCH=a36e8b0d4`, so the
before/after pair differs in the list only):

```
ZENE_AUDIT_BRANCH=030/proof-debt python3 .../scripts/zene-feature-tracker.py \
  --audit-wt .../zene-030/wproof --release-wt .../zene-030/wproof
EXIT=0
```

| # | state before | state after | probes before → after | proof measured after |
|---|---|---|---|---|
| 8 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `MpeNoteStorageTest` (test-source) |
| 17 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `MidiLearnTest` (test-source) |
| 40 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `ProjectRecoveryTest` (test-source) |
| 47 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `ControlRegistryTest` (test-source) |
| 63 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `ControlAutomationScriptTest` (test-source) |
| 85 | THIN-100 100 % | **ADDED 100 %** | 2/4 → 3/4 | `ControlRegistryTest` (test-source) |

Totals: **ADDED 38 → 44 · THIN-100 6 → 0 · IN-PROGRESS 4 (unchanged) · NOT-STARTED 41**.
`engine` stays unmeasurable (3/4) for all six: the list names no file for them, and no file
token was invented to reach 4/4.

## 4. Two row-parse traps (found by measurement, fixed in `e2b79033a`)

Both were in my own row text, and both are traps for anyone editing this list:

1. **A pipe inside a cell splits it.** `mode `on|off`` truncated row 63's status column, so
   the proof names after it left `cols[3]` and the row read 2/4 again. Every table row must
   have exactly 5 columns; a `|` must be written out (`on` or `off`).
2. **A dotted lowercase token is an ID to the tracker**, before the file test runs.
   `recover.mmp` was banked as a fifth declared id and measured 0.8 (4/5) — the row read 93 %
   `IN-PROGRESS`. Written as "an empty recovery file", the same meaning, no token.

Also worth knowing: any backticked dotted token is a declared id (so naming an
*unregistered* verb in backticks lowers the row), and any backticked CamelCase token is a
proof candidate. The audit's own row 85 text dodges this by spelling the boarded verb
undotted.

## 5. Findings left open (not repaired here, on purpose)

- **Row 10's status is stale in the same way row 63's was**: it still says `automation.mode_set`
  is "a **registered command that always refuses**", while the mode set has worked since
  2026-09-15 (`docs/KNOWN-LIMITATIONS.md:431`, `docs/RELEASE-NOTES-v0.3.0-alpha.md:823`,
  `:1115`). It is outside this card's six rows and was left for the audit fold: **one line to
  correct**, and it is the only contradiction my row-63 text introduces into the table.
- **The `limits` probe is weak for all six rows**: its token is the id's first segment
  (`midi`, `project`, `control`, `telemetry`, `automation`, `note`), so it is satisfied by
  lines about other features. Tightening it would *lower* measurements and is the tracker's
  own card (#689), not a lane edit.
- **`engine` is unmeasurable for all six** — if the audit wants a 4-part row, the row must
  name the file that carries the feature; none of these rows did, and none was added.

## 6. Not verified here

**No build, no ctest run.** The change is documentation-only (one markdown file), so nothing
in it compiles or fails; the proofs named are verified as *registered* (in `tests/CMakeLists.txt`
via the `LMMS_TESTS` list, or an explicit `add_test(NAME …)`), by reading their sources, and by
the tracker's own resolution — not by executing them. If the fix-up pass wants that evidence:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
cd build/tests && ctest -R 'MpeNoteStorageTest|MpeExpressionTest|MpePlaybackTest|ControlNoteExpressionCommandsTest|MidiLearnTest|MidiLearnThreadTest|ProjectRecoveryTest|ReversibilityUndoTest|RevisionTimelineTest|ControlRegistryTest|ControlAutomationScriptTest|AutomationModesTest|ControlAutomationModesTest' --output-on-failure; echo EXIT=$?
```

## 7. Next action

**Fold these six rows into `030/audit`'s `docs/FEATURE-LIST-0.3.0.md`** (the way `c967f8f24`
folded the earlier merge lanes), then re-run the canonical tracker command above: it should
read **THIN-100 = 0, ADDED = 44**, and row 85 will appear in the instrument's own
"probes reach 100 %, the list does not say *in the tree*" section, which is the recorded
decision, not a defect.
