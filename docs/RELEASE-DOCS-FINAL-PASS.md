# Final documentation pass — Zene Studio 0.2.0-alpha (the release line)

**Worktree:** `projects/lmms-fl-research/zene-pa-integration` · **branch** `post-alpha/integration` ·
**entry tip** `18c6da128` ("docs+evidence(control-undo): the mechanism, the stack, the fix and the proof").
**Exit:** two documentation commits (`83a7183dd`, `cae9bc332`) and this report.

Nothing was pushed, no tag was created or moved, no remote was contacted, no rebase/amend/reset/force was
used, and nothing was staged with `git add -A` — every commit below names its paths and its staged set was
read first (`git diff --cached --name-only`). Only `docs/` was written: no `src/`, no `tests/`, and no
build directory (`build/`, `build-ci/`, `build-coverage/`) was configured, built or modified. `build/` and
`build-coverage/` were only read.

---

## 0. Verdict, in one paragraph

The drafted control-surface section is applied to `docs/RELEASE-NOTES-v0.2.0-alpha.md` with its
drafting scaffolding and its discrepancy appendix deleted, its **premise flipped** — the surface is *in*
this release, not prepared onto it — and its command count replaced by five numbers each named with the
thing it was counted over. Two new items are on the "Fixed since the rehearsal build" list (the
`control.undo` SIGSEGV and the optional MCP bridge). The stale scope figures in `docs/CONVENTIONS.md`
(and the same claim in `docs/KNOWN-LIMITATIONS.md`) are corrected to what the tree holds — 242 / 1,263,
not 175 / 1,209 — with the coverage capture framed as the capture it is. `docs/STATUS.md` is verified to
carry its 0.1.0-line banner (it does; nothing to add). No `[VERIFY AT FREEZE]` marker remains in any
shipping document, and the two the applied section carried are resolved by citation. **The honesty gate
could not be shown green: the only build directory in this worktree with the release modules built is
configured `WANT_SESSION_VIEW=ON` and is 59 commits stale, so the gate correctly fails one row. §5 is
that finding, with the command output, and why the edit cannot have caused it.**

---

## 1. What was applied: the control-surface section

Source: `drafts/CONTROL-SURFACE-SECTION-DRAFT.md` (315 lines). Destination:
`docs/RELEASE-NOTES-v0.2.0-alpha.md`. Inserted as **headline 3** under a `## The three headlines` heading
(was `## The two headlines`), between headline 2 and `## What else is new`, with its internal `###`
sub-sections kept.

### 1.1 Scaffolding deleted

- The entire drafting-notes block (draft lines 3–28): "Drafting scaffolding, not release copy", the list
  of sources read, the voice sample, the insertion point, the marker conventions, and the two "structural
  facts" the draft was built on — including "(1) `post-alpha/integration` contains **no** `ControlRegistry`
  …" and "(2) no file in `.github/workflows/` … mentions control, agent or headless".
- The whole discrepancy appendix (draft lines 230–315, seven items).
- The draft's own section title `## The command surface: another program can drive a running Zene Studio`
  — replaced by the `**3. …**` headline lead.

### 1.2 The premise flipped

The surface **is** in the release. Merge train 3F (`b96d01d61`) merged
`post-alpha/agent-surface-onto-integration` into this line, and at this tip the following are all
`git ls-tree -r --name-only HEAD` hits: `include/ControlRegistry.h`, `include/ControlServer.h`,
`include/ControlReversibility.h`, `include/UnattendedRun.h`, `src/core/ControlRegistry.cpp`,
`src/core/ControlServer.cpp`, the twenty `src/core/ControlCommands*.cpp` modules,
`src/core/ControlReversibilityTable.cpp`, `src/core/UnattendedRun.cpp`, `src/core/SessionScheduler.cpp`
(independent), `tests/src/core/Control*Test.cpp`, `tests/agent-surface-gate.py` and
`tools/mcp-zene-control/`. Every phrase implying the surface was absent was **removed, not polished**:

