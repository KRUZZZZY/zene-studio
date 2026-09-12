# Integration merges into the Zene Studio release line — merge train 3F (the train that adds something)

Worktree: `projects/lmms-fl-research/zene-pa-foreign` (branch `post-alpha/foreign-merge`).
Entry tip: **`09e313c19`** — the tip train 3E left, with `git status --porcelain` empty apart from this
train's own evidence directory. Exit: **two merge commits** — `b96d01d61` (unit 1) and `c639bbd61`
(unit 2) — **one labelled fix-up commit** `0ddbb1ca1` between them, the evidence commit `1fab5e4f1`
(this report included) and the tip-verification commit that follows it.
`git log --oneline --merges --first-parent 09e313c19..HEAD` names exactly the train's own two merges
without this report having to guess its own tip. **`--first-parent` is load-bearing:** without it the
unit's own history contributes eleven more merges to that output (13 in total), because unit 1 merged a
whole wave of lane branches inside its own line — one more way this train is not 3E's shape.

Nothing was pushed; no remote, PR, issue or **tag** was touched; `origin` (LMMS/lmms) and `messmerd`
were never contacted; no rebase, amend, reset or force; nothing staged with `git add -A` (every
`git diff --cached --name-only` was read before every commit). Every exit code below was measured
unpiped (`cmd > log 2>&1; echo EXIT=$?`) and every log is committed under `tests/integration-logs-3f/`,
never `/tmp`. One build directory (`build/`, this worktree's own), `JOBS=2 bash tools/local-ci.sh
--build-dir build --jobs 4`. `df -h /home`: **52 GB free at entry, 49 GB at exit** (this train's evidence
is 8.2 MB committed; the 3 GB is the build of the unit's 79 new files).

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` (ten gates) · plus the release honesty guard, which the brief's third proof needs.
Gate 2 (coverage) was **not** run — see "What is NOT proven".

## THE HEADLINE: 3E's opposite, and the union of two green sides is red

Train 3E measured five units and found its entry tree and its exit tree differed by **nothing**.
This train is the inverse, and the brief was right about that:

```
$ git diff --shortstat 09e313c19 b96d01d61          # merge 1's own content
 107 files changed, 22449 insertions(+), 131 deletions(-)
$ git diff --name-status 09e313c19 b96d01d61 | awk '{print $1}' | sort | uniq -c
  79 A        <- 79 files the release line did not have at all
  28 M
$ git diff --shortstat 0ddbb1ca1 c639bbd61          # merge 2's own content
 1 file changed, 12 insertions(+)
$ git diff --shortstat 09e313c19 1f33dd200 -- . ':(exclude)tests/integration-logs-3f' ':(exclude)docs/INTEGRATION-MERGES-3F.md'
 108 files changed, 22466 insertions(+), 131 deletions(-)     # product only, evidence excluded
```

(The merge-1 commit message quotes `+22,421/−676`; that was the *staged index mid-resolution*, before the
last two registry resolutions were staged. The committed figure is `+22,449/−131`, above. Corrected here
rather than amended — this train rewrites no history.)

**And the two sides are individually green while their union is not.** The incoming unit ships an
anti-drift gate (`tests/agent-surface-gate.py`, ctest name `agent_surface`) that reflects the *live*
menu surface over its own control socket and fails on any menu action with no registered command,
grandfathering the rest in `tests/agent-surface-baseline.txt` under a shrink-only ratchet. The release
line's own `post-alpha/telemetry` lane added `Help > Telemetry — what we send…` (`82d2e1309`). Neither
side has ever seen the other, so on the merged tree:

```
FAIL: NEW action without a registered command: 'menu:Help/Telemetry - what we send...' (surface=menu container=Help)
  reflection : 3 registered, 43 unregistered (42 grandfathered, 1 new)
  ratchet    : baseline 42 entries, 0 stale
```

`ctest` is therefore **85/86, 1 failed** at the tip of this train. It is left failing **deliberately**:
my brief forbids re-anchoring a ratchet and forbids changing an expectation to make a merge green, and
the two honest repairs (register a command for the action, or authorise a baseline entry) are both
product/owner decisions — decision #1. This is the sharpest instance the merge trains have produced of
the lesson 3E wrote down: **mergeable is not additive, and additive is not green.**

## The two merges, in the order the parent specified

| # | unit | merged from (pinned sha) | merge commit | conflicts | content |
|---|---|---|---|---|---|
| 1 | `post-alpha/agent-surface-onto-integration` | `f8715fa423e44f2dcc260fa2ac09456b0d39843f` | `b96d01d61` | 2 | **107 paths: 79 added, 28 modified, +22,449/−131** |
| — | (fix-up: the unit's two `-Werror` failures) | — | `0ddbb1ca1` | — | 3 files |
| 2 | `fix/latency-complexity` | `df9944ba1d3ffaa302bc8bafaa018b40218d794c` | `c639bbd61` | 3 | **1 file, +12 lines** (everything else already in this line) |

Both pins were still their branches' tips when the train started (`git rev-parse
post-alpha/agent-surface-onto-integration` = `345d4b18c`, one commit *ahead* of the pin — the pin was
merged, as briefed; `fix/latency-complexity` = the pin exactly).

### The conflict forecast, and one place the brief and the tree disagree

The brief states `git merge-tree` shows unit 1 with **zero conflicts**. Re-measured on this branch
before anything was touched (`tests/integration-logs-3f/preflight/merge-tree-preview.log`), unit 1 shows
**two**, and they are exactly the two the merge then produced:
`tests/fork-sources.txt`, `tests/upstream-modifications.txt`. Train 3E's own collision preview in
`docs/INTEGRATION-MERGES-3E.md` predicted these same two — so the tree agrees with 3E and disagrees with
the brief, and the *shape* of the brief's warning survives: the two conflicts are both registries, and
**every product-code hunk of unit 1 arrived through the automatic merge with no flag at all.** Unit 2's
three predicted conflicts were exact (`.github/ISSUE_TEMPLATE/alpha-feedback.yml`,
`docs/KNOWN-LIMITATIONS.md`, `tests/upstream-modifications.txt`).

## Every product-code hunk the automatic merge resolved, and how each was read

Unit 1 changes 108 paths against its own merge base (`3c245cb8a`, a 3B-era merge): **79 new files and 29
edited ones**. The edited set splits cleanly, and the split is the reading plan:

**(a) 19 of the 29 edited files are byte-identical to the merge base on this branch.** For those, the
automatic merge cannot interleave anything — the merged file *is* the unit's file. Asserted, not assumed:

```
$ for f in include/AudioEngine.h include/Effect.h include/EffectChain.h include/MidiLearnGui.h \
    include/ProjectJournal.h include/Track.h plugins/LadspaEffect/LadspaEffect.cpp \
    plugins/MidiImport/MidiImport.cpp src/core/AudioEngine.cpp src/core/ConfigManager.cpp \
    src/core/DataFile.cpp src/core/ImportFilter.cpp src/core/PeakController.cpp src/core/Plugin.cpp \
    src/core/ProjectJournal.cpp src/core/SampleBuffer.cpp src/core/Track.cpp \
    src/core/TrackContainer.cpp src/gui/GuiApplication.cpp; do
      [ "$(git rev-parse 3c245cb8a:$f)" = "$(git rev-parse 09e313c19:$f)" ] && echo "SAME $f"; done
SAME (all 19)
$ git hash-object include/ProjectJournal.h            # after the merge
28cc5bbcc…   ==  git rev-parse f8715fa42:include/ProjectJournal.h   -> IDENTICAL-TO-UNIT
```
Their hunks were **read** anyway, and they are one class, in full: **modal-dialog guards for the
unattended run** (`if (gui::getGUI() != nullptr && !lmms::isUnattendedRun())` in `DataFile.cpp` ×4,
`ConfigManager.cpp` ×2, `Plugin.cpp` ×2, `SampleBuffer.cpp` ×2, `ImportFilter.cpp` ×2,
`LadspaEffect.cpp` ×4 with the reason redirected to `qWarning()`, `MidiImport.cpp` ×2,
`PeakController.cpp` ×1, `TrackContainer.cpp` ×1 progress window), plus four small additive accessors:
`AudioEngine::audioDevRequestName()/audioDevStartReason()` + their two members,
`Effect::setEnabled()`, `EffectChain::effects()`, `MidiLearnGui.h`'s forward declaration → `#include
<QAction>`. Nothing here can double-lock or fork a render path: they add early-exit branches and
accessors and change no existing control flow. **The one semantic judgement recorded:** `Effect::setEnabled()`
and `EffectChain::effects()/`ProjectJournal`'s new API are *new surface* on classes the release line also
evolved — `EffectChain.cpp`/`Effect.cpp` are in the identical-to-base set, so the unit's method bodies are
the only bodies, and the release line's own callers (`src/gui/...`, `docks/`) were checked for name
collisions on the new members: none.

**(b) The six files BOTH sides changed are the whole semantic risk, and each was read hunk by hunk.**

| file | ours since the merge base | the unit's change | how it combined |
|---|---|---|---|
| `CMakeLists.txt` | `VERSION_MINOR 1→2`, `PROJECT_EMAIL` → the repo's issue URL, `ZENE_TELEMETRY` option | `VERSION_MINOR 1→2` — the same change on the same line | merged; **the result is `git diff`-identical to ours**, which is why this file is not in the merge's modified list at all |
| `include/Song.h` | `SessionScheduler` member/accessors inside the `#ifdef LMMS_HAVE_SESSION_VIEW` blocks (lines 44, 386, 531) | `errors()`/`loadRefusal()` + `m_loadRefusal` beside `errorSummary()` (line 105) | disjoint regions; merged file has both (grep-verified: `sessionScheduler()` ×2, `loadRefusal()` ×1) |
| `src/core/Song.cpp` | `SessionScheduler::processAudio` in `processNextBuffer()` + the per-track session takeover; `m_sessionScheduler.reset()` in `clearProject()` | `ProjectIds::reset()/beginLoad()`, the `next-id` root attribute, the duplicate-id repair pass, `m_loadRefusal`, the two `isUnattendedRun()` guards | adjacent but disjoint functions. Merged file carries **both**: `m_sessionScheduler.processAudio(` ×1, `trackIsSessionActive(` ×1, `ProjectIds::reset();` ×2, `ProjectIds::beginLoad();` ×1, `setAttribute("next-id", …)` ×1, `m_loadRefusal.clear()` ×1, and `isUnattendedRun()` at exactly the unit's 2 sites. Read against both sides: neither side's loop/render path was replaced by the other's |
| `src/core/main.cpp` | the `master` action and **the whole render branch restructured** into `if( mastering ) {…} else { <render> }`; `outputSpecified`→`outputGiven`; `scriptExitCode`→`headlessExitCode` | `--control-socket` parsing + `setAgentInstance`, the `ControlServer` brought up *before* the engine, the unattended recovery prompt, `setReady`/`applyPendingQuit`/`cancelShutdownGuard`/`close` around `app->exec()` | **the 3C class, checked first and hardest.** The unit's hunks never reference `outputSpecified`/`scriptExitCode` (only the deleted declaration line, as *context*), so ours' rename wins everywhere and nothing resurrects the old name: the merged file's only `outputSpecified` is inside ours' own explanatory comment. One `if( mastering )` (line 937, `printMasteringReport` inside), one `else`, `r->renderProject()` still in the else branch (line 1008), the `ControlServer` block at line 901 **before** `if( !renderOut.isEmpty() )` at 918, `--control-socket` in `printHelp` at 231 beside the `master` usage at 182 — one implementation of each, no second render path |
| `src/gui/MainWindow.cpp` | the telemetry consent action in `finalize()`'s Help menu (line 407 block) | the A11/A15 `setData("control.undo"/"control.redo"/"midi.learn_toggle")` declarations, the unattended guards for the setup/audio dialogs, `ControlRegistry::quitPromptAnswer()` in `mayChangeProject()`, `toggleMidiLearn()` invoking the registry command | disjoint regions; both present exactly once (`TelemetryConsentDialog dialog(` ×1, `quitPromptAnswer` ×1, `interactive` ×1). **Observation, unit's own, preserved not "fixed":** its copy includes `#include "AudioEngine.h"` **twice** (lines 43 and 45) — harmless (include guards) and the unit's own history, not a merge artefact |
| `tests/CMakeLists.txt` | the release line's test list and its per-test properties | 7 new QtTest sources, 9 new Python `add_test`s, the `ENABLE_EXPORTS` list extended to `ControlDeviceCatalogueTest`/`ReversibilityContractTest`/`ReversibilityUndoTest`, offscreen properties | disjoint additions; the merged list registers all 16 and ctest reports 86 |

**(c) The 79 new files** are the control surface itself — `include/ControlRegistry.h` (327 lines),
`ControlServer.h`, `ControlVocabulary.h`, `ControlEdit.h`, `ControlReversibility.h`,
`ControlAutomationSupport.h`, `ControlDeviceSupport.h`, `ProjectIds.h`, `ProjectRevisions.h`,
`UnattendedRun.h`, ~34 `src/core/Control*.cpp` modules, `ProjectIds.cpp`, `ProjectRevisions.cpp`,
`UnattendedRun.cpp`, the 13 `tests/*.py` harnesses, 8 test sources, 4 fixtures, and the reports
`CMDN-REPORT.md`, `CMDN-TRANSCRIPT.md`, `docs/A16-REVERSIBILITY.md`, `docs/VERSIONING.md`. They cannot
conflict (nothing of ours touches those paths) but the build and the suite are the instrument that says
whether they work, and they are what the 16 new ctest entries exercise.

## The two defects the merge exposed, and the labelled fix-up

**Merge 1 does not build under the release configuration, and that is the unit's defect, not a merge
artefact.** `-DUSE_WERROR=ON` is part of the release's own `CMAKE_OPTS`:

```
src/core/Track.cpp:60: error: 'lmms::Track::m_mutedBeforeSolo' will be initialized after
    [-Werror=reorder] ... 'lmms::BoolModel lmms::Track::m_mutedModel'
tests/src/core/ReversibilityContractTest.cpp:130:34: error: unused variable 'registry' [-Werror=unused-variable]
gmake: *** [Makefile:156: all] Error 2                                  LOCAL_CI_EXIT=1
```

Both files are **byte-identical to the unit's own blobs** (and, for `Track.cpp`/`Track.h`, to the merge
base on this branch), so nothing this branch or this merge wrote is implicated. The unit's initialiser
list puts `m_mutedBeforeSolo( false )` before `m_mutedModel(…)` while the header declares the member
after it. Recorded, not hidden: **the merge commit `b96d01d61` does not build**, its message says so with
the exact error, and the repair is the next commit (`0ddbb1ca1`) so the audit trail shows what arrived.
The fix moves the entry to declaration order — C++ initialises in declaration order whatever the list
says and no initialiser here reads another member, so it is a **no-op at runtime**; the unused local is
deleted. **No test expectation, assertion, tolerance, baseline or gate threshold was touched** by the
fix-up. `cmake --build build -j4` → `BUILD_EXIT=0` afterwards.

Also recorded as an **UNVERIFIED** claim rather than a finding: the unit's own branch presumably does not
build with `-Werror` either (its files are what fails), which would mean its `StableTrackIdsTest`,
`ReversibilityContractTest` and `ReversibilityUndoTest` never compiled under this configuration on its
own line. I did not build the unit's branch to prove it, so it is a question, not a claim.

## The three registry conflicts, each resolved as an asserted union

**(1) `tests/fork-sources.txt` — the one genuinely hard decision in the train.** This file says of itself
that its entry list is "the byte-exact output of the command under *Regenerate with*". The incoming
unit's copy is **not**: its own header says so, at length — its 216 entries are a deliberate hand-built
union of which its command prints **195**, the 21 hand-registered entries being 13 `tests/*.py` harnesses
and 8 `tests/src/core/*` sources registered "so the fork-scoped ratchets measure them", including a
deliberate 474/216-line split of one harness "so the ratchets measure both — a split that hides half the
code from the gate would be a dodge".

So the two rules in the brief collide: *every entry from both sides* versus *every manifest re-derived
and proved `ALL-REPRODUCE`*. Taking theirs stops the file reproducing; taking ours-re-derived drops 21
deliberate registrations and narrows the fork scope. **Resolved by doing both**: the file keeps ours'
header, keeps its list reproducing, and **admits the 21** by extending the file's own documented command —
which is what this file's header already records doing for `SampleClipWindowTest.cpp` ("The awk allow-list
above was extended by exactly those two names, and the file now reproduces from the command under
*Regenerate with*"):

* the awk allow-list gains the eight `tests/src/core` sources,
  `ReversibilityTestSupport.h` joins the `TestSupport.h` exclusion, and
* a **third explicit pathspec line** admits the thirteen `tests/*.py` harnesses by name — the same
  "admitted by name rather than by widening the extension filter" shape the `tools/` line already uses.

Proof that the extension is faithful rather than convenient: the amended command prints **exactly 21**
entries more than ours and they are **the unit's 21 and nothing else** (`+21 -0`, listed in
`tests/integration-logs-3f/merge1/`). Result: **241 entries, `+0 -0 REPRODUCES`** — and gates 4/7/8
(complexity, file-length, duplication) then **PASS** with those 21 inside the fork scope, so the widening
is measured, not asserted. The file's header carries a new recorded section saying all of this. This is
decision #2 for the owner only in the sense that it is the most contestable call in the train.

