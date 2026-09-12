# Integration merges into `post-alpha/integration` — merge train 3C (six post-alpha lanes)

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Entry tip: **`f5c47b0c8`** — train 3B's exit tip `558fbd375` plus 3B's own two report
commits, which is what the worktree actually held when the tree settled.
Exit tip: **`b0d6a2e0b`** — six merge commits, no fix-ups, then the evidence commits and
this report.

Nothing was pushed; no remote, PR, issue, tag or branch was touched; `origin` (LMMS/lmms) and
`messmerd` were never contacted; no branch was rebased, amended, reset or rewritten; nothing was
staged with `git add -A` (every `git diff --cached --name-only` was read before every commit).
Every exit code below was measured unpiped (`cmd > log 2>&1; echo EXIT=$?`) and every log is
committed under `tests/integration-logs-3c/` — not `/tmp`, which has already destroyed one
verification's evidence in this program. `df -h`: 56 GB free at entry, 60 GB at exit (the three
scratch configure trees were removed after their evidence was captured; one build directory, `build/`).

Gate 9 = `tests/fork-sources-gate.sh` · Gate 6 = `tests/no-upstream-regression-gate.sh` ·
`run-all-gates.sh` exits **3** = `PASS-WITH-SKIPS` (gate 2 coverage needs `--with-coverage`),
which is the expected result and is **not** a pass. The runner is at **ten** gates after 3B's merge 3.

## The six merges, in the order the parent specified

| # | Lane | Merge commit | Conflicts | Ledger entries | fork / all / tools | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|----------------|--------------------|--------|--------|--------------------|-------|
| 1 | `post-alpha/router-live` | `35d0b44de` | **0** | 434 | 152 / 1,173 / 17 | 0 | 0 | 3 (10 gates) | 62/62 |
| 2 | `post-alpha/session-scheduler` | `443532ec6` | 4 (2 product code) | 434 | 154 / 1,177 / 17 | 0 | 0 | 3 (10) | 62/62 |
| 3 | `post-alpha/mpe` | `67707a205` | 11 (2 product code) | 437 | 156 / 1,182 / 17 | 0 | 0 | 3 (10) | 65/65 |
| 4 | `post-alpha/warp` | `3efc777bc` | 3 | 437 | 157 / 1,186 / 17 | 0 | 0 | 3 (10) | 68/68 |
| 5 | `post-alpha/telemetry` | `1066a056f` | 4 | 437 | 163 / 1,193 / 17 | 0 | 0 | 3 (10) | 69/69 |
| 6 | `post-alpha/auto-mastering` | `b0d6a2e0b` | 5 (2 product code) | 437 | 167 / 1,199 / 18 | 0 | 0 | 3 (10) | 70/70 |

`git log --oneline --merges 558fbd375..HEAD` returns exactly those six commits and nothing else.
All six named branches are ancestors of HEAD (`git merge-base --is-ancestor`, YES ×6). **No branch
outside the list was merged.** The queued 3D branches (`instrument-hosting-impl`,
`instrument-view-safety`, `release-prep`) and every branch the other session owns (`pr7459-*`,
`lv2-catalogue`, `cmd-*`, `agent-*`, `headless-load`, `control-hardening`, `docs/fork-readme`,
`tools/local-ci`, `test/real-client-e2e`, `feat/*`, `fix/*`) were never touched. No concurrent
off-list merge was observed arriving at integration during this train.

Suites: 62 (entry) → 62 → 62 → 65 → 68 → 69 → **70**. The three jumps are the lanes' own new
suites: `MpeExpressionTest`/`MpeInputPathTest`/`MpeNoteStorageTest` (65),
`ClipWarpPersistenceTest`/`WarpMarkersTest`/`SampleClipWarpTest` (68), `TelemetryTest` (69),
`MasteringTest` (70). Merge 2's new tests are registered but **not built**, because they live
inside `IF(LMMS_HAVE_SESSION_VIEW)` and that option is OFF — see merge 2.

## Method

Every conflict in this train was in an append-only registry, a CMake/CI list, a gate script the
lane had cherry-picked an older copy of, or one of a handful of product-code functions. **No
conflict was resolved by unioning lines across markers.**