| draft (false now) | applied text |
|---|---|
| "This release's tree does not contain the surface at all." | deleted |
| "`post-alpha/integration` has no `ControlRegistry`, no `ControlServer`, no `ControlCommands*` module and no `UnattendedRun`, and none of the surface's branches is an ancestor of it." | deleted |
| "the two integration branches are 178 commits apart in one direction and 25 in the other off the same base, and a `git merge-tree` dry run conflicts in nine paths" | deleted |
| "**Nothing in CI exercises this surface** — no workflow file on the release tree mentions control, agent or headless." | deleted (the claim is also wrong on its own terms: CI runs `ctest`, and `agent_surface` is one of the 86 tests) |
| "Nothing in this section should be published as a shipped capability until that merge is decided." | deleted |
| `docs/A16-REVERSIBILITY.md — … On post-alpha/agent-surface-onto-integration.` | `… **In this tree.**` |
| `### What is not there` | renamed `### What the surface does not do`, and its first paragraph replaced with the two facts that *are* still limits: it is opt-in, and the opt-in is invisible |

### 1.3 The command count: the numbers written, and why

The draft quoted "**72 rows**" and marked it `[VERIFY AT FREEZE: 72; the same table's prose says 70
registered and 71 rows — see the appendix]`. The applied passage names five figures, each with its
method, because **no single number describes the registry without saying what was counted**:

| number | what it counts | where it comes from |
|---|---|---|
| **74** | the **live registry** — what `control.commands_list` returns and what the agent-surface gate sweeps (**73 swept + 1 allowlisted**, the allowlisted one being the human-gated `telemetry.consent`) | `docs/AGENT-SURFACE-TELEMETRY-FIX.md` §3; and `grep -c '\.id = QStringLiteral' src/core/ControlCommands*.cpp` = **74** distinct ids, on this tree |
| **72** | the **same registry before the telemetry fix** that added the two `telemetry.*` commands | `docs/AGENT-SURFACE-TELEMETRY-FIX.md` §3 (72 → 74) |
| **74** | the **rows of the table shipped as data** in `src/core/ControlReversibilityTable.cpp` (30 `true_inverse`, 5 `snapshot`, 3 `irreversible`, 36 `not_mutating`) | counted on this tree: `grep -cE '^\s*R\('` = 74 |
| **72** | **the A16 contract's classified row count** (`docs/A16-REVERSIBILITY.md` §1: 30 + 5 + 3 + 34) — a different thing that coincides with the pre-fix registry size | the table in that document |
| **70** | the **committed snapshot** the bridge carries for offline discovery (`tools/mcp-zene-control/zene_control/commands_snapshot.json`) | counted on this tree: `len(commands)` = 70 |
| **87** | what a **wider surface branch** registers by its own method (71 + 16 — the sixteen being tail groups such as `record.*`, `import.*`, `export.*` that this release line does not carry) | `POST-ALPHA-PLAN.md:534` / `:581` and `RELEASE-0.2.0-CHECKLIST.md:526` |

**The count I chose and why.** For the registry the applied text leads with **74** and calls it "the live
registry in this build — that is what `control.commands_list` returns and what the agent-surface gate
sweeps", because that is the number a user-facing claim should use and it is the one the tree's own
source reproduces. The draft's **72** is kept but explicitly demoted to *the A16 contract's classified row
count*, and the shipped data table is stated as 74 rows. **The "87" is included with its method named,
not as this line's source count**: on this tree the source registers **74** ids
(`src/core/ControlCommands*.cpp`), so writing "the source defines 87 registered ids" would be a claim the
tree contradicts; 87 is a committed figure about the branch that also carries the `record.*`/`import.*`/
`export.*` groups, and it is labelled as such. (This is also why the draft's appendix item 1 — the
70/71/72 trio inside one document — is not carried: the applied text names every figure instead.)

### 1.4 Other corrections the draft could not know (each verified against this tree)

The draft was written against an earlier tree. Every claim below was checked and corrected in place; the
first four would have shipped false:

