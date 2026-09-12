# Save/load integrity — fixes for D3, the dropped `<session>` block, D2, and D6

**Branch:** `post-alpha/saveload-integrity`, based on `post-alpha/integration`.
**Base commit:** `abb3a1a7f` ("Merge branch 'post-alpha/wave-r-rename' into post-alpha/integration").
**Answers:** `feedback/grade-C-saveload.md` (§3, ranks 1–3 and 8; §4b).
**Scope note:** this branch changes persistence code only where the audit found a defect. The
persistence files are byte-identical to upstream at the fork point, and an ordinary save/load
round trip of a valid project still produces the same bytes (proved in §2 and §4).

Everything below was measured on this tree. Where something was *not* measured, it says so.

---

## 1. Verdict table

| # | Defect | Status here | Where the fix lives |
|---|---|---|---|
| D3 | rename failures discarded, `writeFile` returned `true` regardless | **fixed** | `src/core/DataFile.cpp`, `writeFile()` tail (was 425–440, now the checked sequence at 425–490) |
| new | a build without `WANT_SESSION_VIEW` silently drops `<session>` | **fixed** — the block is preserved verbatim, the save is not refused | `src/core/Song.cpp` (`clearProject`, `loadProject`, `saveProjectFile`), `include/Song.h` |
| D2 | a failed open left modified-tracking, undo journalling and autosave off until restart | **fixed** | `src/core/Song.cpp`, `loadProject()` failure branch (1061–1081) |
| D6 | an older build silently drops `<bus>`, `<sidechain-send>`, `prefader`, `slide` | **not fixable here**; this branch's own round trip is proved lossless and the wording for the notes is in §5 | proof only, plus §5 |

Regression tests: `tests/src/core/DataFileSaveIntegrityTest.cpp` (D3) and
`tests/src/core/ProjectOpenIntegrityTest.cpp` (the `<session>` block, D2, D6). Both are registered
in `tests/CMakeLists.txt` and in `tests/fork-sources.txt`; the three touched upstream files are
declared in `tests/upstream-modifications.txt` with a reason read off the diff.

---

## 2. D3 — a refused rename was reported as a successful save

**The line.** `src/core/DataFile.cpp`, `DataFile::writeFile()`, the four statements after
`outfile.commit()` (measured at `post-alpha/integration` lines 425–440):

```cpp
	if (ConfigManager::inst()->value("app", "disablebackup").toInt())
	{
		// remove current file
		QFile::remove(fullName);
	}
	else
	{
		// remove old backup file
		QFile::remove(fullNameBak);
		// move current file to backup file
		QFile::rename(fullName, fullNameBak);
	}
	// move temporary file to current file
	QFile::rename(fullNameTemp, fullName);

	return true;
```

Every `QFile::remove`/`QFile::rename` result was discarded and the function returned `true`
unconditionally. The caller then takes the return value as the truth:

```cpp
// src/core/Song.cpp, Song::guiSaveProjectAs()
1345:	if (saveResult && !withResources)
1347:		setModified(false);
1348:		setProjectFileName(fileNameWithExtension);
```

So a refusal by the filesystem ended with the document marked clean and the new name adopted,
while the file on disk was the old project — or was gone, if the move-aside had already succeeded,
with the only copy of the new content left in `<name>.new` (which the recent-files menu hides).

**The fix.** The sequence is checked and the state is kept consistent:

- `backupDisabled` is read once, before either branch.
- **disablebackup branch**: the destination is removed only if it exists, and a removal that fails
  is a refused save (`showError` + `return false`).
- **backup branch**: the move-aside is checked. If the destination still exists after it, the
  failure is *reported* (`Could not create a backup`, naming `<name>.bak`) but the save continues —
  losing the backup is not losing the project, and turning it into a refused save would fail saves
  that do land.
- **the final rename is checked**, and this is the one that matters. If it fails:
  - the previous project is moved back out of `<name>.bak` (`previousFileMoved` only — a stale or
    foreign `.bak` is never renamed over the project), so the on-disk project is the one that was
    there before;
  - `showError` is called with the message that names `<name>.new`, because that is where the new
    content is and nothing else tells the user;
  - the function returns `false`.

The old `writeFile` began at `DataFile.cpp:315`; the checked sequence is its tail at 425–490.

**Proof.**

`tests/src/core/DataFileSaveIntegrityTest.cpp`:

1. `blockedRenameIsRefusedAndReported()` — the destination is a non-empty directory (the OS refuses
   to rename a file over it, exactly as it refuses to rename over a file another process holds
   open, a foreign-owned file, or a protected path), and `.bak` is a non-empty directory too, so
   the move-aside cannot succeed either. Asserts: `writeFile()` returns **false**; the two warnings
   reach the log/stderr path (`Nothing has been discarded`, and the backup warning before it); the
   destination is byte-for-byte what it was; and `<name>.new` still holds the written project, so
   the message's promise is true.
2. `uncheckedRenameDanceReportedSuccess()` — the **inverted control**. The pre-fix three-statement
   tail, transcribed from the branch this fix replaces, runs on the same fixture and asserts the
   old outcome: `remove`/`rename`/`rename` all refused, the destination never replaced, the new
   content still only in `.new`, and the transcribed sequence reporting **success** anyway. This is
   what makes the test above non-vacuous: if a platform ever lets those renames through, the control
   fails rather than the assertion quietly passing for the wrong reason.
3. `backupFailureAlsoRefusesTheSave()` — the destination is a real file and `.bak` is occupied by
   a non-empty directory, so the move-aside fails. Both failures are reported and the save is
   **refused**; the previous project is intact byte-for-byte and the written project is still in
   `.new`. See §2.1 for why this is the correct outcome and not a regression.
4. `disableBackupRemovalFailureIsRefused()` — the same refusal on the `disablebackup` branch.
5. `ordinarySaveStillWritesAndKeepsABackup()` — behaviour preservation: an ordinary save writes the
   project, and the second save leaves the first version in `<name>.bak`.
6. `unremovableBackupDoesNotBlockANewProject()` — a stale `.bak` that cannot be removed does not
   block saving a project to a path that has none: only the destination decides.

### 2.1 Why the failed-backup case is a refused save: `QFile::rename` does not overwrite

The first version of this fix reported a failed backup and carried on, on the assumption that the
final rename can still replace the destination. **That assumption is false**, and it was measured
rather than argued. A standalone Qt 6.4.2 program (throwaway, not part of this repo) on this host:

```
QFile::remove(empty directory)                -> false   (directory left in place)
QFile::remove(non-empty directory)            -> false
QFile::rename(file -> non-empty directory)    -> false   (source intact)
QFile::rename(file -> existing file)          -> false   (both files unchanged)
QFile::rename(file -> empty directory)        -> false
QFile::rename(file -> absent path)            -> true
QFile::rename(non-empty directory -> absent)  -> true
```

Qt's `QFile::rename` refuses to overwrite an existing file — unlike POSIX `rename(2)`, which
replaces it. Upstream's `writeFile` depends on the move-aside having freed the destination path for
exactly this reason. So when the move-aside fails and the destination is still there, **the final
rename fails too**, and upstream returned `true` with the new project stranded in `<name>.new`:
the same silent data loss through a second door, with no rename ever having been refused by the OS.
The fix reports both failures and refuses the save.

This is not a regression for a user in that situation: the save did not land under upstream either,
it was merely reported as having landed. It does mean the message set distinguishes two outcomes —
"not saved, data at `<name>.new`" (this case, and the refused-replace case) versus a success with a
missing backup, which cannot arise on this code path because `QFile::rename` will not overwrite.

One consequence that *is* behaviour preservation: because only the destination matters, a stale
`<name>.bak` that cannot be removed does not block saving a project that has no file yet — pinned
by `unremovableBackupDoesNotBlockANewProject()`.

**What the audit's suggested design would change here.** Copying to `.bak` instead of moving (so the
destination is never absent), then writing with `QSaveFile` straight onto the target, would make
this case save *successfully* rather than refuse. That is a larger change to the same function with
its own round-trip proof to write, and it is the natural follow-up; see §6.

**Reachability.** The audit rates this *Moderate* — it needs a target the OS refuses to rename over
(locked/foreign-owned/AV-held/network share), or a crash between the two renames. The fixture
reproduces the refusal class directly, on Linux, with no root and no race. **What is not
eliminated:** the crash window *between* the two renames still exists — a process death right there
still leaves the project in `.bak` and the new content in `.new`, with nothing having reported
anything because nothing was still running. Closing that is the audit's other suggestion (copy to
`.bak`, then `QSaveFile` straight onto the target so the file is never absent), a larger change to
the same function. What *is* eliminated is the misreport: no failure of these operations is silent
any more, and the caller can no longer mark a document clean when its data went nowhere. The
Windows-specific `QFile::rename` semantics the audit lists as `UNVERIFIABLE` were not reproduced
(no Windows host here); the defect and the fix are platform-independent, the *trigger* is not.

