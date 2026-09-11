# Integration merges into `post-alpha/integration`

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Nothing was pushed; no PR, issue or remote was touched; no branch was rebased, amended,
reset or deleted. One commit per lane, plus two labelled fix-ups.

Entry tip: `8c0d7c4e4` (eight lanes already merged, tree green).
Lanes merged here, in the order the parent specified:

| # | Lane | Merge commit | Conflicts | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|--------|--------|--------------------|-------|
| 1 | `post-alpha/crash-report` | `7d2132336` (+ fix-up `349aa0732`) | 2 files | 0 | 0 | 3 | 28/28 |
| 2 | `post-alpha/plugin-scan` | `747e818e9` | 1 file | 0 | 0 | 3 | 29/29 |
| 3 | `post-alpha/mmpz-git-depth` | `336a0c6cf` | 1 file | 0 | 0 | 3 | 29/29 |
| 4 | `post-alpha/oop-hosting` | `8d9abb1fe` | 1 file | 0 | 0 | 3 | 30/30 |
| 5 | `post-alpha/lua-api` | `de10bf00f` | 2 files | 0 | 0 | 3 | 31/31 |
| 6 | `post-alpha/wave-r-rename` | `abb3a1a7f` (+ fix-up `bbe3c66ea`) | 1 file | 0 | 0 | 3 | 31/31 |

Exit codes are measured unpiped (`cmd > log 2>&1; echo EXIT=$?`). Gate 9 is
`tests/fork-sources-gate.sh`, Gate 6 is `tests/no-upstream-regression-gate.sh`,
`run-all-gates.sh` is expected to exit **3** (`PASS-WITH-SKIPS`: gate 2 coverage needs
`--with-coverage`). Exit 1 from either of the first two would have meant "not done".

## Method: how a ledger conflict was resolved

Every conflict in this integration was in an append-only registry
(`tests/fork-sources.txt`, `tests/upstream-modifications.txt`, `tests/CMakeLists.txt`),
never in product code — with one exception, recorded under lane 6.

Resolution was never a line-level union across the conflict markers. For each side the
entries were parsed out (`git show :2:<file>` = integration, `:3:<file>` = lane), the
entry **set** was unioned by path, and the result was re-emitted in the file's own
`LC_ALL=C` sort order with the comments kept in place. Reasons for the same path on both
sides were merged by keeping the shared prefix once and appending only the clause each
side added (or the superset, when one side's reason extends the other's).

Every resolution was then verified programmatically, not by eye:

* no conflict markers (`grep -n '^<<<<<<<\|^=======\|^>>>>>>>'` → empty);
* no duplicate entry (one line per path);
* **every entry from BOTH sides present** in the result;
* every reason non-empty (Gate 6 refuses a blank one with exit 2).

That verification earned its place: the first version of the resolver had a
`list.extend` bug that re-emitted a mid-file comment block one character per line. The
verifier reported `578 entries, 34 duplicates` and `blank reason for -`, the resolution
was discarded, the bug fixed, and the file re-resolved from the saved conflict sides. No
broken ledger was ever committed. (The comment block is preserved in place — directly
above the `tools/` group it describes.)

## Lane 1 — `post-alpha/crash-report`

Offline crash reporter: new `include/CrashReporter.h`, `src/core/CrashReporter.cpp`,
`tests/src/core/CrashReporterTest.cpp`; one inherited hook in `src/core/main.cpp`.

**Conflicted:** `tests/fork-sources.txt`, `tests/CMakeLists.txt`.

* `fork-sources.txt`: integration 113 entries ∪ lane 102 → **115**, 0 duplicates, 0
  markers, verify OK. The lane's two additions (`include/CrashReporter.h`,
  `src/core/CrashReporter.cpp`) are the only difference; integration's side was the
  re-sorted file.
* `tests/CMakeLists.txt`: one hunk, the `LMMS_TESTS` list — union of integration's
  `LufsMeterTest.cpp` and the lane's `CrashReporterTest.cpp`.

**Checklist.** (a) both new sources registered in `tests/fork-sources.txt` (via the
union); `CrashReporterTest.cpp` is whole-tree scope in `tests/all-sources.txt`, which the
lane had already added. (b) `src/core/main.cpp` is upstream-inherited and was **not**
declared by the lane — added to `tests/upstream-modifications.txt` with the reason read
off `git diff <merge-base>..post-alpha/crash-report -- src/core/main.cpp`:
`install()/beginSession()/endSession()` around the process lifetime, `setProjectPath()` on
every load, the one-off pending-report offer (dialog in GUI mode, stderr when core-only).