1. **`trk-<n>` ids are now persisted.** The draft said ids other than `trk-<n>` are index-derived — true —
   but implied `trk-<n>` merely "survives" a reload. `dcfc57aad` ("feat(control): persistent,
   creation-assigned `trk-<n>` ids") is an ancestor of this tip; `src/core/Track.cpp:232` writes
   `element.setAttribute("id", m_id)` and `:323` reads it, and `tests/src/core/StableTrackIdsTest.cpp`
   checks save → load → save. The applied text says `trk-<n>` is **assigned at creation and written into
   the project file**, and keeps the index-derived half for `clip-`/`note-`/`ch-`/`dev-`
   (`include/ControlVocabulary.h:89-95`).
2. **`plugin.list` enumerates LV2.** The draft's "two smaller gaps" paragraph claimed it enumerates the
   built-in and LADSPA catalogues "but **not LV2**". `src/core/ControlDeviceCatalogue.cpp:136` calls
   `controlLv2DeviceEntries(&out)` and `:176` handles the `lv2` format. That sentence is **deleted** (the
   tree contradicts it); only the `project.restore_revision` gap remains, now introduced as a single gap.
3. **`docs/KNOWN-LIMITATIONS.md` line numbers moved.** The draft's `automation.mode_set` paragraph said
   the modes-work statement is at line 173 and line 84 is about the instrument editor. On this tree,
   `:185` is the "Modes work (Read / Touch / Latch / Write)" line and `:84` is a save/open-integrity
   paragraph. The paragraph now cites `:185` and `:84` correctly and keeps the same finding — the
   refusal's cited reason is stale and is not repeated as a fact.
4. **`AutomatableModel`'s checkpoint moved.** The draft cited `src/core/AutomatableModel.cpp:303`; the
   `addJournalCheckPoint()` on the non-automated write path is at **`:305`** on this tree.
5. **The unqualified "moves seven rows" is gone.** The draft's reconciliation sentence said the
   implementation "moves seven rows" and then named eight. The tree's own A16 doc does not settle it
   either: `docs/A16-REVERSIBILITY.md` §2 says "7 of the false half (plus the two selection commands)"
   while its §1.1 classes `transport.seek`, `settings.set`, `audio.device_set`, `plugin.state_load` and
   `plugin.preset_load` — all `reversible:false` in the measured baseline — as `true_inverse`. Rather than
   publish a count the tree does not settle, the sentence now reads "the report reconciles the rows the
   implementation changes, one by one" and keeps the names.
6. **Two `[UNVERIFIED FROM THE RECORDS]` markers resolved** (see §4).
7. **Minor citations corrected**: `src/core/CMakeLists.txt:61-92` → `:61-93` (the module list runs to
   `ControlSession.cpp` at `:93`); `docs/A16-REVERSIBILITY.md` is cited as *in this tree*.

---

## 2. Two new items on "Fixed since the rehearsal build"

Appended after the failed-save bullet, matching the list's house voice (bold lead, mechanism, a
"Verified against this tree:" witness).

1. **`control.undo` could kill the application — and the GUI's own Ctrl+Z reached the same fault.**
   `control.undo` over the socket did not drop a connection, it **SIGSEGV'd the DAW** (`returncode -11`),
   and every client died with the process. The fault was a null dereference in
   `PatternStore::updateComboBox()` (`src/core/PatternStore.cpp:203`) reached from
   `ProjectJournal::undo()` — *below* the call the GUI's Edit ▸ Undo makes, because the GUI's Ctrl+Z
   **is** `Engine::projectJournal()->undo()` (`src/gui/MainWindow.cpp:1417-1420`), the identical call the
   socket path makes. The mechanism in one line: a pattern-track destructor erased its entry from a static
   registry, a GUI slot then read that registry with `QMap::operator[]`, **which inserts a fabricated
   entry for the dying track**, the allocator reused that address for the replacement track, which derived
   its pattern number from the registry's size, leaving index 0 vacant while the count said 1. Fix: every
   registry read became `QMap::value()` — five in `src/tracks/PatternTrack.cpp`, one in
   `include/PatternTrack.h`, two in `src/gui/tracks/PatternTrackView.cpp` — plus a null guard in
   `updateComboBox()`; declared in `tests/upstream-modifications.txt`.
   **The GUI equivalence is stated as by code identity, not by a measured Ctrl+Z run** — the diagnosing
   lane is headless (`QT_QPA_PLATFORM=offscreen`), did not click Ctrl+Z, and said so; the applied text
   carries that qualification rather than dressing the claim up. Witness:
   `docs/CONTROL-UNDO-CONNECTION-DROP.md` §2 (the gdb stack, `pt = 0x0`, `numOfPatterns() == 1`) and §7
   (the after-state: `control.undo` answers `ok` on a live connection, restores the recorded inverse tempo
   140, the process survives, the render hash is unchanged).