---

## 3. The grader's new defect — a default build dropped `<session>` silently

**The lines.** `src/core/Song.cpp` at `post-alpha/integration`:

- the reader branch sits inside the feature gate (`loadProject()`, was 1140–1147):

```cpp
#ifdef LMMS_HAVE_SESSION_VIEW
			else if( node.nodeName() == "session" )
			{
				m_sessionModel.restoreState( node.toElement() );
			}
#endif
			else if( getGUI() != nullptr )
```

- nothing else in the chain matches a `<session>` element, and the writer is gated the same way
  (`saveProjectFile()`, was 1256–1262). `WANT_SESSION_VIEW` defaults to `OFF`
  (`CMakeLists.txt:121`), so this is the shipped configuration: **a session-aware project that met a
  default build lost the whole block on the next save, with no error, no warning and no crash.**

**The fix: preserve, do not refuse — and the reason.** A refusal would make a default build unable
to save any project that had ever been touched by a session-aware build, which is a hard regression
for the common case and a worse outcome than the defect (the user cannot save at all, and the block
is not "protected" — it is held hostage). Preservation costs nothing, keeps the feature's data, and
is this codebase's own precedent: `SessionModel::restoreState()` already ignores a `<session>` block
from a newer build while keeping it verbatim, and re-emits it on save
(`SessionModel.cpp`, `m_preservedUnknownXml`). The `#else` of the reader branch now captures the
raw XML of the element, and the `#else` of the writer re-emits it verbatim into the same place in
the document. `Song::clearProject()` clears it with the rest of the project state, so a block from
one project cannot leak into the next.

Scope, stated plainly: this covers the `<session>` element, which is the block a feature introduces
and which this fork owns. **Unknown top-level elements other than `<session>` are still dropped by
this build** — that is a larger, general change (an unknown-element pass over the whole content
loop) and it is not this defect; see §6.

**Proof.** `tests/src/core/ProjectOpenIntegrityTest.cpp`:

1. `sessionBlockSurvivesARoundTripThroughASessionBlindBuild()` — writes a project containing a
   version-1 `<session>` block (two tracks, a named scene, a clip referencing pattern 11), loads it
   with `setLoadOnLaunch(false)`, saves it, and asserts the re-saved document **still has** the
   `<session>` element with `version=1`, `tracks=2`, `scenes=2`, one clip with `pattern="11"` and
   one scene named `Verse`. It then loads that file and saves again and asserts the block is
   byte-identical to the first save, so repeated opens cannot degrade it. The test holds in **both**
   configurations (this is why it is not inside `#ifdef LMMS_HAVE_SESSION_VIEW`): with the option
   **off** it exercises the new preservation path, and with the option **on** it exercises the
   existing `SessionModel` path and keeps the two honest against each other.
2. `projectWithoutASessionBlockIsUnchanged()` — behaviour preservation: a project with no session
   block loads and re-saves **byte-identically** and gains no `<session>` element. (With the option
   on, the same guarantee is already held by
   `SessionModelTest::preSessionProjectLoadsAndSavesUnchanged`.)

**Reachability.** High, and it ships: `post-alpha/integration` is where PR #594's data layer lives
and `WANT_SESSION_VIEW` defaults to `OFF`, so every build from this branch is the session-blind
build in the defect. Before the fix, the loss needed only one open-and-save with a default build.

---

## 4. D2 — a failed open disabled autosave and undo until restart

**The lines.** `src/core/Song.cpp`, `Song::loadProject()` at `post-alpha/integration`:

```cpp
1014:	m_loadingProject = true;
1016:	Engine::projectJournal()->setJournalling( false );
...
1053:	if (cantLoadProject)
1054:	{
1055:		if( m_loadOnLaunch ) { createNewProject(); }
1058:		setProjectFileName(m_oldFileName);
1059:		return;              // <- both flags still off
1060:	}
...
1207:	m_loadingProject = false;   // success path only
1209:	setModified(false);
```

with the two consequences the audit named:

```cpp
// Song::setModified() - a no-op while the loading flag is set
457:	if( !m_loadingProject && m_modified != value)

// MainWindow::autoSave() - refuses to run while the loading flag is set
1494: if( !Engine::getSong()->isExporting() &&
1497:	 !Engine::getSong()->isLoadingProject() && ...
```