3B's tools were carried forward and committed under `tests/integration-logs-3c/tools/`
(`resolve_pair.py`, `regen.py`, `precommit_check.py`, `verify_union.py`, `merge_ledger.py`), and
each merge that touched product code got its own documented resolver
(`resolve_merge2_product.py`, `resolve_merge3_product.py`, `resolve_merge6_product.py`). Every
resolution was asserted programmatically before staging: no markers, no duplicate path, **every
entry from both sides present**, no blank reason, no `;;`, and — for product code — the exact
counts of each side's constructs plus brace/paren balance.

Two rules of this programme did real work in this train and are worth naming:

* **Re-derive every manifest from its own documented command after every merge.** The verdict was
  `ALL-REPRODUCE` ×3 at **all six** merges, and the reproduction check is what found the wrong home
  of five entries (merge 4's three test sources, merge 6's two test sources and one tool).
* **A pre-commit gate script reads HEAD, so it is not a pre-commit check.**
  `no-upstream-regression-gate.sh` computes `git diff --name-only "$BASE"..HEAD`, so before a merge
  commit it reports the pre-merge HEAD and is green no matter what the merge adds. Gate 6's rule was
  therefore replayed **against the index** before every commit (`precommit_check.py INDEX`) and the
  real script was run again afterwards. `PRECOMMIT_EXIT=0`, `GATE6-REPLICA violations: NONE`,
  `cross-manifest entries: NONE` at every merge.

## Manifest reproducibility

| after merge | `fork-sources.txt` | `all-sources.txt` | `tools-sources.txt` | verdict |
|---|---|---|---|---|
| 1 router-live | 152 | 1,173 | 17 | `REPRODUCES` ×3 |
| 2 session-scheduler | 154 | 1,177 | 17 | `REPRODUCES` ×3 |
| 3 mpe | 156 | 1,182 | 17 | `REPRODUCES` ×3 |
| 4 warp | 157 | 1,186 | 17 | `REPRODUCES` ×3 |
| 5 telemetry | 163 | 1,193 | 17 | `REPRODUCES` ×3 |
| 6 auto-mastering | 167 | 1,199 | **18** | `REPRODUCES` ×3 |

Four of the six needed work beyond an entry union, and the reproduction check found each:

* **`all-sources.txt` was missing the fork-NEW C/C++ sources of every product lane** (merge 2: 2;
  merge 3: 2; merge 4: 4; merge 5: 6; merge 6: 6). The file is `git ls-files` over the C/C++
  extension set, so a fork-NEW file is on its list by construction; each was added by re-deriving
  the file from its own command, never by hand.
* **`fork-sources.txt` cannot admit a `tests/` source that is not on its `awk` allow-list, and its
  own header states the rule**: *"A new fork test source belongs in tests/all-sources.txt by
  default; add it to the awk pattern above only if it must carry the fork ratchets too."* Two lanes
  registered new test sources here (warp: `ClipWarpPersistenceTest`, `WarpMarkersTest`,
  `SampleClipWarpTest`; auto-mastering: `MasteringTest`, `MasteringTestSupport.h`). All five were
  **moved to their home**, `tests/all-sources.txt`, whose own command produces them; none carries a
  fork ratchet baseline, so nothing was narrowed. The move is written into `fork-sources.txt`'s header.