2. **An optional MCP bridge ships for the control surface.** `tools/mcp-zene-control/` is a stdio MCP
   server exposing a *running* instance to an MCP client; it is a **client** (the control socket exists
   without it), holds no command knowledge of its own, generates its tool list from the instance's live
   registry so it cannot advertise a command the DAW lacks, and serves a last-known list (cache, then the
   committed snapshot) when nothing is running. Source: `tools/mcp-zene-control/README.md`.

---

## 3. Staleness found and fixed

### 3.1 `docs/CONVENTIONS.md` — the scope figures were a merge behind

The conventions pass (`f68cf8e51`, "bring the coverage figures to the release tree") set the scope to
**175 / 1,209** and the capture to **119 of 175**. It is an ancestor of the 3F merge (`b96d01d61`), and 3F
merged the control-surface modules and their tests, so both manifests grew after it. Measured on this
tree:

```
$ grep -vE '^[[:space:]]*(#|$)' tests/fork-sources.txt | wc -l          ->  242
$ grep -vE '^[[:space:]]*(#|$)' tests/all-sources.txt  | wc -l          ->  1263
$ git show f68cf8e51:tests/fork-sources.txt | grep -vcE '^\s*(#|$)'     ->  175
$ git merge-base --is-ancestor f68cf8e51 b96d01d61 ; echo $?            ->  0
$ bash tests/fork-sources-gate.sh
  PASS: every tracked source in scope is registered (242 fork-NEW, 1036 inherited, 34 tooling).
```

Fixed: the section heading and both counts (`242` / `1,263`); the coverage paragraph re-framed so the
81.60% / 119-of-175 figure is the **2026-09-12 capture** (scope 175, before 3F grew it) rather than a
ratio over today's 242, with the classifier's categories described as properties of the configuration;
a sentence marking every per-gate measurement below as the 2026-09-11 reading at 99 / 1,095; the two
present-tense "1,095" uses (`:81`, and the table header at `:96`) labelled; the row-1 scope cell
"(99 files)" → "(242 files)"; and `:122`'s "119 of its 175 entries" re-scoped.

### 3.2 `docs/KNOWN-LIMITATIONS.md` — the same claim, the same correction

`:163` carried the identical "over the 119 of the 175 fork-scope entries" sentence and "the 67 files our
gate tracks". Both corrected: the pair is named as the capture's, the 3F growth (175 → 242) is stated,
and the ratchet scope is described as the gate's own per-file baseline rather than given a count I cannot
re-derive. This is not one of the two files the brief named, but it is a shipping document carrying the
same stale claim, so leaving it would have been shipping a number the tree contradicts.

### 3.3 `docs/STATUS.md` — verified, not changed

The 0.1.0-line banner **is present** at the top of the file (added by `79d800ef4`):

```
> **This page describes the tree as verified on 2026-09-11 (the v0.1.0-alpha line).** For the 0.2.0-alpha
> release see [`docs/RELEASE-NOTES-v0.2.0-alpha.md`](RELEASE-NOTES-v0.2.0-alpha.md) and
> [`docs/KNOWN-LIMITATIONS.md`](KNOWN-LIMITATIONS.md).
```

Per the brief's condition ("if the banner is missing, add it"), nothing was added, and the file's
measurements and dates were not rewritten or re-dated.

### 3.4 A discrepancy I found but did **not** change

