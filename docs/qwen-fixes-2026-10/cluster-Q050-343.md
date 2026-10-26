IMPLEMENTED

## Integration onto 050/m0-train-16 (round 2)

The accepted patch (`feat/050-Q050-343-oc` @ `eaca835dd`) was applied by the driver's three-way merge onto
`050/m0-train-16` @ `cf3618e61`. Three paths conflicted; each was resolved without changing the accepted
design, and no regression test was weakened.

| Conflict | Cause | Resolution |
|---|---|---|
| `data/manual/index.html` | the train extended the same "Sample clips show fade handles..." line with the clip-gain / gain-envelope / polarity text; the patch appended the Insert Test Signal... and Process paragraph after that line | kept the train's line verbatim and the patch's six added lines after it - both texts present, neither edited |
| `tests/src/gui/DevicePanelTest.cpp` | the train added `#include <QContextMenuEvent>` next to the patch's include change | kept the train's include; the patch's `CommandForm.h` include and its test slot are unchanged |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | `captured_at` / `source_socket` / `version` metadata only | not hand-merged: regenerated live from this tree's `zene` with `snapshot_live.py` -> exit 0, 849 ids / 76 groups, ids_sha256 `fd877b78...` (log `snapshot-1.log`); its diff against the train is exactly the five `sample.*` description strings plus capture metadata (9 lines changed), so no command id was added, lost or renamed |

No semantic difference from the accepted patch: every non-conflicting hunk (the five descriptions, the
three comments, `docs/KNOWN-LIMITATIONS.md`, the three tests, the feature record) applied unchanged, and
the menu route the help names is still at `src/gui/shell/ShellCommandMenus.cpp:293` (Insert Test
Signal... -> `sample.generate`) and `:219-222` (Process -> the four bakers) on the train.

### Affected failure paths and platform hazards (all checked together)

- Stale help after the merge: the train regenerated the snapshot from its own binary; the conflict was
  in metadata only, and the snapshot here is regenerated from THIS tree's `zene`, so the route text an
  agent reads offline is the integrated description (`ControlCommandsSnapshot` exit 0).
- Count drift: 849 ids / 76 groups before and after; `doc-figures-gate.py` exit 0 (log `docfig-1.log`),
  README canary unchanged; `ReversibilityContractTest` exit 0, no A16 change.
- GUI test against the train's newer DevicePanel/menu code: `DevicePanelTest` (19+1 slots incl. the
  train's new ones) exit 0.
- Undo: `ControlSampleOperatorTest` exit 0; the GUI slot's one `control.undo` still removes the clip.
- Realtime / project format / portability: the integration adds no code beyond the accepted patch (a
  manual paragraph, one kept include, regenerated JSON). No audio-path line, no serialisation, no new
  Qt API. TSan does not run on this box; nothing executable changed, so no race is introduced.

### Integration evidence (logs in `qwen/runs/Q050-343.r2/logs`)

| Step | Command | Exit |
|---|---|---|
| Build | `cmake --build build --target zene ControlRegistryTest DevicePanelTest ControlSampleOperatorTest ReversibilityContractTest tripleoscillator amplifier -- -j4` (`build-3.log`) | 0 |
| Snapshot | `python3 .../scratch/m0/snapshot_live.py $PWD` (`snapshot-1.log`) | 0 |
| Figures | `python3 tests/doc-figures-gate.py` (`docfig-1.log`) | 0 |
| Green | `ctest -R '^(ControlRegistryTest\|DevicePanelTest\|ControlCommandsSnapshot\|ControlSampleOperatorTest\|ReversibilityContractTest)$' --output-on-failure` (`test-1-green.log`) | 0, 5/5 |
| Red (product hunks + snapshot reverted to `cf3618e61`, rebuilt `build-red.log` exit 0) | `ctest -R '^ControlRegistryTest$'` / `'^DevicePanelTest$'` / `'^ControlCommandsSnapshot$'` (`test-red-*.log`) | 8 / 8 / 8 - failing at `ControlRegistryTest.cpp:162`, `DevicePanelTest.cpp:503`, and "sample.generate help does not describe the route (4 finding(s))" |
| Restored (rebuilt `build-green.log` exit 0) | the same three (`test-green-*.log`) | 0 / 0 / 0 |
| Gates | `feature_check.py` copy with only its log dir moved to `/tmp` (see Self-check) (`gates-2.log`) | 0, 8/8 PASS |

