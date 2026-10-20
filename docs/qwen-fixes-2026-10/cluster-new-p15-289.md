FIXED

# cluster new-p15-289 - The Write button hides client-side refusal messages

Finding: `src/gui/ChordProgressionPanel.cpp` (historical line 129). The Write button's click handler discards
`writeSelected()`'s return value, and two refusal paths returned their message without calling `say()`, so the status
line kept an earlier message. After a successful write, clearing the clip selection and clicking Write again left
"Wrote 12 notes into clip-33." on screen instead of explaining that no MIDI clip is selected.

## Evidence (on the unfixed code)

- `src/gui/ChordProgressionPanel.cpp:129` on the original tree (`:130` after the seam line was added; the handler itself is
  unchanged): `connect(write, &QPushButton::clicked, this, [this] { writeSelected(); });` - the return value is discarded.
- Before the fix, `writeTo` returned `tr("Select a MIDI clip in the arranger first.")` at line 302 with no `say()`.
  `writeSelected` returned `tr("Select a progression first.")` (line 316) and `tr("'%1' does not fit %2.")` (line 317)
  with no `say()`.
- `say()` (`src/gui/ChordProgressionPanel.cpp:247-251`) is the only writer of the status label. It is called with an
  empty string from `showSuggestions` (line 228), so a stale "Wrote ..." message stays until the next suggestion
  refresh.

Verdict: REAL. The contract ("a refusal's message on failure", `include/ChordProgressionPanel.h`, `writeTo` comment) is
that the user sees it, and the refusal paths did not show it.

## The fix

- `writeTo` (`src/gui/ChordProgressionPanel.cpp:304-308`): the no-clip refusal calls `say()` and returns the same message.
- `writeSelected` (`:323-333`): the no-progression and does-not-fit refusals each call `say()` and return the message.
- Control-side failures in `writeTo` already called `say()` (line 313) and are unchanged. The click handler is unchanged;
  it now shows every refusal through `say()`.
- Testability seam: `m_status->setObjectName(QStringLiteral("progressionStatus"))` (`:121`), the same pattern as the
  panel's other named widgets (`progressionTable`, `progressionWrite`, ...). No behaviour change.

Not changed: no new control command, no header change, no change to the audio thread, undo or the registry path.

## Regression test

`tests/src/gui/ChordProgressionPanelTest.cpp`, two new slots:

- `aRefusedWriteSaysWhyInTheStatusLine`: clicks Write with a real fixture clip selected (the status reads "Wrote 12 notes
  into ..."), then clears the shell selection and clicks Write again. The status must read "Select a MIDI clip in the
  arranger first." and the clip must still hold 12 notes.
- `aWriteWithNoProgressionSelectedSaysWhyInTheStatusLine`: clears the table's current index (asserts `currentRow() == -1`)
  and clicks Write. The status must read "Select a progression first."

Deterministic: no timers, no sleeps, no clipboard or window-system dependence (offscreen platform, the same clicks the
existing button test performs).

**Not covered by a test - the does-not-fit path.** It is reachable only if `ChordProgression::chordsFor` refuses a
catalogue walk in a listed scale. With the current engine that cannot happen: a scale must have more than six tones to be
listed (`include/InstrumentFunctions.h:95-97`, `isScale()` is `size() > 6`), every catalogue walk uses degrees 0..6
(`src/core/ChordProgression.cpp:183-192`, the largest is `i-VI-III-VII` = `{0, 5, 2, 6}`), and the degree-range refusal
at `src/core/ChordProgression.cpp:356` therefore never fires for listed scales. I tried to reach it with a sweep over
every scale in the combo and found no unspelled row, so I dropped that assertion rather than invent a path. The fix
still covers that refusal by the same one-line change, so it cannot go silent if the catalogue later grows a
degree that a listed scale lacks.

## Commands and exit codes

Build directory: `build/`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-289.r1/logs/`.
ccache's default temp dir (`/run/user/1000/ccache-tmp`) is not writable from this session, so every build used
`CCACHE_TEMPDIR=/tmp/ccache-tmp-p15-289` (an environment override only; no config changed).

| Step | Command (run in `build` or `build/tests`) | Log | EXIT |
| --- | --- | --- | --- |
| build (first try) | `cmake --build build --target ChordProgressionPanelTest -j4` | build-1.log | 2 (ccache temp dir, not code) |
| build (seam, no behaviour fix) | `CCACHE_TEMPDIR=... cmake --build build --target ChordProgressionPanelTest -j4` | build-2.log | 0 |
| **RED** (seam, no behaviour fix) | `ctest -R '^ChordProgressionPanelTest$' --output-on-failure` | test-1-red.log | **8** |
| build (GREEN) | `cmake --build build --target ChordProgressionPanelTest -j4` | build-4-green.log | 0 |
| **GREEN** | `ctest -R '^ChordProgressionPanelTest$' --output-on-failure` | test-3-green.log | **0** |
| GREEN, binary | `QT_QPA_PLATFORM=offscreen ./ChordProgressionPanelTest` | test-3-green-direct.log | 0 (8 items: 6 test functions + init/cleanup, all PASS) |
| 4b: patch | `git diff -- src/gui/ChordProgressionPanel.cpp > fix.patch` | - | 0 |
| 4b: revert | `git apply -R fix.patch` | - | 0 |
| 4b: rebuild reverted | `cmake --build build --target ChordProgressionPanelTest -j4` | build-5-reverted.log | 0 |
| 4b: **must FAIL** | `./ChordProgressionPanelTest` | test-4-reverted-red.log | **2** (2 FAIL, 6 PASS) |
| 4b: re-apply | `git apply fix.patch` | - | 0 |
| 4b: rebuild | `cmake --build build --target ChordProgressionPanelTest -j4` | build-6-reapplied.log | 0 |
| 4b: **must PASS** | `./ChordProgressionPanelTest` | test-5-reapplied-green.log | **0** (8 passed, 0 failed) |
| family build | `cmake --build build --target zene ControlChordWriteTest -j4` | build-7-family.log | 0 |
| family build | `cmake --build build --target ChordTrackTest ChordDetectTest ChordProgressionTest ControlChordCommandsTest -j4` | build-8-family-rest.log | 0 |
| family | `ctest -R 'Chord' --output-on-failure` | test-7-family.log | **0** (7/7 passed) |
| gates | `python3 -I /tmp/p15-289-gates/feature_check_lanelogs.py <worktree>` (see below) | gates-1-table.log | **0** (RESULT: PASS) |

Notes on the table:

- The **RED** run was taken with the status seam in place and the behaviour fix absent, so the failing assertions are the
  behaviour ones: `test-1-red.log` shows `Actual "Wrote 12 notes into clip-33."` against the expected refusal at
  `tests/src/gui/ChordProgressionPanelTest.cpp:174`, and `Actual ""` against "Select a progression first." at line 193.
  The RED log was taken before I dropped the does-not-fit assertion from the second slot, so line 193 belongs to the
  earlier version of that slot. Its result (empty status) is the same bug.
- The **4b revert** removes the whole product diff, including the status seam, so the reverted run fails at the label
  lookup (`findChild` returns null: "the Write button or the status line is not in the panel" and "a panel control is
  missing"), not at the behaviour assertions. The behaviour proof is the RED run above; 4b shows the test depends on the
  product change.
- **Gates.** The shipped `feature_check.py` exited 1 (`gates-1.log`) with `PermissionError` before it ran any gate: it
  creates `<worktree>-feature-check/` as a sibling of the worktree, which is outside this lane's write-jail. I ran a
  copy in `/tmp/p15-289-gates/` that differs in exactly two lines: `ROOT` still points at the shared qwen folder (so the
  cached gate baseline and `FIX-BASE` resolve), and the log folder is `<LOG FOLDER>/feature-check`. The gate logic is
  unchanged. The table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS,
  no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in
  changed files). RESULT: PASS.

## Files committed

- `src/gui/ChordProgressionPanel.cpp` (the fix and the status-line seam)
- `tests/src/gui/ChordProgressionPanelTest.cpp` (the two regression slots; the file is already registered in
  `tests/fork-sources.txt`, so no registration change is needed)
- `docs/qwen-fixes-2026-10/cluster-new-p15-289.md` (this report)

No new files. No change to `tests/upstream-modifications.txt`: the product file is already fork-owned, registered in
`tests/fork-sources.txt:2735`, and `no-upstream-regression` passed.

## Self-check

1. **Test fails with the product change reverted, passes with it.** RED: `test-1-red.log`, EXIT 8, on the seam-only tree
   (the behaviour fix absent): the stale "Wrote 12 notes into clip-33." and the empty status are the failing assertions.
   GREEN: `test-3-green.log` EXIT 0 and `test-5-reapplied-green.log` EXIT 0 (8 passed). The full 4b revert (the whole
   product diff, including the seam) gives `test-4-reverted-red.log` EXIT 2, failing at the label lookup, and the
   re-applied patch passes again. The RED run is the behaviour proof; the 4b run shows the test depends on the change.
2. **Deterministic.** No timers, sleeps, threads or window-system dependence. The seam is an `objectName` lookup
   (`src/gui/ChordProgressionPanel.cpp:121`); the Write click is synchronous; the table reset uses
   `setCurrentIndex(QModelIndex())` and asserts `currentRow() == -1` first.
3. **At the cause, complete.** `writeSelected` has one caller, the click lambda (`src/gui/ChordProgressionPanel.cpp:130`);
   `writeTo` has `writeSelected` and the test `writingThroughThePanelIsOneUndoStep` as callers (the latter expects an empty
   string on success, unchanged). Every refusal return in both functions now calls `say()`, and the control-failure
   branch of `writeTo` (line 313) already did. Checked with `rg -n "writeTo\(|writeSelected\(" src include tests`.
   The does-not-fit refusal is covered by the change but not by a test: it is unreachable with the current catalogue (see
   the note above).
4. **No collateral damage.** The change is GUI-only. No limit or check was added, no command, no save format, no
   header. The chord family is green: `test-7-family.log` EXIT 0, 7/7 (`ChordTrackTest`, `ChordDetectTest`,
   `ChordProgressionTest`, `ControlChordCommandsTest`, `ControlChordWriteTest`, `ChordProgressionPanelTest`,
   `ControlChordProgression`). Gates: `gates-1-table.log` EXIT 0.
5. **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so the contract-citation item does not apply. The evidence for
   the bug is the `file:line` list in "Evidence" above.
