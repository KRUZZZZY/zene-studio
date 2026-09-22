# S9 — warp native (ARCH-4 slice, SPEC-ARCH-4 census row 5) — evidence log

Lane: `zene-s9`, branch `040/arch4-s9`, base tip `9d71f47be`. All exit codes below are
unpiped (`cmd > file 2>&1; echo EXIT=$?` style; the build/cmake lines echo `$?` directly).

## 1. Brief re-verified before editing (the spec cites had drifted — both ranges re-read)

| Claim | Re-verified at this tip |
|---|---|
| census row 5 at spec `:506` | `design/specs/SPEC-ARCH-4-DOCUMENT-MODEL-DRAFT.md` line ~506: "`<warp>` is a child of `<sampleclip>` only (`src/core/SampleClip.cpp:498-527`) … Warp moves to the base clip (the seam is already a `Clip` virtual, `include/Clip.h:234`, `:238`)" — present |
| migration row at spec `:405` | upconversion table row "`<warp>` child of `<sampleclip>` + `<marker src pos>` → `<z:warp>` on the base clip; `src/core/SampleClip.cpp:498-527`" — present |
| Q3 recommendation spec `:608-611` | "Warp scope: base `Clip`, or audio clips only? *Recommendation: base `Clip`.*" — present; **followed** (no measurement refuted it: the mapping seam at `Clip.h:234/:238` is real and unchanged) |
| `SampleClip.cpp:498-527` = the `<warp>` save block | exactly so at base tip (save `if` at :503, `createElement("warp")` at :506, `appendChild` at :526) |
| `Clip.h:234` / `:238` | `virtual f_cnt_t sourceFrameAt(...)` / `virtual TimePos timelinePosAt(...)` — both present |

Proof artefacts re-verified (as the brief demanded):
- `tests/src/core/ClipWarpPersistenceTest.cpp` exists — `ls` EXIT=0 (and was built + run below).
- render rate proof in `docs/WARP.md` exists — `grep -c "rate\|render" docs/WARP.md` → **65** (EXIT=0);
  the harness `tests/data/warp/render-proof.sh` exists (`ls` EXIT=0). NOT re-run in this lane:
  it needs TWO full `lmms` binaries (`--base` + `--warp`) and a second build tree, both barred
  by the targeted-build / one-build-dir rules — see `unverified:` in the lane report.

## 2. What moved (the change)

- `include/WarpMarkers.h` (+40): the `WarpTempoMode` / `WarpStretchMode` enums, moved
  verbatim from `SampleClip.h` — the warp vocabulary's new home (this file is S9-owned).
- `include/Clip.h` (+37, lines **35-37** = the `WarpMarkers.h` include, lines **243-276** =
  the seam block after `timelinePosAt`: doc comment, `protected:` four warp members +
  `saveWarp`/`loadWarp` declarations, `public:` reopened) — the ONLY Clip.h edits, for s7.
- `src/core/Clip.cpp` (+148): `Clip::saveWarp` (the child writer, formerly
  `SampleClip::saveSettings:498-527`), `Clip::loadWarp` (the reader + reset-on-absence
  else-branch, formerly `SampleClip::loadSettings:589-635`), both called from
  `saveState`/`restoreState`; the copy constructor now copies the four warp members.
- `src/core/SampleClip.cpp` (−68 net): the save block, the load block and the four copy
  initializers removed; the mapping/authoring code now reads inherited members.
- `include/SampleClip.h`: enums + members removed (pointers left behind); API unchanged.

Behaviour-preservation decisions (each measured or argued at file:line):
1. **Byte order**: `saveWarp` does `element.insertBefore(warp, element.firstChild())` —
   `[warp][journallingObject]` is exactly what the old writer produced (`SerializingObject::saveState`
   runs `saveSettings` first, `JournallingObject::saveState` appends the journal node after), so a
   resave of an existing warp project does not reorder. `contentSignature()` (`ProjectIds.cpp:124`)
   excludes `<journallingObject>` anyway, but the human-diffable bytes stay put too.
2. **Revision fingerprint**: `saveWarp` runs before the `isDocumentElement` block, so
   `ProjectIds::writeRevision` still stamps last over the warp bytes — as when saveSettings wrote them.
3. **Copy payloads**: `loadWarp` runs UNCONDITIONALLY before the copy-payload guard — the old
   reader (in `loadSettings`) ran for every payload, so paste/clone keep their warp.
4. **Order vs `loadSettings`**: `loadWarp` runs right after `JournallingObject::restoreState`,
   i.e. after `len/off/srcin/srcout/autoresize` and after `setSampleFile()` (which clears the map
   when a source changes) — the exact order the old inline code had.
5. **API scope**: the authoring setters stay on `SampleClip` in this slice — each announces through
   `sampleChanged` (a SampleClip signal); the census row cites only the element's placement. State a
   follow-up if the parent wants the setters on the base too (needs a change-notification virtual).

