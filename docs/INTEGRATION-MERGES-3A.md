# Integration merges into `post-alpha/integration` — merge train 3A (six post-alpha lanes)

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Entry tip: **`0581346e4`** (clean, no other writer in the worktree — see "Concurrent activity").
Exit tip: **`bf8f53413`** — six merge commits plus one labelled fix-up, then this report.

Nothing was pushed; no remote, PR, issue or branch was touched; `origin` (LMMS/lmms) and
`messmerd` were not contacted; no branch was rebased, amended, reset or rewritten; nothing
was staged with `git add -A` (every commit's `git diff --cached --name-only` was checked).
Every exit code below was measured unpiped (`cmd > log 2>&1; echo EXIT=$?`), and every log
is committed under `tests/integration-logs-3a/` — not `/tmp`, which is where a previous
verification's evidence was destroyed by a disk reclaim.

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` exits **3** = `PASS-WITH-SKIPS` (gate 2 coverage needs `--with-coverage`),
which is the expected green result and is **not** a pass.

## The six merges, in the order the parent specified

| # | Lane | Merge commit | Conflicts | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|--------|--------|--------------------|-------|
| 1 | `post-alpha/gate-hygiene` | `967a5e57c` | 3 files | 0 | 0 | 3 | 46/46 |
| 2 | `post-alpha/coverage-run` | `0292a1b24` | 0 | 0 | 0 | 3 | 46/46 |
| 3 | `post-alpha/render-determinism` | `34c1f4f86` (+ fix-up `4dae393fe`) | 1 file | 0 | 0 | 3 | 47/47 |
| 4 | `post-alpha/recording-realtime` | `296db5dd7` | 2 files | 0 | 0 | 3 | 49/49 |
| 5 | `post-alpha/saveload-integrity` | `2382d8d22` | 3 files | 0 | 0 | 3 | 51/51 |
| 6 | `post-alpha/mixer-concurrency` | `bf8f53413` | 4 files | 0 | 0 | 3 | 52/52 |

`git log --oneline --merges 0581346e4..HEAD` returns exactly the six merge commits above and
nothing else. No other branch was merged: the ~35 branches ahead of integration that belong to
the other session (`pr7459-rebase`, `post-alpha/lv2-catalogue`, `post-alpha/cmd-*`,
`agent-surface-gate`, `agent-control-surface`, `headless-load`, `control-hardening`,
`docs/fork-readme`, `tools/local-ci`, `test/real-client-e2e`, `feat/stem-split`,
`feat/session-view-model`, `fix/latency-complexity`, …) were never touched, and neither were
the lanes this train did not list (`mpe`, `racks`, `router-live`, `session-scheduler`,
`telemetry`, `warp`, `determinism`'s siblings, `gate-debt`, `coverage-green`, …).

**The brief says gate-hygiene is 8 commits; it is 9** (`git rev-list --count
post-alpha/integration..post-alpha/gate-hygiene` = 9, and `git log --oneline` lists nine).
Its report also records that it landed its ledger rewrite as its own final commit, which is
the ninth. The rest of the lane counts match the brief exactly (2/3/2/5/5 = 17 commits, and
26 across all six).

## Method: how a ledger or manifest conflict was resolved

Every conflict in this train was in an append-only registry, a CMake list or the gate-output
file — never in product code. Nothing was resolved by unioning **lines** across conflict
markers.

For each conflicted file the two sides were read from the index (`git show :2:<file>` =
integration, `:3:<file>` = branch). Then, per class:

* **Entry lists** (`tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/tools-sources.txt`)
  the entry **set** was unioned, and the result was **re-derived from the file's own documented
  command** — the command written in the file's own header. The gate-hygiene lane's report
  states the rule for exactly this case: *"take the union of the entry lines from both sides,
  then run the file's own documented command and `LC_ALL=C sort`; the result is the file."* That
  is what was done, and it is why the numbers below are reproducible rather than asserted.
* **The divergence ledger** (`tests/upstream-modifications.txt`) paths were unioned by key, and
  a shared path's reason was resolved by containment: keep the superset when one side's text
  contains the other's, otherwise both clauses (integration's first, and only when they describe
  *different* changes). Two reasons needed a real clause union (`include/Song.h`,
  `src/core/Song.cpp`, merge 5). No reason text was authored from scratch: every kept segment is
  one of the two sides' own text.
* **`tests/CMakeLists.txt`** hunks were resolved as a union of the **entries inside the list**
  (`src/core/XTest.cpp` lines), never a line union — a structural `)` or a silently deduped
  entry is the failure mode that ruins a CMake list.
* **`tests/file-length-exempt.txt`** (add/add, merge 6) was resolved exactly as the gate-hygiene
  report predicted: that file's header plus the mixer lane's single entry line, the lane's reason
  text verbatim.

Every resolution was then asserted programmatically before it was staged — never by eye:

* no conflict markers;
* no duplicate entry;
* **every entry from both sides present** (the only exceptions are recorded below and each is a
  deliberate move of home, not a loss);
* every reason non-empty, no `;;`, no blank segment (Gate 6 refuses a blank reason with exit 2).

The three tools this train was driven with are committed verbatim under **`tests/integration-logs-3a/tools/`**
(they were written and run from outside the repo, at `AI_KOS_PROJECT/.merge3a/`, and copied in as
evidence — a `.py` under `docs/` would itself be a Gate 6 violation, see merge 2):

* `merge_ledger.py` — union of two conflicted sides of an append-only ledger, with the containment
  rule for reasons and the `every entry from both sides present` assertion;
* `regen.py` — runs each manifest's own documented command (`--head` for the header's command
  verbatim, the index form for a pre-commit check) and prints `REPRODUCES` /
  `DOES-NOT-REPRODUCE` plus the exact added and removed entries, duplicates and totals;
* `precommit_check.py` — replicates Gate 6's mechanical rule against the merged result, checks the
  three manifests for duplicates/blank entries and the ledger for blank reasons and `;;`, and
  reports cross-manifest collisions.

### Two decisions the plain entry union would have got wrong

1. **Deletions must stay deleted.** Merge 5's branch carries a copy of the ledger from before
   merge 1, so a naive union resurrected the seven `tools/mmpz-git/*` entries that merge 1
   deleted (they assert a divergence of inherited code about files upstream has never had, and
   they now live in `tests/tools-sources.txt`, which Gate 6 classifies). The resolver asserts the
   branch's only other new entry is `src/core/DataFile.cpp`; the seven are excluded and the
   exclusion is recorded in the merge commit.
2. **A union of entries is not enough when the branch's own copy is older.** In merge 5, two
   shared reasons needed the branch's *clause* (`save integrity: …`) appended to integration's
   newer, longer reason, and seven others were the branch's *older wording* of a change
   integration already describes — checked one by one. In merge 3 and merge 6, every shared path
   was verified before choosing (`ours extends theirs: True`).

## Manifest reproducibility — the sharpest hazard, and how it was discharged

The manifests have diverged: before this train `fork-sources.txt` was 125 on the previous
train's reading, gate-hygiene's branch carried 129, the previous train's union produced 139, and
the all-sources file was 20 entries short of its own command's output. After **every** merge the
three manifests were re-derived from their own documented commands and each was proved
byte-exact:

| after merge | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | reproduction |
|---|---|---|---|---|
| 1 gate-hygiene | 138 | 1,149 | 12 | `REPRODUCES-FORK` / `REPRODUCES` / `REPRODUCES` |
| 2 coverage-run | 138 | 1,149 | 12 | unchanged (no manifest touched) |
| 3 render-determinism | 139 | 1,151 | 14 | `REPRODUCES` ×3 |
| 4 recording-realtime | 141 | 1,155 | 14 | `REPRODUCES` ×3 |
| 5 saveload-integrity | 143 | 1,157 | 14 | `REPRODUCES` ×3 |
| 6 mixer-concurrency | 144 | 1,158 | 14 | `REPRODUCES` ×3 |

Each proof was run **twice**: against the index before the commit (`--cached` form of the same
command, so the merge commit can carry the registration itself) and again against `HEAD` after
the commit, using the header's command verbatim. Both printed `REPRODUCES`. No manifest was
hand-edited to a result; every entry list in the tree today is the byte-exact output of the
command printed in its own header.

Reproduction needed the headers to be *true*, and three of them were not, in ways that matter:

* `tests/fork-sources.txt`'s command admits `tests/` sources only through an explicit `awk`
  allow-list. Nine such sources arrived from four different lanes in this wave; the allow-list
  gained exactly those names, one merge at a time, with the reason for each written in the header
  (it now names all ten and says why each is in this scope rather than `all-sources.txt`).
  Its pathspec also gained `tests/src/tracks`, and a second explicit pathspec line now names
  `tools/ncpu-shim.c` beside `tools/local-ci.sh` (see merge 3).
* `tests/tools-sources.txt` gained `tools/render-determinism-compare.py` and
  `tools/render-determinism-probe.sh` (merge 3) and its subtraction regex and members paragraph
  were corrected to say where each tools/ file's home is.
* `tests/fork-sources.txt`'s regeneration command could not produce the two clip-lane test
  sources that the *integration* side of merge 1 had hand-added (their lane's own comment says
  so). They are kept — removing them would narrow the fork scope onto nothing and would strand a
  fork file-length baseline entry (`SampleClipWindowTest.cpp` 511).

## Gate and test results

`run-all-gates.sh` per merge — the table row is the same every time, which is itself the result:

| merge | 1 ctest | 2 cov | 3 no-taut | 4 cplx | 5 mut | 6 up-reg | 7 file-len | 8 dupe | 9 fork-src | exit |
|---|---|---|---|---|---|---|---|---|---|---|
| 1–6 (each) | PASS | SKIP | PASS | PASS | PASS | PASS | PASS | PASS | PASS | **3** |

After every merge, unpiped: `tests/fork-sources-gate.sh` → **0**, `tests/no-upstream-regression-gate.sh`
→ **0**, `tests/run-all-gates.sh` → **3**. Gate 6's own counters along the way (changed paths
declared / ledger entries): 100/124 · 100/124 · 102/126 · 105/129 · 106/130 · 106/130. Gate 9's:
138 fork-NEW / 1,012 inherited / 12 tooling → 144 / 1,015 / 14, **0 stale entries** at every step.

Product-code builds were run **before** the gates after merges 1, 3, 4, 5 and 6 (the merges that
change product code), with `bash tools/local-ci.sh --build-dir build --jobs 3` — `configure`,
`build`, `ctest` each `EXIT=0`, `0` compiler errors in `build/build.log`, and ctest run from
`build/tests` (the top-level build dir reports 0 tests, which is an error, never a pass). Merge 2
changes no product code and no rebuild was owed. Suite size over the train: 42 → 46 → 47 → 49 →
51 → **52**, all passing.

### Final verification of the finished tree (`bf8f53413`)

```
cd build && cmake --build . -j3                                   -> EXIT=0   0 errors
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
                                                                  -> EXIT=0
     100% tests passed, 0 tests failed out of 52
bash tests/fork-sources-gate.sh                                   -> EXIT=0
     144 fork-sources entry(ies), 1015 all-sources (whole-tree), 14 tools-sources,
     0 stale entry(ies); PASS: every tracked source in scope is registered
bash tests/no-upstream-regression-gate.sh                         -> EXIT=0
     PASS: every change to upstream-inherited code since 01148947ea is declared
     (106 changed path(s) declared; the ledger holds 130 entries)
bash tests/run-all-gates.sh                                       -> EXIT=3
     PASS-WITH-SKIPS: only gate 2 (coverage) did not run
bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
     -o tests/integration-logs-3a/final/render-final-1.wav         -> EXIT=0
     sha256 6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
     16-bit, 2 channels, 44100 Hz, 544,256 frames, 12.341 s
```

### The render comparison, and a brief/rule mismatch to record

The brief asks for a **byte-identical render comparison against the tip before you started**.
`DELEGATION-RULES.md` §3 forbids exactly that: *"Behaviour preservation is proved by the
measured-spread method now — max |Δ| in LSB and dB against a same-build run-to-run floor,
never `sha256` byte-identity, because renders in this tree are not bit-reproducible … If you
were briefed for byte-identity, that brief predates this rule — say so and use the spread."*
Both were measured, and they agree:

```
baseline  (tree at 0581346e4, before any merge):
  tests/integration-logs-3a/render-baseline.wav
  sha256 6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
final tip (tree at bf8f53413, rebuilt for this run), two runs:
  tests/integration-logs-3a/final/render-final-1.wav  sha256 6b51f70fc32e…
  tests/integration-logs-3a/final/render-final-2.wav  sha256 6b51f70fc32e…
```

All three files are **byte-identical**, so the same-build run-to-run floor for this project and
this recipe is **0 LSB / 0.000000 dB**, and the behaviour-preservation spread is **0** as well.
`final/render-final-1.wav` is kept in the repo as the reference artifact;
`final/render-sha256.txt` records all three hashes (the baseline and the second final run were
removed after hashing, so ~4 MB of reproducible binary does not enter the history).
The sha256 is also the value **both** previous trains recorded for this project, so it is now
byte-identical across three trains and 22 lane-merges. The rule's stated premise (renders here
are not bit-reproducible) does not hold for this project on this build; where it *does* hold, the
rule's spread method is the one that would bind. Reported as a mismatch between the brief and
`DELEGATION-RULES.md`, with both numbers rather than a choice between them.

## Duplicated content: patch-id proof

The brief expects cherry-picked duplicates. **Among these six branches there are none**, and that
is a measurement, not an assumption:

```
for each branch:  git log --no-merges -p --format="commit %H" 0581346e4..<branch> | git patch-id --stable
reference set :   git log --no-merges -p --format="commit %H" 4e677cb6c6ab..0581346e4 | git patch-id --stable
                  -> 236 comparable patch-ids (the pre-train integration)
result        :   0 of the 26 branch commits' patch-ids occur in that set
                  0 duplicate patch-ids within the 26
```

Logs: `tests/integration-logs-3a/final/patchid-*.txt`. So there was no "keep one copy with the
newest reason" decision to make in this train — and, correspondingly, no commit was deduped. The
duplicates the brief describes (`post-alpha/mpe` 6, `post-alpha/session-scheduler`'s Gate 9 run,
`post-alpha/clip-slice0`'s three gate commits) belong to branches that are **not** on this
train's list; `clip-slice0`'s were already resolved by the previous train and are in the tree it
left behind.

## Concurrent activity in the repository (noted, not touched)

Another session is live in this clone, and it was active during the whole train. Branches whose
tip commit is timestamped *inside* my run window: `post-alpha/agent-surface-integration` and
`post-alpha/gate-fix` (03:41), `post-alpha/stable-ids` (03:39), `post-alpha/release-prep`
(03:36), `post-alpha/brand-placeholders` (03:14), `post-alpha/instrument-view-safety` (03:03),
`post-alpha/coverage-green` (03:01), `post-alpha/test-hygiene` (02:56). `gate-fix` and
`release-prep` did not exist in my entry survey, so branches are being created while this train
runs. At entry I also found live `cc1plus` processes belonging to
`worktrees/zene-pa-coverage-green/build` — a sibling lane compiling. I built at `-j3` throughout
for that reason (and because a sibling lane's build was OOM-killed at `-j4`). None of those
branches, worktrees or files was read-write touched; **none of them is being merged here**.

## Findings and refusals

1. **`post-alpha/coverage-run` was RED on Gate 6 on its own tip — the brief's "finished,
   verified" does not hold for that branch.** Reproduced before merging, in its own worktree:
   `bash tests/no-upstream-regression-gate.sh` → **EXIT=1**, 14 violations, every one a
   non-`.md` file under `docs/coverage-run/` (`.info`, `.log`, `.gz`, `.py`, `.sh`, `.txt`).
   Gate 6's mechanical rule allows `tests/**`, build config, CI config, `*.md`, fork-NEW sources,
   registered fork tooling and the ledger — a `.log` under `docs/` falls through all of them.
   **Fixed in the merge commit, without weakening any gate and without editing a gate script:**
   the evidence directory is relocated `docs/coverage-run/` → `tests/coverage-run/`, which is the
   category Gate 6 allows and where this repo already keeps durable evidence
   (`tests/reference/`, `tests/scripted/`, `tests/gate-hygiene-logs/` — the gate-hygiene lane had
   to make exactly the same move for its own logs on the same night). Nine path references in the
   lane's report and two of its scripts were edited for the path alone; the report carries a
   short note recording the relocation; no measurement, log or number was altered. The
   alternative — teaching Gate 6 a `docs/` category, generalising its `tools/` category on the
   same "this top-level directory does not exist at the fork point" argument — was considered and
   refused, because it relaxes a gate to make a path legal while the relocation makes it legal
   without touching any gate. **See "Decisions needed".**
2. **A stale comment block in the ledger, left by merge 1** (`tests/upstream-modifications.txt`,
   the `# --- fork-new developer tooling under tools/` banner) still claimed *"A future
   gate-config change should give tools/ its own category; until then this is the honest place to
   record them"* — false as of merge 1, which added that category and deleted the seven entries.
   Corrected in merge 3's commit to a `SUPERSEDED` record. Nothing else from merge 1 was found to
   be stale.
3. **`tools/ncpu-shim.c` exposed a hole in Gate 6's `tools/` category, and Gate 6 went red on it
   in merge 3, measured.** Gate 6 classifies a `tools/` path by `tests/tools-sources.txt` alone
   and consults no other list, so `all-sources.txt` membership — automatic for any C/C++ file
   under `tools/`, because that list is `git ls-files` — is invisible to it. Re-homing the file
   to `all-sources.txt` therefore reddened the gate (`EXIT=1`, exactly one violation,
   `tools/ncpu-shim.c`). Fixed in fix-up `4dae393fe` by returning the file to
   `tests/fork-sources.txt`, which its own lane had chosen and which Gate 6 honours, admitting it
   by name on the same explicit pathspec line that already carries `tools/local-ci.sh`, with the
   reason written in both files' headers. The same hole means the four
   `tools/mmpz-git/scratch/*.cpp` probes and `tools/wasm/wat2wasm.cpp` would be Gate 6 violations
   if anyone changed them; that is **pre-existing** and reported, not fixed.
4. **The whole-tree scope is red, and it was deliberately not re-anchored.**
   `bash tests/file-length-gate.sh --scope all --check` → **EXIT=1**, 14 regressions. 10 of them
   predate this train (documented in `docs/INTEGRATION-MERGES-2.md`, left un-refreshed by the
   previous train on purpose). 4 exist because of this train's merges:
   `src/core/AudioEngine.cpp` 974→1010 (merge 4), `src/core/DataFile.cpp` 2239→2289 (merge 5),
   `include/Mixer.h` 502→509 and `src/core/Mixer.cpp` 1725→1793 (merge 6). No run of the three
   gates this task specifies measures the whole-tree scope — it is an advisory monitoring ratchet
   by `tests/QA-GATES.md`'s own scope policy — and `--reanchor` rewrites the entire baseline from
   the current tree, which would grandfather all 14 unreviewed. **Refused; escalated instead.**
5. **`--reanchor-file` was never used and no baseline was moved.** No gate needed it: after the
   fixes above, file-length, complexity and duplication are `EXIT=0` in fork, tools and (for the
   three gates in scope) at every merge. `tests/file-length-exempt.txt`'s exemption mechanism was
   exercised with both negative controls before being committed (merge 6): a scratch exemption
   took the all-scope regression count 14 → 13 and removed its target's line entirely, and a blank
   reason exited **2** with `entry 'src/core/Mixer.cpp' has no reason`. Both were reverted
   immediately and the file as committed is the resolved union.
6. **A dirty `src/core/RoutingGraph.cpp` was preserved as a patch, then restored** —
   `tests/integration-logs-3a/dirty-RoutingGraph-mutant.patch` (13 lines, one mutant statement).
   Full disclosure: it was not pre-existing dirt. My first `run-all-gates.sh` at the entry tip hit
   the tool's 420-second limit and was killed **inside the mutation gate**, which mutates that file
   and restores it afterwards; that invocation was abandoned, the file restored by hand from
   `HEAD`, and every `run-all-gates.sh` run under a merge below ran to completion (the mutation gate
   restores the file itself — `git status` was re-checked after each run). The patch is kept as
   evidence and the file is byte-identical to `HEAD` in every merge commit.
7. **Nine paths were not kept in the scope they were registered in, and each is a recorded move
   of home, not a lost entry**: `tools/stem-export-demo.py` (merge 1 → `tests/tools-sources.txt`,
   gate-hygiene's decision, kept), `tools/render-determinism-compare.py` and
   `-probe.sh` (merge 3 → `tests/tools-sources.txt`), `tools/ncpu-shim.c` (merge 3 → moved out and
   back to `tests/fork-sources.txt` in the fix-up), and the seven `tools/mmpz-git/*` ledger
   entries (merge 5 → **not** resurrected). Everything else from both sides of every conflict is
   present, asserted programmatically at resolution time.
8. **The stale comment block a lane brought with it was dropped, not kept** (merge 6,
   `tests/fork-sources.txt`): it asserted "the regeneration command in the header only walks
   src/include/plugins", which stopped being true when the allow-list was introduced. Keeping it
   would have preserved a false statement about the file it sits in.

## What is NOT proven

* **Gate 2 (coverage) was not run at any point in this train.** Every `run-all-gates.sh` invocation
  was the default; coverage needs `--with-coverage` and an instrumented build. The expected `3` is
  `PASS-WITH-SKIPS` and is not a pass. The coverage-run lane's own measurement is now in the tree
  under `tests/coverage-run/`, but I did not re-run it.
* **CI was not run** and no CI configuration was changed. Every exit code here is local.
* **The whole-tree scope was not made green** (finding 4).
* I verified the conflicts I resolved and the manifests I re-derived; I did **not** audit the
  product code any lane brought in. Product correctness is each lane's own report and its tests.
* The `PdcMixerTest` teardown abort that the gate-hygiene lane reported (1 in 3 suite runs under
  parallel load, 4/4 isolated runs clean) **did not reproduce in any of my runs** — **12** full
  suite runs (the entry-tip baseline plus `local-ci`'s and `run-all-gates.sh`'s ctest for each of
  the five product-code merges and merge 2), every one `100% tests passed` — but that is absence
  of evidence at a different moment, not a fix.

## Concurrent activity — explicit note for the parent

`zene-pa-coverage-green`'s build tree was actively compiling when I entered, and eight branches in
this repository took new commits during the train (see the list above). I did not race any of
them: the entry check (`git status` clean; `find . -newermt '-10 minutes' -type f` returning only
the previous train's `RoutingGraph.cpp` restore and its report) passed before merge 1, and I
re-checked `git status` after every gate run.

## Decisions needed from the owner

1. **Where does the coverage lane's evidence live — `tests/coverage-run/` (what I did) or
   `docs/coverage-run/` with a new Gate 6 `docs/` category?** I chose the relocation because it
   greens the gate without changing a gate, and because `tests/**` is where this repo already
   keeps durable evidence. If you prefer the gate change, the relocation is one `git mv` plus
   nine path references to reverse, and the category is one `case` arm to add.
2. **The whole-tree (`--scope all`) file-length baseline is stale by 14 entries, 4 of them from
   this train.** Options: (a) one `--reanchor` with a reason, which grandfathers all 14 at once;
   (b) 14 per-file `--reanchor-file "<path>" "<reason>"` decisions, which is what the gate-hygiene
   lane did and is the honest instrument; (c) leave it advisory-red and record it. It is not one
   of the three gates this task measures, so I did not choose for you. The same question applies
   to the complexity all-scope baseline if you want the whole-tree scope green before a freeze.
3. **Gate 6's `tools/` category should probably read `all-sources.txt` too, or classify every
   `tools/**` path as fork tooling outright** (its own stated justification — *"tools/ does not
   exist at the fork point, so nothing under it can be an inherited divergence"* — is true of
   every path under `tools/`, not only the ones registered in `tools-sources.txt`). Today,
   changing any of `tools/mmpz-git/scratch/*.cpp` or `tools/wasm/wat2wasm.cpp` turns Gate 6 red.
   That is a gate change and therefore your call, not mine.
4. **`docs/COVERAGE-RUN.md`'s quoted figures** (`docs/CONVENTIONS.md` still says 99 fork /
   1,095 all-sources, per the coverage lane) remain stale. `docs/CONVENTIONS.md` was not touched
   by any lane in this train or the last one; the parent refreshes it centrally.

## Evidence index (`tests/integration-logs-3a/`)

```
baseline-ctest.log · baseline-fork-sources-gate.sh.log · baseline-no-upstream-regression-gate.sh.log
baseline-run-all-gates.sh.log · baseline-render.log · render-recipe.log
dirty-RoutingGraph-mutant.patch            the killed mutation run's file, preserved then restored
merge1/ … merge6/                          merge log, commit message, gate9/gate6/run-all-gates logs,
                                           local-ci.log (configure/build/ctest, all unpiped)
final/render-final-1.wav                   the reference render artifact (see below)
final/render-{1,2}.log · final/render-sha256.txt
                                           every sha256 of the three byte-identical renders
final/patchid-*.txt                        the patch-id proof inputs and outputs
sides/merge<N>/                            :2: and :3: copies of every conflicted file, as read
```

The tree is clean at `bf8f53413` apart from the untracked log directory this report is committed
from; `df -h` reported 35 GB free at entry and 39 GB at exit, with no stray build tree left
behind (one build directory, `build/`).