**Gates after the merge: 0 / 0 / 3**, ctest 28/28.

**Fix-up `349aa0732`.** Gate 7 (per-file length) went red: registering
`src/core/CrashReporter.cpp` exposed a new **526-line** file to the ≤500-line ratchet.
Trimming code to satisfy a metric is explicitly forbidden (`docs/CONVENTIONS.md` rule 4:
"code is never trimmed to satisfy a metric"), so the documented mechanism was used:

```
bash tests/file-length-gate.sh --reanchor "<reason>"     # exit 0
```

That added exactly one baseline entry (`src/core/CrashReporter.cpp	526`); the eight
pre-existing entries are untouched and any *other* new file over 500 still fails. The
measurement and the reason are recorded in `tests/QA-GATES.md` (Gate 7), as the
2026-09-11 re-anchor was. Gates re-run: **0 / 0 / 3**, ctest 28/28.

## Lane 2 — `post-alpha/plugin-scan`

Scan cache + quarantine: new `include/PluginScanCache.h`, `src/core/PluginScanCache.cpp`,
new test, six upstream hooks (`PluginFactory`, `PluginBrowser`, `MainWindow`, `embed.h`).

**Conflicted:** `tests/upstream-modifications.txt` — 40 ∪ 37 → **45** entries, 0
duplicates, 0 markers, verify OK. No path needed adding, so the work was the two shared
paths where both sides rewrote a reason:
* `src/gui/MainWindow.cpp` — the lane's reason extends integration's compile-only reason
  by a `; plugin-scan: …` clause → the superset was kept.
* `include/MainWindow.h` — integration's MIDI-learn reason and the lane's plugin-scan
  reason are independent → both clauses kept, integration's first.
`tests/CMakeLists.txt` auto-merged (the `LMMS_TESTS` entry plus its
`ENABLE_EXPORTS`/offscreen block landed cleanly).

**Checklist.** (a) both new sources registered; `PluginScanCacheTest.cpp` in
`tests/all-sources.txt`. (b) all six upstream hooks declared, reasons verified against
`git diff <merge-base>..post-alpha/plugin-scan -- <file>`.

**Gates: 0 / 0 / 3**, ctest 29/29.

## Lane 3 — `post-alpha/mmpz-git-depth`

Deepened `tools/mmpz-git/` (Python) + `docs/MMPZ-GIT-DEPTH.md`. No product-source change.

**Conflicted:** `tests/upstream-modifications.txt` — 45 ∪ 37 → **51** entries, 0
duplicates, 0 markers, verify OK. The lane contributes six `tools/` entries and an
explanatory comment block; the block is re-emitted in place above the `tools/` group.
No existing reason was altered.

**Checklist.** (a) nothing to register: the three new `tools/` files are outside the scope
Gate 9 scans (`src/`, `include/`, `plugins/`, `tests/`) and the lane documents that in the
ledger block rather than widening the C/C++ ratchets. (b) all six `tools/` paths declared
by the lane.

**Gates.** First run: gate 1 **red** — `TwoTrackRecordingHarness` failed on a sample-digest
mismatch. It is not a regression: the harness writes to the shared
`/tmp/lmms-recording-harness`, and sibling lanes were running the same harness
concurrently (load average ~47). Re-run in isolation → 3/3 passes; full gates re-run →
**0 / 0 / 3**, ctest 29/29.

## Lane 4 — `post-alpha/oop-hosting`

Opt-in "run in a separate process" toggle for ZynAddSubFx (92 added lines in the two
inherited Zyn files), two project fixtures and a new test.

**Conflicted:** `tests/upstream-modifications.txt` — 51 ∪ 31 → **51** entries (the lane's
are a subset), 0 duplicates, 0 markers, verify OK. `git diff HEAD` on the resolved ledger
shows exactly **two** changed lines, both reason extensions
(`plugins/ZynAddSubFx/ZynAddSubFx.cpp`, `.h` gained `; post-alpha/oop-hosting: …`).
`tests/CMakeLists.txt` auto-merged.

