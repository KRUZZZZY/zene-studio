FALSE-POSITIVE

# Cluster gui-1105-main-delete - "Delete tool click leaves the toolbar selection unchanged"

## Verdict

The Delete tool button works. In the GUI sweep (`terra-pilot/buttons/runs/m0-train-25`, step 23), the click at
main-window point (210, 742) did not reach it. It landed on the **floating Inspector window**, which the sweep had
itself created at step 12 ("Pop out the Inspector panel"; step 8 had popped out the Browser). A popped-out panel
opens where it was docked and is a separate top-level window. It stays above the main window. Once the left
region is empty, the main window's middle column moves to x = 0. The Note Editor's Draw, Paint, Delete, Mute and
Slice buttons (x ≈ 150-300) are then under the 480 px wide floating Inspector. The click goes to that window, so
the toolbar does not change. That is correct behaviour for a click on another window. It is not a click-delivery
fault in the toolbar. No product code changed.

Base: `af010bc3cc` (train-26). Branch: `bugs/qwen-gui-1105-main-delete`.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| Each tool's `triggered` calls `setTool` | `src/gui/noteeditor/NoteEditorToolbar.cpp:175` |
| `setTool` stores the tool and checks the matching action in the exclusive group | `src/gui/noteeditor/NoteEditor.cpp:278-287` |
| A pop-out opens at the docked panel's old global position | `src/gui/shell/ShellFloating.cpp:245-247` ("a pop-out never sends the panel far from the eye"), `:260` |
| …at no less than 480 x 320, so the Inspector (240 px docked) floats 480 px wide | `src/gui/shell/ShellFloating.cpp:193-196` (`floatSize`) |
| The floating panel is its own top-level `Qt::Window` | `src/gui/shell/ShellFloating.cpp:248` |
| Sweep step order: 8 = pop out Browser, 12 = pop out Inspector, 23 = Delete at (210, 742) | `.../m0-train-25/results.jsonl` lines 9, 13, 24 |
| Before step 12 the Note Editor toolbar is whole; after it, the left 480 px of the main window is under the Inspector window, and the Song Editor toolbar is cut off at that edge as well | `.../m0-train-25/shots/0012-pair.png`, `0023-pair.png` |

### Reproduced in the real GUI (probe, not committed)

A temporary slot in `ItemListButtonsTest` (saved as `LOGS/probe-ItemListButtonsTest.cpp.txt`) started the full
GUI at the sweep's 1600 x 1000 offscreen size. The output is in `LOGS/probe-2.log` and `LOGS/probe-3.log`:

- Docked layout: `"Delete" true QPoint(451,741) true`. The button's centre hit-tests to the button itself, and so
  does every other toolbar button.
- After `detachPanel("browser")` and `detachPanel("inspector")` (what sweep steps 8 and 12 did):
  `floating inspector QRect(2,87 480x800)`, `hit at sweep (210,742): QWidget "" window "floatingPanel_inspector"`,
  `Delete now at QPoint(209,741) topmost there: "floatingPanel_inspector" false`.

The Delete button is at exactly the sweep's coordinate, and the window on top there is the floating Inspector.

### Not changed, noted

- The floating window placement (old docked position, 480 px minimum width) is the shell's stated pop-out
  design. Changing where pop-outs open is a design question for the owner, not this bug. For the sweep, a
  pop-out step needs a dock-back, or a fresh layout, before later controls are clicked.

## Test (contract, not a RED/GREEN regression)

There is no product defect here, so no fix-RED-GREEN cycle is possible. The commit pins the toolbar's click
contract, which the earlier tests only covered through the keys
(`theNumberKeysPickTheToolsAndDeleteRemovesTheSelection`). The new slot is
`NoteEditorTest::aClickOnAToolButtonSelectsItInTheToolbar` in `tests/src/gui/NoteEditorTest.cpp`. For each of the
7 tool buttons, Delete included, it:

- checks that the button's centre hit-tests to the button;
- sends a real `QTest::mouseClick`;
- asserts that the button is checked, that the exclusive group's `checkedAction()` is that action, and that
  `state().tool` equals the action's tool.

To prove the test is not vacuous, I ran a mutation: `connect(...triggered...setTool)` at
`NoteEditorToolbar.cpp:175` was disabled (`LOGS/mutation.patch`). The test then FAILS at
`NoteEditorTest.cpp:418` (`Compared values are not the same`, `state().tool`). After `git apply -R` it passes
again.

