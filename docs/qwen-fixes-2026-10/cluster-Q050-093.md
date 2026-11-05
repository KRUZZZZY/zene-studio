IMPLEMENTED

# Cluster Q050-093 - Detect tempo and auto-warp imported loops

Goal: drop a loop, detect its tempo and key, and choose to follow the project tempo and key or keep it unstretched.
Branch `feat/050-Q050-093-oc`, cut from `050/m0-train-21` (`37eaf0755`). The feature record is
`docs/050-features/Q050-093.md`; the manual paragraph is `data/manual/index.html` (`#loop-warp`).

`clip.import_file` (the one command behind every file drop and an agent's import) now analyses each audio file with the
`detect.analyze` detector and takes a loop mode: `loop_mode` `follow_project` (lead at the detected tempo rounded to a
whole BPM, pitch kept, and transpose the tonic to the project key's tonic the nearest way), `keep_unstretched` (as before),
or `preference` (the default: config key `import/loopwarp`, set by `settings.set` or **Edit > Auto-Warp Dropped Loops**).
Holding Alt on a drop takes the other mode for that drop. The new `clip.source_get_state` (`not_mutating`) reads back the
detected values and what the clip plays. The project's tempo and key are never written; what could not be applied goes in
`loop.notes` and the status bar. The browser's sample drag on a sample track now goes through the same command.

## Integration onto `050/m0-train-22` (round 2, 2026-10-30)

The accepted patch (`feat/050-Q050-093-oc` @ `d61c04f14`) was applied three-way onto `4ee5321ff` and conflicted in six
files. Resolution, no new design:

- `src/core/SampleClip.cpp` (the real conflict): the train's Q050-111 per-clip effects and this patch's loop source both
  appended to the end of `saveSettings` and `loadSettings`. Kept both, effects first: `saveClipEffects` then the additive
  `<loopsource>` child; `loadClipEffects` then `loadLoopSource`. Each reads its own child by name
  (`firstChildElement("fxchain"/"loopsource")`) and each resets on absence, so neither sees the other's element. The copy
  constructor merged cleanly (`m_clipEffects` copied by `copyClipEffects` in the body, `m_loopSource` in the initialiser list).
- `tests/fork-sources.txt`, `tests/upstream-modifications.txt`: union of both sides (the train's rows, then this patch's).
  The README row now names the re-measured canary.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json`: regenerated live from this tree's `zene`
  (`snapshot_live.py`, 879 ids / 77 groups, wasm.* out; `logs/snapshot-1.log` EXIT=0). README canary 878 -> 879.
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`: the train's rows kept, the Q050-093 row after them, histogram re-taken from
  `ReversibilityContractTest`'s measurement: `rows=888 true_inverse=434 snapshot=42 irreversible=25 not_mutating=387`
  (train-22's 887 plus `clip.source_get_state`). First run failed on the stale block (`logs/test-1.log`, EXIT=8, the
  test printed the measured figures); after the update it passes (`logs/test-2.log`).

Failure paths and hazards reviewed for the merge: save/load of a clip with both effects and a loop source (independent
children, both reset on absence - old projects and pre-import checkpoints load as before); clip copy (both copied);
undo checkpoints (the Track checkpoint carries both children); realtime (`m_loopSource` is never read by playback, the
effects path is the train's, untouched); portability (no new API in the resolution).

Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-093.r2/logs/`; env as the brief, plus
`CCACHE_TEMPDIR=/tmp/ccache-tmp-q093` because the inherited ccache temp dir was not writable, `build-1.log` EXIT=2):

| Step | Command | Exit |
| --- | --- | --- |
| Build `zene` + 11 test targets + `amplifier` | `cmake --build build --target zene LoopImportWarpTest LoopDropWarpTest ClipImportFileTest ArrangerFileDropTest ClipEffectsTest SampleClipStretchTest SampleClipWarpTest ReversibilityContractTest ImportDetectionTest ControlWarpCommandsTest amplifier -- -j4` (`build-2.log`) | 0 |
| Build neighbours | `... --target zene ClipTransposeTest ClipSerialisationTest SampleClipWindowTest ReversibilityUndoTest ClipEffectsDialogTest` (`build-3.log`) | 0 |
| 18 focused tests | `ctest -R '^(LoopImportWarpTest\|LoopDropWarpTest\|ControlLoopImportWarp\|ClipImportFileTest\|ArrangerFileDropTest\|ClipEffectsTest\|ClipEffectsDialogTest\|SampleClipStretchTest\|SampleClipWarpTest\|ImportDetectionTest\|ControlWarpCommandsTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ControlCommandsSnapshot\|ClipTransposeTest\|ClipSerialisationTest\|SampleClipWindowTest\|ControlDetectCommands)$'` (`test-2.log`) | 0, 18/18 |
| Product reverted (the ten behavioural files set to `4ee5321ff`'s content), rebuilt | `build-5-reverted.log` | 0 |
| New tests, reverted | `ctest -R '^(LoopImportWarpTest\|LoopDropWarpTest\|ControlLoopImportWarp)$'` (`test-4-reverted.log`): 8 + 4 QtTest failures, transcript 5/10 | 8 |
| Restored, rebuilt, rerun (+ snapshot) | `build-6-restored.log` 0; `test-5-restored.log` | 0, 4/4 |
| Doc figures | `python3 tests/doc-figures-gate.py` (`docfig-1.log`) | 0 |
| Gates | `feature_check.py` copy with only the log folder moved to `/tmp` (`feature-check-2.log`; the original cannot create its folder next to the worktree, `feature-check-1.log` PermissionError) | 0, 8/8 PASS |

(`test-3-VOID-nothing-reverted.log` is a void run: a zsh word-splitting slip meant nothing was reverted; it is kept and
not counted.) The tables below are the round-1 evidence on `37eaf0755`; the integrated tree reproduces them.

## Acceptance

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-093.r1/logs/`. Commands from `build/tests`:
`ctest -R "^(LoopImportWarpTest|LoopDropWarpTest|ControlLoopImportWarp)$" --output-on-failure`.

| Criterion | Test | Result | Exit with change | Exit, product reverted |
| --- | --- | --- | --- | --- |
| 1. Drop a fixture loop with known tempo and key: source tempo/key detected, the clip follows project tempo, transpose matches the project key | `LoopDropWarpTest` (clicks the Edit menu action, sends a real `QDropEvent` with a file URL to the sample lane), `LoopImportWarpTest::followProjectLeadsAtTheDetectedTempoAndTransposesToTheProjectKey` | A major loop at 100 BPM into a 140 BPM C major project: detected 100.35 BPM, tonic A; clip warp mode `source` at 100, `preserve_pitch`, transpose +3, 768 ticks (four bars); project tempo/key unchanged; status bar names BPM, "A Major", "+3"; one undo | 0 (`test-12-restored.log`) | 8 (`test-9-reverted.log`) |
| 2. Keep-unstretched preference; the rendered loop duration matches the selected mode | `LoopImportWarpTest::theRenderedDurationMatchesTheLoopMode`, `theRememberedPreferenceIsTheDefaultAndAnArgumentOverridesIt`; `LoopDropWarpTest::autoWarpOffKeepsTheLoopUnstretchedAndAltFlipsOneDrop` | Render through `SamplePlayHandle`: source 9.6000 s, kept 9.6000 s, followed 6.8571 s = four bars at 140 BPM; the preference is reused by later imports and an argument (or Alt in the GUI) overrides it for one import | 0 | 8 |
| 3. The same choice through the registered agent command, with clip-state read-back | `ControlLoopImportWarp` (`tests/control-loop-import-warp.py`, live `zene` over the socket), `LoopImportWarpTest::oneUndoRemovesTheImportAndItsConfiguration`, `theStateReadsBackThroughTheAgentCommand` | 10/10 checks: `clip.import_file loop_mode=follow_project`, `clip.source_get_state` (tempo mode `source` at 100, transpose 3, length 768), `warp.list` agrees, project unchanged, one `control.undo`, preference keep/follow via `settings.set`, unknown mode refused | 0 (`test-19.log`) | 8, 5/10 checks (`test-18-reverted.log`) |

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** Reverted: the ten behavioural product
  files (`src/core/ControlCommandsClipImportFile.cpp`, `include/ArrangerFileDrop.h`, `src/gui/ArrangerFileDrop.cpp`,
  `src/gui/MainWindow.cpp`, `src/gui/tracks/SampleTrackView.cpp`, `src/gui/clips/ClipView.cpp`,
  `src/gui/editors/TrackContainerView.cpp`, `src/gui/tracks/TrackView.cpp`, `include/TrackContentWidget.h`,
  `src/gui/tracks/TrackContentWidget.cpp`) restored to `37eaf0755` with `git checkout 37eaf0755 -- <files>`, rebuilt
  (`build-12-reverted.log` EXIT=0), ran: ctest EXIT=8, all three tests failed (`args: unexpected property 'loop_mode'`;
  `Edit > Auto-Warp Dropped Loops is missing`; transcript 5/10). The new library and the clip field stayed so the QtTests
  still compile; with them reverted too the QtTests do not compile. The transcript was edited after that (a refactor for the
  complexity gate), so its proof was run again: reverted `build-20-reverted.log` EXIT=0, ctest EXIT=8 (5/10). Restored
  (`git checkout HEAD -- <files>`, `build-21.log` EXIT=0): EXIT=0 for all three (`test-12-restored.log`, `test-19.log`).
- **Every acceptance criterion is proven by a test I ran.** See the table: LoopDropWarpTest 0, LoopImportWarpTest 0,
  ControlLoopImportWarp 0. Neighbours, all exit 0 (`test-13-neighbours.log`, 13/13; `test-14.log`, 4/4): ClipImportFileTest,
  ControlWarpCommandsTest, ImportDetectionTest, ReversibilityContractTest, ReversibilityUndoTest, ReversibilityUndoDeviceTest,
  ClipTransposeTest, ArrangerFileDropTest, SampleClipStretchTest, SampleClipWarpTest, ControlDetectCommands,
  ControlReversibilityTranscript, ControlCommandsSnapshot, ClipSerialisationTest, SampleEditorDialogTest, BrowserDragUrlTest,
  SampleClipWindowTest. Note: `ReversibilityUndoDeviceTest` and `ControlReversibilityTranscript` first failed because
  `ladspaeffect`/`amplifier` were not built in this tree; I built them. Its first run straight after building
  `ladspaeffect` segfaulted inside the LADSPA restore slot. It then passed on full base product code (`test-10-base.log`) and
  passed 3 out of 3 on this branch (`test-11-run{1,2,3}.log`), so I read the segfault as a stale-artifact flake, not a
  regression.
- **One undo step per edit; honest classes.** The loop configuration is applied to the new clip inside `clip.import_file`'s
  existing step (Track checkpoint, or the created-track action checkpoint), so it stays `true_inverse`. Tests: one undo
  returns the track count and clip count, undo depth +1 per import, redo restores warp mode, transpose and loop source
  (`oneUndoRemovesTheImportAndItsConfiguration`, GUI drops in `LoopDropWarpTest`, the transcript).
  `clip.source_get_state` is `not_mutating` (it only reads). The preference goes through the existing `settings.set`
  (`true_inverse`, unchanged). `ReversibilityContractTest` 0 after the histogram update (881 rows).
- **Nothing new on the audio thread.** Decode, analysis, `LoopSource` and every setter run in the command handler on the
  control thread. The warp and stretch setters are the existing ones: `setWarpStretchMode` builds the Rubber Band pool
  before publishing the mode. The audio thread never reads `LoopSource`. TSan does not run on this box; that is the
  design reasoning.
- **Old projects load and render as before; new data round-trips.** The `<loopsource>` child is written only for an
  analysed clip and reset on absence on load. `theLoopSourceRoundTripsAndAnOldClipLoadsAsBefore` saves and loads the
  state equal and loads a clip element without the child as not analysed, linear and at zero transpose.
  `ClipSerialisationTest` and `SampleClipWindowTest` are 0. Without `loop_mode` and with no preference set, an import is
  keep_unstretched, which is today's behaviour (`theRememberedPreferenceIsTheDefaultAndAnArgumentOverridesIt`).
- **Portability.** The only version-dependent call, `QDropEvent::modifiers()` vs `keyboardModifiers()`, is behind
  `QT_VERSION_CHECK(6, 0, 0)` (`arrangerdrop::dropModifiers`). Everything else is Qt 5.15 API (`QJsonArray::fromStringList`,
  `QFlags::testFlag`, functor `connect`). No `windows.h` macro names, no `small`/`near`/`far`/`interface`/`min`/`max`
  identifiers, no `std::filesystem`, no POSIX calls. `LoopSource` uses a C++20 defaulted `operator==`. Built and tested only
  on Linux GCC/Qt 6 here; the hosted matrix has not run.
- **Gates.** `submit_check.py`'s gate row FAILs because `feature_check.py` creates its log folder next to the worktree
  (`.../zene-feat-q050-093-feature-check`), which this lane may not write (PermissionError). I ran a copy that differs only
  in the log folder (`/tmp/q093/feature-check`): fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity and file-length all PASS, RESULT: PASS (`feature-check-3.log`).
  Round 1: snapshot regenerated live (872 ids / 76 groups on its base). Integrated: 879 ids / 77 groups, `doc-figures-gate.py` PASS, A16 histogram 888 rows (see the integration section).