**(2) `tests/upstream-modifications.txt` — a true entry union, twice.** 439 entries ours + 8 theirs-only =
**447**; reasons: 11 kept-ours (the branch's copy was the merge base's), 10 superset, 6 clause-union;
**0 lost, 0 duplicate path, 0 blank reason, no `;;`**, and nothing resurrected — measured explicitly:
neither side deletes an entry the base had (`comm -23 base ours` and `comm -23 base theirs` are both
empty). `resolve_pair.py` asserts every resolved reason is traceable to a side. Unit 2's ledger conflict
resolves the same way to **447 + 0 theirs-only** (its single added entry is already here).
*Lesson recorded in the tool itself:* `resolve_pair.py` reads the three index stages, so it **destroys the
file if re-run after the resolution is staged** — I measured that (a re-run zeroed the ledger, and Gate 6
would have caught it); the merge was restarted and the note is now in the tool's header.

**(3) `.github/ISSUE_TEMPLATE/alpha-feedback.yml` — kept ours, superset asserted by entry set.** 9/9
`id`s, 9/9 labels, 10/10 field types, **0 branch-only entries missing, no ours-only label**. Ours is the
0.2.0 template: it already carries the unit's naming changes and adds two things the unit's copy lacks
(the crash-report file as the one attachment a report may carry, and the untagged-version wording).

