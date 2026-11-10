IMPLEMENTED

# Q050-283.2 - Configure neutral tracks for instruments (stage 2 of 3)

Branch `feat/050-Q050-283.2-oc`, cut from `050/m0-train-26` (`af010bc3cc`). Full detail: `docs/050-features/Q050-283.2.md`.
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-283.2.r1/logs`.

New command `track.set_role` (true_inverse): a neutral row is given an instrument (or its kept one back), an instrument track
is switched back to neutral keeping its whole instrument role (plug-in, settings, clips) as text in `<neutraltrack><keptrole>`.
The row object is rebuilt from the row's own XML at the same index, so its `trk-<n>` id, position, name, mute/solo, colour
and folder stay. GUI: the row's Actions menu (Assign instrument / Restore / Make neutral (keep instrument)), all invoking
`track.set_role` through `ControlRegistry`. `track.get_state`/`track.list` report `role` and `kept_roles`.

## Acceptance

All from `build/tests`, `ctest -R "^(...)$" --output-on-failure`.

| Criterion | Tests | Command / log | Exit |
|---|---|---|---|
| Instrument assignment keeps the same track identity, arrangement position, and existing project content | `HybridTrackInstrumentRoleTest::assigningAnInstrumentKeepsIdPositionAndProject`, `::aSwitchIsOneUndoStepThatKeepsTheRow`; `HybridTrackRoleGuiTest::assignInstrumentFromTheRowMenu` (menu item triggered); `ControlHybridTrackRoleTranscript` A/B | `test-7.log` (8 tests incl. stage-1 tests, ReversibilityContractTest, ControlCommandsSnapshot) | 0 |
| Instrument role state remains available after save/load and when switching back to neutral | `HybridTrackInstrumentRoleTest::switchingBackToNeutralKeepsTheInstrumentRole`, `::theKeptRoleRoundTripsThroughSaveAndLoad`, `::aRowWithoutAKeptRoleSavesNoMark`; `HybridTrackRoleGuiTest::makeNeutralAndRestoreFromTheRowMenu`; transcript C/D | `test-7.log` | 0 |
| Each new test fails without the product change | 9 modified product files checked out from `af010bc3cc`, 4 new product files removed, rebuilt (`build-neg-1.log` EXIT=0) | `test-neg-1.log`: all 3 new tests FAILED | 8 (expected) |
| Restored and passing | `git checkout HEAD -- include src`, rebuilt (`build-10.log` EXIT=0) | `test-7.log` | 0 |
| Neighbours | ReversibilityUndo, StableTrackIds, TrackFolder, AutomationLaneChild{,Gui}, TrackAddRouting, ChainPresetMenu, HybridTrack{RoundTrip,Gui}, ControlHybridTrackTranscript, ControlCommandsSnapshot | `test-5.log` (built `build-9.log` EXIT=0) | 11/12 pass; ReversibilityContractTest failed only on the published histogram, updated -> `test-6.log` exit 0 |

Builds: `cmake --build build --target zene HybridTrackInstrumentRoleTest HybridTrackRoleGuiTest ... -- -j4`: `build-2.log` (zene) EXIT=0,
`build-4.log`, `build-8.log`, `build-10.log` EXIT=0. `build-1.log` EXIT=2 (ccache temp dir outside the write area; then
`CCACHE_TEMPDIR=/tmp/ccache-tmp-q2832`), `build-3.log` EXIT=2 (make resolved the new test targets before CMake regenerated; rerun passed).
Two intermediate test runs failed and were fixed: `test-1.log` (the QtTest executables had no plug-in dir: added to the
ENABLE_EXPORTS / `LMMS_TEST_PLUGIN_DIR` list like `TrackAddRoutingTest`), `test-2.log`/`test-3.log` (the model index was right but
the redrawn row was appended last: the view is now moved to the row's index, queued behind the editor's own queued view creation).

## Gates

`feature_check.py` cannot create its `<worktree>-feature-check` directory from this sandbox (`gates-1.log`, PermissionError), so a
copy with only the log directory changed (to `logs/feature-check`) was run: `gates-2.log` EXIT=0 - fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length: no new item in my files.
Snapshot regenerated live (`snapshot-1.log`, EXIT=0, 925 ids / 78 groups: +`track.set_role`); README canary updated;
`doc-figures-gate.py` `docfig-2.log` exit 0 (`docfig-1.log` exit 1 before the canary update).

## Self-check

- **Every criterion proven by a test I ran:** yes - table above, `test-7.log` exit 0 (8/8).
- **New tests fail with the change reverted, pass restored:** yes - `test-neg-1.log` exit 8 (3/3 failed; `HybridTrackInstrumentRoleTest`
  5 of 6 cases, the sixth is a format guard that passes on the base), restored + `build-10.log` EXIT=0, `test-7.log` exit 0.
- **GUI and agent through the same command:** the menu items call `ControlRegistry::invoke("track.set_role", ...)`
  (`src/gui/TrackRoleMenu.cpp`); `HybridTrackRoleGuiTest` triggers the real menu actions after `aboutToShow`; the transcript drives the
  same command over the socket.
- **One undo step, honest class:** `track.set_role` records one structural step (before/after XML) - `true_inverse`, table row in
  `ControlReversibilityTableStructure.cpp`; depth +1 asserted in core and GUI tests; undo/redo asserted. `track.get_state` stays
  `not_mutating` (schema gained `role`, `kept_roles`). A16 histogram line updated (`rows=934 true_inverse=460`), `test-6.log` exit 0.
- **Snapshot / doc figures:** `snapshot_live.py` exit 0; `doc-figures-gate.py` exit 0; `ControlCommandsSnapshot` passes.
- **Docs:** manual `data/manual/index.html#neutral-track-instrument`; `docs/050-features/Q050-283.2.md` (criteria table, command,
  format, limits).
- **Realtime:** no audio-thread code changed. The switch, persistence and menu run on the GUI/control thread; track removal and creation
  go through the product's own paths that take `requestChangeInModel` themselves (as track.remove and its undo do). TSan does not run
  on this box - reasoned, not measured.
- **Projects:** old projects unchanged (no new `Track::Type`; a neutral row without a kept role saves the stage-1 bytes -
  `aRowWithoutAKeptRoleSavesNoMark`; stage-1 `anOldProjectLoadsAndSavesNoNeutralMark` passes). New data round-trips
  (`theKeptRoleRoundTripsThroughSaveAndLoad`, GUI save/load case, transcript D).
- **Portability:** only Qt APIs present in 5.15 (QDomDocument/QDomNamedNodeMap, QMetaObject::invokeMethod with a functor - Qt >= 5.10,
  QObject::connect functor with context + Qt::QueuedConnection); no `small/near/far/interface/min/max` identifiers, no std::filesystem,
  no POSIX calls. Built here on Qt 6.12/GCC only; Qt 5.15/MSVC/macOS not built - reasoned.
- **Known limits (documented):** automation clips aimed at the kept instrument's knobs are not re-linked; a channel the instrument used
  stays in the mixer while neutral; audio input is stage 3.
- **Completion check:** `submit_check.py` (`submit-1.log`, exit 1): every item PASS except "gates (feature_check.py)", which fails
  only because `feature_check.py` cannot `mkdir` `<worktree>-feature-check` from this sandbox (PermissionError, as in `gates-1.log`).
  The same gates, with only the log directory moved inside the run folder, PASS (`gates-2.log`, exit 0). The loop's own run
  outside the sandbox is the authoritative one.
