# The audit fix pass — applying `docs/INDEPENDENT-NOTES-READ.md`

**What this is.** The per-item record of applying the independent audit's findings to the two 0.2.0-alpha
release documents. Claim by claim: what changed, **the command that was run in this worktree, and its real
output**. An item that was deliberately *not* changed has a row here too, with its reason.

**The tree.** `post-alpha/integration`. HEAD was `0834e40f1` when this pass began and every command below was
run there; the report is committed on top of it. `docs/INDEPENDENT-NOTES-READ.md` was **not** edited — it is
the audit record and is left exactly as its author wrote it. Nothing in `src/`, `tests/`, or any build
directory was modified, no build was run, no tag exists, nothing was pushed, and `tests/advertised-features.tsv`
was not touched.

**The rule applied.** Each claim is either made true with a command that was actually run, or deleted. No
false claim was softened into a vaguer true-sounding one. Where a line number was incidental to its claim it
became a symbol, because a line number is true only of the commit it was taken at — and three of this audit's
findings were exactly that failure. Every `file:line` that survives in either document was re-checked against
the file as it stands at `0834e40f1` (scan at the bottom of this page); the only one that does not resolve as a
path is a verbatim gdb frame.

**Commits.**

| commit | subject |
|---|---|
| `b11d06a13` | provenance, marker count, version and asset strings (items 1, 4/A1, 11, 12; A2, A6) |
| `f9accb9ca` | symbolise or re-derive the stale `file:line` citations (item 4; item 10) |
| `7c0224c2c` | qualify the four absolute claims (items 2, 5, 6, 7, 8; I4's source half) |
| `6cbb4b4a5` | the unverifiable items and the two count-without-universe errors (items 3, 9; F6, F18, H14) |
| `326aa53b4` | name the limitations page's real path in both documents (the path fix) |
| `cd0358bd0` | attribute the 87 tally to its own words in the record (F18) |
| (this file + freeze item 5) | the report, and the audit recorded as done |

---

## The twelve FALSE claims, in the audit's order

### 1 — A11: "the release-honesty check enforces that everything in the notes is present", and freeze item 4 is "Done"

**Not mine to finish, and now marked so.** The enforcement half needs the release-configuration build that is
being run by the release engineer (`scripts/release-verify.sh`), so both documents now state the check's
**mechanism** — the half that is verified — and mark the result pending.

| | |
|---|---|
| changed | notes, freeze item 4 (*"Done — except the enforcement half, which is not green on either build on this box and is not claimed to be"*); limitations, the "Two features are compiled out" bullet |
| command | `sed -n '47p;187p' tests/release-honesty-gate.sh; grep -c 'release-honesty-gate' .github/workflows/build.yml` |
| output | `# Exit status: 0 when every documented feature matches the build, 1 when at` / `echo "RESULT: FAIL — $failures of $checked documented feature(s) do not match this build"` / `6` |

So: the guard fails an option the build does not report (`AUTO` is not `ON`), it exits 1, and **six** build jobs
run it in `.github/workflows/build.yml`. The gate was **not** run again for this pass (it needs a binary, and a
build is in flight), and neither document claims a green. **Still open:** the six-of-six result, on the
engineer's run.

### 2 — D8: "the only `IPlugView` occurrences in the repository are inside the vendored Carla copy"

The zero is real but it belongs to the searched paths, and the absolute was false of the repository.

| | |
|---|---|
| changed | notes, headline 2; limitations, "No instrument editor" |
| command | `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/` → **no output, exit 1**; `git grep -n IPlugView -- '*.cpp' '*.h' \| grep -v CarlaBase` |
| output | (first command: nothing) / `plugins/Vst3Instrument/Vst3InstrumentView.h:43: * This is NOT the plug-in's own editor. `IPlugView` is not implemented` |

Both documents now say the **0 is over the four searched paths**, and name the product comment and the vendored
SDK headers as the places the string does occur.

### 3 — C14: the §3b sweep "resolves … with `0 NEW` unresolved names"

The `1 NEW` finding was a real defect (a Help-menu action asked for an icon name that does not exist) and it has
been fixed at `0834e40f1`. The sweep is green now, and the count in the text was stale.

| | |
|---|---|
| changed | limitations, "The name, honestly" |
| command | `python3 tests/brand-resource-sweep.py` |
| output | `call sites scanned: 993` / `call sites resolved: 848` / `UNRESOLVED call sites: 3 (3 pre-existing baseline, 0 NEW)` / `EXIT=0` |

The text now says **993**, names the three pinned pre-existing sites, keeps **0 NEW** (now true rather than
false), and quotes the command so a reader can re-run it. The `989` came from before the control-surface merge
added its call sites.

### 4 — the line-number family (C1–C5, E2, E3, G15, H11, I1–I3, I5, and A1's family)

Thirteen references were true of an earlier commit and false in this tree. They are now symbols, or claims the
file can be greped for.

| item | was | is now | the check |
|---|---|---|---|
| A1 | "re-checked against that frozen tree and its built binary" | the provenance paragraph says the file was edited twice after `2239f3cb6` and that the control-surface section describes code that tip does not contain | `git show 2239f3cb6:include/ControlVocabulary.h` → `fatal: path 'include/ControlVocabulary.h' exists on disk, but not in '2239f3cb6'` |
| C1 | `src/core/Plugin.cpp:228` | the `pi.library->resolve("lmms_plugin_main")` lookup in `src/core/Plugin.cpp` | `grep -n lmms_plugin_main src/core/Plugin.cpp` → `237` |
| C2/C3 | `DataFile.cpp:128/136/313`, `:1741-1742` | the three `"zene-project"` sites, and the `firstChildElement("lmms-project")` fallback | `git grep -n 'zene-project\|lmms-project' -- src/core/DataFile.cpp` → `129,137,321,1749,1750` |
| C4/C5 | the read-both comment at `:1739`, write-new at `:2277` | the quoted comment strings, in `src/core/DataFile.cpp` | same grep → `1747`, `2294` |
| E2/E3 | `tests/CMakeLists.txt:806-821`, `:796` | the `Vst3InstrumentFixtureProbe` target and the `WANT_VST3_TEST_INSTRUMENT` option | `grep -n Vst3InstrumentFixtureProbe\|WANT_VST3_TEST_INSTRUMENT tests/CMakeLists.txt` → option at `815`, `add_executable` at `825` |
| G15 | `main.cpp:371`, `:177`, `:230-237`, `:281-286` | the `master`/`--master` subcommand, its usage text, its option block, and the `Auto-mastering: %d candidates` print | `grep -n master src/core/main.cpp` → usage `182`, dispatch `378`, options `237`, prints `288`, `293` |
| H11 | `DataFile.cpp:461-490/494/510/516/485/523` | `DataFile::writeFile()`'s checked sequence, the `.bak` move, the rename, its rollback | `sed -n '469p' src/core/DataFile.cpp` → the "final renames are the point" comment; renames at `502`, `518`, rollback `524` |
| I1 | `DataFile.cpp:347/435/438/427` | the `fullName + ".bak"` composition, the move, the rename, the `app/disablebackup` skip | same file → `391`, `502`, `518`, `480` |
| I2 | `DataFile.cpp:140/2105` written, `:2179` read | "set from `LMMS_VERSION` wherever the file creates the root, read back on load" | `grep -n creatorversion src/core/DataFile.cpp` → `141,328,2212,2286,2291,2339` |
| I3 | `legacyFileVersion()` at `:2226-2238` | `DataFile::legacyFileVersion()` | same file → definition at `2335` |
| I5 | `ConfigManager.cpp:790/823/709-790`, `ConfigManager.h:319-336` | the two `ConfigMigration` functions and the header | `grep -n adoptConfigFile\|adoptWorkingDir src/core/ConfigManager.cpp include/ConfigManager.h` → cpp `717–831`, header `338,339` |

### 5 — I6: both documents reported a stale half that no longer exists

| | |
|---|---|
| changed | notes, the footnote under "Coming from 0.1.0-alpha"; limitations, "The name, honestly" |
| command | `grep -n "orphan\|lmms-workspace" docs/RELEASE-NOTES-v0.2.0-alpha.md` |
| output | the only headline hit is the bullet that says user state is **migrated rather than kept**; `lmms-workspace` appears in the footnote alone |

The notes no longer accuse their own first headline; only `docs/WAVE-R-RENAME.md` §6 is named, in both
documents, as the stale record. §6 is a lane record and is left as written (the audit's own rule: it needs a
decision, not an over-write).

### 6 — F21: "the registry and the menus call the same implementation"

| | |
|---|---|
| changed | notes, headline 3 |
| command | `sed -n '2p;78p' tests/integration-logs-3f-fix/08-gate-committed.log; grep -vc '^#' tests/agent-surface-baseline.txt` |
| output | `control.surface_report: 46 reflected actions` / `reflection : 4 registered, 42 unregistered (42 grandfathered, 0 new)` / `42` |

The sentence now claims the direction that is proven — where a command has a menu/toolbar entry, entry and
handler are one implementation (`telemetry.consent`) — and states the gate's one-way measurement: **46** actions
reflected, **4** registered, **42** baselined as unregistered. The claim that those 42 are commands is gone.

### 7 — F20: the eviction bound cited at `ControlCommandsArrangement.cpp:53`

| | |
|---|---|
| changed | notes, "The reversibility contract" |
| command | `sed -n '53p' src/core/ControlCommandsArrangement.cpp; git grep -n 'evictedCount\|m_evicted' -- include/ControlRegistry.h` |
| output | `constexpr int MaxTrackSnapshotChars = 65536;` / `include/ControlRegistry.h:227: int evictedCount() const { return m_evicted; }` / `:264: int m_evicted = 0;` |

Line 53 is a per-record cap, not the eviction code. The sentence now names `MaxTransactionRecords`,
`MaxTransactionBytes`, `ControlSnapshotLimit` and `MaxTrackSnapshotChars`, and puts the eviction accounting in
`ControlRegistry` (reported through `control.transactions`' `evicted` count).

### 8 — D14: "line 185 says the modes work"

| | |
|---|---|
| changed | notes, "What the surface does not do" |
| command | `sed -n '185,188p' docs/KNOWN-LIMITATIONS.md` |
| output | `185` is the "build an effect fixture …" sentence; the automation bullet is `187`: "**Automation is not sample-accurate.** Modes work (Read / Touch / Latch / Write) …" |

The wrong number is **deleted, not corrected**: the sentence now cites the page's automation bullet by its
content, because the next edit to that page would move a line number again.

### 9 — H5: "the nine projects this repository bundles"

| | |
|---|---|
| changed | notes, "Renders are now reproducible"; limits summary; limitations, "Renders are reproducible — with two exceptions" |
| command | `git ls-files '*.mmp' '*.mmpz' \| wc -l`; `find data -name "StrictProduction*" -o -name "Root84*"` |
| output | `68` / `data/projects/shorties/Root84-TrancyLoop.mmpz`, `data/projects/demos/StrictProduction-DearJonDoe.mmp`, `data/projects/demos/Root84-Initialize.mmpz` |

Both documents now say **the nine projects the determinism sweep covers**, name 68 as the repository's count
with its command, and quote the two paths as the tree ships them (`data/projects/...`, not `demos/`/`shorties/`).

---

## UNVERIFIABLE FROM THIS BOX — what was done with each

The audit's header counts **6**; its table lists **7** rows. All seven are covered.

| item | action | command and output |
|---|---|---|
| A2 — "16 markers" | **number deleted.** The item now states what is checkable — that no live marker remains — and says plainly that no marker count is derivable, because the release-prep table and this file are records of two different passes | `grep -n "VERIFY AT FREEZE" docs/RELEASE-NOTES-v0.2.0-alpha.md` → three hits (`7`, `646`, `647`), each this convention being described; `grep -c` on the limitations page → `1` |
| A6 — the untagged `0.1.0-alpha.123+34c1f4f` string | **artefact half deleted, mechanism kept.** The `git describe` output is quoted (it is checkable); the string is described as what the assembly builds from it, and no `lmmsversion.h` is attributed to a commit that has none on this box | `git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' 34c1f4f86` → `v0.1.0-alpha-123-g34c1f4f86`; `head -1 build/lmmsversion.h` → `0.1.0-alpha.247+f68cf8e`; `head -1 build-coverage/lmmsversion.h` → `0.1.0-alpha.315+6daed30` |
| D20 — "no stable project format", "no measured crash-free rate" | **unchanged, deliberately.** Both are policy statements, not measurements; neither names a number or a capability, and there is nothing for a command to confirm or refute. The audit's own verdict on them is that they "read as honest hedges and nothing contradicts them" | — |
| F6 — task #625's "never becomes usable" | **re-cited to a record that exists in the tree's sibling workspace, and the unattributed figure dropped.** The 92 s / `engine_ready` false measurement is quoted to `ableton-gap/AGENT-TOOLING.md` §4, its reproducer named, and the base commit shown to be a real commit; the per-run figures are marked as that lane's | `grep -n 'engine_ready. false and every command' ../ableton-gap/AGENT-TOOLING.md` → `141: engine_ready` false and every command `busy` for 92 s and counting`; `git cat-file -t 6b01b98eb` → `commit` |
| F18 — "87 = 71 + 16" | **settled by the record the audit said would settle it.** The figure is now attributed to the program workspace's own tally and to its exact words, and marked as not measurable from this tree | `grep -n "registered ids in ControlCommands" ../POST-ALPHA-PLAN.md` → `749: … "registered ids in ControlCommands*.cpp \| 87 \| 71 + 16"`, with `755: 87 ids registered in the SOURCE (71 + 16) — a static count, right for its method`. The citation is file-level on purpose: that plan was edited while this pass ran and the line moved from 717 to 749 |
| H14 — "a segfault at `0x188`" and "86 %" | **the address is deleted, the figure is kept and cited.** The address appears nowhere in the tree outside the notes themselves; the crash frame, the backtrace log and the percentage are in `docs/COVERAGE-GATE-GREEN.md` §1, and the sentence now cites them | `grep -c 0x188 docs/RELEASE-NOTES-v0.2.0-alpha.md` → `0`; `grep -n '86.00 %' docs/COVERAGE-GATE-GREEN.md` → `44`, `90`, `295` (and §1 carries the `SIGSEGV` frame and `tests/coverage-green/midilearngui-segfault-backtrace.log`) |
| I4 — "an older 1.3-alpha build will open a 0.2 project and silently drop parts of it" | **the unverifiable half is still not claimed, and its source-level half was corrected.** The limitations page already said no LMMS 1.3.0-alpha binary exists here; what was false was "**and nothing else**" — `origin/master`'s `Song.cpp` also compares node names for the GUI editors' panels. That phrase is gone and the file's own dispatch names are given | `git show origin/master:src/core/Song.cpp \| grep -cE 'nodeName\(\) =='` → `10` (the five container elements plus the five GUI panels) |

---

## The path fix

The release process looks for `docs/KNOWN-LIMITATIONS-v0.2.0-alpha.md`; the page is
`docs/KNOWN-LIMITATIONS.md`.

| | |
|---|---|
| changed | both documents: the notes' "Known limitations" section and the limitations page's own convention block now name the page's real path and state that **no version-suffixed 0.2.0 limitations file exists** (the 0.1.0 page, `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`, is the one it replaced) |
| command | `git grep -n 'KNOWN-LIMITATIONS-v0.2.0-alpha' -- docs/` |
| output | every hit is `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` (a real file in the workspace's `drafts/`) or `docs/RELEASE-PREP-0.2.0.md`'s map, which already gives `docs/KNOWN-LIMITATIONS.md` as the applied page. **There was no broken reference in the product worktree to correct** |

**Reported to the release engineer, not edited:** `RELEASE-0.2.0-CHECKLIST.md` (one directory up, in the
workspace). Its F4a block is the only place a reader meets both filenames — lines `383` and `385` explain the
pattern and diff against the correct unsuffixed `docs/KNOWN-LIMITATIONS.md`, and line `428` quotes the
unsuffixed path. **A link-check would not fail on it as it stands**; the block's risk is that a reader skims it
and types the suffixed name. That file is the engineer's and was not touched.

---

## Deliberately unchanged, and why

- **D20's two policy statements** (above). Unchanged.
- **Line references the audit verified TRUE at this tree** — `src/core/Track.cpp:232`,
  `include/ControlVocabulary.h:89-95`, `include/ControlReversibility.h:143`, `src/core/AutomatableModel.cpp:305`,
  `src/core/Rack.cpp:48`, `src/core/Mixer.cpp:720`/`:1893`, `CMakeLists.txt:140`, `src/CMakeLists.txt:6-14`,
  `src/core/AudioEngineWorkerThread.cpp:204`, `tests/src/core/AudioEngineTeardownTest.cpp:135-148`,
  `tests/run-all-gates.sh:186`, `src/core/audio/TrackRecorder.cpp:190`, `include/TrackRecorder.h:41`,
  `src/core/Song.cpp:1141-1163`, `.github/workflows/build.yml:147`, `src/core/main.cpp`'s recovery-file gate —
  were left in place. They were re-checked by the scan below and they resolve; converting them buys nothing and
  risks a transcription error in a pass whose whole point is accuracy.
- **One `file:line` in the notes is a verbatim quote**, not a citation: the gdb frame
  `#0 lmms::PatternStore::updateComboBox ... PatternStore.cpp:203` inside the `control.undo` bullet. It is
  reproduced as the debugger printed it and is left alone.
- **`docs/BRAND-PLACEHOLDERS.md` §3b still prints `989`.** That is the brand lane's own record, not a release
  document; the audit did not flag it and it is left as written — noted here so the next reader is not surprised
  by the two numbers.
- **`docs/RELEASE-PREP-0.2.0.md` §3's own table carries stale line references** (`DataFile.cpp:347` for the
  `.bak`, `tests/CMakeLists.txt:737-752` for the fixture probe). It is the release-prep lane's report at base
  `34c1f4f86`, left as written; the release documents no longer take line numbers from it.

---

## Extra findings fixed in passing

- **The telemetry kill-switch path in the limitations page was wrong**: it cited `src/CMakeLists.txt:376`, and
  `src/CMakeLists.txt` has 272 lines. The guard is in the root file — `sed -n '376p' CMakeLists.txt` →
  `IF(ZENE_TELEMETRY_ENABLED)`. Corrected to the root `CMakeLists.txt`.
- **Freeze item 5 of the notes still said the independent read was "not done".** It has been done — the audit
  exists — so item 5 now names it, its count, and this report.

## Still open, on purpose

1. **Freeze item 4's enforcement half** — the six-of-six honesty-gate result, pending the release engineer's
   `scripts/release-verify.sh` run. Both documents say mechanism, not green.
2. **The digest/SHA-256 block** (freeze item 3) — needs the published artefacts. Untouched.

---

## The scan that proves the citations resolve

Every `path.ext:NNN` in both documents was re-read against the file at `0834e40f1`:

```
doc:161 src/core/CMakeLists.txt:61-93   => core/ControlCommandsArrangement.cpp
doc:189 src/core/Track.cpp:232          => element.setAttribute( "id", m_id );
doc:190 include/ControlVocabulary.h:89-95
doc:253 include/ControlReversibility.h:143 => constexpr int MaxTransactionRecords = 100;
doc:260 src/core/AutomatableModel.cpp:305  => if (!isAutomated) { addJournalCheckPoint(); }
doc:388 src/core/Rack.cpp:48            => constexpr auto RACK_ELEMENT = "rack";
doc:393 src/core/Mixer.cpp:720          => VcaGroup* Mixer::createVcaGroup(…)
doc:412 CMakeLists.txt:140              => option(ZENE_TELEMETRY … ON)
doc:449 src/core/AudioEngineWorkerThread.cpp:204 => // stranded in wait())…
doc:464 tests/run-all-gates.sh:186      => bash tests/unregistered-tests-gate.sh
doc:499 src/core/audio/TrackRecorder.cpp:190 => std::clamp(…)
doc:547 src/core/PatternStore.cpp:203   => if (pt == nullptr)      (the crash frame's guard)
LIMITATIONS
doc: 34 .github/workflows/build.yml:147 => if: startsWith(github.ref, 'refs/tags/')…
doc: 89 src/core/Song.cpp:1141-1163     => the failed-open restore branch
doc:128 src/core/Mixer.cpp:720 / doc:137 src/core/Rack.cpp:48 / doc:238 AudioEngineWorkerThread.cpp:204
doc:251 CMakeLists.txt:140
```

Run over the two documents at the parent commit (`0834e40f1`) and over them again after this pass, the same
scanner reports: **46 `file:line` references before, 26 after** (the rest became symbols or grepeable strings),
and **references that resolve to nothing: 4 before, 1 after** — the one that remains is the verbatim gdb frame
quoted above. The four before were the two bare exporter filenames, the bare `PatternStore.cpp:203` frame, and
`src/CMakeLists.txt:376` in the limitations page (a file with 272 lines at this tree).

**Highest-impact fix, one command:** `python3 tests/brand-resource-sweep.py; echo EXIT=$?` → the tree's own
evidence command for the brand claim, which the audit found red (`1 NEW`, exit 1) and which now exits 0 with
`UNRESOLVED call sites: 3 (3 pre-existing baseline, 0 NEW)` over 993 call sites.