`docs/CONVENTIONS.md` and `docs/KNOWN-LIMITATIONS.md` both described the ratchet scope as "the 67 files"
the baseline tracks; `tests/coverage-baseline.tsv` holds **75** entries on this tree *and* held 75 at
`f68cf8e51`, so 67 was never the baseline size (it is the older capture's count of records produced). I
removed the count from that clause rather than replace it with 75, because the **85.77%** in the same
sentence is a measured pair whose denominator I cannot re-derive without an instrumented build. Flagged
here rather than guessed.

---

## 4. Markers resolved or deleted

**No `[VERIFY AT FREEZE]` marker is left in any shipping document.** A repository-wide search finds the
token only inside three explanatory passages that *describe* the convention — `RELEASE-NOTES-v0.2.0-alpha.md`
`:7`/`:17`/`:368`, `KNOWN-LIMITATIONS.md:6`, `RELEASE-PREP-0.2.0.md:34` — never as a live marker. This is
unchanged by the pass, and the applied section introduces none.

The two markers the *draft* carried, and the two `[UNVERIFIED FROM THE RECORDS]` markers in its body:

| marker | resolution |
|---|---|
| `[VERIFY AT FREEZE: re-run the exercise against the frozen tree and confirm 36 and 17.]` (draft `:98`) | **Resolved by citation, not by re-running.** The 36-exercised / 17-reversible figures are the recorded baseline of a measurement taken on `post-alpha/agent-surface-integration` @ `059bf6bad`, and the applied text names both the workspace record (`ableton-gap/A16-STATUS-MEASURED.md` line 35) and its in-tree carrier (`docs/A16-REVERSIBILITY.md` §2). The brief asked for "the fact, or delete"; re-running needs a build and a headless instance, so the fact is the record's, and the marker is gone. |
| `[VERIFY AT FREEZE: 72; the same table's prose says 70 registered and 71 rows — see the appendix]` (draft `:125`) | **Resolved by naming the method** — see §1.3. The whole sentence was replaced by the five-figure passage; no marker remains and the draft's appendix (which carried the contradiction) is deleted. |
| `[UNVERIFIED FROM THE RECORDS: whether any test drives the surface against a real audio backend rather than the Dummy device.]` | **Resolved against the tree.** Every surface test runs on the **Dummy** device (the documented headless recipe), and `tests/control-no-audio-device.py` makes the device unopenable on purpose to prove the fallback; the applied text states this positively and says no test drives it against a real backend. |
| `[UNVERIFIED FROM THE RECORDS: whether an in-app indication exists that a socket is listening …]` | **Resolved against the tree.** `grep -rn isAgentInstance src/ include/` returns only `include/UnattendedRun.h:36` and `src/core/UnattendedRun.cpp:66` — no GUI consumer — so the applied text states that nothing in the interface reports a listening socket, and the only reader of `isAgentInstance()` is `UnattendedRun` itself. |

---

## 5. The honesty gate: what was run and what it says

The brief's command was run exactly as given, after the edits:

```
$ bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build ; echo EXIT=$?
=== release honesty: what the release documents vs what the build contains ===
manifest : tests/advertised-features.tsv
build    : build/lmmsversion.h  (generated header; the exact string the binary prints)
artifacts: build

  [PASS] vst3-hosting     ON matches ON; module build/plugins/libvst3effect.so
  [PASS] vst3-instrument-hosting ON matches ON; module build/plugins/libvst3instrument.so
  [PASS] clap-hosting     ON matches ON; module build/plugins/libclapeffect.so
  [FAIL] session-view     WANT_SESSION_VIEW='ON', but the release documents this as present and requires OFF
         claim it must keep true: The Session View (clip launcher and launch scheduler) is documented as absent from these builds
  [PASS] wasm-sandbox     OFF,OFF matches OFF
  [PASS] stem-separation  OFF matches OFF

RESULT: FAIL — 1 of 6 documented feature(s) do not match this build
EXIT=1
```

**It does not print 6/6 PASS, and the reason is the build directory, not the documents.** `build/` in
this worktree is **not the release configuration**:

```
build/lmmsversion.h   LMMS_VERSION "0.1.0-alpha.247+f68cf8e"   (59 commits behind this tip)
build/CMakeCache.txt  WANT_SESSION_VIEW:BOOL=ON
build-coverage/…      LMMS_VERSION "0.1.0-alpha.306+18c6da1"   = this tip (`git describe` → v0.1.0-alpha-306-g18c6da128)
```

`WANT_SESSION_VIEW` defaults `OFF` (`CMakeLists.txt:121`), the release jobs do not pass it, and the
manifest's row for it is `required=OFF` — so a directory configured with it `ON` is a local
configuration, and the gate is right to fail that row. **No directory in this worktree carries the
release configuration with the hosting modules built**, so the gate cannot be shown green here at all:

```
$ bash tests/release-honesty-gate.sh --header build-ci/lmmsversion.h --artifacts build-ci
RESULT: FAIL — 3 of 6 (WANT_VST3/WANT_CLAP say ON but the modules were never built there)
$ bash tests/release-honesty-gate.sh --header build-coverage/lmmsversion.h --artifacts build-coverage
RESULT: FAIL — 3 of 6 (WANT_VST3/WANT_CLAP are 'AUTO', and AUTO is not ON by this gate's rule)
```

**Why the edit cannot have moved it.** The gate reads exactly two inputs — `tests/advertised-features.tsv`
(unchanged: `git diff 18c6da128..HEAD -- tests/advertised-features.tsv` is empty) and the `--header` file —
and this pass touched only `docs/`. The run above is identical to the one made before the edits: the same
six rows, the same single `FAIL` (`session-view`), the same exit 1. On a
correct release build the gate passes: `docs/AGENT-SURFACE-TELEMETRY-FIX.md` §4 records
`release-honesty-gate.sh --header … --artifacts …` → **exit 0, 6 of 6 PASS** on the release configuration,
which is the committed evidence that the gate and the manifest agree; what is missing here is a release
configuration to point it at, not a green result to report.

---

## 6. Findings I did not change, with the reason

1. **`build/` is a stale, non-release directory** (`0.1.0-alpha.247+f68cf8e`, `WANT_SESSION_VIEW=ON`).
   The brief forbids touching it; recorded so the next pass does not read that gate run as a document
   regression.
2. **The manifest's session-view comment may be stale, and the notes' freeze item 6 with it.** The
   `tests/advertised-features.tsv` header says "the audit found no consumer (no scene launcher, no clip
   grid)", and `docs/RELEASE-NOTES-v0.2.0-alpha.md:648` says "there is no clip launcher even with it on".
   `docs/SESSION-SCHEDULER.md` is in this tree and `src/core/SessionScheduler.cpp` /
   `include/SessionScheduler.h` exist behind `WANT_SESSION_VIEW` (its own report: the engine has an
   LAUNCH SCHEDULER; the user-facing grid is `#598`, not done). So "even with it on, nothing launches" is
   arguable at best. I did **not** change either: the brief forbids changing the manifest, and the notes'
   sentence can be read as "no user-facing clip launcher", which holds. Flagged for a decision rather than
   silently rewritten.