The ccache config on this box points its temp dir at a path this sandbox cannot write; builds set
`CCACHE_TEMPDIR=/tmp/q343-ccache-tmp` (nothing else changed; `build-1.log` shows the original failure).

## Round 1 (the accepted patch) - original report


Cluster Q050-343 - Document the sample generator menu route. Branch `feat/050-Q050-343-oc`, base `b9ab28248` (050/m0-train-12 worktree base; the branch itself was cut from 050/m0-train-7 and this worktree's HEAD is the train-12-era base it was checked out at).

The route already existed (`src/gui/shell/ShellCommandMenus.cpp`, sample track -> **Insert Test Signal...** ->
`sample.generate`); what was wrong was every piece of help that called it socket-only. This unit changes
text only: the command descriptions, the two source comments, `docs/KNOWN-LIMITATIONS.md` and the user
manual. The id, schemas, handler, menu, menu defaults, `mutating` flag and `true_inverse` row are unchanged.

## Acceptance

| Criterion | Test | Command and exit code |
|---|---|---|
| The `sample.generate` help text names its track-menu route | `ControlRegistryTest::sampleGenerateHelpNamesItsTrackMenuRoute` - reads the registered description, requires "Insert Test Signal...", "More", "sample track", "control socket", "MCP" and the generated-clip wording; rejects "socket-only" and "no interface reaches" | `cd build/tests && ctest -R '^ControlRegistryTest$' --output-on-failure` -> GREEN exit 0 (log `test-12-green-final.log`); RED with product hunks reverted exit 8, `ControlRegistryTest.cpp` line 162, log `test-08-red-registry-final.log` |
| The command snapshot documents the menu route and its registered command ID | `tests/control-commands-snapshot.py` -> `snapshot_help_problems` / `_help_text_problems`, run by ctest `ControlCommandsSnapshot`: loads the committed snapshot through the bridge's own `load_bundle`, finds `sample.generate`, requires the menu label and **More**, rejects the stale claim, pins `id == "sample.generate"` | `cd build/tests && ctest -R '^ControlCommandsSnapshot$' --output-on-failure` -> GREEN exit 0 (log `test-12-green-final.log`); RED with the snapshot at base + product reverted exit 8, 4 findings, log `test-11-red-snapshot-final2.log`. Direct run prints `PASS: the snapshot's sample.generate entry documents its track-menu route (More > Insert Test Signal...)` (log `test-06-snapshot-direct.log`, exit 0) |
| GUI and agent are one command, and a GUI test drives the GUI control | `DevicePanelTest::theSampleGeneratorsMenuActionOpensItsFormAndMakesAClip` - triggers the real menu action, requires `controlCommand == "sample.generate"`, reads the opened `CommandForm`'s about label (the registry description), checks fixed `track` and defaults `0`/`192`/`tone`, accepts the form, captures `ran()`, checks clip count +1 and one `control.undo` back to the same count, and that an instrument track has no such entry | `cd build/tests && ctest -R '^DevicePanelTest$' --output-on-failure` -> GREEN exit 0 (log `test-12-green-final.log`); RED with product hunks reverted exit 8, `DevicePanelTest.cpp` line 370, log `test-09-red-devpanel-final.log` |
| One undo step per edit (unchanged, re-proved) | the same GUI test's single `control.undo`; plus the existing `ControlSampleOperatorTest::generateMakesAClipPerKindAndOneUndoRemovesIt` | `cd build/tests && ctest -R '^ControlSampleOperatorTest$' --output-on-failure` -> exit 0 (log `test-12-green-final.log`, 4/4 tests passed) |

Snapshot regenerated live: `python3 research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD`
-> exit 0, 833 ids / 76 groups, same `ids_sha256 8995baf1...` as the base snapshot (log `snapshot-01.log`);
the diff is the five `sample.*` description strings plus `captured_at`. `python3 tests/doc-figures-gate.py`
-> exit 0 (833 ids / 76 groups, README canary unchanged). `ReversibilityContractTest` -> exit 0, no A16
histogram change (no command added or removed).

Gates (each run by hand because `feature_check.py` cannot create its log directory in this sandbox - see
Self-check): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0,
no-tautology 0, doc-figures 0. Complexity and file-length still report the train's pre-existing
regressions in files this unit does not touch; both were verified to name none of my files after I split
`snapshot_help_problems` (CCN) and brought `ControlRegistryTest.cpp` back under 500 code lines (logs
`gate-complexity3.log`, `gate-filelength2.log`).

## Files

- `src/core/ControlCommandsSample.cpp` - description + file comment.
- `src/core/ControlCommandsSampleEdit.cpp` - four baker descriptions + file comment (they would otherwise
  contradict the corrected limitation section, which names their Process menu).
- `src/core/ControlRegistryRegistrations.cpp`, `include/ControlRegistryGroups.h` - registration and group
  comments.
- `docs/KNOWN-LIMITATIONS.md` - the `sample.*` section rewritten: menus named, remaining absence restated
  (no waveform view; placement beyond the menus still the open owner decision).
- `data/manual/index.html` - the route and its defaults in the Arranging section.
- `tests/control-commands-snapshot.py`, `tests/src/core/ControlRegistryTest.cpp`,
  `tests/src/gui/DevicePanelTest.cpp` - the three assertions.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` - regenerated live.
- `docs/050-features/Q050-343.md` - the feature record.

## Self-check

- **? Each new test FAILS with the product change reverted, then PASSES restored.** Save the six product
  files + the snapshot, `git checkout --` them, rebuild `zene ControlRegistryTest DevicePanelTest`
  (exit 0, log `build-05-red-final.log`), then `ctest` each: `ControlRegistryTest` exit 8 (27 passed,
  1 failed), `DevicePanelTest` exit 8 (19 passed, 1 failed), `ControlCommandsSnapshot` exit 8 (4 findings),
  logs `test-08/09/11-red-*-final*.log`. Restored with `git apply` and re-ran: build exit 0
  (`build-06-green-final.log`), all four ctests exit 0 (`test-12-green-final.log`). No test file was
  changed between the red and green runs.
- **? Every acceptance criterion proven by a test I ran.** The table above names the test, the command and
  the exit code for each; the only criterion re-proved rather than newly created is the undo one, whose
  GUI half is new and whose command half is the pre-existing `ControlSampleOperatorTest` slot.
- **? Each edit is ONE undo step, every new or changed command honest.** No command, schema or handler was
  changed; `sample.generate` keeps its `true_inverse` row, so one `control.undo` still removes the clip
  and its buffer. `ReversibilityContractTest` exit 0 confirms the table and the A16 histogram still agree.
  The new GUI test proves the one-step claim for the menu route itself.
- **? Nothing added runs on the audio thread.** The diff touches no audio-path code: under `src/`,
  `include/` and `plugins/` it is four files of string literals and comments (30 insertions, 19
  deletions, zero statements changed). The command's handler and `Track::createClip` are untouched, so no
  allocation, lock, wait, I/O or signal was added anywhere on a render path. The prompt's audio-path
  heuristic fires only because the code files' descriptions mention the generation command by name; no
  render-path symbol appears in the changed lines. (TSan does not run on this box; the argument is that
  no executable line changed at all.)
- **? Old projects load and render exactly as before; new data round-trips.** No format change: the diff
  contains no serialisation code. Generated audio still uses the existing `SampleClip` path
  (`SampleClip::saveState`/`loadSettings`), and `ControlSampleOperatorTest`'s generate-then-undo legs pass
  (exit 0).
- **? Qt 5.15 / MSVC / macOS < 10.15 portability.** The only C++ additions are `QString::contains`,
  `QApplication::topLevelWidgets`, `QObject::connect` and `QCOMPARE/QVERIFY2` - all present in Qt 5.15.
  The test file gained `#include <QApplication>` and `#include "CommandForm.h"`. No Qt-6-only API, no
  `windows.h` macro names, no `std::filesystem` was added.
- **Tooling limits (stated, not hidden).** `feature_check.py` writes its logs to
  `<worktree>-feature-check`, beside the worktree; this session's filesystem sandbox allows writes inside
  the worktree and in `/tmp` but denies them in `.../zene-studio-program/` (measured: `mkdir` there, and
  `touch` inside an existing sibling `*-feature-check` directory, both return EACCES). Unpatched,
  `feature_check.py` therefore aborts with `PermissionError` before running any gate, and `submit_check.py`
  reports that wrapper as its one `gates` FAIL. Two independent readings show the gates themselves pass:
  1. each gate run by hand from the worktree - fork-sources 0, all-sources-reproduce 0,
     no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, and both ratchets
     verified to name no file this unit changed (logs `gate-*.log`);
  2. a copy of `feature_check.py` with ONLY its log path redirected to `/tmp/q050-343-logs/gate-logs`
     (no gate or rule changed), run against this worktree: `feature-check zene-feat-q050-343 (base
     b9ab28248, 12 changed files)` -> all eight rows PASS, `RESULT: PASS`, exit 0 (logs `gates-02.log`
     and `gates-03-final.log`, the latter on the final HEAD).
  The two ratchets are red on the train itself (`src/core/TakeLane.cpp`,
  `tests/src/gui/ArrangerTimelineTest.cpp`, `ControlCommandsMixerTools.cpp` and others - all untouched by
  this unit); the redirected run's own rule excuses them against the base and reports "no new item in your
  files" for both.
- **Submit-check WARNs, answered.** `a GUI path for the user-facing action - no GUI file changed`: this
  unit is docs-only by design and the GUI route already exists unchanged in
  `src/gui/shell/ShellCommandMenus.cpp`; the new `tests/src/gui/DevicePanelTest.cpp` slot drives it, which
  the check reports PASS on the next row. `canary figures (README) updated - new command ids:
  control.undo, sample.generate`: that list is the check's diff heuristic reading ids quoted in the new
  test and in this report's prose; no command id was added or removed (833 ids / 76 groups before and
  after), so the README canary is correctly unchanged and `doc-figures-gate.py` passes.
- **Final test run on the committed HEAD:** `ctest -R "^ControlRegistryTest$|^DevicePanelTest$|^ControlCommandsSnapshot$|^ControlSampleOperatorTest$|^ReversibilityContractTest$" --output-on-failure` -> exit 0, 5/5 passed (log `test-14-final-all.log`).
- **Bridge test suite, for completeness (not part of this unit's proof).** The offline half passes:
  `python3 -m unittest test_units test_offline_staleness test_wire_client` from
  `tools/mcp-zene-control/tests` -> exit 0, 71 tests OK (log `bridge-units2.log`). The nine
  binary-driving tests in `test_declared_surface` / `test_mcp_errors` / `test_mcp_e2e` error out on this
  host inside the `mcp` 1.29.0 package (`InitializeResult` has `serverInfo`, the tests read
  `server_info`), before any Zene call is made - a pre-existing incompatibility in test/harness code this
  unit does not touch and cannot fix without widening its scope. The same claims are covered here by
  ctest `ControlCommandsSnapshot`, which passed.