**(4) `tests/all-sources.txt`** auto-merged and was regenerated from its own command anyway: **1,210 →
1,262** — the release line was missing the unit's `StableTrackIdsTest.cpp` entry, a registration gap the
re-derivation closed. `tests/tools-sources.txt` is unchanged at 18.

**Manifest reproducibility, per state** (each file's own documented command, run against the index *and*
against `HEAD`; two independent runners: `run_manifest_cmd.py`, which extracts the command **from the
file's own header**, and 3E's `regen.py` transcription updated to match):

| after | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | verdict |
|---|---|---|---|---|
| entry (`09e313c19`) | 175 | 1,210 | 18 | baseline (`regen.py` at 3E's tip) |
| merge 1 (`b96d01d61`) | 241 | 1,262 | 18 | `ALL-REPRODUCE` (index) |
| fix-up (`0ddbb1ca1`) | 241 | 1,262 | 18 | `ALL-REPRODUCE` (index **and** HEAD) |
| merge 2 (`c639bbd61`) | 241 | 1,262 | 18 | `ALL-REPRODUCE` (index **and** HEAD) |

`REGEN_INDEX_EXIT=0` and `REGEN_HEAD_EXIT=0` at every state from the fix-up onward; the merge-1
`REGEN_HEAD_EXIT=1` is expected and is not a defect — the merge was not committed yet, so the `HEAD`
form of the command legitimately could not see it (the index form, which is the pre-commit check, was
`0`).

## The three gates, re-run after every merge (exit codes unpiped)

| state | `local-ci` (build+ctest) | Gate 9 | Gate 6 | ten gates | `regen` INDEX/HEAD | precommit | contract | honesty |
|---|---|---|---|---|---|---|---|---|
| entry `09e313c19` | **0** — 70/70 | 0 — 175 fork-NEW, 1,036 inherited, 18 tooling | 0 — 427 paths, 439 entries | **3** (`PASS-WITH-SKIPS`) | 0 / 0 | 0 | 6 rows | 0 — all 6 PASS |
| merge 1 `b96d01d61` | **1** — `-Werror` build failure (§ above) | 0 | 0 — 427 paths, 447 entries | 1 (gate 1 FAIL) | 0 / 1 (pre-commit, see above) | 0 | 6 rows | 0 |
| fix-up `0ddbb1ca1` | **1** — 85/86, `agent_surface` | 0 — 241 fork-NEW | 0 — 435 paths, 447 entries | 1 (gate 1 FAIL, other nine PASS/SKIP) | 0 / 0 | 0 | **6 rows** | **0 — all 6 PASS** |
| merge 2 `c639bbd61` | **1** — 85/86, `agent_surface` | 0 — 241 fork-NEW | 0 — 435 paths, 447 entries | 1 (gate 1 FAIL, other nine PASS/SKIP) | 0 / 0 | 0 | **6 rows** | **0 — all 6 PASS** |

Gate 9 reads **241 fork-NEW** (175 → 241: the unit's 45 new fork sources plus the 21 admitted), 1,036
inherited and 18 tooling, **0 stale**. Gate 6 declares **435 changed paths against 447 ledger entries**
(427/439 at entry — the 8 new declarations are the unit's). `run-all-gates.sh` is **1, not 3**, because
gate 1 is red; gates 2–10 are `2 SKIP, 3 PASS, 4 PASS, 5 PASS, 6 PASS, 7 PASS, 8 PASS, 9 PASS, 10 PASS`.
Gate 1 is red *because of* the `agent_surface` finding, not because of anything else. Pre-commit replay
of Gate 6's rule against the index (`precommit_check.py INDEX`) → `PRECOMMIT_EXIT=0`.

### The new ctest count, and the tests that arrived

**70 → 86.** The 16 that arrived with unit 1, all of them its own:

```
ControlAutomationScriptTest   ControlDeviceCatalogueTest   ControlEditCommandsTest
ControlRegistryTest           ReversibilityContractTest    ReversibilityUndoTest
StableTrackIdsTest            ControlSocketIntegration     ControlShutdown
ControlReadiness              ControlNoAudioDevice         ControlNegativeControl
agent_surface                 ControlHeadlessProjectOpen    ControlHeadlessWorkingDirectory
ControlHeadlessNoAudioDevice
```

Seven are QtTest binaries in the `LMMS_TESTS` loop; nine are Python `add_test` entries that start the
real binary under `QT_QPA_PLATFORM=offscreen` and drive it over the socket (`ControlShutdown`,
`ControlReadiness`, `ControlNoAudioDevice` and `ControlNegativeControl` carry the unit's own negative
controls; `agent_surface` is the anti-drift gate). **15 of the 16 pass; `agent_surface` fails** — the
headline finding. The ctest summary is printed from `build/tests`; the top-level build directory has no
`CTestTestfile.cmake` and reports 0 tests, which is an error, not a pass.

## The three verifications the brief demanded, proved on the merged tree

1. **No new build option; the capability contract is still six rows; the honesty guard passes.**
   `tests/advertised-features.tsv` has **6** data rows before and after (`CONTRACT_ROWS=6` at every
   state), and the release honesty guard judges the built binary's own `Build options:` line:
   `bash tests/release-honesty-gate.sh --dump …/version.txt` → **`HONESTY_EXIT=0`, 6 of 6 PASS**
   (`WANT_VST3=ON`, `WANT_CLAP=ON`, `WANT_SESSION_VIEW=OFF`, `WANT_WASM=OFF`, `WANT_STEM_SPLIT=OFF`),
   with the unit merged. The unit's build has no `WANT_*` of its own: nothing in its 79 files or its
   CMake edits adds an option, and the contract file is byte-identical to the entry tip.
   (`build/zene --version` → `Zene Studio 0.1.0-alpha.260+09e313c`, unchanged in kind from 3E's
   `…247+f68cf8e`: the untagged `git describe` string, not a defect.)
2. **Nothing is compiled out.** All 40 `Control*`/`ProjectIds`/`UnattendedRun`/`ProjectRevisions`
   lines in `src/core/CMakeLists.txt` sit at **IF-nesting depth 0** (script-computed), and none of the
   unit's new sources carries an `LMMS_HAVE_*` gate — the `#ifndef` hits are include guards, as the
   brief said. The only conditional mention of them is the `AUTOMOC` header listing, which is the same
   shape the tree already uses for `StemSplitController.h`.
3. **The rename is intact.** The merged tree's project root element is `zene-project`
   (`src/core/DataFile.cpp:129`, `:137`, `:321`), with the legacy `lmms-project` root still *read*
   (`:1749-1750`) — the rename's one-way writer, not undone. **None** of the unit's 79 new files contains
   a user-visible `LMMS` string (`grep -ln '"LMMS|LMMS Software|lmms.sf.net|tr( "LMMS'` over the added
   files → no hits). Its deliberate residues are the same two the release page documents: the `lmms::`
   namespace and the `LMMS_*` macros.

## The render — nothing moved, not the audio and not a tag

```
$ bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o …/render-final-1.wav
RENDER_1_EXIT=0    RENDER_2_EXIT=0    (run-to-run identical: the same-build floor is 0)
file sha256   943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526   (×2, 2,177,120 B)
```

`943e3238…` is the value trains 3B, 3C, 3D and 3E each confirmed, so this is the **fifth** independent
confirmation and it is stronger than a hash comparison:

```
$ cmp tests/integration-logs-3f/final/render-final-1.wav tests/integration-logs-3e/final/render-final-1.wav
CMP_VS_3E=IDENTICAL
```

**The chunk comparison the brief asked for** (`tests/integration-logs-3f/final/chunk-parse.log`, from
this train's copy of 3E's parser, fields labelled from the actual offsets):

| | file sha256 | `fmt ` | `data` chunk (2,177,024 B) | `LIST`/`INFO` |
|---|---|---|---|---|
| 3A's artifact | `6b51f70f…` | 2 ch / 16 bit / 44,100 Hz | **`b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`** | `ISFT = "LMMS (libsndfile-1.2.2)"` |
| 3B/3C/3D/3E | `943e3238…` | identical | **`b37cefc5…`** (same) | `ISFT = "Zene Studio (libsndfile-1.2.2)"` |
| **this train** | **`943e3238…`** | identical | **`b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`** | `ISFT = "Zene Studio (libsndfile-1.2.2)"` |

**Which changed: neither the data chunk nor a header tag — nothing.** The repo's own comparator agrees
against both 3E's and 3A's committed artifacts (`COMPARE_3E_EXIT=0`, `COMPARE_3A_EXIT=0`):
**0 differing frames (0.000000 %), max |Δ| 0 LSB (−inf dBFS), 0 of 2,126 periods dirty, best lag 0
frames.** So the control surface's ~20,000 added lines — including the audio-adjacent guards on
`AudioEngine`, `Track`, `Song` and `ProjectJournal` — move the shipped render by not one sample. **No
release-stopping audio difference exists in this train.**

## Unit 2: the port into `docs/KNOWN-LIMITATIONS.md`, and what was refused

The brief's one unbreakable instruction was honoured: the page was **not substituted**. Its current text
(285 lines, reconciled at the 0.2.0 freeze, marker-resolved hours before this train) is untouched except
for **one added bullet**, and the port is the merge-2 resolution rather than a post-hoc edit:

```
$ git diff --cached --stat HEAD          # the whole of merge 2
 docs/KNOWN-LIMITATIONS.md | 12 ++++++++++++
```

Straight answers to "which of its sentences are still true against this tree", measured by asking which
of the unit's **59 added lines** the current page lacks (47) and then classifying each block:

* **Ported (1 bullet, 12 lines):** *"Packages come from the release page, and only from there."* It is
  the only sentence in the unit's edit with no counterpart anywhere on the current page, and it is
  **verifiable in the tree**: six of `build.yml`'s seven `upload-artifact` steps carry
  `if: startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'`
  (`:147`, `:278`, `:420`, `:545`, `:717`, `:825` — the seventh, `:688`, uploads the ctest log on
  failure and is commented as "evidence, not a package"), and the triggers are `push`,
  `pull_request`, `workflow_dispatch`. The port cites those lines.
* **Refused — its `## Getting it running` section (FUSE 2 / `--appimage-extract-and-run` / no menu
  entry; SmartScreen; Gatekeeper).** The current page excludes this **on the record**: every step
  describes how a *packaged* artefact behaves, and no artefact of this release exists on this machine to
  re-check it against; it is preserved as `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`. Porting it would have
  been a **silent revert of a decision made hours ago** — the single worst outcome the brief named — so
  it was left out, deliberately, and this is the most important sentence in this section.
* **Refused — its `The release page shows a SHA-256 digest next to each file` sentence.** Not
  established for this release: `docs/RELEASE-PREP-0.2.0.md` lists the digest block as *"Pending by
  design — needs the published artefacts"* and `docs/RELEASE-NOTES-v0.2.0-alpha.md:389` says the same.
  True of the 0.1.0 release and its preserved page; a claim this page must not make yet.
* **Refused — its title and version strings (`… 0.1.0-alpha …`, "a v0.1 project").** False on a page
  whose subject is 0.2.0-alpha.
* **Already carried, in a newer and stricter form (`Superseded`):** the alpha and backup bullets; the
  unsigned-builds bullet; the compiled-out-features content (its `## Scripting and AI DSP` section →
  the current page's "Two features are compiled out of these builds", which cites `build/lmmsversion.h`);
  `No project-format stability promise yet` → the current page's much stronger *"an older 1.3-alpha build
  will OPEN a 0.2 project and silently drop parts of it"*; the macOS paragraph; the
  `LMMS 1.3.0-alpha`/`LMMS 0.1.0-alpha` provenance sentence (wrapped differently, content identical).

Its other four files need no porting at all: `cmake/modules/VersionInfo.cmake` and
`docs/RELEASE-NOTES-v0.1.0-alpha.md` are **byte-identical to this line's copies**, its
`tests/upstream-modifications.txt` entry is already here, and the `alpha-feedback.yml` conflict resolved
to ours as an asserted superset. **So unit 2's entire residue in this line is those 12 lines** — the
sharpest possible demonstration of the train's premise: unit 1 was the new content, unit 2 was a
re-issue with one page-shaped exception.

## Verification on the committed tip (the evidence files trip no gate)

The whole bundle was run once more **on the committed evidence commit** (`1fab5e4f1`, i.e. with this
train's 159 evidence files tracked), because a new `.py`/`.sh`/`.md`/`.wav` class under `tests/` is
exactly what Gates 9, 10 and 7 read:

```
LOCAL_CI_EXIT=1 (85/86, agent_surface)   GATE9_EXIT=0   GATE6_EXIT=0   RUN_ALL_GATES_EXIT=1
REGEN_INDEX_EXIT=0   REGEN_HEAD_EXIT=0   PRECOMMIT_EXIT=0   CONTRACT_ROWS=6   HONESTY_EXIT=0
Gate 9: 241 fork-NEW · Gate 6: 435 changed paths / 447 entries · gates 3–10 PASS, gate 2 SKIP, gate 1 FAIL
```

**Every number is identical to the merge-2 state**, which is the point: the evidence commit itself
changes no gate result, and the one red gate is the same `agent_surface` finding. `tests/integration-logs-3f/tip/`
holds it, and `git status --porcelain` is clean apart from that directory before it is committed.

## Test expectations changed during this train

**One test file changed behaviour, and no expectation was touched to accommodate it.** The count of
assertions, tolerances, expectations, exemptions, baselines and gate thresholds changed by the merges is
**zero**: `--reanchor` was used nowhere; `tests/file-length-baseline*.tsv`,
`tests/complexity-baseline*.tsv`, `tests/file-length-exempt.txt`,
`tests/coverage-entry-floor-exempt.txt`, `tests/agent-surface-baseline.txt` (still 42 entries) and
`tests/agent-surface-allowlist.txt` (still 0) are all untouched. The only edits to test *code* are:

* `tests/src/core/ReversibilityContractTest.cpp` — **one unused local deleted** so the release's
  `-Werror=unused-variable` build succeeds. Its assertions are unchanged.
* `tests/fork-sources.txt` — the manifest entry set, discussed above; an entry-set widening, not a
  threshold move.

The one red test (`agent_surface`) was **not** touched, exempted, re-anchored or deleted.

## What looked like competing work

* **The other control-surface branch is still unmerged and now conflicts more.**
  `post-alpha/agent-surface-integration` (`67b26c023`) is neither an ancestor of this branch nor of the
  pin — the pin integrated a **differently-tipped copy** of that work (`e92bd1bd8`, "resolved, scratch"),
  and the branch ref still exists at its own tip. Any later attempt to merge it collides with content
  that is now in `HEAD` by another route. Nothing was merged off-list; this is a report.
* **The unit carries a large wave of other lanes' work transitively**, which is worth naming because it
  means this merge is bigger than its pin suggests: its history contains
  `post-alpha/cmd-plugins` (`1f29badb3`), `post-alpha/cmd-notes` (`79c5c369a`), `post-alpha/cmd-automation`
  (`dfc3ba13e`), `post-alpha/control-hardening` (`4950938b9`), `post-alpha/agent-surface-gate`
  (`c43910ea7`), `post-alpha/headless-load` (`7e844b458`), `post-alpha/lv2-catalogue` (`c366d1bbd`),
  `post-alpha/stable-ids` (`f29414ff4`), `post-alpha/reversibility` (`3cecfa9eb`) and
  `post-alpha/gate-fix` (`904e5a599`). Those lane refs are now reachable from this branch; a later train
  that finds one of them "unmerged" would be re-merging something this train already brought in.
* **In-file competition inside the unit, preserved and reported, not harmonised:** the duplicated
  `#include "AudioEngine.h"` in `src/gui/MainWindow.cpp`; the unit's `fork-sources.txt` header documents
  a hand-union that its own command contradicts (handled above); and the unit's
  `docs/VERSIONING.md`, `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` are that line's records arriving in
  the product tree — new files, no collision, and not this train's to curate.
* **A pre-existing staleness in the current page, reported rather than edited:** its Platforms section
  still says a build from the release tag reports `LMMS 0.1.0-alpha` on a page whose subject is
  0.2.0-alpha. It arrived with the reconciled page, is not this unit's doing, and fixing it is an
  editorial act outside a merge train.

## Decisions needed from the owner

1. **`agent_surface` is red at this tip, and only the owner can settle it.** The release line's
   telemetry action has no registered control command; the incoming gate says a menu action without one
   must not exist. Two honest repairs: (a) a product lane registers a `telemetry.*`/`app.*` command for
   the action (the gate then passes with no baseline change), or (b) the owner authorises an entry in
   `tests/agent-surface-baseline.txt` — which **grows** a shrink-only ratchet and is therefore the
   owner's call, not a train's. I did neither.
2. **`tests/fork-sources.txt` now admits the unit's 21 hand-registered entries** by extending its own
   documented command, so the file both reproduces (241, `+0 -0`) and loses nothing — and the fork scope
   now measures 13 `tests/*.py` harnesses and 8 test sources it did not measure before (gates 4/7/8
   PASS). The alternative is to revert to a 220-entry file that reproduces and silently drops those 21.
   The call is defensible either way; I chose not to drop another programme's deliberate registrations.
3. **`b96d01d61` (merge 1) does not build; `0ddbb1ca1` repairs it.** The merge commit is deliberately
   left unbuildable-but-labelled so the record shows what arrived. If the owner prefers merge commits
   that build on their own, the fix-up should be squashed into it — which I did not do, because squashing
   removes the evidence that the defect is the unit's.
4. **The unit's own `-Werror` record is worth a follow-up**: its three new test binaries and one core
   header could not have compiled under the release's configuration on its own line. I did not build its
   branch to prove that; if it is true, that line has been green without ever having built this way.
5. **Unchanged from 3A–3E:** Gate 2 (coverage) is still unrun, the whole-tree ratchets are still
   unmeasured, and Gate 6's `tools/` category question is still open.
6. **`docs/KNOWN-LIMITATIONS.md` gained 12 lines.** If the owner wants the "Getting it running" section
   back, that is a decision for whoever builds and ships packages (the page says so itself) — and it is a
   copy, not a verification.

## What is NOT proven

* **Gate 2 (coverage) was not run** at any state. Every `run-all-gates.sh` invocation was the default and
  lists gate 2 as the gate that did not run. The unit adds ~20,000 lines of new product code with 16 new
  test entries; **the coverage effect of this train is unmeasured**, and it is the largest thing this
  report does not know.
* **CI was not run** and no workflow file was changed. Every exit code here is local; the six jobs this
  box cannot reproduce are named in every `local-ci.log`.
* **`agent_surface`'s failure is not fixed**, only diagnosed and reproduced by ctest.
* **I did not audit the unit's design.** I read every hunk the merge produced and asserted the invariants
  above; whether the control surface's ~20 supervisor/registry modules are *good* is its author's and the
  owner's question, and 15 of its 16 tests passing is all this train establishes.
* **Two of the unit's own files were edited by me** (the `-Werror` fix-up). That is the only place this
  train touched another author's code, it is two lines of initialiser order and one deleted local, and it
  is in its own commit so it can be reverted alone if the owner disagrees.
* **The render covers one project.** `sv-DnB-Startup.mmpz` is the project every train since 3A used, and
  it is reproducible; the audio path's behaviour under the unit's code is therefore proven for that
  project and not for `Root84`/`StrictProduction` (the two non-reproducible demos).

## Evidence index (`tests/integration-logs-3f/`, 8.2 MB)

```
preflight/  merge-tree-preview.log (unit 1, read-only, before any merge)
            delta-residue-pin1.log + pin1-per-path.tsv   (the unit's own delta vs HEAD, per path)
            delta-residue-pin2.log                        (unit 2: 4 of 5 files already in this line)
            pin1-g1..g4.diff, pin1-product-diffstat.txt   (the unit's hunks, grouped for reading)
            ours-since-MB.diff                            (what THIS branch changed in the same files)
            pin2-own-diff-KL.diff, pin2-KNOWN-LIMITATIONS.md, head-KNOWN-LIMITATIONS.md
baseline/   the entry-tip bundle: local-ci.log (70/70) + .exit, gate9/gate6/run-all-gates logs + .exit,
            regen-{index,head} + .exit, precommit + .exit, honesty-gate.log + .exit + version.txt,
            contract-rows.txt/.exit, tree-after-gates.txt
merge1/     merge.log + merge.exit, sides/ (all three index stages of BOTH conflicted files),
            index-vs-head.txt, merged-product-diff.diff, ledger-resolution.log, resolution evidence,
            fork-sources.resolved-241.txt, all-sources.resolved-1262.txt,
            build-fixup.log, build-fixup2.log (the two -Werror failures and the clean build)
merge1-fixup/ the same nine-part bundle on the fixed tree (85/86, gates, regen, honesty)
merge2/     merge.log + merge.exit, sides/ (3 files × 3 stages), ledger-resolution.log,
            KL-with-ported-bullet.md (the port), KL-ours-HEAD.md, the nine-part bundle
final/      tip.txt, render-1|2.log + .exit, render-final-1|2.wav, render-sha256.txt, chunk-parse.log,
            compare-vs-3E.log, compare-vs-3A.log
tools/      do_merge_3f.sh            - merge by sha, save all three stages, no automatic resolution
            run-merge-checks-3f.sh    - the nine-part per-state bundle (build+ctest, gates 9/6/ten,
                                        regen, precommit, contract rows, honesty guard, tree check)
            run_manifest_cmd.py       - run a manifest's OWN documented command, extracted from its
                                        header (--write regenerates from it) - the authoritative one
            regen.py, resolve_pair.py, precommit_check.py, delta_residue.py, entries_check.py,
            wav-chunks.py             - 3E's instruments, re-pointed; regen.py's transcription updated to
                                        the amended fork-sources command, resolve_pair.py carries a new
                                        warning that it reads the index stages (see the ledger note)
```

The git history of the train is `b96d01d61` (merge 1), `0ddbb1ca1` (fix-up), `c639bbd61` (merge 2),
`1fab5e4f1` (evidence + this report) and `1f33dd200` (the tip verification) — **two merges and one
labelled fix-up** plus two evidence commits. `git log --oneline --merges --first-parent 09e313c19..HEAD`
returns exactly the two merges.

## Tools used, and why

MCP where it fit: none of `lmms-lab`'s tools was used for the build/test/render, because the brief names
the exact commands (`tools/local-ci.sh`, `ctest` from `build/tests`, `tools/mmpz-git/render-recipe.sh`)
and the train's evidence has to be the same instrument 3A–3E used so the numbers compare. Raw shell and
git for everything else; the repo's own `tools/render-determinism-compare.py` for the audio comparison
rather than a new parser, and this train's `wav-chunks.py` (3E's) for the chunk table. No KB write of any
kind: no `ai_kos_create`, no `ai_kos_link`, no board change. No `ai-kos atq`.