3. **The 67-vs-75 ratchet-scope discrepancy** — §3.4.

---

## 7. What was not done, and the limits

- **The honesty gate was not shown green** — §5. Reporting this is the honest form of the brief's own
  instruction ("say so rather than guessing"); the marker this lane cannot clear is a build directory, not
  a document.
- **The 36/17 baseline was not re-measured** — that needs a build and a headless instance; the figures are
  cited to the record instead (§4).
- **No coverage re-run.** The coverage build in `build-coverage/` was running and was not touched; the
  119-of-175 pair is kept as the capture's, with the current scope (242) stated and the measured count
  marked "not yet recorded".
- **The 85.77% ratchet figure's denominator was not re-derived** — §3.4.
- **`docs/RELEASE-NOTES-v0.1.0-alpha.md`, `docs/WAVE-R-RENAME.md`, `docs/phase-f/**`, the integration-merge
  records and `tests/QA-GATES.md`** were left alone: they are dated records of what happened, and
  `RELEASE-PREP-0.2.0.md` §5 already names them as deliberately kept.
- **Untracked files that are not mine** (`docs/release-verification-0.2.0-alpha/`,
  `tests/integration-logs-3d/final/f3c-pre-foreign-verify.log`) were left untracked and uncommitted.

---

## 8. Commits and hygiene

| commit | paths | what |
|---|---|---|
| `83a7183dd` | `docs/RELEASE-NOTES-v0.2.0-alpha.md` | the control-surface section applied, premise flipped, count method-named, two new "Fixed since the rehearsal build" items |
| `cae9bc332` | `docs/CONVENTIONS.md`, `docs/KNOWN-LIMITATIONS.md` | the scope figures corrected to the tree's 242 / 1,263 and the capture re-scoped |
| (this file) | `docs/RELEASE-DOCS-FINAL-PASS.md` | this report |

- Every commit's staged set was read with `git diff --cached --name-only` before committing; neither used
  `git add -A`.
- `git status --short` after the pass shows the two commits' files clean and only the two pre-existing
  untracked paths above.
- No `src/`, `tests/` or build-directory path was written by this pass.
- Nothing was pushed; no tag exists or was created.
