IMPLEMENTED

# Q050-227.1 - Persistent mixer zones (stage 1/2)

Condensed report for the queue unit. Full behaviour and design notes: `docs/050-features/Q050-227.1.md`.
Manual: `data/manual/index.html` (the paragraph after the mode-bar paragraph).

## What changed

- `include/MixerZones.h`, `src/gui/MixerZones.cpp` (fork-new): `mixerzones::Layout` = docks + sizes, the
  portable JSON form, the strict argument parser and the one live `MixerZoneState`.
- `src/gui/MixerView.cpp`, `include/MixerView.h` (inherited, ledger rows added): the fixed `QHBoxLayout`
  body is now one horizontal `QSplitter` of dock hosts (`master`, `channels`, `vca`, `rack`), with
  `relayoutZones()` and `writeZoneSizes()`. The channels' host keeps the scroll area and the New/Return
  buttons together; master-first and the rack stack order are unchanged.
- `src/gui/shell/Shell.cpp`: `snapshot()` gains `mixer_zones`, `restoreSnapshot()` reads it (absent -> the
  default arrangement).
- `src/gui/ControlCommandsMixerView.cpp`: new `window.mixer_zones` command (`dock`, `zones`, `sizes`,
  `reset` -> `zones`, `sizes`, `mixer_zones`).
- `src/gui/MixerModeBar.cpp`, `include/MixerModeBar.h`: a **Zones** chip whose menu docks each region
  left/right and resets, every entry declaring and running `window.mixer_zones`.
- `src/core/ControlReversibilityTableMixing.cpp`: one `not_mutating` row for `window.mixer_zones`.
- `src/gui/ControlCommandsLayouts.cpp`: `window.layout_project_save`'s `apply` now restores the stored
  layout on screen, so its already-`true_inverse` step is honest in the interface as well as in the project.
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`: A16 histogram re-taken to
  `rows=888 true_inverse=434 snapshot=42 irreversible=25 not_mutating=387` (one new row).
- `README.md`: canary re-measured to `879 ids / 77 groups`.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json`: regenerated live from this build.
- `tests/src/gui/MixerZonesTest.cpp` (fork-new, registered in `tests/CMakeLists.txt`,
  `tests/fork-sources.txt`, `tests/all-sources.txt` re-derived from its own recipe).

Two things I had to change to keep the mechanism honest, and neither is cosmetic:

1. `window.layout_project_save`'s `apply` was empty. The command was `true_inverse` and wrote the project
   part back correctly, but undoing it left the window showing the layout the user had just undone. It now
   restores the stored layout on the shell, which is what the transaction text already claimed.
2. `Shell::restoreSnapshot` had to *reset* the zones when the document has no `mixer_zones`, not leave
   whatever was on screen. Otherwise "a project without the new fields loads with a usable default layout"
   is only true if the mixer happened to be on its default already.

## Acceptance