**Checklist.** (a) the lane's one omission was `tests/src/plugins/ZynSeparateProcessTest.cpp`
— added to `tests/all-sources.txt` in its alphabetical position (whole-tree scope, the
same treatment the crash-report and plugin-scan lanes' test files got). (b) the only
inherited files touched, `plugins/ZynAddSubFx/ZynAddSubFx.{h,cpp}`, are declared with the
reason read off the lane's diff (per-instance `separateprocess` BoolModel +
`hostingState()`).

**Gates: 0 / 0 / 3**, ctest 30/30.

## Lane 5 — `post-alpha/lua-api`

Lua API version + console + package format: eight new sources, a new test, four docs, and
edits to the two inherited hooks `main.cpp` / `MainWindow.cpp`.

**Conflicted:** `tests/fork-sources.txt` and `tests/upstream-modifications.txt`.
* `fork-sources.txt`: 117 ∪ 108 → **125** entries, 0 duplicates, 0 markers, verify OK;
  `git diff HEAD` shows the eight added entries are exactly the lane's new headers and
  sources.
* `upstream-modifications.txt`: 51 ∪ 32 → **51** entries, 0 duplicates, 0 markers. Both
  shared paths needed a real reason union:
  * `src/gui/MainWindow.cpp` — integration's reason (compile-only fixes + plugin-scan
    clause) and the lane's (compile-only fixes + Lua-console clause) share a paragraph
    prefix. The union keeps the prefix once:
    `… (7f08809e4); plugin-scan: build the Tools menu on first open … (023770f7b); Lua
    console: runScript stops printing the captured log a second time … (task #613)`.
    The first attempt repeated the prefix; the verifier's `;;` scan caught it, the merge
    rule was fixed to combine divergent suffixes, and the file was re-resolved and
    re-verified.
  * `src/core/main.cpp` — independent reasons (crash reporter; the `--run-script` stdout
    contract) → both clauses kept.

**Checklist.** (a) `tests/src/core/ScriptStabilisationTest.cpp` was the lane's one
unregistered source → `tests/all-sources.txt`, alphabetical position. (b) the two
inherited files touched are declared.

**Gates: 0 / 0 / 3**, ctest 31/31.

## Lane 6 — `post-alpha/wave-r-rename` (last, and the widest)

`PROJECT(zene)`, the CMake target `lmms` → `zene`, packaging names, every CI upload glob,
desktop/man/icon metadata, README, and the user-visible strings.

**The scan said this lane merges cleanly. It did not, quite: there is exactly one
conflict — and it is semantic, not textual.** (`tests/CMakeLists.txt`, the
`ENABLE_EXPORTS` block of the per-test loop.)

```
integration (ours):  # binary must export its symbols exactly like the lmms executable does.
                     if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest" OR LMMS_TEST_NAME STREQUAL "PluginScanCacheTest")
lane (theirs):       # binary must export its symbols exactly like the zene executable does.
                     if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest")
```

Both sides are partially right and both naive resolutions are wrong: taking theirs
verbatim silently drops `PluginScanCacheTest` from the `ENABLE_EXPORTS` set and destroys
the export properties that test needs to load a real plugin module through the host path;
taking ours verbatim keeps the stale "lmms executable" wording. Resolved by hand as the
union of both intents — the renamed comment **and** the wider condition:

```
	# binary must export its symbols exactly like the zene executable does.
	if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest" OR LMMS_TEST_NAME STREQUAL "PluginScanCacheTest")
```

Everything else in the lane — the CMake project/target rename, the packaging renames, the
string rewrites — auto-merged. A post-merge audit found no stale reference to the retired
`lmms` CMake target (the `partc_ref_*` and `synthetic_audio_plugin` links had already been
renamed by the lane), and `lmmsobjs` survives with its dependants.

**Checklist.** (a) no new source files — the lane only renames packaging/doc assets and
edits existing sources — so `tests/fork-sources.txt` is untouched; Gate 9 confirms every
tracked source in scope is still registered. (b) the lane brought **no** declarations, so
Gate 6's divergence was declared here: **61 paths**, each with a reason read off
`git diff <merge-base>..post-alpha/wave-r-rename -- <path>`:
* **13 modified files** — `src/core/{ConfigManager,ImportFilter,PluginFactory,main}.cpp`,
  `src/gui/{GuiApplication,LmmsStyle,MainWindow}.cpp`,
  `src/gui/modals/{SetupDialog.cpp,about_dialog.ui}`, `doc/Doxyfile.in`, `.yamllint`,
  `INSTALL.txt`, `vcpkg.json`, `cmake/linux/apprun-hooks/{carla-hook,jack-hook}.sh`,
  `tools/mmpz-git/run-demo.sh` (the real behavioural ones are named as such:
  `ConfigManager.cpp` moves the data search path and the CMake cache-entry names to the
  `zene_` spelling — the old one is still accepted; `PluginFactory.cpp` moves
  `../lib/lmms` → `../lib/zene`; `main.cpp` renames the banner/`--help`/single-instance
  text);
