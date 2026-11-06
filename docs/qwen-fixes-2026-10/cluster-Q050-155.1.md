IMPLEMENTED

# Round 2 - integration onto 050/m0-train-25 (`081630835`)

The accepted patch `9a1a74ec6` was three-way merged onto the train; this is integration, not a reimplementation.
Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-155.1.r2/logs/`.

**Conflicts and resolution.** Only two, both mechanical:
- `tests/CMakeLists.txt`: the offscreen `set_tests_properties(...)` line - the train added `VideoClipsTest`, this
  unit `WideDiscreteBusTest`. Resolved to the union of both lists.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` (generated): regenerated live from the integrated
  `zene` with `snapshot_live.py` (exit 0, 917 commands / 78 groups, `wasm.*` out). Compared with the train, only
  `mixer.channel_format`'s enum and description and the capture metadata differ. `tests/doc-figures-gate.py` exit 0.

**No semantic difference from the accepted patch.** No product file conflicted, and the train changed none of the
files this unit touches (`git diff 2a7fd9f9c 081630835` over them is empty), so all seven product files are
byte-identical to the accepted patch. Nothing on the train uses `surround::MaxChannels/SideChannels/FormatCount/
Matrix/Gains` in new code (I grepped the train's diff).

**Affected failure paths and platform hazards (unchanged from the accepted patch, re-checked):** format readback and
undo (`mixer.channel_format`, `mixer.get_state`, `mixer.surround_get`); project save/load of `discrete-N`; side buffer
growth in `ChannelSurround::setFormat` (control thread, under the change lock); index-addressed render in
`addSideChannels` / `foldInto`; the GUI `Surround > Format` submenus and the panner strip; the export tap's plane count.
Portability: no new API in the integration; the patch itself is as reviewed in round 1 (no Qt-6-only API, no reserved
identifiers, no std::filesystem). Only GCC/Qt6 was built here.

| Criterion | Test (cwd `build/tests`, `QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR`) | Exit (integrated tree) |
|---|---|---|
| Select and read back a discrete-64 bus through the GUI and command surface; save and reload preserves its format and count. | `ctest -R '^(WideDiscreteBusTest\|SurroundTest\|SurroundExportTest\|MixerWindowsTest)$' --output-on-failure` (test-1.log) - slots `wideDiscreteFormatsReadBackAndUndoInOneStep`, `wideDiscreteLayoutsSurviveASaveAndLoad`, `theSurroundMenuOffersTheWideDiscreteBuses` | 0 (4/4) |
| A render sends distinct indexed channel signals through the discrete bus without reordering or cross-channel leakage. | same run - slots `distinctChannelsCrossADiscrete64BusInOrder`, `theWideMasterMixAllocatesNothing` | 0 |
| (regression) | `ctest -R '^(WideDiscreteBusTest\|SurroundTest\|MixerWindowsTest\|ProjectStateTest\|SurroundExportTest\|SurroundPannerTest\|HardwareIoTest\|MixingRoutingTest\|ReversibilityContractTest\|AgentSurfaceReportTest\|MixerToolsTest\|ControlCommandsSnapshot)$'` (test-2.log; again after the revert proof, test-3-restored.log) | 0 (12/12), 0 (12/12) |

## Self-check (round 2)

- **New tests fail reverted, pass restored.** The seven product files written from `081630835` (the train), rebuilt
  `WideDiscreteBusTest MixerWindowsTest` (build-4-reverted.log, exit 0): `ctest -R '^WideDiscreteBusTest$'` exit 8
  (4 of 6 slots fail, `discrete-9: format: value not in the allowed set`, red-wide.log); `ctest -R '^MixerWindowsTest$'`
  exit 8 (`theSurroundMenuOffersTheWideDiscreteBuses`: `wide != nullptr`, red-mixerwindows.log). Restored from the
  index (byte-compared with `cmp`), rebuilt (build-5-restored.log, exit 0): the 12-test set exit 0.
- **Every criterion proven by a test I ran:** the table above.
- **One undo step; honest reversibility.** No new command; `mixer.channel_format` stays `true_inverse` with one undo
  step (asserted in both tests). `ReversibilityContractTest` exit 0, no A16 change asked for.
- **Realtime.** Unchanged from the accepted patch: the render path only multiply-adds over preallocated planes;
  the only growth is in `setFormat` on the control thread under the change lock; `theWideMasterMixAllocatesNothing`
  passes. TSan does not run on this box.
- **Old projects; round-trip.** `SurroundTest` (stereo projects unchanged) and `ProjectStateTest` exit 0;
  `wideDiscreteLayoutsSurviveASaveAndLoad` exit 0.
- **Portability.** The integration added no code; see round 1 below. Only GCC/Qt6 compiled locally.
- **Gates.** `feature_check.py` cannot create its log directory (`<worktree>-feature-check`, outside my write jail:
  `PermissionError`, feature-check-1.log). I ran a copy at `/tmp/q155-fc2/feature_check.py` whose only changes are
  `ROOT` pinned to the original `qwen/` directory and the log directory set to `/tmp/q155-fc2/logs` (two-line diff):
  exit 0, every row PASS - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures, complexity and file-length (no new item) - feature-check-2.log, run on commit `432ef3627`.
  `submit_check.py` therefore still reports `FAIL gates` in this sandbox for the same reason.
- **Build note.** The worktree's ccache config points its temp dir outside my write jail; I built with
  `CCACHE_TEMPDIR=/tmp/q155-ccache-tmp` (build-1 failed on exactly that, build-2 onward exit 0).

# Round 1 record (accepted)

Q050-155.1 (stage 1 of 4 of Q050-155): discrete mixer buses through 64 channels. Both acceptance criteria are met and
proven by tests run in this worktree. Full record: `docs/050-features/Q050-155.1.md`. Logs:
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-155.1.r1/logs/` (build-N, test-N, green-*, red-*, green2-*).

## Acceptance

| Criterion | Test (cwd `build/tests`, `QT_QPA_PLATFORM=offscreen`) | What it proves | Exit |
|---|---|---|---|
| Select and read back a discrete-64 bus through the GUI and command surface; save and reload preserves its format and count. | `ctest -R '^(WideDiscreteBusTest\|MixerWindowsTest)$' --output-on-failure` - slots `wideDiscreteFormatsReadBackAndUndoInOneStep`, `wideDiscreteLayoutsSurviveASaveAndLoad`, `theSurroundMenuOffersTheWideDiscreteBuses` | `mixer.channel_format` takes discrete-9/16/33/64 and refuses 65/1/128 (`invalid_args`); `mixer.get_state` and `mixer.surround_get` report name, count, `discrete`, no speakers, labels 1..N; each edit is one undo step. Save -> clear -> load keeps discrete-9/24/64 name, count and readback. The GUI test triggers `Surround > Format > Discrete 49-64 > discrete-64` (the action runs `mixer.channel_format` through ControlRegistry), reads back 64 channels, draws the panner strip, undoes in one step. | 0 (WideDiscreteBusTest 6/6 passed; MixerWindowsTest 12/12 passed) |
| A render sends distinct indexed channel signals through the discrete bus without reordering or cross-channel leakage. | same ctest run - slots `distinctChannelsCrossADiscrete64BusInOrder`, `theWideMasterMixAllocatesNothing` | 64 distinct constants on a discrete-64 channel, sent only into a discrete-64 bus whose direct out is outputs 1..64 of a 128-output device: after real `Mixer::masterMix` periods, output i holds exactly channel i's level, outputs 65..128 are silent; send level 0.5 halves every channel alike; a discrete-16 receiver keeps 1..16 only; a discrete-12 sender fills 1..12 only. The master mix of the graph allocates nothing. | 0 |

Wider run after the last product change: `ctest -R '^(WideDiscreteBusTest|SurroundTest|MixerWindowsTest|ProjectStateTest|SurroundExportTest|SurroundPannerTest|HardwareIoTest|MixingRoutingTest|ReversibilityContractTest|AgentSurfaceReportTest|MixerToolsTest|ControlCommandsSnapshot)$'`
-> exit 0, 12/12 passed (`logs/test-5.log`); after the final test-include commit, the 6-test subset re-ran exit 0
(`logs/test-6.log`).

## Self-check

- **Each new test fails with the product reverted, passes restored.** `git checkout 2a7fd9f9c -- include/MixerSurround.h
  src/core/MixerSurround.cpp src/core/MixerSurroundMix.cpp src/core/SurroundExport.cpp src/core/ControlCommandsMixerSurround.cpp
  src/gui/MixerHardwareMenu.cpp src/gui/SurroundPanner.cpp`, rebuild (`logs/build-9-reverted.log`, exit 0), run:
  `WideDiscreteBusTest` exit 4 (4 failed: `discrete-9: format: value not in the allowed set`, and the three slots that
  set discrete-64/24), `MixerWindowsTest` exit 1 (`theSurroundMenuOffersTheWideDiscreteBuses`: `wide != nullptr`).
  Restored with `git checkout HEAD -- <same files>`, rebuilt (`logs/build-10-restored.log`, exit 0): both exit 0
  (6/6 and 12/12). `SurroundTest` (changed, not new) passed both ways.
- **Every criterion proven by a test I ran:** see the table - WideDiscreteBusTest and MixerWindowsTest, exit 0.
- **One undo step per edit; honest reversibility classes.** No new command. `mixer.channel_format` stays
  `true_inverse` with one `addUndoStep` per change; the readback test undoes each format in one `control.undo` and the
  GUI test does the same after triggering the menu entry. `ReversibilityContractTest` exit 0; no A16 histogram change
  was requested by it.
- **Realtime.** The new render code (`carrySideChannel`, the index branch of `addSideChannels`, the discrete return in
  `foldInto`) only multiplies and adds over already-allocated planes. The one allocation is the side buffer growth in
  `ChannelSurround::setFormat`, on the control thread under the change lock (`underChangeLock` in the command;
  `Mixer::loadSettings` for a project load). `theWideMasterMixAllocatesNothing` counts 0 allocations in a 64-channel
  master mix. TSan does not run on this box; that is the race argument.
- **Old projects; round-trip.** Existing format names and enum values are unchanged (stereo..mono, discrete-2..8 keep
  their indices and matrices; the discrete matrix is unchanged for counts <= 8); a missing `<surround>` still loads
  stereo and a stereo channel still writes none (`SurroundTest::aStereoProjectIsUnchanged`, exit 0). New data:
  `wideDiscreteLayoutsSurviveASaveAndLoad` (exit 0).
- **Portability.** No Qt-6-only API (QString::arg, QMenu::addMenu, QJsonArray only); no `min`/`max`/`small`/`near`/
  `far`/`interface` identifiers; no std::filesystem; no POSIX calls; `std::min({...})` is C++20 standard. Enumerators
  `Discrete16/32/64 = Discrete2 + k` use the fixed underlying type (`enum class Format : int`), valid on MSVC/clang.
  Only built here with GCC/Qt6 - the other CI targets were not compiled locally.
- **Gates.** `submit_check.py` reports `FAIL gates (feature_check.py)` in this sandbox because `feature_check.py`
  creates its log directory at `<worktree>/../zene-feat-q050-155.1-feature-check`, outside my write-jail
  (`PermissionError: [Errno 13]`). I ran the same script copied to `/tmp/q155-fc/feature_check.py` with only the log
  directory changed to `/tmp/q155-fc/logs` (ROOT, FIX-BASE and cached baselines unchanged): exit 0, every row PASS -
  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures (878 ids /
  77 groups), complexity and file-length (no new item in my files). `logs/feature-check-2.log`.
- **Snapshot.** Regenerated live with `snapshot_live.py` (exit 0, 878 commands / 77 groups, `wasm.*` out); only
  `mixer.channel_format`'s enum and description changed. `tests/doc-figures-gate.py` exit 0;
  `ControlCommandsSnapshot` exit 0. The canary WARN in submit_check lists existing commands the tests call, not new ids.
- **Docs.** `data/manual/index.html` (Mixer: 2..64 range, the Format submenus, index routing, the source limit);
  `docs/050-features/Q050-155.1.md` (one row per criterion, format change, known limits).

## Not done here (later stages, by the scoping brief's split)

Device pins / track routes carrying 64 channels (stage 2), independent per-pair sends (stage 3) and their Routing
Matrix editing (stage 4). Today a discrete bus's extra channels come from another surround or discrete channel's send;
a track or device feeds the first pair only - stated in the manual and the feature record.