`m_loadingProject` is only cleared inside `createNewProject()` (reached from the failure branch only
when `m_loadOnLaunch` is set, i.e. at start-up) and at 1207 on success. So a user who opened one
bad file — a truncated project, or the local-plugin refusal — and carried on had **no autosave, no
undo journalling and no modified-tracking for the rest of the session**, with nothing shown.

**The fix.** The failure branch now restores both:

```cpp
		m_loadingProject = false;
		Engine::projectJournal()->setJournalling( true );
```

This is safe by construction: the branch returns *before* `clearProject()`, so the previous project
is still the loaded one and restoring the two flags restores exactly its state. When
`m_loadOnLaunch` is set, `createNewProject()` has already set both, so the assignment is idempotent.
Nothing else on the load path changed.

**Proof.** `ProjectOpenIntegrityTest::failedOpenLeavesTheDocumentUsable()` — loads a non-project
file with `setLoadOnLaunch(false)` (the path a user takes when opening a file while the app is
running; with it true, `createNewProject()` masks the defect) and asserts:

- `isLoadingProject()` is **false** — the exact condition `MainWindow::autoSave()` tests at line
  1497, so autosave is not disabled;
- `Engine::projectJournal()->isJournalling()` is **true** — undo journalling is back on;
- `setModified(true)` makes the document modified and `setModified(false)` clears it — this is the
  assertion that pins the defect, because `setModified()` is a no-op while the flag is set
  (line 457), so before the fix the document could never become modified again;
- the document is still usable end to end: it saves, and the file exists.

**Reachability.** The audit's *Moderate*: one corrupt/truncated/unreadable open, or the local-plugin
refusal, reachable from Open, the recent menu, the file browser and drag-and-drop. Every safety net
went off at once and invisibly.

---

## 5. D6 — an older build silently drops `<bus>`, `<sidechain-send>`, `prefader`, `slide`