* **46 rename endpoints** (23 renames × 2): the desktop entry, 14 icon files, the man
  page, bash completion, the RPM spec, the AppStream XML, the three NSIS resources, the
  macOS plist;
* **2 add/delete endpoints**: `cmake/linux/lmms` → `cmake/linux/zene` (the AppImage
  launcher).
Three paths already declared for other lanes (`src/core/main.cpp`,
`src/gui/MainWindow.cpp`, `src/core/PluginFactory.cpp`) had their existing reasons
**extended** with a wave-R clause rather than replaced. Verified `--dedup`, sorted, no
blank reason; Gate 6 then reported **0 violations** over the whole tree (112 ledger
entries).

**Gates: 0 / 0 / 3**, ctest 31/31.

**Fix-up `bbe3c66ea` — the rename's cross-lane tail.** Merging the rename last exposed what
no per-lane verification could see: two earlier lanes shipped tooling that hard-codes the
built binary's path, written while it was still `lmms`. `tools/mmpz-git/mmpz_git.py`'s
`find_renderer()` could not find `build/zene` at all (so `audible-diff` was unusable),
`render-recipe.sh` probed only `lmms` paths, the lane's `BinarySafety`/`AudibleDiffBinary`
tests **skipped** instead of failing (which is why no gate caught it), and the two
`tests/data/oop-hosting/*.sh` observation scripts called `$WORKTREE/build/lmms` literally.
Fixed additively: `zene` candidates first, the `lmms` names kept as fallbacks, no assertion
changed. Proof (unpiped): the lane's own suite
`python3 -m pytest tools/mmpz-git/tests/test_mmpz_git.py` → **43 passed, 1 skipped
(`scratch/qtsave2` not built), 206 subtests passed, exit 0** — the render-dependent tests
now run instead of skipping; `find_renderer()` → `<worktree>/build/zene`.

## Final verification of the finished tree

```
cd build && cmake --build . -j4                   -> EXIT=0   (100% built, no warnings-as-errors)
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
                                                  -> EXIT=0   100% tests passed, 0 tests failed out of 31
bash tests/fork-sources-gate.sh                   -> EXIT=0   125 fork-NEW, 996 inherited, 0 stale
bash tests/no-upstream-regression-gate.sh         -> EXIT=0   112 files in the ledger
bash tests/run-all-gates.sh                       -> EXIT=3   PASS-WITH-SKIPS (gate 2 coverage skipped)
```

`run-all-gates.sh` table: gate 1 ctest PASS, 2 coverage SKIP, 3 no-tautology PASS,
4 complexity PASS, 5 mutation PASS, 6 upstream-regression PASS, 7 file-length PASS,
8 duplication PASS, 9 fork-sources PASS.

Headless render of a real project, via the lane-supplied recipe (which also proves the
rename fix-up end to end):

```
bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
     -o /tmp/zene-render.wav                      -> EXIT=0
renderer: <worktree>/build/zene     2,177,112 bytes
sha256  : 6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
channels: 2          rate: 44100 Hz        frames: 544,256      duration: 12.341 s
peak    : 0.946686 (-0.48 dBFS)            RMS: 0.167444 (-15.52 dBFS)
non-zero samples: 1,084,197 / 1,088,512 (99.60%)   -> NON-SILENT
```

## Not merged, and why

Still running / not verified by the parent, so deliberately left alone (never touched, no
branch modified): `post-alpha/stem-export`, `post-alpha/lufs-wire`,
`post-alpha/clip-slice0`, `post-alpha/pipeline-hardening`, `post-alpha/autosave`.

Also present in the clone but outside this task's list, therefore not merged:
`post-alpha/agent-control-surface`, `post-alpha/clip-capture-spec`, `post-alpha/gate-debt`,
`post-alpha/instrument-hosting`, `post-alpha/lufs-meter`, `post-alpha/midi-learn`,
`post-alpha/pr594`, `post-alpha/readme-truth`, `post-alpha/v0.2`. The eight lanes merged
before this task are, of course, already in the branch.
