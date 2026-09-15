# The in-app revision timeline — what was built, and what was measured

Feature-list **row 76** (OWNER-31 item 30), board task **#665**, lane `030/revision-timeline`
(worktree `zene-030/wrev`). Base `797ee7afe`, the wave-1 integration tip.

## 1. The finding that shaped the work

The row's premise is that the artefacts a timeline would list **already exist on disk**, and the
panel over them is the work. Checking that first (as the task spec required) found it true, with one
correction worth recording:

| artefact | who writes it | what the row says | what is actually there |
|---|---|---|---|
| `<file>.rev0` .. `<file>.rev2` | `project.save` → `control::rotateProjectRevision` (the A16 keep-3 policy, `include/ProjectRevisions.h`) | *not named* | a real revision SET, with per-revision paths, sizes and sha256, capped at 8 MiB each — added by the A16 lane after the row was written |
| `<file>.bak` | `DataFile::writeFile` (`src/core/DataFile.cpp:391`), on **every** save from the interface | named | exactly as described — one revision, replaced on the next save |
| `recover.mmp` (+ `recover.mmp.bak`) | the periodic autosave (`MainWindow::autoSave`) | named ("the autosave sidecar") | the recovery file is a **SESSION** artefact (one per working directory), and its `.info` sidecar is what says which project it came from |
| the commits that touched the file | the user's own git repository (`tools/mmpz-git` is the filter/diff/merge tooling) | named | reachable read-only through one bounded `git log` |

**No new store was written and no project format changed.** The work is the list/compare/restore
surface: `include/RevisionTimeline.h` and its three translation units
(`RevisionTimeline.cpp`, `RevisionTimelineGit.cpp`, `RevisionTimelineCompare.cpp`).

## 2. The surface

Group `revisions`, three ids, registered from `src/core/ControlRegistryRegistrations.cpp` (declared in
`include/ControlRegistryGroups.h` because `include/ControlRegistry.h` sits at the file-length ratchet):

* **`revisions.list`** `{project?, include_git?}` → `{file, count, sources{}, revisions[],
  git{in_repository, listed, reason, sizes_measured}}`. One entry per artefact, newest first, each with
  `id`, `source` (`rotation` | `backup` | `autosave` | `git`), UTC `timestamp`, `bytes`, `path`
  and `sha256`. An autosave is listed for a project only when its sidecar names that project (the rule
  `ProjectRecovery::decideRecovery` already applies to the startup prompt). The git half is bounded
  (2500 ms) and optional in every direction, and says which.
* **`revisions.compare`** `{project?, a, b?}` → each side's metadata + `comparison`: `identical`
  (bytes), `readable`, element counts per tag for both sides, and the differing tags. **Structural, not
  semantic**: `mmpz-git diff` owns the musical diff and is not re-implemented here. `b` defaults to the
  working file, the id `live`.
* **`revisions.restore`** `{project?, id}` → the file on disk is replaced *after* the live file is
  rotated into the keep-3 set, so `control.undo` restores revision 0 (or removes the file the restore
  created when there was none). Refused typed, before any write, when the id is unknown, the artefact
  is gone, or the live file is over the policy's 8 MiB per-revision cap.

A16 (`src/core/ControlReversibilityTableRevisions.cpp`, joined into the table): two `not_mutating`
reads and one recorded-action `true_inverse` writer. **No irreversible row** — the one path that could
be one is the typed refusal above. The documented histogram moves with the rows: base
`263/141/21/7/94` → `266/142/21/7/96` (release config `265` → `268`), updated in the test and in
`docs/RELEASE-NOTES-v0.3.0-alpha.md` in the same commit, which is what
`ReversibilityContractTest::theTableHistogramIsTheDocumentedOne` asserts.

## 3. What was proved, and with what

**The registered proof.** ctest **`RevisionTimelineTest`** (`tests/src/core/RevisionTimelineTest.cpp`,
registered in `tests/CMakeLists.txt`) — a real project document, a real `.bak`/`.rev0`/autosave+sidecar
fixture in a temp directory, a real git repository with a commit, and all three ids driven through
`ControlRegistry::invoke` (the path a socket client takes):