## Commands and results (LOGS = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-delete.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| Build (ccache's temp dir is outside the write-jail: `build-probe-1.log` EXIT=2, `Permission denied`; every later build uses `CCACHE_DISABLE=1`) | `CCACHE_DISABLE=1 cmake --build build --target ItemListButtonsTest -j4 > LOGS/build-probe-2.log` | 0 |
| Probe, docked | `./ItemListButtonsTest zzProbe > LOGS/probe-2.log` | 0 |
| Probe, after pop-outs | `./ItemListButtonsTest zzProbe > LOGS/probe-3.log` | 0 |
| Build with the new test | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > LOGS/build-4.log` | 0 |
| New test (ctest) | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-5.log` (1/1 passed) | 0 |
| New slot alone | `./build/tests/NoteEditorTest aClickOnAToolButtonSelectsItInTheToolbar > LOGS/test-6-slot.log` (PASS) | 0 |
| Mutant build (line 175 connect disabled) | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > LOGS/build-7-mutant.log` | 0 |
| Mutant run - **RED** | `./build/tests/NoteEditorTest aClickOnAToolButtonSelectsItInTheToolbar > LOGS/test-8-mutant.log` (FAIL at :418) | 1 |
| Restore | `git apply -R LOGS/mutation.patch` | 0 |
| Rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorTest -j4 > LOGS/build-9.log` | 0 |
| Family `ctest -R NoteEditor` | `> LOGS/test-10-family.log` - see below | 8 |
| Build NoteEditorFlTest | `> LOGS/build-11.log` | 0 |
| `ctest -R '^NoteEditor(Fl)?Test$'` - **GREEN** (2/2 passed) | `> LOGS/test-12.log` | 0 |
| Gates (see note) | feature_check, `> LOGS/gates-14.log`: 8/8 PASS, RESULT: PASS | 0 |

Family note (`test-10-family.log`):
- `NoteEditorFlTest` showed "Not Run" because it was not built yet. It was built afterwards and passes
  (`test-12.log`).
- `ControlNoteEditor` fails on one check only, "a real instrument is available for the conversion"
  (`devices=[]`). This build tree lacks the instrument plugin that check needs. The commit changes no product
  file, so the `zene` binary that test drives is built from unchanged source. Every other check in it is `ok`.
- I did not run `ControlNoteEditor` on a tree with the plugins built.

Gates note: `feature_check.py` creates its log folder as a sibling of the worktree, which is outside this lane's
write-jail (`gates-13.log`: `PermissionError`, EXIT=1). I ran the same script unchanged, with only that one log
path line swapped for `LOGS/feature-check` (in-memory substitution, asserted to match once). Its gate logic and
baselines were not touched.

## Files committed

- `tests/src/gui/NoteEditorTest.cpp` (new slot `aClickOnAToolButtonSelectsItInTheToolbar`, plus `#include <QActionGroup>`)
- `docs/qwen-fixes-2026-10/cluster-gui-1105-main-delete.md` (this report)
- `tests/fork-sources.txt` (registers this report)

## Self-check

- **Regression FAILS without the change, PASSES with it.** There is no product change, because the verdict is
  FALSE-POSITIVE. The equivalent proof is the mutation run. With the toolbar's `triggered` → `setTool` connection
  (`NoteEditorToolbar.cpp:175`) disabled (`LOGS/mutation.patch`), the new slot FAILS (EXIT=1,
  `LOGS/test-8-mutant.log`, `NoteEditorTest.cpp:418`). After `git apply -R` (EXIT=0) and a rebuild (EXIT=0), it
  PASSES (EXIT=0, `LOGS/test-12.log`, 2/2 tests).
- **Deterministic.** The test uses no wall-clock time, sleeps or races. It sends synchronous
  `QTest::mouseClick`s to each button and reads `isChecked()`, `QActionGroup::checkedAction()` and
  `state().tool` straight after each click. The fixture's `qWaitForWindowExposed` is the existing one in `init()`.
- **Fix at the cause, siblings consistent.** No fix was needed. All 7 siblings in `kTools`
  (`NoteEditorToolbar.cpp:71-76`) go through the same loop and connection (`:165-176`). The test drives every
  one of them and asserts that exactly 7 were found. The key path (`toolForKey`, `:99-106`) is already covered by
  `theNumberKeysPickTheToolsAndDeleteRemovesTheSelection`.
- **No collateral damage.** No product file changed (`git show --stat` lists only the test, this report and
  `tests/fork-sources.txt`), so loading, saved data, limits and realtime paths are untouched. No control command
  was added.
- **FALSE-POSITIVE evidence.** See "Evidence (file:line)" above: `ShellFloating.cpp:245-248,260` and `:193-196`
  (the pop-out opens over the old docked place at 480 px or more). Reproduced: `LOGS/probe-3.log` shows that the
  topmost window at the sweep's (210, 742) is `floatingPanel_inspector`, while the Delete button sits underneath
  at (209, 741). With the panels docked, the button's centre hit-tests to the button (`LOGS/probe-2.log`).
- **Gates.** `feature_check.py` run directly (and `submit_check.py`, which calls it) fails here only with
  `PermissionError` while creating `<worktree>-feature-check`, a sibling of the worktree that is outside this
  lane's write-jail. The same script, with only that log path redirected, reports 8/8 PASS and EXIT=0
  (`LOGS/gates-14.log`). The loop's own run outside the jail should not hit the permission error.