**What is fixable from this tree, and what is not.** The loss happens in the *older* build's reader:
LMMS 1.3.0-alpha and earlier Zene builds match only `<send>` and ignore the elements this fork
writes (`Mixer.cpp` Phase D comments; `Note::saveState`'s `slide` attribute). No change made here
can reach code that is already shipped in another binary. So this branch does the two things that
are actually available:

1. **It proves this build's own round trip is lossless** for those elements, so the degradation is
   demonstrably the older reader's and not something this tree does on the way through.

   `ProjectOpenIntegrityTest::currentBuildRoundTripsBusSidechainAndPrefaderSends()` creates a
   parallel bus, a pre-fader channel send and a sidechain send, saves them, and asserts the written
   XML contains `<bus`, `sidechain-send` and `prefader="1"`, then loads that XML back and asserts
   the bus flag and the sidechain route survived. `slide` is pinned by the registered
   `SlideNotesTest::slideNoteRoundTrip()` and `projectWithoutSlidesRoundTripsUnchanged()`.

2. **It supplies the wording the shipped notes are missing** (§5.1 below, item b), because the only
   lever this tree has over an older reader is telling the user not to hand it the file.

**A finding while doing this, recorded rather than fixed:** the two tests that already cover the
mixer half in more depth — `tests/src/core/MixerRoutingBackwardCompatTest.cpp` (bus, sidechain,
pre-fader, identical audio after a round trip) and `tests/src/core/PhaseDSidechainTest.cpp` — are
**not registered in `tests/CMakeLists.txt`**, so they do not build and do not run in this branch's
ctest. They are in `tests/all-sources.txt` and therefore visible to the whole-tree ratchets, but
nothing executes them. That is why the D6 proof above is a smaller case in a registered test rather
than a citation. Wiring those two targets in is a separate change (they predate this branch and are
not mine to re-scope); it is the single highest-value follow-up in this area, because the deeper
proof already exists and currently proves nothing.

---

## 5.1 Proposed `KNOWN-LIMITATIONS.md` wording (for central application)

Not applied here: `docs/KNOWN-LIMITATIONS.md` is being edited by another lane, so this report
carries the wording instead. Placeholders in angle brackets are the only edits needed. All four are
additions to **`## Your projects`** (currently `docs/KNOWN-LIMITATIONS.md:135-141`), except where
noted.

**(a) Where the `.bak` file lives** — insert as a new bullet after *"Keep copies of anything you
care about."* (line 141). The notes send the user off to make backups while every save is already
making one at a path they are never told:

```markdown
- **Every save leaves the previous version next to the project as `<name>.mmp.bak`** (or
  `.mmpz.bak`). The new project is written to a temporary file first, the current project is moved
  to that `.bak` name, and only then is the new project put in place. The `.bak` file is replaced
  by the next save and nothing in the app lists or opens it, so copy it or delete it yourself — it
  is a plain file.
```

**(b) An older build opens a new project and silently degrades it** — insert directly after the
*"No project-format stability promise yet."* bullet (lines 137–139). The notes say "may not open"
and "Do not try"; the truth the audit found is worse, and the operative word is *silently*:

```markdown
- **An older build does open these projects, and quietly removes things from them.** LMMS
  1.3.0-alpha and any Zene build from before this alpha open a project saved here without an error,
  then drop whatever they do not understand: parallel-bus markers, sidechain sends, pre-fader
  sends and slide notes are gone from the file the moment you save there, and nothing tells you.
  "May not open" understates it — assume an older build *will* open your project and silently
  degrade it, so keep the original and do not save over it.
```

*Basis for the wording: the older reader's source as reviewed in the audit — `Mixer.cpp` at the
fork point matches only `<send>` and has no branch for the Phase D elements, and `Note` has no
`slide` reader — not an execution of a 1.3.0-alpha binary (none was run here). The current build's
own round trip of those elements is executed and passes (§5).*

**(c) The version string is stamped into every project as `creatorversion`** — insert after (b).
This is the causal link between the version string the notes already document (`## Platforms`,
lines 150–153) and the corruption/silent-drop they only hint at:

```markdown
- **Every project records which build wrote it.** The project file carries a `creatorversion`
  attribute holding the version string of the build that saved it — `0.1.0-alpha`, or
  `1.3.0-alpha` for a CI build that inherited LMMS's version. That string is what another build
  reads to decide whether to "upgrade" the project, so it is why a much older reader treats a
  v0.1 project as ancient and rewrites it, and why two builds of this same project can behave
  differently on the same file. Treat the string as provenance, not as a promise.
```

**(d) After a failed open: save and restart** — insert after (c). **Read this before applying it:**
the gap the audit found had two halves. The first — *"autosave and undo are disabled until you
restart"* — has been **fixed on this branch** (§4), and shipping that sentence as current behaviour
would be wrong. What is still true, and what the wording below says, is the advice itself:

```markdown
- **If a project fails to open, save what is on screen and restart.** The failed open is refused
  before anything loaded is cleared, so the document you had open is still there and unchanged.
  Save it (or save it under a new name), restart the application, and then investigate the file
  that would not open.
```

---

## 6. What I did not do, and the limits of these fixes

- **I did not edit `docs/`.** `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-*.md` are another
  lane's files; the wording for the four gaps is quoted in §5.1 for central application. The only
  file this branch adds under `docs/` is this report.
- **I did not touch the older builds' readers.** D6's loss is in code that is already shipped
  elsewhere; §5 says what was done instead.
- **The general unknown-element case is not fixed.** A build without `WANT_SESSION_VIEW` now
  preserves `<session>`; it still drops **any other** element it has no reader for. Doing this
  properly means an unknown-element pass in `Song::loadProject()`'s content loop with a policy for
  every block, which changes what a GUI-less or feature-reduced build re-saves for *all* unmatched
  elements — a behavioural change beyond this defect's brief, and one that needs its own round-trip
  proof per element. Stated, not hidden.
- **The crash window between the two renames is still open** (§2). The misreport is fixed; the
  window is a distinct failure mode and needs the copy-then-`QSaveFile`-onto-target design. That
  same design is also what would let a save succeed when the move-aside fails, instead of refusing
  it (§2.1) — one follow-up closes both.
- **The Windows trigger for D3 was not reproduced** (no Windows host). The refusal *class* is
  reproduced on Linux; the OS-specific semantics the audit lists as `UNVERIFIABLE` remain so.
- **D1, D4, D5, D7 and D8 are untouched.** They are outside this task's brief (D1's damage extent is
  still unexecuted per the audit; D4/D5/D8 are the recovery-file/shared-working-folder family).
- **`MixerRoutingBackwardCompatTest` / `PhaseDSidechainTest` are not wired into the test build**
  (§5). Reported, deliberately not fixed here.