```
$ ctest -R RevisionTimelineTest --output-on-failure          # from build/tests
1/1 Test #83: RevisionTimelineTest .............   Passed    1.42 sec
100% tests passed, 0 tests failed out of 1
```

**The A16 anti-drift tests, against a build carrying the three new rows:**

```
$ ctest -R Reversibility --output-on-failure
1/2 Test #81: ReversibilityContractTest ........   Passed    1.40 sec
2/2 Test #82: ReversibilityUndoTest ............   Passed    1.63 sec
100% tests passed, 0 tests failed out of 2
```

**The socket surface**, against the real `zene` binary started with `--control-socket` (a throwaway
transcript, not committed — the registered proof above is the ctest):
two real `project.save` calls produced `socket-song.mmp.bak` and `socket-song.mmp.rev0`; `revisions.list`
reported both with their sources, times and (equal) hashes; `revisions.compare` of `rev0` against `live`
reported 11 differing tags and an element delta of 11; `revisions.restore id=backup` restored 15079 bytes
and left the replaced 17986-byte document as revision 0; `control.transactions` showed the record with
`"class": "true_inverse"`, `"reversible": true` and the mechanism text; `control.undo` answered
`"undone_command": "revisions.restore"`; and the refusals were typed (`not_found` for an unknown id,
`invalid_args` for a session with no project and for a non-boolean `include_git`).

**The agent-surface gate** (SPEC A15), which sweeps every registered command headless:

```
reverse    : 268 commands, 267 swept, 1 allowlisted, 0 compiled out
   revisions.compare            typed_error   0.0s  not_found
   revisions.list               ok            0.1s  args={}
   revisions.restore            typed_error   0.0s  invalid_args
PASS: agent surface gate (reflection + ratchet + reverse completeness + headless sweep)
```

**The gates** on this lane's files: `complexity-gate --check` 0 findings in them (two functions had to
be split - an 11-CCN scanner and an 11-CCN `findRevision`); `duplication-gate --check` PASS (0.32% of a
5% budget); `file-length-gate --check` adds no new violation (my largest file is 435 lines);
`fork-sources-gate.sh` PASS; `no-upstream-regression-gate.sh` PASS with the two inherited edits
(`src/core/CMakeLists.txt`, `tests/CMakeLists.txt`) declared in the same commit.

## 4. What is NOT proved, and the known red

* **`ControlCommandsSnapshot` is RED, with exactly three ids** — `revisions.list`,
  `revisions.compare`, `revisions.restore` — because the MCP bridge's committed offline list is a
  derived file that is **regenerated ONCE, after the last command-group merge, from a live instance of
  the merge tip** (`tools/mcp-zene-control/snapshot_commands.py --socket <sock>`; never by hand). This
  lane is not the last merge: the wave-2 integration train is merging other groups at the same time, so
  the regeneration belongs to the integrator. The test names the fix itself, and its report is exact:
  `MISSING from the snapshot 3 ... 0 missing, 0 extra` on the other two checks.
* **No GUI, and that is the release's decision, not a gap in this lane**: `grep -rniI 'RevisionTimeline|
  revisions.list|revisions.restore|RevisionEntry' src/gui/` returns **0** hits. There is no revision
  panel, no timeline strip and no "restore this revision" entry; `docs/KNOWN-LIMITATIONS.md` and
  `docs/RELEASE-NOTES-v0.3.0-alpha.md` carry the same one-line absence.
* **Not verified here**: the other six CI jobs (`linux-arm64`, `mingw64`, both `macos-*`, `msvc-x64`,
  `windows-arm64`) — everything above is Linux x86_64, gcc, Qt 6, `WANT_QT6=ON`. The git half is
  skipped by name where no `git` is on the machine, which is the case the code reports rather than
  fails.
* **The comparison is structural.** Two documents that differ only in attribute VALUES (a tempo, a
  volume) are reported as `identical: false` with **no** differing tag counts. That is the honest
  reading of an element-count comparison, and it is why the release notes say the semantic diff is
  `mmpz-git`'s.
