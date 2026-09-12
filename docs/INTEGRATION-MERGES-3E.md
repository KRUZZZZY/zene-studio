# Integration merges into the Zene Studio release line — merge train 3E (another author's pinned units)

Worktree: `projects/lmms-fl-research/zene-pa-foreign` (branch `post-alpha/foreign-merge`).
Entry tip: **`f68cf8e51`** — the release line's own tip, which is what this worktree held when the
tree settled (`git status --porcelain` empty apart from this train's evidence directory).
Exit: **five merge commits** — `6d1204b7f`, `345fd747b`, `131ce442a`, `2a245406b`, `f6364b7bd`, in
that order — then the evidence commit `tests/integration-logs-3e/` and this report. `git log
--oneline --merges f68cf8e51..HEAD` is the authoritative list; a report cannot name its own tip
without going stale, so it does not.

Nothing was pushed; no remote, PR, issue or **tag** was touched; `origin` (LMMS/lmms) and `messmerd`
were never contacted; no branch was rebased, amended, reset or rewritten (the five merge commits are
additions, and the only branch refs this train read were read-only); nothing was staged with
`git add -A` (every `git diff --cached --name-only` was read before every commit, and there are no
git hooks installed in this clone — `core.hooksPath` unset, `<common>/hooks/` holds only `.sample`
files — so hook-skipping was never in play; merge 1's commit did pass `--no-verify` before that was checked,
which was a no-op). Every exit code below was measured unpiped
(`cmd > log 2>&1; echo EXIT=$?`) and every log is committed under `tests/integration-logs-3e/`,
never `/tmp`. One build directory (`build/`), built from scratch (the provision step fetched the
pinned VST3 SDK v3.8.1_build_84 + CLAP 1.2.10, both `EXIT=0`), `JOBS=2 bash tools/local-ci.sh
--build-dir build --jobs 4` — the brief's job count, on a box where the other session was compiling
in parallel (load average ~21, 15 compilers running before this train's first build). `df -h`:
**47 GB free at entry, 52 GB at exit** (the scratch evidence is committed, 9.1 MB, not kept in
`build/`).

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` exits **3** = `PASS-WITH-SKIPS` (gate 2 coverage needs `--with-coverage`), which is
the expected result and is **not** a pass. The runner is at **ten** gates.

## THE HEADLINE FINDING: there was nothing to merge

**This train was expected to be the first to bring another author's work into the release line. It
is not. Measured, every one of the pinned units is a re-issue of work the release line already
carries, and the five successful merges changed *not one byte* of the tree:**

```
$ git diff --name-only f68cf8e51 HEAD          # the entry tip vs the tip after five merges
(no output)
$ git diff-tree -r --stat HEAD HEAD^           # and each merge vs its first parent, one at a time
(no output, five times)
```

Two independent instruments say the same thing.

**(a) patch-id: four of the six units' commits are *exact* patch-id duplicates of commits already
reachable from `HEAD`**, and the other two have same-subject/same-author/same-date twins there
(`git show <c> | git patch-id --stable`):

| unit | pinned commit | its patch-id | twin already in `HEAD` | twin patch-id | verdict |
|---|---|---|---|---|---|
| 1 | `6eb3a0c99` | `e4e056d2c465` | `a4717846a` | `e4e056d2c465` | **SAME** |
| 1 | `3e1480477` | `0a738a9e07b8` | `5b82864e0` | `0a738a9e07b8` | **SAME** |
| 2 | `ae89fa894` | `506083ff9ebb` | `0c23587d2` | `506083ff9ebb` | **SAME** |
| 4 | `dbcb8a2ba` | `54a95eb1f682` | `416fb186b` (and `9327c350f`) | `54a95eb1f682` | **SAME** |
| 3 | `bbf7a307c` | `b83532e2f09f` | `038e0edda` | `2c73effcf6ac` | twin, rebased onto a newer base |
| 5 | `e317f1062` | `e9d5bf9d7b92` | `ca5c19376` | `de9435a49350` | twin, rebased onto a newer base |

How the twins got there: `a4717846a`/`5b82864e0` and `0c23587d2` through the
`post-alpha/gate-debt` merge `b9b3ae0ce`; `416fb186b` through the `integration/round2` work
(`feat/rnnoise-denoiser`, merge `5e4cb02b6`); `ca5c19376` through the `post-alpha/pr594` merge
`8900efbb0`; `038e0edda` is directly on the line. **314 of the fork's 356 non-merge commits are by
the same author as these pins** — the two sessions share an author identity, which is why the
"other author's branches" describe work the release line has been integrating all along.

**(b) delta residue: of the 2,756 lines the pinned units add to the tree, `HEAD` already carries
every one.** `tests/integration-logs-3e/tools/delta_residue.py` takes each unit's *own* delta
(`git diff <merge-base> <unit>`) and asks of every added line whether the entry tree has it —
verbatim, or after the fork's own `lmms`→`zene` rename:

| # | unit | paths | lines its delta adds | already in the entry tree | RESIDUE |
|---|---|---|---|---|---|
| 1 | `tools/local-ci` `3e1480477` | 1 | 24 | 24 | **0** |
| 2 | `test/real-client-e2e` `ae89fa894` | 4 | 931 | 931 | **0** |
| 3 | `feat/stem-split` `bbf7a307c` | 1 | 8 | 8 | **0** |
| 4 | `part-b-engine-integration` `dbcb8a2ba` | 3 | 107 | 86 | **21** (see merge 4) |
| 5 | `feat/session-view-model` `e317f1062` | 15 | 1,677 | 1,677 | **0** |

Unit 5's zero is the sharpest single result in the train: all 307 lines of `include/SessionModel.h`,
331 of `src/core/SessionModel.cpp`, 118 of `SessionModelPrivate.h`, 210 of `SessionClip.cpp`, 322 of
`tests/src/core/SessionModelTest.cpp` and 308 of `ProjectVersionTest.cpp` are already in the entry
tree, and six of its 15 paths are byte-identical blobs there.

**So the brief's own subset escape-hatch applies to all five, not to one.** What was delivered:
`git merge <sha>` was performed exactly as instructed for each of the five, each conflict was kept
**as ours** only where ours was *proved* to be a superset, and every merge's result was asserted
byte-identical to the pre-merge tree. These are therefore **provenance merges**: they record that
the pinned units are integrated (they are now ancestors of this branch, which is what a later train
would otherwise have to work out again) and they are labelled as carrying no content — in the commit
messages as well as here. Had a unit been genuinely additive, the same procedure would have carried
its content; none was.

## The five merges, in the order the parent specified

| # | unit | merged from (pinned sha) | merge commit | conflicts | content | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `tools/local-ci` | `3e1480477b04c7a49612f96ac99cc502b7b56d8c` | `6d1204b7f` | 1 (`build.yml`) | none (tree identical) | 0 | 0 | 3 (10 gates) | 70/70 |
| 2 | `test/real-client-e2e` | `ae89fa89401e409ddf2aaa456b1cdf52c3c7e4c6` | `345fd747b` | 2 (`tests/CMakeLists.txt`, `tests/all-sources.txt`) | none | 0 | 0 | 3 (10) | 70/70 |
| 3 | `feat/stem-split` | `bbf7a307cb17f10727bdb4fe68960528a71254ad` | `131ce442a` | 1 (`src/CMakeLists.txt`) | none | 0 | 0 | 3 (10) | 70/70 |
| 4 | `part-b-engine-integration` | `dbcb8a2ba61cd4ee4a67a832cc0b582f187069cc` | `2a245406b` | 2 (`tests/CMakeLists.txt`, `tests/src/core/AudioBusTest.cpp` **add/add**) | none | 0 | 0 | 3 (10) | 70/70 |
| 5 | `feat/session-view-model` | `e317f1062dcca0f0411e293588c3cf731cc0b127` | `f6364b7bd` | 6 | none | 0 | 0 | 3 (10) | 70/70 |
| — | `docs/fork-readme` | `2e36486d5f6b58b136c3fc226dfab994ff56f776` | **not merged** | — | — | — | — | — | — |

All five named branches are ancestors of the tip (`git merge-base --is-ancestor`, YES ×5); all six
pins were still the branches' tips when the train started (`git rev-parse <branch>` == the pin, ×6 —
the other session did not move them while this train ran). **The sixth unit was deliberately not
merged** — see its own section; `2e36486d5` is not an ancestor of this branch and never was.

Every merge's gate bundle re-ran the full procedure and every result was **numerically identical to
the entry baseline**: ctest `100% tests passed, 0 tests failed out of 70`; Gate 9 `175 fork-NEW,
1,036 inherited, 18 tooling, 0 stale`; Gate 6 `427 changed path(s) declared; the ledger holds 439
entries`; the ten-gate runner `RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 10 gates did not run`. That
the numbers do not move is the point: the merges carry no content, so nothing downstream of the tree
could move either. Per-merge evidence: `tests/integration-logs-3e/merge1..5/` (`bundle.log`,
`local-ci.log` + `.exit`, `gate9/gate6/run-all-gates` logs + `.exit`, `regen-{index,head}.log`,
`precommit-index.log`, `merge.log` + `merge.exit`, `sides/`, `tree-after-gates.txt`).

## Method

* **Merge by sha, never by branch name** — `git merge --no-commit --no-ff <pinned sha>`, as briefed.
* **Conflict surface measured before anything was touched.** `git merge-tree --write-tree --messages
  HEAD <sha>` (git 2.43; writes objects, touches neither index nor worktree) was run for all six
  units first, and the whole preview is committed as
  `tests/integration-logs-3e/preflight/merge-tree-preview.log`. The predicted conflicts are exactly
  the six the merges produced.
* **Resolution rule for this train: keep ours, then prove ours is a superset** — because in every
  conflict the branch's copy was an *older revision of the same lineage*, not a competing edit. Each
  claim is asserted, not asserted-by-eye:
  * CI lists — `entries_check.py --kind yaml` (job ids and step names; `build.yml`: 6 jobs ours =
    6 theirs, 0 branch-only step missing *after* the fork's own `lmms`→`zene` rename is applied);
  * CMake lists — `entries_check.py --kind cmake` (registered file tokens; `tests/CMakeLists.txt`
    merge 2: 277 ours vs 221 theirs, 0 missing; merge 4: 277 vs 11, 0 missing; merge 5: 277 vs 218,
    0 missing; `src/CMakeLists.txt` merge 3: 15 vs 12, 0 missing);
  * manifests — kept ours **and re-derived from each file's own documented command** (`regen.py`),
    never unioned line-by-line (`tests/fork-sources.txt` merge 5: 175 entries ours ⊇ 103 theirs);
  * the ledger — `entries_check.py --kind ledger` plus a per-path reason comparison:
    **ours 439 paths ⊇ theirs 13 paths, 0 theirs-only path, and for every shared path the branch's
    reason is a literal substring of ours'** (so no clause of the branch's is lost), no blank reason,
    no `;;`;
  * a QtTest source (merge 4's add/add) — `entries_check.py --kind qtest` (test slots: 22 ours vs 1
    theirs, 0 branch-only slot missing).
* **The marker check is strict and excludes this programme's own evidence.** `git grep -nE
  '^(<<<<<<< |>>>>>>> |=======$)'` over the tree: clean. Recorded for the next train: three of
  **3C's committed resolver scripts** (`tests/integration-logs-3c/tools/resolve_merge*_product.py`)
  contain conflict-marker text as string *literals*, so a naive `^(<<<<<<<|=======|>>>>>>>)` scan hits
  them; this train's scan excludes `tests/integration-logs-3a..3e`. (The same naive pattern also hits
  `================ SUMMARY ================` separator lines in four `docs/*.md` files and in
  `plugins/LadspaEffect/caps/README`.)
* **THE assertion of this train**: after resolving, `git diff --cached --name-only HEAD` must be
  empty — the merged index byte-identical to the pre-merge tree — and `git diff-tree -r --stat HEAD
  HEAD^` empty after the commit. Both hold for all five merges.
* Two rules of the programme did real work again: **re-derive every manifest from its own command**
  (verdict `ALL-REPRODUCE` ×3 at every merge, before the commit against the index and again against
  HEAD), and **a pre-commit gate script reads HEAD, so it is not a pre-commit check** — Gate 6's rule
  was replayed against the index each time (`precommit_check.py INDEX` → `PRECOMMIT_EXIT=0`,
  `GATE6-REPLICA violations: NONE`, `cross-manifest entries: NONE`) and the real script run again
  afterwards. This time the reproduction check found **no** registration gap, which is itself the
  expected consequence of no content arriving.

## Manifest reproducibility

| after merge | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | verdict |
|---|---|---|---|---|
| entry (`f68cf8e51`) | 175 | 1,209 | 18 | baseline |
| 1 `tools/local-ci` | 175 | 1,209 | 18 | `ALL-REPRODUCE` |
| 2 `test/real-client-e2e` | 175 | 1,209 | 18 | `ALL-REPRODUCE` |
| 3 `feat/stem-split` | 175 | 1,209 | 18 | `ALL-REPRODUCE` |
| 4 `part-b-engine-integration` | 175 | 1,209 | 18 | `ALL-REPRODUCE` |
| 5 `feat/session-view-model` | 175 | 1,209 | 18 | `ALL-REPRODUCE` |
| final tip | 175 | 1,209 | 18 | `ALL-REPRODUCE` |

Full commands: `tests/integration-logs-3e/tools/regen.py` (3B's transcription, re-pointed at this
worktree and re-checked against the headers as committed here), which runs each manifest's own
documented command with `--rev HEAD` (the header's command) and `INDEX` (the pre-commit form). The
two merges that conflicted *in* a manifest (2: `tests/all-sources.txt` and merge 5:
`tests/fork-sources.txt`) were resolved by taking ours and re-deriving, never by a line union — and
in both cases the re-derivation printed `+0 -0 REPRODUCES`, i.e. ours was already the byte-exact
output of the command.

## The product-code hunks the automatic merges resolved, and how each was read

This is the class 3B's double-locked `recursive_mutex` and 3C's two render paths came from, and this
work is another author's on older bases, so every hunk an automatic merge resolved was read — not
just the conflicts.

### Merge 1 — `tools/local-ci` (`.github/workflows/build.yml`, +24 across two commits)

The conflict is the whole file (the branch's 557 lines vs HEAD's 841). Read end to end, then checked
structurally: **HEAD has all six jobs (`linux-x86_64`, `linux-arm64`, `macos`, `mingw`, `msvc`,
`msys2`) and every step name the branch's version has** (`entries_check.py --kind yaml`: 0 missing).
The branch's two commits' own content is present in newer form:
* `6eb3a0c99` "make the msvc test failure readable, and prove the import-binding mechanism" → HEAD
  line 666 `- name: Prove the plugin modules' zene.exe import binding (diagnostic, non-fatal)` with
  the three `dumpbin /imports|/exports … findstr` probes (the branch's read `lmms.exe`: wave R
  renamed the product);
* `3e1480477` "yamllint — keep the dumpbin lines under the 120-character limit" → HEAD wraps the
  block in `# yamllint disable rule:line-length` / `enable` (lines 585/592) and no
  `dumpbin`/`findstr` line exceeds 120 characters (`awk length>120` → no output). Its single
  branch-only line, `'on': [push, pull_request]`, is HEAD's `'on':` + `push:`/`pull_request:`/
  `workflow_dispatch:` — the multi-line form yamllint prefers, and a superset (it adds
  `workflow_dispatch`). Headers kept from ours.

### Merge 2 — `test/real-client-e2e`

* **`tests/src/core/RemotePluginClientE2ETest.cpp` and `tests/src/plugins/FakeRemotePluginClient.cpp`
  are byte-identical blobs at `HEAD`** (`31e4673d31b988ae57dd8d699a2209067938be9f` and
  `da9b38262a77368aa17ec5c2bf76c1733bafeb4b`) — not just equivalent, the same bytes, added by
  `0c23587d2` and already reachable from `HEAD`.
* Their registrations are present too: `tests/CMakeLists.txt:52` registers the test source, and
  lines 235–255 carry the `FakeRemotePluginClient` target, the two
  `target_compile_definitions(RemotePluginClientE2ETest PRIVATE REMOTE_PLUGIN_E2E_FAKE_CLIENT=…)`
  / `add_dependencies` blocks and the `set_tests_properties` call.
* The branch's `tests/CMakeLists.txt` is an older revision of the same file — its branch-only lines
  are older forms of lines HEAD has evolved (e.g. its
  `if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest")` vs HEAD's `... OR LMMS_TEST_NAME STREQUAL
  "PluginScanCacheTest")`; `target_link_libraries(partc_ref_${PLUGIN_ID} … PRIVATE lmms …)` vs
  HEAD's `PRIVATE zene …`). Nothing in it is product surface.
* `tests/all-sources.txt` is a generated manifest: kept ours, re-derived (`+0 -0 REPRODUCES`).

### Merge 3 — `feat/stem-split` (`src/CMakeLists.txt`, +8)

The unit's entire change is an AUTOMOC guard, and **`HEAD` carries it verbatim** at
`src/CMakeLists.txt:111-119`:

```
IF(NOT LMMS_HAVE_STEM_SPLIT)
        SET_SOURCE_FILES_PROPERTIES("${CMAKE_SOURCE_DIR}/include/StemSplitController.h"
                PROPERTIES SKIP_AUTOMOC ON)
ENDIF()
```

`delta_residue.py` measures the guard's 8 added lines as 8/8 already present. The branch's other 15
branch-only lines are older forms (`WINRC lmms.rc` vs `zene.rc`, `ADD_EXECUTABLE(lmms)` vs `(zene)`,
`# Paths relative to lmms executable` vs `zene executable`,
`target_static_libraries(lmmsobjs ringbuffer)` vs HEAD's `… ringbuffer lua luabridge`, so HEAD's is a
superset of that one too).

### Merge 4 — `part-b-engine-integration` (3 files, patch-id-identical to `416fb186b`)

* **`src/core/AudioBus.cpp` is a byte-identical blob at `HEAD`**
  (`03836f91b0c13694e1ecb21a069f1409217640f3`) — the `<iostream>` fix and the pair-iteration fix are
  in the tree, and the commit's whole patch-id matches a commit already in `HEAD`.
* **`tests/src/core/AudioBusTest.cpp` is an add/add conflict and this is the only place in the train
  where a unit's lines are *not* literally at `HEAD`.** HEAD's copy is the successor of the branch's
  102-line file: 633 lines, 22 test slots, from `6c6609f93` (the port of the same pair-iteration fix)
  and `961052a0c` ("sync the standards test suite into the product, and fix the two defects it
  caught"). **Asserted, not assumed**: `entries_check.py --kind qtest` → 22 slots ours vs 1 theirs,
  **0 branch-only slot missing**. **Observation recorded, nothing changed**: HEAD's `SilenceAllChannels`
  case drives a two-pair `TestBus` and checks all four of its channels after `silenceAllChannels()`,
  so it still fails if the loop steps by two *pairs*; the branch's case used four pairs, gave each
  pair a distinct non-zero value and named the pair that was missed, which is a stronger assertion
  in one dimension (it would also catch a skip that a two-pair bus cannot express). The fork replaced
  it deliberately with the larger synced suite; porting the four-pair case back is a product-test
  decision, not a merge resolution, so it was **not** done (decisions #3).
* `tests/CMakeLists.txt` kept ours (0 branch-only token missing).

### Merge 5 — `feat/session-view-model` (6 conflicts, 1,677 added lines, 0 residue)

* The five added sources and the edited `tests/src/core/ProjectVersionTest.cpp` are byte-identical
  blobs at `HEAD`; `delta_residue.py` finds **zero** of the unit's 1,677 added lines absent from the
  entry tree, including every line of `include/SessionModel.h` (307), `src/core/SessionModel.cpp`
  (331), `SessionModelPrivate.h` (118), `SessionClip.cpp` (210), `SessionModelTest.cpp` (322) and
  `ProjectVersionTest.cpp` (308).
* The nine edited files conflict or auto-merge only because the branch's base (`0b52941404`) is older
  than `HEAD`: `include/Song.h` and `src/core/CMakeLists.txt` have **0** branch-only lines;
  `src/core/Song.cpp`'s seven branch-only lines are older upstream text (the pre-rename
  `tr("LMMS Error report")`, `m_exportSongEnd = TimePos(m_length, 0)`, the automation-recording
  comment block); `CMakeLists.txt`'s eight are pre-rename project metadata
  (`PROJECT(lmms)`, `VERSION_MINOR "3"`).
* **The ledger union, proved line by line.** The branch's `tests/upstream-modifications.txt` reasons
  for `include/Song.h`, `src/core/Song.cpp` and `src/lmmsconfig.h.in` are the `#594` session-view
  declarations; each is a **literal substring** of `HEAD`'s longer, newer reason for the same path
  (e.g. ours for `src/lmmsconfig.h.in` = the branch's text + `; #617 telemetry: the
  `#617 telemetry: the ZENE_TELEMETRY_ENABLED packager kill switch define (OFF removes the client and its networking
  code)`). So keeping ours loses no clause: 0 theirs-only paths, 0 blank reasons, 0 `;;`. No entry was
  resurrected and no `tools/` path was added to the ledger.
* **The capability contract is untouched and the option is still `OFF`.** `CMakeLists.txt:121` reads
  `OPTION(WANT_SESSION_VIEW "Include the Session View data layer (<session> project block, opt-in)"
  OFF)`; `tests/advertised-features.tsv` still has exactly 6 rows × 5 TAB-separated columns with
  `session-view  WANT_SESSION_VIEW  OFF  -  …`; and `git diff --name-only f68cf8e51 HEAD` (which
  covers all five merges) is **empty**, so no merge could have flipped it. The parent's specific
  concern — "a branch that turned the session view on by default would falsify the release's
  capability contract" — is answered: **it is `OFF`, before and after**, and the branch did not
  touch the option's value (`delta_residue.py`: the option line is already ours).

## THE RENDER: the sha256 is the expected value, and the audio did not move

The brief allowed for a changed hash ("this work adds automation/command/session code, so a
different hash is plausible"). **It did not change — as it could not, since the five merges left the
tree byte-identical.**

```
expected from train 3B / 3C / 3D       943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 1 (f6364b7bd tree)     943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 2                      943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
```

`bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o …` →
`RENDER_1_EXIT=0`, `RENDER_2_EXIT=0`; 16-bit, 2 channels, 44,100 Hz, **544,256 frames**. Run-to-run
identical, so the same-build floor is 0. **The chunk comparison the brief asked for**
(`tests/integration-logs-3e/final/chunk-parse.log`, from this train's own parser, whose fields are
labelled from the actual offsets):

| | file sha256 | `fmt ` | `data` chunk (2,177,024 B) | `LIST`/`INFO` |
|---|---|---|---|---|
| 3A's artifact | `6b51f70f…` | 2 ch / 16 bit / 44,100 Hz | **`b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`** | `ISFT = "LMMS (libsndfile-1.2.2)"` |
| 3B/3C/3D artifacts | `943e3238…` | identical | **`b37cefc5…`** (same) | `ISFT = "Zene Studio (libsndfile-1.2.2)"` |
| **this train** | `943e3238…` | identical | **`b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`** | `ISFT = "Zene Studio (libsndfile-1.2.2)"` |

**Which changed: neither the data chunk nor a header tag — nothing.** And at the byte level stronger
than a hash comparison:

```
$ cmp tests/integration-logs-3b/final/render-final-1.wav  tests/integration-logs-3e/final/render-final-1.wav  -> IDENTICAL
$ cmp tests/integration-logs-3c/final/render-final-1.wav  tests/integration-logs-3e/final/render-final-1.wav  -> IDENTICAL
$ cmp tests/integration-logs-3d/final/render-final-1.wav  tests/integration-logs-3e/final/render-final-1.wav  -> IDENTICAL
$ cmp tests/integration-logs-3a/final/render-final-1.wav  …  -> differs at byte 5 (the RIFF size field)
```

and the repo's own comparator, `tools/render-determinism-compare.py`, against **3A's, 3C's and 3D's**
committed artifacts: `0 differing frames (0.000000 %)`, `max |delta| 0 LSB (-inf dBFS)`, `0 of 2,126
periods dirty`, `header match=True`, `best lag 0 frames` (`COMPARE_EXIT=0`,
`tests/integration-logs-3e/final/compare-vs-3A.log`).

**Verdict: the render is unchanged — not a metadata tag, not the audio. `943e3238…` is confirmed a
fourth time and the `data` chunk is still `b37cefc5…`.** No release-stopping audio difference exists
in this train, and the expected-constant question is now four trains deep.

## Unit 6 — `docs/fork-readme` `2e36486d5`: DELIBERATELY NOT MERGED

The orchestrator directed mid-train that this unit be skipped, on the ground that it is a 95-file
rewrite of documentation whose conflicts would land in the *auto-merged* class and silently re-open
the release line's just-reconciled docs. **It was not merged and no merge of it was ever started**
(`git merge-base --is-ancestor 2e36486d5 HEAD` → NO; no `MERGE_HEAD` at any point; nothing in the
train touched it). The directive's mechanism is right, and the measurement is worse than described,
so both reasons are recorded here — evidence in `tests/integration-logs-3e/unit6-not-merged/`.

**Where the brief and the tree disagree (reported, not silently accepted).** The brief describes the
unit as "96 files, +1429/−543 — documentation". The file classes of those 96 paths, measured:
**87 are PRODUCT CODE** (`src/`, `include/`, `plugins/`, `cmake/`), 6 are documentation (README.md,
`doc/`), 3 are CI/deps. It is not a documentation change with a couple of strays; it is mostly
upstream product code.

**And the unit is not one commit but six — five of them upstream LMMS commits already on
`origin/master`:**

```
2e36486d5 2026-09-09 Zachariah Markusson | docs: fork README — … banner, branch inventory, product-repo pointer   <- FORK
3d97b11f3 2026-09-09 regulus79            | Revert knife tool auto-selecting short ends (#8502)                    <- UPSTREAM
76ffa57ef 2026-09-08 Fawn                 | Resolve all Doxygen warnings & docs cleanup (#8361)                    <- UPSTREAM
e64cf9b8a 2026-09-08 Fawn                 | Remove OSS audio backend (#8482)                                       <- UPSTREAM
d10f6a23d 2026-09-08 Fawn                 | Remove libsoundio audio backend (#8475)                                <- UPSTREAM
b6908f60a 2026-09-08 Bimal Poudel         | Add .vs, .vscode to gitignore (#7988)                                  <- UPSTREAM
```

(each of the five verified with `git merge-base --is-ancestor <c> origin/master` → YES). So merging
"the docs branch" would have merged **five upstream master advances into the 0.2.0 release line**,
including two audio-backend removals — while `HEAD` still has every one of the files they delete:
`cmake/modules/FindSoundIo.cmake` (16 lines), `include/AudioOss.h` (91), `include/AudioSoundIo.h`
(142), `include/MidiOss.h` (81), `src/core/audio/AudioOss.cpp` (288), `src/core/audio/AudioSoundIo.cpp`
(473), `src/core/midi/MidiOss.cpp` (114) — **1,205 lines removed from the release**, with a
`modify/delete` conflict on `AudioSoundIo.cpp` as the only flag that it was happening.

The fork-authored part of the unit is one commit changing one file: `README.md` +23/−3 — a
"development-fork banner" written against *upstream's* README (its parent is `3d97b11f3`), pointing
at `KRUZZZZY/lmms-complete` (a superseded product-repo name), advertising "12 feature branches",
`integration/all-verified` and "coverage 85.24% on fork sources", and stating "Everything below is
upstream LMMS documentation, retained unchanged". Every one of those claims is false of the release
line's README, which was rewritten for the product (Zene Studio's own download/identity sections,
`~/.zenestudio.xml`, `docs/RENAME-COMPLETE.md`) — so this is **competing documentation**, the case the
brief says to report and pick nothing on.

**Auto-merge exposure, measured.** Of the 96 paths, `git merge-tree` flags only **6** as conflicting
(`.gitignore`, `README.md`, `doc/CMakeLists.txt`, `doc/Doxyfile.in`, `include/Clip.h` and the
`modify/delete` above). **The other 90 would have been resolved by the automatic merge with no flag
at all** — which is exactly the orchestrator's concern, now with a number attached.

## Verification on the committed tip

The whole bundle was run a sixth time **on the committed evidence commit** (`50d6a9b1a`, i.e. with
this train's 198 new evidence files tracked), to prove the evidence itself breaks nothing — a new
`.py`/`.sh` class under `tests/` is exactly the kind of addition Gate 9 and Gate 10 read:

```
LOCAL_CI_EXIT=0    GATE9_EXIT=0    GATE6_EXIT=0    RUN_ALL_GATES_EXIT=3
REGEN_INDEX_EXIT=0 REGEN_HEAD_EXIT=0 PRECOMMIT_EXIT=0
git status: only the untracked report itself
```

`tests/integration-logs-3e/tip/` holds it. So the train's gate results are the same before the
merges, after each of the five merges, on the finished tree, and on the committed tree — six runs,
identical numbers.

## Findings

1. **The train's premise is wrong in a way that matters for 3F/3G.** These six units are not
   un-integrated work: five are re-issues of commits already in the release line (patch-id twins for
   four of them, same-subject twins for the rest; total added-line residue 0/24, 0/931, 0/8, 21/107
   and 0/1,677), and the sixth is upstream master plus a README banner. **Recommendation for 3F: run
   `delta_residue.py` against `post-alpha/agent-surface-onto-integration` and
   `post-alpha/agent-surface-integration` *before* merging either.** If those are also largely
   already-present, the train's shape changes completely, and a conflict-only preview (`merge-tree`)
   will not tell you: it reports a *mergeable* branch, not an *additive* one.
2. **`docs/fork-readme` carries five upstream LMMS commits** (finding-level detail in its section).
   Any future attempt to take its fork README must take `2e36486d5`'s own README change deliberately
   (it is one file), never the branch. This collision also touches **3F's `pr7459-rebase`**: a rebase
   of upstream PR #7459 describes a base that merging these commits would move, and
   `fix/latency-complexity` edits `docs/KNOWN-LIMITATIONS.md`, which this unit's Doxygen/doc cleanup
   also rewrites — the brief's warning about that unit is substantiated, and both stay untouched.
3. **The release line's `AudioBusTest.cpp` is stronger in one dimension and weaker in another than
   the pinned unit's** (merge 4). HEAD's `SilenceAllChannels` covers the two-pair boundary case and
   would fail on a step-by-two-pairs bug; the unit's four-pair version would additionally name the
   missed pair. Nothing was changed; decision #3.
4. **Two ledger reasons carry an exactly duplicated clause** — `include/Song.h` and
   `src/core/Song.cpp` each contain the `#594 Session View: gated …` clause twice, stacked inside one
   reason by an earlier clause union (3C recorded and deduped the same class for a `#605 PDC` clause).
   Found while proving the union; **not** fixed here, because the duplication predates this train and
   touching the ledger would have made these merges carry content (decision #4).
5. **No registration, declaration or ledger entry was needed by any merge** — because no path
   arrived, so no file became a modification to declare. `tests/upstream-modifications.txt` is
   byte-identical to the entry tip (439 entries), `tests/fork-sources.txt` is still 175 entries and
   reproduces, and `git diff f68cf8e51 HEAD -- tests/` is empty. A merge train that has nothing to
   register is the signature of a train that brought nothing.
6. **`merge-tree` is the right pre-merge instrument and belongs in the next briefs.**
   `git merge-tree --write-tree --messages <ours> <theirs>` gave the exact conflict set for all six
   units before a single file was touched, read-only; it is in `preflight/merge-tree-preview.log`.
7. **Author identity: the two sessions are one author.** 314 of the 356 non-merge commits reachable
   from the entry tip are by the same author as the pinned units, and four of the twins were landed
   by this session's own lanes (`post-alpha/gate-debt`, `post-alpha/pr594`, `integration/round2`).
   "Another author's work" is therefore not a clean boundary here, and the report's unit table should
   be read with that in mind.

## The 3F/3G queue: collision preview (informational, read-only, nothing merged)

Run with the same read-only instrument, for the parent's planning only:

| queued branch | commits not in `HEAD` | conflicts vs the entry tip | note |
|---|---|---|---|
| `post-alpha/agent-surface-onto-integration` | 27 | **2** — `tests/fork-sources.txt`, `tests/upstream-modifications.txt` (both manifests) | the other session's own integration branch; a *mergeable* shape, but see finding 1 — run `delta_residue.py` before assuming it is additive |
| `post-alpha/agent-surface-integration` | 27 | **9** — `src/core/CMakeLists.txt`, `src/core/ConfigManager.cpp`, `src/core/main.cpp`, `src/gui/GuiApplication.cpp`, `src/gui/MainWindow.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | the competing version of the same work; the two product-code conflicts in `main.cpp`/`GuiApplication.cpp` are the class that hid 3C's two render paths |
| `fix/latency-complexity` | 2 | **3** — `.github/ISSUE_TEMPLATE/alpha-feedback.yml`, **`docs/KNOWN-LIMITATIONS.md`**, `tests/upstream-modifications.txt` | confirms the brief's warning; `docs/KNOWN-LIMITATIONS.md` was rewritten at the freeze and must not be re-opened by an auto-merge |

## Test expectations changed during this train

**None — and there was nothing to change.** No test assertion, tolerance, expectation, exemption,
baseline or gate threshold was touched by any of the five merges: `git diff --name-only f68cf8e51
HEAD` is empty, i.e. the train changed no tracked file at all before this report. `--reanchor` was not
used anywhere; `tests/file-length-baseline.tsv`, `tests/file-length-baseline-all.tsv`,
`tests/complexity-baseline*.tsv`, `tests/file-length-exempt.txt` and `tests/coverage-entry-floor-exempt.txt`
are untouched. No code was trimmed to satisfy a metric and no `-Werror` failure was silenced.

## Refusals

* **`docs/fork-readme` was not merged** (orchestrator directive, and the independent evidence above),
  and no merge of it was started. `2e36486d5` is not an ancestor of this branch.
* **Nothing off-list was merged, and no upstream commit was merged** — `origin`/`messmerd` were never
  contacted, fetched or read from the network; the only upstream knowledge in this report comes from
  the local `refs/remotes/origin/master` that the clone already had.
* **No history was rewritten**: no rebase, amend, reset, filter or force; the five merge commits are
  pure additions on top of `f68cf8e51`, and the only other commits this train creates are its own
  evidence and this report.
* **No tag was created or moved**, nothing was pushed, and `-DFORCE_VERSION` was not passed into
  anything committed (for the record, the untagged tree reports
  `Zene Studio 0.1.0-alpha.247+f68cf8e` — the expected `git describe` behaviour, not a defect; the
  version constant is unchanged by this train because the train changed no file).
* **No gate script was edited or weakened**, no manifest was hand-edited (all three are the
  byte-exact output of their own commands), and no conflict was resolved by unioning lines across
  markers — every conflict was resolved to ours *after* proving ours a superset by entry set, delta
  residue or both.
* **The product code of no unit was "fixed".** Nothing was ported, re-anchored, re-declared or
  re-registered: five of the units needed no work because they were already integrated, and the one
  observation that could motivate a change (finding 3) was reported instead.

## What is NOT proven

* **Gate 2 (coverage) was not run** at any point. Every `run-all-gates.sh` invocation was the
  default; the `3` is `PASS-WITH-SKIPS` with gate 2 listed as the gate that did not run, and it is
  not a pass. Given that the tree did not change, the entry tip's coverage figure still describes
  this tip — but this train did not measure it.
* **CI was not run** and no workflow file was changed. Every exit code here is local.
* **The five provenance merges were not reviewed by anyone but this train.** Their content is nil by
  construction and asserted byte-for-byte, but the *decision to record them* is the parent's to
  confirm (decision #1).
* **`docs/fork-readme` was not merged and its README banner was not applied**, so nothing here says
  what the release's README should or should not say about being a development fork — that is an
  editorial decision, and the two candidate texts now disagree (finding 2).
* **I did not audit the units' designs.** I read every hunk the merges resolved (automatic or
  conflicting) and asserted the invariants above; the units' own choices are theirs, and for these
  five units the release line's own copies of them are what the render comparison covered.
* **The 3F/3G previews in this report are conflict-surface measurements, not content measurements**
  — `merge-tree` says what would conflict, not what would arrive (finding 1).

## Concurrent activity — noted, not touched

The other session is live in this clone and was compiling throughout (`zene-pa-onto/`). This train's
entry check passed (`git status --porcelain` empty apart from this evidence directory) and the tree
was re-checked after every gate run (`tree-after-gates.txt`, clean at all five merges and at the final
tip). **No other worktree was read-write touched**; `zene-pa-integration` (the release line) was never
opened and has not moved; `zene-pa-onto/` was neither built in nor written to. No concurrent branch
was merged and no concurrent merge was observed. All six pins were still the branches' tips at entry
(`git rev-parse <branch>` == the pin, ×6) and all six branches still existed at the end
(`git rev-parse` re-checked for the five merged ones — identical shas; `docs/fork-readme` untouched).
The build directory is this worktree's own; `df -h` moved 47 GB → 52 GB free, and this train's
evidence is 9.1 MB committed (no scratch tree was kept).

## Decisions needed from the owner

1. **Do the five provenance-only merges stand?** They add no content (`git diff f68cf8e51 HEAD` is
   empty at the tip); what they add is the record that the pinned units are integrated, plus that the
   five branches are now ancestors, which would simplify a later merge of the other session's own
   superset branch. The alternative is to drop them and keep only this report — the tree is identical
   either way. If they stand, they should be described in the release record as provenance merges,
   not as five units of the other author's work.
2. **`docs/fork-readme`: confirm the skip and decide the README question separately.** Not merging it
   is already done and is the right call for two independent reasons (the reconciled docs, and five
   upstream commits including two backend removals). But the *content* the other session wanted — a
   development-fork banner — is a one-file change whose facts are stale (superseded repo name,
   another session's branch inventory and coverage figure). If the release's README should say it is
   a development fork, that sentence is an editorial decision for a later train, written against
   *this* README.
3. **`AudioBusTest.cpp`: port the four-pair `silenceAllChannels` case?** (finding 3). The product fix
   is present and covered; the pinned unit's stronger assertion is not. A one-case addition to
   `tests/src/core/AudioBusTest.cpp` would close it — a product-test change, deliberately not made by
   a merge train.
4. **The duplicated `#594` clause in two ledger reasons** (finding 4) — an idempotent dedupe, the same
   repair 3C made for a `#605` clause. Not done here, so that these merges stay content-free.
5. **3F's shape.** Run `delta_residue.py` against the two agent-surface branches before merging either
   (finding 1); if they are largely already-present, 3F is a much smaller train than its 27 commits
   suggest, and the driver to merge them is the command/control surface, not the session view.
6. **Unchanged from 3A–3D:** where fork evidence lives (I used `tests/integration-logs-3e/`, as they
   used theirs), Gate 6's `tools/` category vs `all-sources.txt`, the stale whole-tree baselines, and
   the un-run Gate 2 coverage.

## Evidence index (`tests/integration-logs-3e/`)

```
preflight/     merge-tree-preview.log  (the read-only conflict set for all six units, before any merge)
               patch-id-twins.log      (unit commit vs its twin in HEAD, patch-id and file lists)
               delta-residue-vs-entry.log  (the 0-residue table: what each unit adds that HEAD lacks)
               residue.log, table.log  (per-path file-state comparison, incl. the untracked/manifest noise)
               artifact-chunks.log     (chunk tables of 3A/3B/3C/3D's committed render artifacts)
baseline/      entry-tip bundle: local-ci.log (ctest 70/70) + .exit, gate9/gate6/run-all-gates logs + .exit,
               regen-{index,head}.log + .exit, precommit-index.log + .exit, tree-after-gates.txt
merge1 … merge5/  merge.log + merge.exit (the conflict list), sides/<path>.{base,ours,theirs} (all three
               stages of every conflicted file), the same six-part bundle as baseline, tree-after-gates.txt
final/         tip.txt, the same bundle on the finished tree, render-1|2.log, render-final-1|2.wav,
               render-recipe.log, render-sha256.txt, chunk-parse.log, compare-vs-3A.log
unit6-not-merged/ evidence.log  (the unit's six commits, which are upstream, the 87/6/3 file-class split,
               the seven files it would delete, the 6-conflict surface, why it was not merged)
tip/           tip.txt + the same six-part bundle re-run on the committed evidence commit (50d6a9b1a),
               which is what proves the evidence files themselves trip no gate
tools/         this train's instruments:
                 preflight.py       - per-unit, per-path "what would the merge bring" table
                 residue.py         - unit-only lines vs HEAD (file state, rename-aware)
                 delta_residue.py   - the sharp one: the unit's OWN delta vs HEAD, `--against <rev>`
                 entries_check.py   - "every entry from both sides" for yaml / cmake / list / ledger / qtest
                 do_merge.sh        - merge by sha, save the three sides, resolve to ours, assert no content
                 run-merge-checks.sh- the per-merge six-part bundle (build+ctest, Gate 9, Gate 6, ten gates,
                                      manifest re-derivations, Gate 6 replayed against the index, tree check)
                 numbers.sh         - the one-line numbers for the report table
                 wav-chunks.py      - WAV chunk table + per-chunk sha256 + INFO tags (labels from the offsets)
               plus 3D's resolve_pair.py / regen.py / precommit_check.py / verify_union.py / merge_ledger.py
               re-pointed at this worktree (no other change).
```

The git-history commits of the train are `6d1204b7f`, `345fd747b`, `131ce442a`, `2a245406b`,
`f6364b7bd` — **five merges, no fix-ups** — plus the evidence commit and this report. `git log
--merges f68cf8e51..HEAD` returns exactly those five and nothing else, at any later tip.