- **Gate 2 (coverage) does not run in the default gate sweep**, and the two-gate `--with-coverage`
  build was not run on this branch: the budget went to the OFF/ON pair that the `<session>` defect
  requires. The new tests are measured by Gates 3, 4, 7, 8 and 9.

---

## 7. Verification (measured, unpiped exit codes)

Configuration deviation from CI, as required to be stated: **Qt5 development files are absent on
this machine**, so `tools/local-ci.sh` added `-DWANT_QT6=ON` and printed it as a DEVIATION (its own
line in the run log). On top of that, `WANT_SESSION_VIEW` was exercised **both ways in the same
`build/` directory** — the default (`OFF`), then an explicit reconfigure to `ON` — because the
`<session>` defect is specific to the `OFF` configuration and the fix has a branch in each.

### 7.1 `WANT_SESSION_VIEW=OFF` (the default, the shipped configuration)

```
bash tools/local-ci.sh --build-dir build --jobs 4        (JOBS=4)
configure EXIT=0      (deviation printed: -DWANT_QT6=ON)
build     EXIT=0
ctest     EXIT=0      (from build/tests, -j2)
ctest totals: 100% tests passed, 0 tests failed out of 33
    DataFileSaveIntegrityTest   Passed
    ProjectOpenIntegrityTest    Passed
```

### 7.2 `WANT_SESSION_VIEW=ON` (explicit reconfigure, same build dir)

```
cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON \
      -DWANT_QT6=ON -DWANT_SESSION_VIEW=ON
configure EXIT=0      (verified in build/lmmsconfig.h: #define LMMS_HAVE_SESSION_VIEW;
                       WANT_SESSION_VIEW:BOOL=ON in build/CMakeCache.txt)
build     EXIT=0
ctest     EXIT=0      (from build/tests, -j2)
ctest totals: 100% tests passed, 0 tests failed out of 34
    DataFileSaveIntegrityTest   Passed
    ProjectOpenIntegrityTest    Passed
    SessionModelTest            Passed      (built only with the flag ON)
```

Both totals are recorded above rather than one, because they are not the same claim: the OFF run is
the configuration that shipped the defect, and the ON run is the one where `SessionModelTest`'s
byte-identity guard (`preSessionProjectLoadsAndSavesUnchanged`) still holds — i.e. the new
preservation path did not disturb the existing one.

### 7.3 The three gates

```
bash tests/fork-sources-gate.sh              EXIT=0
    scanned 1122 tracked source file(s) under src/, include/, plugins/, tests/
    127 fork-sources entry(ies), 996 inherited upstream, 0 stale entry(ies)
    PASS

bash tests/no-upstream-regression-gate.sh    EXIT=0
    include/Song.h, src/core/DataFile.cpp and src/core/Song.cpp -> "declared divergence"
    (the new tests -> "tests (allowed)")
    PASS: every change to upstream-inherited code since 01148947e is declared (89 files in the ledger)

bash tests/run-all-gates.sh                  EXIT=3   (= PASS-WITH-SKIPS, the expected code)
    gate 1 ctest                PASS   100% tests passed, 0 failed out of 34
    gate 2 coverage             SKIP   (--with-coverage not passed)
    gate 3 no-tautology         PASS   49/37 and 39/36 slots/assertions, 0 tautologies
    gate 4 complexity           PASS   (check mode: no regressions)
    gate 5 mutation             PASS   kill score 88.5% >= 80%
    gate 6 upstream-regression  PASS
    gate 7 file-length          PASS   (check mode: no regressions; both new files under 500 lines)
    gate 8 duplication          PASS   duplicated lines 0.68% (budget 5%)
    gate 9 fork-sources         PASS   127 fork-NEW, 996 inherited
```

Gate 2 is the one skip, so exit 3 is the complete answer here and not a failure — but it is still
incomplete: no coverage ratchet run was recorded for the two new test files on this branch.

### 7.4 One environment note worth passing on

The first full ctest on this branch **hung**: `AudioBusTest` and `AudioBusHandleTest` sat in
`futex_do_wait` for over 15 minutes at ~0.1% CPU and had to be killed. They are not files this
branch touches, they pass in ~2 seconds in `zene-pa-integration`'s log on the same base commit, and
on the re-run above both passed in the 33/34 totals. The machine was at load average ~48 with five
sibling lanes building and running ctest concurrently at the time. Whatever the mechanism, **a
ctest run in this worktree is not reliable while several sibling lanes are testing at once** — a
future lane that sees those two tests hang should re-run rather than debug the audio bus.
