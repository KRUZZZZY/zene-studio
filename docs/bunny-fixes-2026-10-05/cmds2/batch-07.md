# cmds2 batch-07 — 2026-10-05

Six findings, five distinct defects. Two fixed, one duplicate of a fix in this
batch, one already handled in the tree, one false positive, one out of scope.

### Cluster 5742 — MIDI export can report a stale file as a successful export

Verdict: FIXED

Evidence: `src/core/ControlCommandsProjectLifecycle.cpp:220-234` (pre-fix) called
`Engine::getSong()->exportProjectMidi(path)` and then treated
`QFileInfo::exists(path)` as proof of the write. `src/core/Song.cpp:2438-2452`
writes nothing at all when the `midiexport` `ExportFilter` fails to load and
reports that only through `qDebug()`, so a pre-existing `.mid` passed the check
and the handler reported the STALE file's byte count as a fresh export. The
handler now exports to `path + ".zene-export.tmp"`, verifies that temporary
output exists, then `QFile::remove(path)` + `QFile::rename(temp, path)` and
reports the byte size of the output it proved was written; a missing temporary
is a refusal that leaves the previous export intact.

Commit: 354232bbf

Test: ControlSuccessPathsTest::exportMidiDoesNotReportAStaleFileAsAFreshExport

Red/green:
- Red: `cmake --build build --target ControlSuccessPathsTest zene` EXIT=0;
  `cd build/tests && ctest -R '^ControlSuccessPathsTest$'` EXIT=8 —
  `FAIL! ... 'project.export_midi reported success for the pre-existing file'`,
  preceded by `QWARN ... The plugin "midiexport" wasn't found or could not be
  loaded!` and `QDEBUG ... failed to load midi export filter!`
- Green: same build EXIT=0, same ctest EXIT=0 (12 passed);
  `ControlSurfaceReferenceTest`, `ControlResultSchemaTest`,
  `ControlVerbInverseTest` together EXIT=0
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 4832 — Project merge overwrites a file but is registered as non-mutating

Verdict: SKIPPED-SCOPE

Evidence: the observation is accurate —
`src/core/ControlCommandsProjectMmpzGit.cpp:286` sets `cmd.mutating = false`
while the handler writes the merged document to `ours`. But `mutating` is not
free to flip: the A16 reversibility row at
`src/core/ControlReversibilityTableMmpzGit.cpp:52` classifies `project.merge` as
`RC::NotMutating`, and `tests/src/core/ControlSurfaceReferenceTest.cpp:184-189`
asserts that any command the table classes `NotMutating` must have
`!cmd->mutating`. Correcting the flag therefore requires a contract-table change
and a new undo payload for a byte-for-byte file overwrite, which is rule 8
(schema/contract surface, parent's canary) rather than a defect-local fix.

Commit: none

Test: none

Red/green: none

### Cluster 5748 — Conflict and audible-diff commands treat unexpected exit codes as success

Verdict: FIXED

Evidence: `src/core/ControlCommandsProjectMmpzGit.cpp` `registerProjectConflicts`
and `registerProjectAudibleDiff` each refused only exit 2. `tools/mmpz-git/mmpz_git.py`
documents 0 and 1 for `cmd_conflicts` (lines 1324-1328) and 0/1/2 for
`cmd_audible_diff` (2 for renderer/input errors, lines 1585-1650 and 1717), so
any other status — a tool that died — fell through to the success path and
answered `conflict_count: 0` or `differ: false`. Both handlers now route every
other status through the existing `toolFailed` helper (same file, lines 218-225),
which accepts only 0/1. Exit 2 keeps its tailored message.

Commit: 6ea3b2719

Test: ControlSuccessPathsTest::mmpzToolExitsWithAnUndocumentedCode (points
`MMPZ_GIT_TOOL` at a script that exits 3, which `findMmpzGitTool` honours ahead
of the discovered tool)

Red/green:
- Red: `cmake --build build --target ControlSuccessPathsTest zene` EXIT=0;
  `cd build/tests && ctest -R '^ControlSuccessPathsTest$'` EXIT=8 —
  `FAIL! ... 'project.conflicts reported success for an undocumented tool exit'`
- Green: same build EXIT=0, same ctest EXIT=0 (12 passed);
  `ControlSurfaceReferenceTest`, `ControlResultSchemaTest`,
  `ControlVerbInverseTest` together EXIT=0
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 4833 — Conflict reporting treats unexpected tool exits as success

Verdict: DUPLICATE

Evidence: same site and same defect as cluster 5748 —
`src/core/ControlCommandsProjectMmpzGit.cpp` `registerProjectConflicts`, refusing
only exit 2. A narrower restatement of the same line; fixed and regression-tested
once, under cluster 5748.

Commit: 6ea3b2719 (same fix)

Test: ControlSuccessPathsTest::mmpzToolExitsWithAnUndocumentedCode

Red/green: same commands and EXIT codes as cluster 5748

### Cluster 5734 — Punch regions never gate recording

Verdict: FALSE-POSITIVE

Evidence: the capture path does consult the gate. `src/core/AudioEngine.cpp:556`
builds `const auto gate = [this](f_cnt_t frames) { return punchWindow(frames); };`
and applies it on both the wide-input and the stereo capture path (lines 551-575);
`punchWindow` / `setPunchWindow` are at lines 277-291.
`src/core/Song.cpp:186-192` publishes the song's punch region to the engine on
`punchChanged`, and `src/core/RecordOptions.cpp:137,153` do the same for a
recording configured from options. `tests/src/core/PunchWindowTest.cpp` covers
the gate, and `docs/KNOWN-LIMITATIONS.md:821-823` was corrected on 2026-09-30 to
say the capture path has consulted it since R2.4. The report quotes the stale
comment, not the code.

Commit: none

Test: none (existing `PunchWindowTest` covers the wired behaviour)

Red/green: none

### Cluster 2904 — Huge punch positions are converted before they are range-checked

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsPunch.cpp:128-131` — `clampedTicks` applies
`std::clamp(args.value(key).toDouble(), -1.0, MaxSongLength + 1.0)` and only
then performs `static_cast<tick_t>(...)`, so the double is bounded before the
floating-to-integer conversion. `parsePunchRange` (lines 135-152) then refuses
negatives and `end > MaxSongLength`, where `MaxSongLength = 9999 *
DefaultTicksPerBar` (`include/Song.h:77`). A value such as `1e300` is clamped to
`MaxSongLength + 1.0` and refused; it never reaches an undefined conversion.

Commit: none

Test: none

Red/green: none

Leftovers: the punch group still describes itself as unwired —
`src/core/ControlCommandsPunch.cpp:19-24` (file header) and `cmd.description` at
lines 245-247 say no capture path consults the region, and the older
`docs/KNOWN-LIMITATIONS.md` bullet at lines 825-830 repeats it. Both contradict
the wiring cited above and are the actual source of cluster 5734, but they are
documentation of a code path this batch is forbidden to change, so they are left
for the parent to correct in the same pass as the `punch.*` description work.

## Totals

FIXED 2 · DUPLICATE 1 · ALREADY-FIXED 1 · FALSE-POSITIVE 1 · SKIPPED-SCOPE 1
