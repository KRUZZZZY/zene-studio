# Cluster review-cb1ac493e-port: passive focus change rounds stored follow-action probability

**Verdict: ALREADY-FIXED** on `bugs/qwen-review-cb1ac493e-port` (no product change needed in this commit).

## The finding

A passive focus loss or OK with an untouched follow-action chance box (loaded `0.1234`, shown `0.12`)
wrote back `0.12`. Required: only real user input marks a box as edited; a deliberate typed value equal to
the displayed round value still counts; no-input focus navigation keeps the exact stored value.

## Why it is already fixed here

The original fix `fead3e304` was ported and later reworked on the newest line. The rework is in this branch's
history and the reworked code is what HEAD has:

- `89f471cca` (`fix(rework cb1ac493e): passive focus loss counted as a typed chance edit`) is an ancestor of
  HEAD (`git merge-base --is-ancestor 89f471cca HEAD` → ancestor). It changed the edit flag from
  `QAbstractSpinBox::editingFinished` to `QLineEdit::textEdited` on the spin box's own editor.
- `src/gui/FollowActionEditor.cpp:98-113` (`numberBox`): the edit flag is set only from
  `QLineEdit::textEdited`, found via `box->findChild<QLineEdit*>()` (comment at lines 104-108).
- `src/gui/FollowActionEditor.cpp:116-122` (`valueOf`): an edited box returns `box->value()`; an untouched box
  returns the loaded value while it still shows the value it was loaded with.
- `tests/src/gui/FollowActionEditorTest.cpp:62-84` `anExplicitChanceEditIsWritten`: typing `0.12` (the displayed
  value) is written as `0.12`.
- `tests/src/gui/FollowActionEditorTest.cpp:86-103` `aPassiveFocusLossKeepsTheLoadedChance`: an untouched box
  emitting `editingFinished` keeps `0.1234`.

The original `fead3e304` test (`focusTraversalAndOkKeepTheLoadedChance`, with real `setFocus`/`Key_Return`
traversal) is not in this line. The rework replaced it with the `emit chance->editingFinished()` simulation
because a real focus change crashes in the offscreen D-Bus accessibility bridge (comment at
`FollowActionEditorTest.cpp:92-93`). Real typing is covered by `anExplicitChanceEditIsWritten`.

## RED / GREEN evidence

The RED was made by temporarily reverting `numberBox` to the pre-fix
`QObject::connect(box, &QAbstractSpinBox::editingFinished, ...)` edit flag (edit of the working tree, not a
patch file), then restored by hand. The tree is clean after the restore (`git status --short` empty).

| Step | Command (cwd `build/tests` for ctest) | Log | EXIT |
| --- | --- | --- | --- |
| Build, first try (ccache temp dir under `/run/user` denied) | `cmake --build build --target FollowActionEditorTest -j2` | `build-1-green-head.log` | 2 |
| Build, retry with ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target FollowActionEditorTest -j2` | `build-2-green-head.log` | 0 |
| GREEN baseline (HEAD) | `ctest -R '^FollowActionEditorTest$' --output-on-failure` | `test-1-green-head.log` | 0 (1/1 passed) |
| RED: pre-fix connection | build as above → `build-3-red-buggy.log` (EXIT 0); `ctest -R '^FollowActionEditorTest$' --output-on-failure` | `test-2-red-buggy.log` | 8 |
| RED failing assertion | `FAIL! : FollowActionEditorTest::aPassiveFocusLossKeepsTheLoadedChance() Compared doubles are not the same (fuzzy compare)`; `Totals: 3 passed, 1 failed` | `test-2-red-buggy.log` | |
| Fix restored: rebuild | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target FollowActionEditorTest -j2` | `build-4-green-restored.log` | 0 |
| GREEN after restore | `ctest -R '^FollowActionEditorTest$' --output-on-failure` | `test-3-green-restored.log` | 0 (100% passed, 1/1) |
| Test function count (GREEN) | `QT_QPA_PLATFORM=offscreen build/tests/FollowActionEditorTest -v2` | `test-4-green-verbose.log` | 0 (`Totals: 4 passed, 0 failed`; both test functions PASS) |

The affected family is `FollowActionEditorTest`: 1 ctest test (`#361`), executing 4 Qt test slots including
`initTestCase`/`cleanupTestCase`, of which the two real tests both pass on GREEN.

## Ratchet gates (run on this tree, which equals HEAD + this report)

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check` | 1 | FAIL: `complexity ratchet regressed` for `dotCentreAlong` (XyPadPairTest.cpp, CCN 16), `parseNotes` (DawProjectReadTracks.cpp, CCN 13), `adjust` (ControlCommandsMixerTools.cpp, CCN 12), `setStrip` (ControlCommandsMixerStrip.cpp, CCN 11), `rotateProjectRevision` (ProjectRevisions.cpp, CCN 11), `importedOverrides` (ControlCommandsShortcuts.cpp, CCN 11). Log: `gate-1-complexity.log`. |
| File length | `bash tests/file-length-gate.sh --check` | 1 | FAIL: `file-length ratchet regressed` for 17 files over 500 lines, incl. `ArrangerTimelineTest.cpp` (689), `DeviceDspTest.cpp` (752), `SmfInterchangeReader.cpp` (504), `TakeLane.cpp` (520). Log: `gate-2-file-length.log`. |

None of the failing entries is `FollowActionEditor.cpp` or `FollowActionEditorTest.cpp`; none is in this
commit. They are pre-existing on this line (the tree has no product change from HEAD, and e.g.
`ArrangerTimelineTest.cpp` was merged in `337ee790d`). The gate baselines are not mine to change, and
fixing 6 CCN functions and 17 file splits is outside this single-bug port, so I did not touch them.

**Gate status: NOT PASSING, for reasons outside this cluster.** This needs an owner or line-level decision on
the baseline / the listed functions and files before the line's gates can be called green.

## Files changed in this commit

- `docs/qwen-fixes-2026-10/cluster-review-cb1ac493e-port.md` (this report) only.
- No product or test file changed; `src/gui/FollowActionEditor.cpp` is back to HEAD (verified by
  `git status --short` being empty after the RED/GREEN cycle).

## Notes

- No new file was created, so `tests/fork-sources.txt` / `tests/upstream-modifications.txt` were not touched.
- The clang-tidy note on `FollowActionEditor.cpp:57` (`modernize-avoid-c-arrays`) is in the existing
  `Column`/action table and is pre-existing, not part of this fix.
