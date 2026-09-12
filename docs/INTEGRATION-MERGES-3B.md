# Integration merges into `post-alpha/integration` — merge train 3B (six post-alpha lanes)

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Entry tip: **`627fb1e4f`** — the tip train 3A left, once 3A's own final gate-log commit had landed.
Exit tip: **`558fbd375`** — six merge commits plus two labelled fix-ups, then this report.

Nothing was pushed; no remote, PR, issue, tag or branch was touched; `origin` (LMMS/lmms) and
`messmerd` were never contacted; no branch was rebased, amended, reset or rewritten; nothing was
staged with `git add -A` (every `git diff --cached --name-only` was read before every commit). Every
exit code below was measured unpiped (`cmd > log 2>&1; echo EXIT=$?`) and every log is committed
under `tests/integration-logs-3b/`. `df -h` was 39 GB free at entry and 27 GB at exit (the builds).

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` exits **3** = `PASS-WITH-SKIPS` (gate 2 coverage needs `--with-coverage`), which is
the expected result and is **not** a pass.

## The six merges, in the order the parent specified

| # | Lane | Merge commit | Conflicts | Ledger entries | fork / all / tools | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|----------------|--------------------|--------|--------|--------------------|-------|
| 1 | `post-alpha/rename-complete` | `12b2994e0` | 5 files (1 product code) | 130→**410** | 144 / 1161 / 14 | 0 | 0 | 3 (9 gates) | 55/55 |
| 2 | `post-alpha/brand-placeholders` | `d77b34d14` | 2 files (both ledgers) | →**434** | 144 / 1161 / 15 | 0 | 0 | 3 (9 gates) | 55/55 |
| 3 | `post-alpha/test-hygiene` | `046203b7c` | 1 file | 434 (deduped) | 144 / 1162 / 15 | 0 | 0 | 3 (**10** gates) | 59/59 |
| 4 | `post-alpha/coverage-green` | `4a4f96449` | **0** | 434 | 144 / 1162 / 15 | 0 | 0 | 3 (10 gates) | 59/59 |
| 5 | `post-alpha/racks` | `3c245cb8a` (+ fix-up `a7ae65361`) | 7 files (1 product code) | 434 | 150 / 1170 / 17 | 0 | 0 | 3 (10 gates) | 61/61 |
| 6 | `post-alpha/vca` | `558fbd375` | 4 files (1 product code) | 434 | 152 / 1173 / 17 | 0 | 0 | 3 (10 gates) | 62/62 |

`git log --oneline --merges 627fb1e4f..HEAD` returns exactly those six commits and nothing else. All
six named branches are now ancestors of HEAD (checked with `git merge-base --is-ancestor`). **No
branch outside the list was merged**; the queued 3C/3D branches (`router-live`, `session-scheduler`,
`mpe`, `warp`, `telemetry`, `auto-mastering`, `instrument-hosting-impl`, `instrument-view-safety`,
`release-prep`) and every branch the other session owns (`pr7459-*`, `lv2-catalogue`, `cmd-*`,
`agent-*`, `headless-load`, `control-hardening`, `docs/fork-readme`, `tools/local-ci`,
`test/real-client-e2e`, `feat/*`, `fix/*`) were never touched.

Suites: 52 (3A's exit) → 55 → 55 → 59 → 59 → 61 → **62**. The four recovered test sources from
`test-hygiene` are why 55→59; `RackTest`/`RoutingGraphLiveTest` are 59→61; `VcaGroupTest` is 61→62.

## Method

Every conflict in this train was in an append-only registry, a CMake list, the coverage baseline, the
gate runner, or one of two product-code functions. Nothing was resolved by unioning **lines** across
conflict markers.

Two tools drove it, both committed as evidence under `tests/integration-logs-3b/tools/`:

* `resolve_pair.py` — resolves a conflicted file **from the index stages `:1:`/`:2:`/`:3:` directly**,
  which needs no special handling for multiple hunks. For a `<path><TAB><reason>` ledger: the result
  is ours verbatim; a shared path whose reason differs keeps ours when theirs is still the merge
  base's text, keeps the superset when one side's text contains the other's, and otherwise takes both
  as clause segments (ours first); every theirs-only path is appended under the banner section it
  sits in in theirs' own file. For a plain entry list: the entry **set** union, sorted by bytes, under
  ours' header.
* `regen.py` — runs each manifest's own documented command and prints `REPRODUCES` /
  `DOES-NOT-REPRODUCE` with the exact added and removed entries. `--write` re-derives a manifest from
  its own command when the union is not enough.
* `verify_union.py` — proves a ledger result is a true union: 0 entries lost, 0 duplicate path,
  0 blank reason, no `;;`, and every result reason traceable to one of the two sides.
* `precommit_check.py` — replicates Gate 6's mechanical rule against the index.

Every resolution was asserted programmatically before it was staged, never by eye.

### A pre-commit gate script reads HEAD, so it is not a pre-commit check

`tests/no-upstream-regression-gate.sh` computes its changed set as `git diff --name-only
"$BASE"..HEAD` (line 47). Run before a merge commit, it reports the **pre-merge** HEAD and is green no
matter what the merge is about to add. After merge 1 the same script jumped from 106 to 382 changed
paths. Every merge in this train therefore had Gate 6's rule **replayed against the index** before
committing (the `precommit_check.py` replica and an explicit replay), and the real script was run
again after the commit. That is how merge 4's 16 findings and merge 6's Gate 9 failure were caught
before they could be committed.

## Merge 1 — `post-alpha/rename-complete` (`12b2994e0`, fix-ups `21ba5d2be`, `9de48fe20`)

Six commits, 309 files: the user-visible rename (layer 1), user-state migration (layer 2) and the
project/preset format's read-both/write-new (layer 3). Five conflicts.

**`src/core/ScriptBindings.cpp` — the product-code conflict, SEMANTIC, both sides wanted.** The
three-way read against `:1:` showed each side had changed a different thing: the lane renames the
Lua-facing global namespace (`lmms` → `zene`) and adds a 5-line alias at the end of `bindScripts()`
so scripts written against the published alpha keep running; integration carries the `ScriptApi`
function surface and the luabridge `Stack` specialisations that were split out to
`include/ScriptLuaQtTypes.h`. Taking either side alone loses work — theirs reverts the ScriptApi
surface to a hardcoded `"0.1"` and drops four functions; ours reverts the rename. Resolved as
theirs' namespace line **plus** ours' five functions. Asserted: exactly one
`.beginNamespace("zene")`, no `.beginNamespace("lmms")`, all five `ScriptApi::*` calls present, the
`lua_setglobal(L, "lmms")` alias present, no `std::string("0.1")` left. **The C++ `lmms::` namespace,
the `LMMS_*` macros and the plugin entry symbol are untouched by this merge** — nothing in the train
changes `lmms_plugin_main`.

**`tests/upstream-modifications.txt` — entry union, 130 + 280 = 410 entries.** Both sides had
independently declared the same wave-R rename divergence retroactively; the wave-R commit
`018d2041f` itself declared nothing (the ledger still held 31 entries at its tip), and the two
retroactive declarations cover different paths — integration's covers `cmake/`, `doc/`, `nsis` and
`vcpkg.json`, the lane's covers `data/presets/**`, `data/projects/**`, `data/scripts/**` and the
built-in plugin logos. Neither is a superset, so the union is the honest result. 8 shared paths kept
integration's newer reason, 46 got a clause union, 280 entries added, 55 kept. Verified by
`verify_union.py`: 410 entries, 0 lost, 0 duplicate, 0 blank, 0 untraceable.

**`tests/all-sources.txt`** — entry union, 1161 entries, +3 (the lane's new tests). The lane's copy
carried an older header (unpinned `sort`, no verify step); integration's pinned, verified header is
kept. **`tests/CMakeLists.txt`** — 3 hunks: two entry unions and one comment+`set_tests_properties`
block where BOTH `StemExportTest` (ours) and `PluginLogoResourceTest` (theirs) need offscreen Qt;
both complete blocks kept. **`tests/file-length-baseline.tsv`** — the inline conflict was
`ScriptBindings.cpp` 1169 (ours) vs 1222 (theirs); the lane's 1222 is a pre-refactor count (it still
had the 53-line inline `Stack` block). The merged file measures 1174 and the single 1169 → 1174 move
was taken through the gate's own documented valve with a recorded reason —
`bash tests/file-length-gate.sh --reanchor-file src/core/ScriptBindings.cpp "<reason>"` → EXIT=0 —
not hand-edited and not a bulk re-anchor.

**Fix-up `21ba5d2be` — the merge commit could not configure.** My own hunk-3 resolution emitted both
`set_tests_properties` calls and then re-appended the shared tail line as well, so the file carried
`ENVIRONMENT "QT_QPA_PLATFORM=offscreen")` twice and `cmake` failed at `tests/CMakeLists.txt:223`
with `Parse error. Expected "(", got quoted argument`. One line deleted; `cmake -S . -B <tmp>
-DWANT_QT6=ON` → EXIT=0. The resolver's bug was found and fixed the same way and the script is
committed under `tests/integration-logs-3b/tools/`. This was caught by the build, not by eye.

**Fix-up `9de48fe20` — a real cross-lane defect the merge exposed.** `DataFileSaveIntegrityTest`
(from 3A's `saveload-integrity`) asserted `<lmms-project` against files it had just **written**;
layer 3 writes `zene-project`. Full analysis and the required one-line answer in "Test expectations
changed during this train" below.

## Merge 2 — `post-alpha/brand-placeholders` (`d77b34d14`)

12 commits, no product code at all — artwork, the lane's tooling and evidence, docs and the ledgers
(`git diff --cached --name-only | grep -E '\.(cpp|c|h|hpp|cc|cxx)$'` is empty).

**Ledger** — entry union, 410 + 24 = 434. The 24 are artwork the lane replaced with hand-authored,
licence-free placeholders. 40 shared reasons keep integration's text (the lane's copy was still the
merge base's), 31 get a clause union because the two sides describe two *different* changes to the
same inherited file.

**`tests/fork-sources.txt` — resolved as integration verbatim (144) plus a header note.** The lane
hand-added four entries that this file's own command cannot produce, and `regen.py` measured exactly
that (`DOES-NOT-REPRODUCE`, naming all four). Each is resolved by where its home actually is:
`tools/brand/rasterise-placeholders.py` **moved** to `tests/tools-sources.txt` (15 entries), whose own
command does produce it; `tests/brand-resource-sweep.py` and the two
`tests/evidence/brand-placeholders/*.py` are registered **nowhere**, deliberately — reading
`tests/fork-sources-gate.sh` shows its scope is `is_source()` (C/C++ only) outside `tools/` and
`is_tools_source()` (C/C++/Python/shell) under `tools/`, so a `.py` under `tests/` is outside every
list Gate 9 reads. Admitting them would have widened the fork-scoped ratchets onto test-side scripts,
which is the scope widening 3A refused for `plugins/RnnoiseDenoiser/testdata/*.sh`.

## Merge 3 — `post-alpha/test-hygiene` (`046203b7c`)

The audio-engine teardown fix (`src/core/AudioEngine.cpp`, `src/core/AudioEngineWorkerThread.cpp` —
declared by the lane), four recovered test sources, the class sweep, and the **new Gate 10**.

**`tests/CMakeLists.txt`** — one hunk, resolved as an **entry union** (10 entries: ours' 7, theirs' 5,
2 shared), and the stale `# NOT REGISTERED, deliberately` block was **removed**: it declared
`PhaseDSidechainTest.cpp` unregistered because `PART_D_COMPRESSOR_LIBRARY` was "referenced exactly
once in the repo and defined nowhere", and its own last sentence reads *"Remove this note and add the
file to LMMS_TESTS when it compiles."* This merge is that moment — the lane wires the macro as
`PART_D_COMPRESSOR_LIBRARY="$<TARGET_FILE:compressor>"`. Keeping the note would have preserved a
false statement about the file it sits in (3A dropped a stale comment block for the same reason).

**Ledger: an auto-merged DUPLICATE PATH, found and fixed here.** `tests/upstream-modifications.txt`
auto-merged without a conflict and that hid a defect: it produced TWO lines for each of
`src/core/AudioEngine.cpp` and `src/core/AudioEngineWorkerThread.cpp`. Gate 6 passes on a duplicated
path — only the counts exposed it (`the ledger holds 436 entries` against a 434 unique-key count).
Deduped by path with a clause union, integration's clause first, each entry staying in its section:
434 entries, 0 duplicate, 0 blank.

**Duplicate content, proved by patch-id.** The lane's `tests/src/core/TwoTrackRecordingHarness.cpp`
change does not appear in the merge diff, and that is a measurement:
`git show 2c11fe4c0 | git patch-id --stable` → `da4b8210d8467acafa813336a838ee84ecc66f8e`; the same
patch-id is in integration's own history as `dd121d606` ("test(recording): give the two-track harness
a per-run output directory"). The lane's commit is an exact cherry-copy of a commit already
reachable from integration, so the content is present exactly once and there was nothing to dedupe.

**Gate 10 in this merge.** `tests/unregistered-tests-gate.sh` (new, 129 lines) is wired into
`tests/run-all-gates.sh`, so the runner reports TEN gates from here on; run before the commit →
EXIT=0, "74 test sources scanned, 69 registered, 5 declared-not-built, 4 helpers".

## Merge 4 — `post-alpha/coverage-green` (`4a4f96449`)

**No conflicts at all** — which is reported, not assumed harmless. An auto-merge is not a union, so
both things it could have got wrong were checked directly.

**The baseline migration is a true union, measured.** base `0a92be489` 47 entries · integration 47
(unchanged since the base, which is *why* git could auto-merge) · lane 75 · merged 75 ·
HEAD-entries-missing-from-merged: NONE · lane-entries-missing-from-merged: NONE ·
`merged == HEAD | lane`: True. So the migration is **47 → 75 with 0 dropped**, which is the property
the lane existed for. Checked as a set comparison against both sides, not by counting lines.
`tests/coverage-green/coverage-baseline.before.tsv` (the lane's own 47-entry pre-migration copy) is
kept as the evidence.

**Gate 6 was RED on this merge and was fixed in the merge commit.** The lane's evidence sat under
`docs/coverage-green/`, and Gate 6 classifies a non-`.md` path under `docs/` as an undeclared
upstream divergence: **16 violations**, reproduced against the index before committing. Fixed the way
3A fixed the identical finding for `post-alpha/coverage-run`: **relocated**
`docs/coverage-green/` → `tests/coverage-green/`, `tests/**` being the category Gate 6 allows and
where this repo already keeps durable evidence. No gate script was edited, no gate weakened. 24 path
references rewritten (14 in `docs/COVERAGE-GATE-GREEN.md`, 2 in `tests/QA-GATES.md`, 1 in
`tests/coverage-gate.sh`, 1 in `docs/CONVENTIONS.md`, 1 each in the lane's three scripts and three
logs); no measurement, log or number altered; a relocation note citing 3A's decision added to the
lane's report. Replaying Gate 6's rule against the index afterwards: **0 VIOLATIONS**.

The alternative — teaching Gate 6 a `docs/` category — was considered and refused for 3A's reason:
the relocation makes the path legal, the category change relaxes a gate.

## Merge 5 — `post-alpha/racks` (`3c245cb8a`, fix-up `a7ae65361`)

Eight commits: the mixer channel's rack, the `RoutingGraph` behind the mixer channel's effect chain,
their tests and docs. Seven conflicts, the widest merge of the train.

### The headline: the auto-merge silently double-locked the change mutex

`src/core/EffectChain.cpp` was a real conflict, but git auto-merged most of it and left only two
trivial-looking hunks in `moveDown()` and `moveUp()`:

```
    <<<<<<< HEAD
    =======
            rebuildRoutingGraph();
    >>>>>>>
```

The text around them looked resolved and was wrong, and **neither side wrote it**: both sides had
moved the same two lines relative to the base in different ways, so git stacked *both* copies of the
lock —

```cpp
    Engine::audioEngine()->requestChangeInModel();     // ours   (mixer D5)
    auto it = std::find(...); assert(...);
    Engine::audioEngine()->requestChangeInModel();     // theirs (racks)
    std::swap(...); rebuildRoutingGraph(); doneChangeInModel();
```

Measured, not assumed: `requestChangeInModel()` is `m_changeMutex.lock()` and `doneChangeInModel()`
is `m_changeMutex.unlock()` (`src/core/AudioEngine.cpp:628-638`), and the member is a
`std::recursive_mutex` (`include/AudioEngine.h:455`). So the second lock does not deadlock in the GUI
thread — it raises the recursion count to 2, the single unlock drops it to 1, and **the mutex is never
released**. The next `std::lock_guard{m_changeMutex}` taken by the audio thread
(`AudioEngine.cpp:368`, `renderStageEffects`) would block forever: the first effect reorder by a user
hangs rendering permanently. Balance at the conflicted revision: **7 requests / 6 dones**.

Fixed in the merge commit: one request and one done per path, with the swap and `rebuildRoutingGraph()`
both inside the lock, and both sides' comments kept. After the fix the file is 5 requests / 6 dones,
which is the pre-existing shape — `removeEffect()` takes one lock and releases it on either the
early-return path or the normal path. `grep`-verified per function.

### The other conflicts

* `tests/fork-sources-gate.sh` (add/add) and `tests/run-all-gates.sh` — the lane's
  `test(gates): import Gate 9 ...` commit carries an OLDER copy of the fork's gate work. Resolved by
  keeping ours, **verified rather than assumed**: each of the 18 (resp. 10) theirs-only lines is
  present in ours in a newer form — `all-sources (whole-tree scope)` vs `all-sources (inherited
  upstream)`, `stale entry in tests/$2` vs the `tests/fork-sources.txt`-only wording, `mapfile -t
  TRACKED` over `src include plugins tests tools modules` vs the older four-directory scan, and
  `record 4/7/8` checking BOTH scopes (`$rc4 -eq 0 && $rc4t -eq 0`) vs the older single-scope check.
  One line needed real care: the lane's gate 1 built with `/tmp/gate1-build.log`; ours still builds,
  into `build/gate1-build.log` in the repo, with the same `build FAILED (exit $build_rc)` diagnostic.
  `bash -n` passes on both.
* `tests/CMakeLists.txt` — two hunks, each a single entry pair; **entry union**: `RackTest.cpp` and
  `RoutingGraphLiveTest.cpp` join `RecordClipTest.cpp` and `RenderJobQueueTest.cpp`. 0 duplicate
  targets afterwards.
* `tests/all-sources.txt` / `tests/fork-sources.txt` — entry union, then **both re-derived from their
  own documented commands**, which caught two things the union alone did not: (a) the lane's two
  hand-added `tools/` entries (`tools/rack-render-fixture.py`, `tools/rack-render-proof.py`) cannot be
  produced by fork-sources.txt's command — `regen` reported them as `-` removals — so they are
  **moved** to `tests/tools-sources.txt` (17 entries), with the move recorded in fork-sources.txt's
  header; (b) all-sources.txt was missing four fork-NEW sources the lane added and never registered
  (`include/Rack.h`, `include/RackNodes.h`, `src/core/Rack.cpp`, `src/core/RackNodes.cpp`) — added by
  re-deriving from `git ls-files`.
* `tests/upstream-modifications.txt` — 434 entries, 0 added (the lane's ledger copy is its
  branch-point version, a strict subset), 10 reasons keep integration's newer text, 1 clause union.

### Fix-up `a7ae65361` — the runner ran Gate 9 twice

Found by reading the gate summary rather than trusting it: it printed **eleven rows for ten gates**
(`9 fork-sources`, `10 unregistered-tests`, `9 fork-sources`). The lane's older `run-all-gates.sh`
has Gate 9 as its last block because it predates Gate 10; the three conflict hunks were resolved to
ours but the lane's tail auto-merged in, appending a second complete Gate 9 block after Gate 10. One
5-line block deleted; Gate 9 now appears once (171-174) and Gate 10 once (176-182); `bash -n` passes;
the three gates were re-run and `run-all-gates.sh` reports `1 of 10 gates did not run`, exit 3.

## Merge 6 — `post-alpha/vca` (`558fbd375`)

Three commits: VCA / mix-and-edit groups (#622). This lane and merge 5 both edit `src/core/Mixer.cpp`,
so this is where two mixer features have to agree.

**`src/core/Mixer.cpp` — SEMANTIC, in `Mixer::moveChannelLeft()`.** Ours adds the D4 mutex pair
(`requestChangeInModel()` at the head for the whole swap, `doneChangeInModel()` at the tail). Theirs
adds the VCA half — a loop calling `group->channelsSwapped(a, b)` for every group, then
`refreshGroups()` — and its copy predates D4, so it has no mutex pair at all. Neither side alone is
right: taking ours loses the group bookkeeping (a channel drag would silently desynchronise every VCA
group); taking theirs reverts the reorder onto an unguarded path the render thread walks. Resolved as
**BOTH**, with the VCA block and `refreshGroups()` *inside* the change mutex immediately before
`doneChangeInModel()`. Asserted: exactly one request and one done in the function, `channelsSwapped()`
and `refreshGroups()` both present, no marker.

**`tests/CMakeLists.txt`** — our whole block plus theirs' `VcaGroupTest` block; 0 duplicate targets.
**`tests/fork-sources.txt`** — entry union, +`src/core/VcaGroup.cpp` (152). **Ledger** — 434 entries,
0 added, 1 clause union.

**Gate 9 was RED on this merge before the fix, measured.** The lane registered its two product sources
in fork-sources.txt but never registered its test source anywhere:
`tests/src/core/VcaGroupTest.cpp NOT IN tests/fork-sources.txt (and not in tests/all-sources.txt or
tests/tools-sources.txt)`. Fixed the documented way — all-sources.txt re-derived from its own command,
adding the three fork-NEW sources it was missing (`include/VcaGroup.h`, `src/core/VcaGroup.cpp`,
`tests/src/core/VcaGroupTest.cpp`; 1173 entries). After: Gate 9 EXIT=0. This is the recurring gap the
new Gate 10 exists to catch from the other side.

## Manifest reproducibility

After **every** merge the three manifests were re-derived from their own documented commands and each
was proved byte-exact. The verdict was `ALL-REPRODUCE` at all six merges.

| after merge | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | verdict |
|---|---|---|---|---|
| 1 rename-complete | 144 | 1161 | 14 | `REPRODUCES` ×3 |
| 2 brand-placeholders | 144 | 1161 | 15 | `REPRODUCES` ×3 |
| 3 test-hygiene | 144 | 1162 | 15 | `REPRODUCES` ×3 |
| 4 coverage-green | 144 | 1162 | 15 | `REPRODUCES` ×3 (no manifest touched) |
| 5 racks | 150 | 1170 | 17 | `REPRODUCES` ×3 |
| 6 vca | 152 | 1173 | 17 | `REPRODUCES` ×3 |

Three of them needed work beyond a union, and the reproduction check is what found each:

* `fork-sources.txt` could not produce six hand-added entries across two lanes (4 from
  brand-placeholders, 2 from racks). Five moved to `tests/tools-sources.txt` (three brand, two racks)
  where the command produces them; the three test-side scripts are registered nowhere, by Gate 9's own
  extension rule. Each move and each exclusion is written in fork-sources.txt's header.
* `all-sources.txt` was missing seven fork-NEW C/C++ sources across two lanes (4 from racks, 3 from
  vca) that no lane had registered; re-derived from `git ls-files`.
* Headers were kept from integration in every case (the lanes' copies were the older unpinned ones).

## Gate and test results

After every merge, unpiped: `tests/fork-sources-gate.sh` → **0**, `no-upstream-regression-gate.sh` →
**0**, `run-all-gates.sh` → **3**. Gate 6's counters along the way (changed paths declared / ledger
entries): 382/410 · 423/434 · 423/434 · 423/434 · 423/434 · 423/434. Gate 9's: 144 fork-NEW / 1018
inherited / 14 tooling, rising to **152 / 1022 / 17**, 0 stale entries at every step. The
`run-all-gates.sh` row was `gate 1 ctest PASS · 2 coverage SKIP · 3 no-tautology PASS · 4 complexity
PASS · 5 mutation PASS · 6 upstream-regression PASS · 7 file-length PASS · 8 duplication PASS ·
9 fork-sources PASS · 10 unregistered-tests PASS` at every merge from #3 on.

Product-code builds were run with `JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 3` **before**
the gates at every merge (all six change the built tree, including the artwork-only merge 2, whose
SVGs are embedded), `configure`/`build`/`ctest` each `EXIT=0`, `0` compiler errors in
`build/build.log`, ctest run from `build/tests`. Gate 2 (coverage) was **not** run at any point: every
`run-all-gates.sh` invocation was the default and coverage needs `--with-coverage` plus an
instrumented build. The `3` is `PASS-WITH-SKIPS` and is not a pass.

### Final verification of the finished tree (`558fbd375`)

```
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 3
    provision EXIT=0 · configure EXIT=0 · build EXIT=0 · ctest EXIT=0
    ctest 100% tests passed, 0 failed out of 62
bash tests/fork-sources-gate.sh          -> EXIT=0  152 fork-NEW, 1022 inherited, 17 tooling, 0 stale
bash tests/no-upstream-regression-gate.sh -> EXIT=0  423 changed path(s) declared; 434 ledger entries
bash tests/run-all-gates.sh              -> EXIT=3  1 of 10 gates did not run (coverage)
```

## THE RENDER: the sha256 does NOT match the expected value — and why

`bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o …` → EXIT=0,
`build/zene`, 16-bit, 2 channels, 44100 Hz, 544,256 frames.

```
expected (3A, and the two trains before it)  6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
this train, run 1                            943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 2                            943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
```

**The audio is byte-identical. The difference is a metadata string, and it is a deliberate change this
train landed.** Diagnosed in three steps, each measured:

1. The repo's own comparator, `tools/render-determinism-compare.py`, against 3A's committed artifact
   `tests/integration-logs-3a/final/render-final-1.wav`: **0 differing frames (0.000000 %), max
   |Δ| 0 LSB, delta RMS −inf dB (i.e. zero), best lag 0, 0 of 2126 periods dirty, header match=True.**
   `tests/integration-logs-3b/final/compare-vs-3a.log`.
2. Chunk-level parse of both WAVs: same `fmt ` (16 bytes, 2 ch, 44100 Hz, 16 bit), **same `data` chunk
   size 2,177,024 bytes**, and a `LIST`/`INFO` chunk holding the software tag —
   3A: `INFOISFT` **`LMMS (libsndfile-1.2.2)`** (chunk size 24) ·
   3B: `INFOISFT` **`Zene Studio (libsndfile-1.2.2)`** (chunk size 32). That is the entire 8-byte
   difference in file size (2,177,112 → 2,177,120), and the only difference of any kind.
3. The sha256 of the **`data` chunk alone is identical across all four renders**:
   `b37cefc5a97e2d46…` for 3A's reference, 3B run 1, 3B run 2 and a render of the pre-train project
   file. So the render is still reproducible (run 1 == run 2, same-build floor 0) and the audio path
   is unchanged.

The tag is the documented change merge 1 landed: `src/core/audio/AudioFileWave.cpp` — *"rename layer
1: the software tag written into rendered audio files names Zene Studio"* (its own ledger entry). The
8 bytes are that string plus chunk alignment.

**Verdict.** The expected hash is no longer reachable **by design**, because train 3B deliberately
changed a user-visible product identifier written into every rendered WAV. Per the brief this is
reported as a finding rather than papered over. The behaviour-preservation question — "did the merges
change the audio?" — is answered **no**: 0 LSB, 0 differing frames, identical `data` chunk hash. The
new hash for this project on this build is `943e3238…`; a future train should expect that value or a
further deliberate tag change, not `6b51f70f…`. Rendering the pre-train project file
(`git show 627fb1e4f:data/projects/shorties/sv-DnB-Startup.mmpz`) gives the same `943e3238…`, so the
layer-3 rewrite of the shipped project is not part of the difference.

## Duplicated content: the patch-id proof

Every non-merge commit reachable from each of the six branches was patch-id'd (`git show <c> | git
patch-id --stable`) and compared against the 265 patch-ids of the pre-train integration
(`4e677cb6c6ab..627fb1e4f`), and against the other 37 commits in the six:

```
rename-complete        6 commits   0 duplicates
brand-placeholders    12 commits   0 duplicates
test-hygiene           5 commits   1 duplicate  2c11fe4c0 == integration's dd121d606  (da4b8210d8)
coverage-green         3 commits   0 duplicates
racks                  8 commits   0 duplicates
vca                    3 commits   0 duplicates
0 duplicates among the six themselves
```

Only `test-hygiene`'s `2c11fe4c0` is an exact cherry-copy, of `dd121d606`, already reachable from
integration before the train — so the merge applied nothing for it and there was no keep-one-copy
decision to make. Proved empty with `git diff HEAD post-alpha/test-hygiene --
tests/src/core/TwoTrackRecordingHarness.cpp`. Evidence: `tests/integration-logs-3b/merge3/patchid-*`.

Separately, the **ledger** carried genuinely duplicated *paths* — merge 3's auto-merge produced two
lines for two `src/core/AudioEngine*` files, resolved by a path-level dedupe with a clause union (see
merge 3). That is the one place in this train where "keep one copy with the newer reason" applied, and
it applied to a path, not to a commit.

## Test expectations changed during this train

**Exactly one test file, four assertions — and this is the complete list.**

| where | what changed | why the new expectation is the intended behaviour, not the convenient one |
|---|---|---|
| `tests/src/core/DataFileSaveIntegrityTest.cpp` lines 123, 234, 241, 259 | `contains("<lmms-project")` → `contains("<zene-project")` | These four assert the root element of a file the test has just **written** with `DataFile::writeFile()`. Layer 3 (`c30c93082`) deliberately makes the format write-new: `write()` sets `dataFile.documentElement().setTagName("zene-project")` and the constructor creates a `zene-project` root, both stated in the lane's own commit message and held by its own test. The old literal therefore asserted behaviour the product deliberately stopped having; the save-integrity properties the file exists to check (a refused rename keeps the recovery copy, a backup is kept, a stale `.bak` does not block a save) are untouched. The change is provably not vacuous — it is the assertion that **failed** against `<lmms-project` on this very tree before the edit. |

**The read-both half: it was never in that test, and it is covered elsewhere.** Answering the question
directly — `DataFileSaveIntegrityTest` **only ever covered the write side**. Every one of its
assertions reads a file it wrote; the one remaining `<lmms-project` literal in it (line 146,
`writeText( temp, QStringLiteral( "<lmms-project/>" ) )`) is a fixture fed to the *rename-dance
control*, never parsed through `DataFile`'s reader, and it is deliberately left byte-for-byte as it
was. The load-half lives in the lane's own `tests/src/core/DataFileFormatTest.cpp` and is green:

```
DataFileFormatTest::newDocumentWritesTheNewRoot           PASS   (write side: zene-project + creator)
DataFileFormatTest::preRenameFileStillLoads               PASS   (read-both: <lmms-project> loads, <head>
                                                                 and <song> resolve, timesig_numerator==4)
DataFileFormatTest::reSavingAnOldFileWritesTheNewRoot     PASS   (round trip: read old -> write new,
                                                                 content survives, DOCTYPE dropped)
DataFileFormatTest::newRootFileLoads                      PASS
DataFileFormatTest::legacyMultimediaRootStillLoads        PASS   (pre-LMMS root still accepted)
Totals: 7 passed, 0 failed
```

Independently, five other tests build a `<lmms-project …>` document as **input** and pass
(`MidiProbabilityPersistenceTest`, `ProjectVersionTest`, `SlideNotesTest`, `ProjectOpenIntegrityTest`,
`StemExportTestSupport.h`), which is further evidence that read-both works. So no read-side assertion
was relaxed, and none is missing.

**No other test expectation, assertion, tolerance or baseline was adjusted anywhere in this train.**
The other file-level changes I made to the test tree were:

* `tests/CMakeLists.txt` — the duplicated `ENVIRONMENT` line (fix-up `21ba5d2be`) and the removal of
  the now-false `# NOT REGISTERED, deliberately` note at merge 3. That note is a *declaration*, not a
  test expectation, and the file it declared unbuilt was made to build by the lane that merged it.
* `tests/run-all-gates.sh` — the duplicate Gate 9 block (fix-up `a7ae65361`).
* `tests/file-length-baseline.tsv` — one entry moved 1169 → 1174 through the gate's own
  `--reanchor-file` valve with a reason, never by hand.
* Manifests, the ledger, `docs/COVERAGE-GATE-GREEN.md` (a relocation note) and the 21 path references
  rewritten for the `docs/coverage-green/` → `tests/coverage-green/` move. No number, measurement or
  log was altered by any of these.

## Findings and refusals

1. **The render sha256 differs from the expected value** — reported above with the diff, the
   chunk-level cause and the proof that the audio is bit-identical. Not papered over; the expected
   constant is simply no longer reachable after a deliberate rename.
2. **The auto-merge double-locked `EffectChain`'s change mutex** (merge 5) — a would-be permanent
   render stall, found by reading the resolved text rather than the conflict markers, fixed in the
   merge commit with the lock semantics measured from `AudioEngine.cpp`.
3. **`DataFileSaveIntegrityTest` asserted stale write-side behaviour** (merge 1) — a real cross-lane
   defect, fixed in a labelled fix-up with the full analysis above.
4. **Two auto-merge artefacts in my own merges** — the duplicated `ENVIRONMENT` line (merge 1, caught
   by `cmake`) and the duplicated Gate 9 block (merge 5, caught by reading the gate summary). Both are
   labelled fix-ups; neither is a silent sweep. The resolver bug behind the first is fixed and
   committed.
5. **Gate 9 was red on two lanes' own trees** and was fixed at integration both times: the vca lane's
   test source was registered nowhere; the racks and brand lanes hand-added entries that made
   `fork-sources.txt` stop reproducing. Both were caught before the merge commit by running the
   manifests' own commands and the gate replays.
6. **`tests/file-length-baseline.tsv` moved by exactly one entry**, through the gate's own
   `--reanchor-file` valve with a written reason (merge 1). No `--reanchor`, no baseline re-anchored
   to green a gate, and no code trimmed to satisfy a metric. The whole-tree (`--scope all`) file-length
   baseline is **still red and was deliberately not touched** — 3A reported 14 stale entries there and
   this train did not re-anchor any of them; that remains an owner decision.
7. **Gate 6's rule was replayed against the index rather than trusting a green script**, because a
   pre-commit `no-upstream-regression-gate.sh` reads HEAD and cannot see the merge it is about to
   commit. This is how merge 4's 16 `docs/` findings and merge 6's Gate 9 failure were caught.

## What is NOT proven

* **Gate 2 (coverage) was not run at any point in this train.** Every `run-all-gates.sh` invocation was
  the default; the expected `3` is `PASS-WITH-SKIPS` and is not a pass. The coverage-green lane's own
  measurement and its 47→75 baseline union are in the tree, but I did not re-run coverage.
* **CI was not run** and no CI configuration was changed. Every exit code here is local.
* **The whole-tree scope was not made green** (finding 6).
* I verified the conflicts I resolved, the manifests I re-derived and the gate results. I did **not**
  audit the product code any lane brought in; the audio path is covered only by the render comparison
  above (0 LSB, identical `data` chunk hash) and by each lane's own tests.
* The `PdcMixerTest` teardown abort that 3A and 2A saw did not reproduce in any of my runs (six full
  `local-ci` ctest runs plus six `run-all-gates.sh` ctest runs, all green). That is absence of
  evidence, not a fix.

## Concurrent activity — noted, not touched

Another session is live in this clone. `post-alpha/*` branches took new commits during the train
(`brand-placeholders` at 03:14, `coverage-green` 03:01, `test-hygiene` 02:56 per 3A's survey; the
worktrees `zene-pa-headless` and others had live processes). At entry the tree was settled: 3A's
final `run-all-gates.sh` was still running (PID 1102772), so I waited — bounded, re-checking every
~60 s — until it exited, and only then started; it wrote `tests/integration-logs-3a/final/run-all-gates-tip.log`
(untracked, left alone, later committed by 3A itself as `627fb1e4f`) and 3A then committed its own tip
log, which is why the entry tip is `627fb1e4f` rather than the `748daa907` the brief names. No
concurrent-session branch was read-write touched and none was merged.

## Decisions needed from the owner

1. **The render sha256 constant.** `6b51f70f…` is dead for this project on this build — merge 1's
   `AudioFileWave.cpp` software-tag change makes it unreachable, while the audio is bit-identical.
   Do you want the constant replaced by `943e3238…` in the release/verification docs (and any CI
   comparison updated), or the tag reverted to `LMMS` for the render recipe's input only? I did not
   change any documented expected value.
2. **Where fork evidence lives: `tests/` (what 3A and now I do) or a new Gate 6 `docs/` category.**
   I relocated `docs/coverage-green/` → `tests/coverage-green/` for the same reason 3A relocated
   `docs/coverage-run/`. The alternative is one `case` arm in Gate 6. Same question 3A raised; it is
   now been answered the same way twice and should be settled centrally.
3. **The whole-tree (`--scope all`) file-length and complexity baselines are stale** (14 entries at
   3A's exit, untouched here). Options as 3A listed them: one `--reanchor` (grandfathers all), per-file
   `--reanchor-file` decisions, or leave advisory-red and recorded.
4. **Gate 6's `tools/` category should read `tests/all-sources.txt` too, or classify every `tools/**`
   path as fork tooling outright.** Unchanged from 3A's finding; this train added two more `tools/`
   files and hit the same hole at merge 2 and again at merge 5.
5. **Do the lanes own their test-source registration?** Four separate lanes in two trains reached
   integration with a new test source registered in no manifest (vca here; three in 3A's wave). Gate 10
   now catches it from the "never built" side, but the registration gap itself is a lane-discipline
   question, not an integration one.

## Evidence index (`tests/integration-logs-3b/`)

```
merge1/ … merge6/   local-ci.log (configure/build/ctest, unpiped), gate9-*.log, gate6-*.log,
                    run-all-gates.log, EXITS.txt, plus per-merge extras (see below)
merge1/file-length-reanchor.log, file-length-check-{before,after}-reanchor.log
merge3/gate10-unregistered-tests-precommit.log, patchid-integration-history.txt
merge4/gate6-index-replay-BEFORE-relocation.log        the 16 findings that justified the move
merge5/EXITS.txt                                       the gate exit codes unpiped
final/render-final-1.wav, render-final-2.wav           run 1 and run 2 (same-build floor 0)
final/render-oldinput.wav                              the pre-train project file, same hash
final/render-{1,2}.log, render-oldinput.log            the recipe's own output for each run
final/compare-vs-3a.log                                the repo's comparator against 3A's artifact
tools/resolve_pair.py, regen.py, precommit_check.py, merge_ledger.py, resolve1.py, verify_union.py
```

The tree is clean at `558fbd375` apart from this report before it is committed. The git-history
commits of the train are `12b2994e0`, `21ba5d2be`, `9de48fe20`, `d77b34d14`, `046203b7c`, `4a4f96449`,
`3c245cb8a`, `a7ae65361`, `558fbd375` — six merges, two fix-ups to merge 1, one fix-up to merge 5 —
plus four evidence commits.