* **`tools/auto-mastering-demo.py` was registered in `fork-sources.txt`**, where its command cannot
  produce it (the file's own pathspec admits `tools/local-ci.sh` and `tools/ncpu-shim.c` by name).
  Moved to `tests/tools-sources.txt` (18 entries) — the brief's explicit fix, and the same move the
  stem-export tool needed at 3A. `fork-sources.txt` now reproduces at 167 entries.
* Headers were kept from integration in every case (the lanes' copies are older).

## The product-code hunks an automatic merge resolved, and how each was checked

This is the class 3B's `EffectChain` double-lock came from, so every merge's diff was read for
product code, not just for conflicts. The checks below are the ones that decided the merge.

### Merge 2 — `session-scheduler`

* **`src/core/Song.cpp`, the per-tick track loop (SPEC A1 takeover) — auto-merged.** The lane's
  `continue` sits directly before `track->play(...)` and **nothing else is in that loop body**, so
  it skips exactly that call; `sessionTrackIndex` is declared immediately before the loop and is
  only consumed when `m_playMode == PlayMode::Song`. Neither side's other work is in the loop.
* **`src/core/CMakeLists.txt` / `tests/CMakeLists.txt`** — `SessionScheduler.cpp` and both test
  sources are added **inside `IF(LMMS_HAVE_SESSION_VIEW)`**.
* The two conflicts in this file were unions (`include/Song.h`: our `#else` arm + the lane's member
  inside the `#ifdef` arm; `Song.cpp`: the automation transport token at the head of the period
  *and* the session drain, both before the `if (!m_playing) { return; }` gate the lane's own comment
  requires).

### Merge 3 — `mpe`

* **`src/core/NotePlayHandle.cpp` `updateFrequency()` — auto-merged, audio path.**
  `if (mpePitchCents() != 0) m_frequency *= mpePitchRatio(...)` — guarded, so a note with no
  expression takes the pre-feature path; the lane's whole bit-identity claim rests on that guard.
  The hunk's single deletion is a stray blank line.
* **`src/core/Note.cpp` / `include/Note.h`** — the two conflicts were unions; the copy constructor's
  init list, `saveSettings()` and `loadSettings()` each take both sides' insertions. Declaration
  order is `m_slide, m_probability, m_velocityJitter, m_mpeCaptured, m_mpePitchCents, m_mpePressure,
  m_mpeTimbre`, which matches the init list exactly (no `-Wreorder` under `-Werror`).
* **`src/tracks/InstrumentTrack.cpp` — auto-merged.** `trackMpeInputEvent()` is gated on
  `MpeExpression::isEnabled()` and returns false for every event when off, so the call site in
  `processInEvent` cannot change existing behaviour; `loadMpeExpressionOntoChannelNotes()` null-checks
  the handle.
* **`src/gui/editors/PianoRoll.cpp` — auto-merged.** `finishRecordNote()` copies the expression only
  when `hasMpeExpression()`.

### Merge 4 — `warp` (resampling: the highest-risk branch)

* **`src/core/SamplePlayHandle.cpp` — auto-merged, and the resampler argument is the only
  behavioural change.** Old call: `play(buffer, &m_state, frames)`; the declaration is
  `play(SampleFrame*, PlaybackState*, size_t, Loop loopMode = Loop::Off, double ratio = 1.0)`
  (`include/Sample.h:80-81`), so the omitted arguments were exactly `Loop::Off, 1.0`. The new call
  passes `Sample::Loop::Off, warpRatio()`, and `warpRatio()` returns exactly `1.0f` when the marker
  map is empty or no natural rate is known — i.e. for every project with no warp. The documented
  reciprocal of the rate is pinned by the lane's `SampleClipWarpTest::theResamplerRatioConventionIsPinned`.
* **`totalFrames()` returns `m_timelineFrames` only when `!m_rendersLinearly`**; otherwise the
  historical expression is returned unchanged. `rendersLinearly()` is the AND of "empty map" and
  "follow the project tempo".
* **`src/core/SampleClip.cpp` — auto-merged and additive:** `clipFramesPerTick()` returns
  `Engine::framesPerTick()` for `FollowProject`; `windowTicksFor()` returns
  `length() / framesPerTick()` when the map is empty; the mapping takes the linear branch when
  `m_warp` is empty; `saveSettings()` writes the `<warp>` child **only** when the map is non-empty
  or the mode is not `FollowProject`, so an unwarped project serialises exactly as before.
* `include/WarpMarkers.h` is a fixed-capacity value type (`std::array<WarpMarker, 128>`) with no
  allocation — checked, because the handle snapshots it on the audio thread.

### Merge 5 — `telemetry`

* **`src/gui/MainWindow.cpp` — auto-merged.** The Help-menu entry and its lambda are inside
  `#ifdef ZENE_TELEMETRY_ENABLED`, and the header include is conditional too (the lane's comment:
  an unconditional include makes AUTOMOC emit moc for a `Q_OBJECT` whose `.cpp` the kill switch
  removed, and the link then fails).
* `src/gui/CMakeLists.txt` adds the dialog source only under `IF(ZENE_TELEMETRY_ENABLED)`;
  `src/lmmsconfig.h.in` adds one `#cmakedefine`; the top-level option is
  `option(ZENE_TELEMETRY "..." ON)`.

### Merge 6 — `auto-mastering`

* **`src/core/main.cpp`, the render block — a structural conflict, and the most dangerous
  resolution in the train.** Ours had replaced the base block with a stem-aware one; theirs wrapped
  the **base** block in `if (mastering) { ... } else { <base> }`. Git left ours' block in the
  conflict **and** theirs' else-wrapped base block in the "shared" text, so removing the markers
  naively would have left two render paths — the second using the pre-stems `if (!renderTracks)`
  arithmetic and creating a second `RenderManager`. Resolved as `if (mastering) { theirs } else
  { ours }` with the duplicate base block deleted. Asserted afterwards: exactly one
  `new RenderManager(`, one `r->renderProject()`, ours' `!renderTracks && !renderStems` intact, two
  `if (renderStems)` readers.
* **Two pairs of same-fact locals the auto-merge created, both set**, because the `-o` parsing block
  merged cleanly: `outputGiven`/`outputSpecified` and `headlessExitCode`/`scriptExitCode`.
  `scriptExitCode` ended up **declared and never used** — a `-Werror` build breaker — because theirs
  renamed it to `headlessExitCode` to carry both the Lua action's and the mastering job's exit code.
  Resolved to one name per fact (`outputGiven`, `headlessExitCode`), readers updated, 0 code uses of
  `outputSpecified`, and the unification is a comment at the declaration.
* `src/core/ProjectRenderer.cpp` — union: `DeterministicRenderScope` **and**
  `s_renderCount.fetch_add(1)`. The counter is instrumentation only (`include/ProjectRenderer.h`
  says so) and does not touch the render path.
* `tests/CMakeLists.txt` — the lane's `src/core/LufsMeterTest.cpp` line is **already in the list**
  (an earlier lane registered it), so the entry union adds only `MasteringTest.cpp`; a line union
  would have registered the same test twice. The second hunk is 3B's merge-1 trap exactly: both
  sides end on `set_tests_properties(<T> PROPERTIES` and **share** the `ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen")` line; the union supplies one ENVIRONMENT line per block and consumes
  the shared one once.

## The telemetry watch items

**(a) Does the kill switch change what `lmms --version`'s build-options dump reports? No, and it
cannot.** Three independent pieces of evidence:

1. **Mechanism.** `LMMS_BUILD_OPTIONS` is generated by enumerating CMake variables matching
   `^WANT|LMMS_(HAVE|DEBUG)` (`src/CMakeLists.txt:7-11`). The lane sets only
   `ZENE_TELEMETRY_ENABLED`, which matches neither alternative.
2. **Executable probe** (`tests/integration-logs-3c/merge5/build-options-filter-probe.cmake`, run
   with `cmake -P`): with `WANT_*`, `LMMS_HAVE_*` and `ZENE_TELEMETRY_ENABLED` all declared, the
   filter selects `LMMS_HAVE_ALSA;WANT_SESSION_VIEW;WANT_TELEMETRY` and **not**
   `ZENE_TELEMETRY_ENABLED`.
3. **Measurement.** Two clean configures of *this merged tree* with the same flags,
   `-DZENE_TELEMETRY=ON` and `=OFF` (both `EXIT=0`, logs and both generated `LMMS_BUILD_OPTIONS`
   strings committed): **byte-identical — 86 tokens each, same order, no TELEMETRY token in either.**
   `lmms --version` before and after the merge contains no telemetry token either.

> Correction for the record: my first comparison used the *pre-merge* `build/lmmsversion.h` against
> the post-merge OFF configure and showed a 32-byte difference. That was two different trees, not the
> kill switch; it was redone apples-to-apples as (3). The first OFF configure also "failed" — my
> attempt to seed a scratch build dir from a copied `CMakeCache.txt`, which CMake refuses. The clean
> OFF configure passes.

**(b) Does anything claim a capability the release does not have? No.**

* `tests/advertised-features.tsv` is **untouched** by this merge (no diff) and has no telemetry row.
* The runtime default is off and granular: `TelemetryConsent`'s `enabled = false` and
  `maySend()` requires `enabled && anyGroup()` (`include/Telemetry.h:77-88`).
* **"Opt-in, with no server to send to" holds.** The endpoint is read from configuration, ships
  **empty**, and the lane's own comment says *"there is no ingest service yet, so an enabled build
  still refuses to open a socket"* (`src/core/TelemetryNetworkTransport.cpp:39-45`). No host is
  hard-coded. The configure option defaults **ON** for packagers, which is a packager opt-*out*
  (see "Decisions needed").

## Duplicated content: the patch-id proof

Every non-merge commit reachable from each of the six branches was patch-id'd
(`git show <c> | git patch-id --stable`) and compared against the 304 patch-ids of the pre-train
integration (`4e677cb6c6ab..HEAD`) and against each other:

```
router-live        1 commit   0 duplicates
session-scheduler  4 commits  0 duplicates
mpe                8 commits  6 duplicates   <-- the cherry-picked gate-debt commits
warp               3 commits  0 duplicates
telemetry          3 commits  0 duplicates
auto-mastering     2 commits  0 duplicates
0 duplicates within any one branch; 0 duplicates among the six themselves
```

`mpe`'s six are exact copies of commits already reachable from integration:

```
ec34cafb2 == deb349727   coverage entry floor / never bank 0 lines as 100%
40fbd2401 == ddc2f11cc   red/green fixture proof for the three debt fixes
9b59d7eb2 == 84388107e   the three verification-debt fixes (QA-GATES.md, STATUS.md, report)
76d378749 == e4fc8cd7e, 879e251ef   Gate 9 - every tracked source is in a scope manifest
5d6a720de == 99724e970, d96db9e4c   run Gate 9 from run-all-gates.sh
a2361e1f8 == e77fa7b4b, 9763a185c   a skipped gate is not a pass
```

**The dedupe, measured.** The content of three of those commits is byte-identical at both tips:
`git diff HEAD post-alpha/mpe -- tests/test-verification-debt.sh docs/STATUS.md
tests/coverage-entry-floor-exempt.txt` is **empty**, so the merge re-applied nothing and the content
is present **exactly once**. For the three gate scripts and `tests/QA-GATES.md` the lane's copy is an
older revision of a file integration has evolved past, so ours was kept and **the keep was verified
line by line, not assumed** — every theirs-only line is an older form of something ours has:

* `tests/fork-sources-gate.sh` — theirs scans four directories and calls all-sources "inherited
  upstream"; ours scans `tools/` and `modules/` too and reports the tooling count.
* `tests/run-all-gates.sh` — theirs builds into `/tmp/gate1-build.log` (a disk reclaim has already
  destroyed one verification's evidence in this program) and has no Gate 10 and no dual-scope
  (fork+tools) checks.
* `tests/coverage-gate.sh` — theirs is the pre-LF+sha256 baseline, whose
  `round(baseline_pct * instrumented_lines_now)` arithmetic ours' header documents as the
  fabricated-regression bug that was replaced.
* `tests/QA-GATES.md` — every theirs-only paragraph exists in ours in a newer form (checked by
  marker counts: "import descriptor" 1/1, "static-gates" 9/3, "Stale manifest entries" 2/1).
* `.github/workflows/quality-gates.yml` — **theirs-only lines = 0**: a strict subset of ours.

Separately, **the ledger carried genuinely duplicated content created by an earlier train**: the
identical `#605 PDC: alignment points, …` clause appeared twice in `include/Mixer.h`'s reason,
stacked by a previous clause union. Deduped in merge 2's commit (an exact duplicate clause carries
no information).

## THE RENDER: the sha256 matches the expected value

```
expected from train 3B               943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 1 (b0d6a2e0b tree)   943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
this train, run 2                    943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526
```

`bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o …` →
`RENDER_EXIT=0` on both runs, 16-bit, 2 channels, 44,100 Hz, **544,256 frames**. Run-to-run
identical, so the same-build floor is 0. **No chunk comparison was needed** — but it was taken
anyway, because it is the cheapest way to say "the audio did not move through six merges":

* **`data` chunk sha256 = `b37cefc5a97e2d46…`** — identical to the value 3A, 3B run 1, 3B run 2 and a
  pre-train render all recorded. 2,177,024 bytes, unchanged.
* The `LIST`/`INFO` chunk still holds `ISFT = "Zene Studio (libsndfile-1.2.2)"`, which is 3B's
  documented `AudioFileWave.cpp` rename; file total 2,177,120 bytes (3A's was 2,177,112 — the same
  8-byte tag delta). Merge 1 of this train (router-live) is docs-only and does not touch it.
* The repo's own comparator against 3A's committed artifact: **0 differing frames (0.000000 %),
  max |Δ| 0 LSB (−inf dBFS), 0 of 2,126 periods dirty, header match=True**
  (`tests/integration-logs-3c/final/compare-vs-3A.log`).

**Verdict: `ALL-REPRODUCE` on the render too. `943e3238…` is confirmed as this line's value, and the
audio is bit-identical to 3A's.** (One probe artefact for the record: my own chunk-parser printed
the `fmt ` fields in the wrong order — it labelled the block-align/bits fields as channels/bits. The
authoritative numbers are the comparator's and the recipe's: 2 ch, 16 bit, 44,100 Hz.)

## Final verification of the finished tree (`b0d6a2e0b`)

```
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 3      -> EXIT=0
    provision EXIT=0 · configure EXIT=0 · build EXIT=0 · ctest EXIT=0
    ctest 100% tests passed, 0 tests failed out of 70
bash tests/fork-sources-gate.sh                               -> EXIT=0
    167 fork-NEW, 1,033 inherited, 18 tooling, 0 stale
bash tests/no-upstream-regression-gate.sh                     -> EXIT=0
    426 changed path(s) declared; the ledger holds 437 entries
bash tests/run-all-gates.sh                                   -> EXIT=3
    ten gates: 1 ctest PASS · 2 coverage SKIP · 3 no-tautology PASS · 4 complexity PASS
    · 5 mutation PASS · 6 upstream-regression PASS · 7 file-length PASS · 8 duplication PASS
    · 9 fork-sources PASS · 10 unregistered-tests PASS
```

Gate 6's counters along the way (changed paths declared / ledger entries): 423/434 · 423/434 ·
426/437 · 426/437 · 426/437 · 426/437. Gate 9's: 152/1022/17 → 154/1024/17 → 156/1027/17 →
157/1030/17 → 163/1031/17 → **167/1033/18**, 0 stale entries at every step.

Product-code builds were run **before** the gates at every merge, with
`JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 3` — `configure`/`build`/`ctest` each
`EXIT=0`, ctest run from `build/tests`. **Deviation, stated:** the brief's command says `--jobs 4`;
I built at `--jobs 3`, as 3B did, because a sibling lane was compiling and this box has been
OOM-killed at `-j4` more than once. It is inside the brief's `-j2`…`-j4` range.

## Test expectations changed during this train

**None. No test assertion, tolerance, baseline or expectation was changed anywhere in this train.**
That is a measurement, not an omission: the file-level changes I made to the test tree were
*registrations* only — `tests/CMakeLists.txt` (three merges: entry unions plus one `MasteringTest`
block), the three manifests, the ledger and `tests/fork-sources.txt`'s header. Two candidate
cross-lane defects that would have needed one did **not** materialise here:

* `DataFileSaveIntegrityTest` (3B's fix-up) asserts the new project root; nothing in this train
  touches the file format's write side.
* The lane whose tests could have asserted stale behaviour — `session-scheduler` — is compiled out
  by default, so its tests never ran; see the finding below rather than a silent edit.

`tests/file-length-baseline.tsv` was **not touched**: no merge needed it. **No `--reanchor-file` was
used, no baseline was re-anchored to green a gate, and no code was trimmed to satisfy a metric.**

## Findings

1. **`post-alpha/router-live` is ONE commit ahead of integration, not the six the brief names.**
   `git merge-base --is-ancestor` shows each of `fcec2ebe3`, `3875183fa`, `4a3a38311`, `e5d6486ee`,
   `d09ebad52` already reachable from `558fbd375` — they arrived with train 3B's `post-alpha/racks`
   merge. Merge 1 therefore carries `docs/ROUTING-GRAPH-LIVE.md` (+17/−1) and nothing else, and it
   had **zero** conflicts. Nothing was rebased; the merge carries what was actually missing.
2. **`post-alpha/warp`'s own tree is RED on Gate 9** — measured in its own worktree at its tip:
   `tests/src/core/LufsMeterTest.cpp`, `tests/src/core/MidiLearnTest.cpp`,
   `tests/src/core/SessionModelTest.cpp` are in no scope list, `EXIT=1`. Those three are registered
   at integration, so the merge is clean; this is information about the lane, like 3B's two.
   (`mpe`, `router-live` and `auto-mastering` are Gate 9-green on their own trees; `session-scheduler`
   and `telemetry` predate the gate script and report 127, as the delegation rules expect.)
3. **`mpe`'s six cherry-picked gate commits are duplicates of commits already in integration** —
   proved by patch-id, deduped as described above, three of their files byte-identical at both tips.
4. **The auto-merge created two pairs of same-fact locals in `main.cpp`** (`outputGiven`/
   `outputSpecified`, `headlessExitCode`/`scriptExitCode`), one of which (`scriptExitCode`) was a
   `-Werror` build breaker. Resolved to one name per fact in merge 6's commit.
5. **The `main.cpp` render block was a structural conflict whose "shared" text was a duplicate of
   the base block.** A marker-removal resolution would have left two render paths, the second with
   the pre-stems arithmetic. Resolved as `if (mastering) { theirs } else { ours }`.
6. **`auto-mastering`'s ledger re-declared six `tools/mmpz-git/*` entries that 3A/3B deliberately
   deleted.** They are entries whose own text says "NOT upstream-inherited", i.e. false statements
   in a divergence ledger, and their home is `tests/tools-sources.txt` (all six are there). A naive
   theirs-only append would have resurrected them. **Excluded** — the same deletion-vs-union
   decision 3A recorded, hit from the other direction.
7. **`session-scheduler` ships source and tests only, and the merge did not change that** — the
   brief's stop-condition was checked and does not fire: the lane does **not** change
   `OPTION(WANT_SESSION_VIEW … OFF)` (the top-level `CMakeLists.txt` is untouched by it) and does
   **not** touch `tests/advertised-features.tsv`. Its sources and tests sit inside
   `IF(LMMS_HAVE_SESSION_VIEW)`, so with the flag OFF they are registered but never compiled and
   never run — ctest stayed at 62/62 through that merge. **What lands is source and tests, not a
   shipped capability**, and the suite count is the evidence.
8. **The telemetry kill switch cannot change the `--version` dump** (proved three ways above), and
   nothing in the telemetry work claims a capability the release does not have (empty endpoint, no
   hard-coded host, granular default-off consent, `advertised-features.tsv` untouched).
9. **A pre-commit gate script reads HEAD** — Gate 6 was replayed against the index before every
   commit; that is how merge 4's and merge 6's registration gaps were caught before they landed.
10. **`post-alpha/auto-mastering` is Gate 9-green on its own tree, and its `tools/` entry was
    registered in the wrong manifest** (`fork-sources.txt`). Module-level discipline was otherwise
    good in this train: no lane left a product source unregistered.

## Refusals

* **No `--reanchor` and no re-anchor of any kind.** No baseline moved; the whole-tree
  (`--scope all`) file-length/complexity baselines remain stale and untouched, as 3A and 3B left
  them — that is an owner decision, and no gate this task measures reads them.
* **No gate script was edited or weakened.** Where a lane's older gate-script copy conflicted, ours
  was kept and verified line by line; where an entry was in the wrong manifest, the entry moved and
  the manifest was re-derived — the gate stayed as it is.
* **The six `tools/mmpz-git/*` ledger entries were not resurrected** (finding 6), and
  `tests/upstream-modifications.txt` was **not** allowed to grow through an unverified union.
* **`tests/CMakeLists.txt` was not resolved as a line union** — a line union would have registered
  `LufsMeterTest` twice and built the same test twice.
* **No test expectation was adjusted** to make a merge green, and no `-Werror` failure was silenced.
* **Nothing off-list was merged**, nothing was pushed, tagged or force-updated, and no branch was
  rebased, amended or reset. No branch's own tree was modified except by the merge itself.
* **The whole-tree scope was not made green** and Gate 2 (coverage) was not run.

## What is NOT proven

* **Gate 2 (coverage) was not run at any point in this train.** Every `run-all-gates.sh` invocation
  was the default; coverage needs `--with-coverage` plus an instrumented build. The `3` is
  `PASS-WITH-SKIPS` and is not a pass.
* **CI was not run** and no CI configuration was changed. Every exit code here is local.
* **The telemetry kill switch was configure-verified, not compile-verified.** Both configures pass
  and the dump comparison is byte-exact, but I did not compile a `-DZENE_TELEMETRY=OFF` tree (the
  lane's second commit claims the OFF build; I did not re-run it). Its `#ifdef` discipline in
  `MainWindow.cpp` was read, not built.
* **I did not audit the product code any lane brought in.** I read every product-code hunk the
  merges resolved (automatic or conflicted) and asserted the invariants above, but the lanes' own
  designs are theirs and are covered only by their tests and by the render comparison (six merges,
  0 LSB, identical `data` chunk).
* **The lanes' own reports were not re-verified** beyond the specific claims this report names.
* The `PdcMixerTest` teardown abort 3A and 2A saw did not reproduce: **13 suite runs** (the entry
  baseline, six `local-ci` runs — one per merge — and six `run-all-gates.sh` gate-1 ctests), every
  one `100% tests passed`, 0 aborts. Absence of evidence, not a fix.

## Concurrent activity — noted, not touched

Another session is live in this clone and was active throughout: at entry a `ctest` was running in
`zene-pa-onto`, and `zene-pa-headless` had a long-lived `lmms` process. The entry check passed
before merge 1 (`git status --porcelain` empty, no file written in the previous 5 minutes) and was
re-checked after every gate run. The mutation gate (gate 5) mutates `src/core/RoutingGraph.cpp` and
restores it; `git status` was re-checked after every `run-all-gates.sh` run and the file was clean
at every merge commit. No concurrent branch was read-write touched, and none was merged.

## Decisions needed from the owner

1. **The render constant is settled by this train.** `943e3238…` matches, run-to-run, and the
   `data`-chunk hash is still `b37cefc5…`; 3B's open question ("replace the constant or revert the
   tag?") can be closed by updating the documented constant to `943e3238…` — the audio is provably
   identical. I changed no documented expected value.
2. **`ZENE_TELEMETRY` defaults ON (packager opt-out), while the user-facing consent defaults OFF.**
   The release documents describe telemetry as opt-in with no server; that holds. But a packager who
   does nothing ships the client (and Qt Network) even though it can never send (the endpoint ships
   empty). Is "default ON, packagers must remember `-DZENE_TELEMETRY=OFF`" intended, or should the
   option default OFF and the release jobs pass ON?
3. **A new fork test source has a documented default home (`tests/all-sources.txt`) that two lanes in
   this train did not follow** (warp's three, auto-mastering's two) — and one lane registered a
   `tools/` file in `fork-sources.txt`. A merge train can catch this, but it is the wrong place to
   discover it. Gate 9 catches unregistered sources; nothing catches a source registered in the
   wrong manifest until the file stops reproducing. Options: a gate that runs each manifest's own
   command, or the lane-discipline reminder repeated in the next briefs.
4. **`post-alpha/warp`'s own tree is Gate 9-red** (finding 2). The lane's report should be read with
   that in mind; the three unregistered test sources are pre-existing files from other lanes, not
   warp's own work.
5. **Unchanged from 3A/3B:** where fork evidence lives (I used `tests/`, as they did); Gate 6's
   `tools/` category vs `all-sources.txt`; and the stale whole-tree baselines.

## Evidence index (`tests/integration-logs-3c/`)

```
baseline/            entry-tip ctest, Gate 9, Gate 6 (before merge 1)
merge1/ … merge6/    merge.log, local-ci.log + local-ci.exit (configure/build/ctest, unpiped),
                     gate9.log, gate6.log, run-all-gates.log, regen-index.log, precommit-index.log,
                     resolve-*.log, and per-merge extras below
merge2/…merge6/      regen-write-allsources.log, resolve-*.log
merge5/              configure-telemetry-{ON,OFF}.log, build-options-{ON,OFF}.txt,
                     build-options-filter-probe.cmake + .log
final/               render-final-1.wav, render-final-2.wav (both 943e3238…), render-{1,2}.log,
                     render-sha256.txt, chunk-parse.log, compare-vs-3A.log
sides/merge2…merge6/ :1: :2: :3: copies of every conflicted file, as read from the index
tools/               3B's resolve_pair.py / regen.py / precommit_check.py / verify_union.py /
                     merge_ledger.py, plus resolve_merge{2,3,6}_product.py written for this train
pre-telemetry-version-dump.txt, post-telemetry-version-dump.txt (merge5/),
pid-<lane>.txt       the patch-id inputs for all six branches
```

The git-history commits of the train are `35d0b44de`, `443532ec6`, `67707a205`, `3efc777bc`,
`1066a056f`, `b0d6a2e0b` — **six merges, no fix-ups** — plus four evidence commits
(`bf1511983`, `a843a3c74`, `84bcfb69f`, `a6dc675d0`) and this report. The tree is clean at
`b0d6a2e0b` apart from this report before it is committed.