## 3. Runs (unpiped exit codes)

| Step | Command | EXIT |
|---|---|---|
| disk | `df -h /home` before configure | 69G free (floor 12G) |
| configure | `bash tools/local-ci.sh --configure-only --build-dir build` (see `configure-run.md`) | 0 (provision 0) |
| baseline build (8 targets, `-j2`) | `cmake --build build --target <8 warp/clip tests> -j2` | 0 |
| baseline ctest | `ctest -R <8>` from `build/tests`, env inline (see `baseline-ctest.md`) | **0 — 8/8 passed** |
| post-change build | same 8 targets, `-j2` | 0 |
| post-change ctest | same run (see `postchange-ctest.md`) | **0 — 8/8 passed** |
| new test, direct | `./ClipWarpPersistenceTest` (see `clipwarppersistence-slots.md`) | 0 — 6/6 slots incl. `restoringAnElementWithoutAWarpUnwarpsTheClip` |
| NEGATIVE CONTROL build | reset else-branch in `Clip::loadWarp` neutered → rebuild 2 catchers | 0 |
| NEGATIVE CONTROL ctest | `ctest -R "^(ClipWarpPersistenceTest|ControlWarpCommandsTest)$"` (see `negative-control.md`) | **8 — 2/2 FAILED, as owed** |
| revert build + ctest | reset restored → rebuild 8 → `ctest -R <8>` | build 0, **ctest 0 — 8/8 passed** |
| widened sweep build | +9 round-trip targets (`ClipEdits/ClipLink/ClipLinkPersistence/TakeLaneComp/TakeLane/SampleClipWindow/StableTrackIds/ProjectOpenIntegrity/MmpzGitDepth`) | **2 — explained: `gmake: No rule to make target 'MmpzGitDepthTest'` — that ctest is `python3 tools/mmpz-git/tests/test_mmpz_git.py`, i.e. NOT a build target (`ctest -N -V -R ^MmpzGitDepthTest$` shows the python command); every compiled target shows `Built target`, zero error lines in `build/build-sweep.md`** |
| widened sweep ctest | 17 tests (the 8 above + the 9 new), same env, from `build/tests` (see `sweep-ctest.md`) | **0 — 17/17 passed**, incl. MmpzGitDepthTest's byte-identity fixtures (92.9 s) |

The control: with the reset neutered, `ClipWarpPersistenceTest::restoringAnElementWithoutAWarpUnwarpsTheClip`
AND `ControlWarpCommandsTest::markerEditsAreReversibleThroughTheJournal` both fail — absent `<warp>`
MUST mean unwarped, and the mechanism that makes undo work is precisely that else-branch
(`Clip::loadWarp`), replayed by the journal checkpoint. Reverted; tree diff back to intended state.

Test target set (8): ClipSerialisationTest, WriteRefusalGateTest, ClipWarpPersistenceTest,
ControlWarpCommandsTest, ProjectRevIdsTest, WarpMarkersTest, SampleClipStretchTest, SampleClipWarpTest.
ctest always run from `build/tests` with `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$PWD:$PWD/.."` inline.

## 4. Known-stale, deliberately NOT touched (follow-up for the parent)

Agent-surface description strings still name the old writer/reader (harmless claims about the
mechanism, but the file/function attribution is now stale; `tests/control-commands-snapshot.py`
may snapshot them, so rewording = separate change + snapshot regen):
- `src/core/ControlCommandsWarp.cpp:151-153`
- `src/core/ControlCommandsWarpEdit.cpp:375-376`
- `src/core/ControlReversibilityTableLive.cpp:203-204` and `:227-228`
Not stale (verified): `ControlReversibilityTableSample.cpp:68-69` is about `src`/`data`, still
SampleClip's. `include/Clip.h:266-267` (saveClipEdits comment mentioning `<warp>` as a
`SampleClip::saveSettings` example) sits in s7's region — left alone, flagged in the lane report.
Line-cites into SampleClip.cpp that MY shift moved were refreshed where they were true before:
`docs/WARP.md` (`:171`→`:170`) and `tests/src/core/WriteRefusalGateTest.cpp` (`:399-400/:419-420`
→ `:397-398/:417-418`, header + marking string). Other pre-existing stale cites
(`ControlProjectAssets.h:52` was already off before this lane) left alone.

## 5. Registration

No new files were created anywhere in the tree — `tests/CMakeLists.txt` needed no new entry
(the new test case lives inside the registered `tests/src/core/ClipWarpPersistenceTest.cpp`,
already in `tests/all-sources.txt:1620`), so `tests/fork-sources.txt` / `tests/all-sources.txt`
are unchanged; Gate 9's reproduce check is run below to prove it. The four extended
`tests/upstream-modifications.txt` rows each carry exactly one `040/arch4-s9` clause
(`grep -c 040/arch4-s9` → 4) — extended in place, no duplicate path rows.

## 6. Gates (run after staging; outputs appended below)

see `gates.md` in this directory.