Commands run from the worktree; logs in `/tmp/q050-227.1-logs/` (the lane's usual log directory
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-227.1.r1/logs` is not writable in this session: the home
directory is read-only, so `build_guard.py` cannot take its lease either - builds ran `/usr/bin/cmake`
directly, still at `-j4`).

| # | Criterion | Test | Exit code |
| - | --- | --- | --- |
| 1 | At least two mixer regions can be docked in supported positions and separators can be resized in the UI | `MixerZonesTest::twoRegionsDockInSupportedPositions`, `::aSeparatorDragResizesAZone` | 0 (`ctest -R '^MixerZonesTest$'` -> `ctest-green-4.log`); red 8 |
| 2 | Saving and reopening the project layout restores dock positions and separator sizes | `::savingAndReopeningTheProjectRestoresTheDocksAndTheSizes` (real `saveProjectFile` / `loadProject` of a real `.mmp`), `::theLayoutSaveIsOneUndoStepForTheWholeArrangement` | 0; red 8 |
| 3 | Projects without the new layout fields load with a usable default layout | `::theDefaultArrangementIsTheFixedOneTheMixerHad`, `::aLayoutWithoutTheNewFieldsOpensWithTheDefault` | 0; red 8 |
| GUI/agent parity | Every user-facing action has a GUI entry on the same registered command | `::everyZonesMenuEntryDeclaresTheCommand`; the criterion-1 case triggers the built menu action | 0; red 8 |
| A16 | One undo step per edit, honest class | `ReversibilityContractTest::theTableHistogramIsTheDocumentedOne`, `::everyRegisteredCommandIsClassified`; `::theLayoutSaveIsOneUndoStepForTheWholeArrangement` | 0 (`ctest-11.log`) |

Exact commands and logs:

- `ctest -R '^MixerZonesTest$' --output-on-failure` -> **exit 0**, 9 slots, 8 passed + `cleanupTestCase`,
  `ctest-green-4.log`. Earlier green run `ctest-9.log`.
- `ctest -R '^(MixerZonesTest|MixerModesTest|MixerMeteringTest|MixerSelectionIdentityTest|MixerSendsOnFadersTest|MixerStripFeaturesTest|MixerWindowsTest|ShellTest|VcaStripViewTest|ReversibilityContractTest|ControlCommandsSnapshot)$'`
  -> **exit 0**, 11/11 (`ctest-14.log`). An earlier run of the same set before the histogram was re-taken
  was 9/10 in `ctest-10.log` (the A16 failure); re-run alone after the notes change,
  `ReversibilityContractTest` was **exit 0** (`ctest-11.log`).
- `ctest -R '^(AccessibilityWalkTest|CustomAccessTest|MixerInsertLatencyTest|VcaStripLifetimeTest|MixerZonesTest)$'`
  -> **exit 0**, 5/5 (`ctest-13.log`) - the suites that walk the mixer's widget tree or outlive a strip.
- `python3 tests/control-customisation.py build/zene` -> **exit 0**, every check held
  (`python-customisation.log`): the project-layout save/apply/clear/undo path the zones travel on still
  behaves end to end over the control socket.
- `ctest -R '^ControlCommandsSnapshot$'` -> **exit 0** (`ctest-12.log`).
- `python3 tests/doc-figures-gate.py` -> **exit 0** (`docfig-2.log`).

Red/green, measured. The red configuration keeps the test file and neuters the product: `relayoutZones()`
returns before touching the splitter, `writeZoneSizes()` returns immediately, `Shell::snapshot()` omits
`mixer_zones`, `Shell::restoreSnapshot()` ignores it, `MixerModeBar::fillZones()` adds no entries and the
command refuses. Rebuilt, then:

- `ctest -R '^MixerZonesTest$'` **exit 8**, `ctest-red-3.log`, 6 of 8 slots failing:
  `theDefaultArrangementIsTheFixedOneTheMixerHad`, `aLayoutWithoutTheNewFieldsOpensWithTheDefault`,
  `twoRegionsDockInSupportedPositions`, `aSeparatorDragResizesAZone`,
  `savingAndReopeningTheProjectRestoresTheDocksAndTheSizes`,
  `theLayoutSaveIsOneUndoStepForTheWholeArrangement`.
- Restored, rebuilt, `ctest -R '^MixerZonesTest$'` **exit 0** (`ctest-green-4.log`).

An earlier, coarser red run (restoring the whole pre-change product files) also exited 8 but crashed in
the harness's global teardown after the failures (`ctest-red-1.log`), so the finer configuration above is
the recorded proof, and it is the one whose failure lines name the assertions.

## Self-check

**? Each new test FAILS with the product change reverted (reverse the non-test hunks, rebuild, run: exit
code?), then PASSES restored.**

Answered above, with commands and logs: red **exit 8** (`ctest-red-3.log`), green **exit 0**
(`ctest-green-4.log`). The reverts are the product hunks only; `tests/src/gui/MixerZonesTest.cpp` is
untouched in both runs. An earlier coarser red (whole-file revert) is also recorded as **exit 8**, with a
harness teardown crash after the assertion failures.

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per
criterion).**

Criterion 1: `twoRegionsDockInSupportedPositions`, `aSeparatorDragResizesAZone` - part of the
`MixerZonesTest` run, **exit 0**. Criterion 2: `savingAndReopeningTheProjectRestoresTheDocksAndTheSizes`,
`theLayoutSaveIsOneUndoStepForTheWholeArrangement` - same run, **exit 0**. Criterion 3:
`theDefaultArrangementIsTheFixedOneTheMixerHad`, `aLayoutWithoutTheNewFieldsOpensWithTheDefault` - same
run, **exit 0**. I did not run any of these as isolated single slots (ctest runs the target); the per-slot
`PASS` lines are in `ctest-9.log` and `ctest-green-4.log`.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**

`window.mixer_zones` writes no project data of its own: the arrangement is part of the window layout, and
the layout write (`window.layout_project_save`) is the one `true_inverse` step that carries it. The new row
says exactly that, and `ReversibilityContractTest` (which requires one row per registered command and
`not_mutating` only where the command genuinely writes nothing) passes: **exit 0**. `control.undo` of one
layout save restores both the dock and the size, asserted in
`theLayoutSaveIsOneUndoStepForTheWholeArrangement`.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**

Everything added is GUI-thread widget code: `relayoutZones()` / `writeZoneSizes()` run from a splitter
signal, a menu action or a command handler; `MixerZoneState` is constructed on first use from the GUI
thread and emits queued/auto Qt signals only there. No `updateFaders()` path, no `Mixer` model path and no
`processNextBuffer()` path was touched. This is reasoning, not a measurement: local TSan is unavailable in
this session.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**

`mixer_zones` is an optional key in the layout object; a document without it restores the default
arrangement (asserted in `aLayoutWithoutTheNewFieldsOpensWithTheDefault`) and nothing else in the layout
model changes. New data round-trips through a real project save/open, asserted in
`savingAndReopeningTheProjectRestoresTheDocksAndTheSizes` (**exit 0**). No project XML node is added. I did
not run a render-diff test for old projects; the claim rests on the key being additive and unread when
absent.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro
names, no std::filesystem outside _WIN32.**

QString/QJsonObject/QHash/QSplitter only; `QStringLiteral`, `QLatin1Char`, signal/slot connect by pointer,
`constexpr int` - all Qt 5.15-safe. No `std::filesystem`, no POSIX call, no `windows.h`, and no identifier
named `small`, `near`, `far`, `interface`, `min`, `max` (checked in the new files). `enum class` and
`if constexpr`-free code, C++20 is the project's standard. Nothing guarded by `QT_VERSION` is needed. This
is a source review, not a cross-platform build: only the Qt 6.12 Linux build was compiled here.

## Gates (run individually - `feature_check.py` cannot create its log dir under the read-only parent)

| Gate | Exit |
| --- | --- |
| `tests/fork-sources-gate.sh` | 0 (`gate-fork-sources.log`) |
| `tests/all-sources-reproduce.sh` | 0 (`gate-all-sources.log`) |
| `tests/no-upstream-regression-gate.sh` | 0 (`gate-no-upstream-regression.log`; ledger rows added for `include/MixerView.h`, `src/gui/MixerView.cpp`, `data/manual/index.html`) |
| `tests/scripted/check-namespace` | 0 |
| `tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `tests/complexity-gate.sh --check` | 1 (pre-existing train regressions only; **no item in my files** - the first run did flag `mixerzones::parse`, `fromJson` and `mixerZones`, which I split until they were under the target; `gate-complexity3.log` has none of them) |
| `tests/file-length-gate.sh --check` | 1 (pre-existing train regressions only; nothing of mine - new files are 292/125/354 lines, `MixerView.cpp` is inherited and absent from the baseline list, `gate-filelength2.log`) |

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` fails before running a gate with
`PermissionError: ... zene-feat-q050-227.1-feature-check`; the parent directory
(`.../projects/zene-studio-program`) is not writable, so its `logs.mkdir()` cannot run. I ran every gate it
wraps directly instead, and their exit codes are the table above.
