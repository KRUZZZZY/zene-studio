IMPLEMENTED

Q050-125 - Edit clip gain, envelope and polarity on the waveform (L05 audio editing). A sample clip's gain is
now a handle on its waveform: drag it and the level follows the pointer, the drawn waveform growing or shrinking
with it, and the release is one `clip.set_gain` step. Alt-click the waveform to add a gain-envelope point and
drag it in time and level (a plain press on a point moves it; points dragged past each other are re-sorted,
which is the order `clip.gain_envelope_set` requires). The Gain and fades submenu adds **Invert polarity**
(`clip.set_polarity`), which flips the clip's rendered sign and mirrors its drawn waveform while the SOURCE
sample keeps every sample. The drawn amplitude is `clipGainCurveAt` (the one definition the audio path and the
automation editor's ghost already share) times the polarity sign, sampled per drawn column, so the waveform
shows the level the clip renders at. GUI and agent surface run the same registered commands.

Acceptance (commands as run from `build/tests`; exit codes are the ctest exits):

| Criterion | Command | EXIT |
| --- | --- | --- |
| Drag the clip gain handle and add/move envelope points directly on the waveform; verify the drawn waveform follows the gain curve | `ctest -R ClipWaveformEditTest --output-on-failure` (QtTest `draggingTheGainHandleSetsTheGainAndTheWaveformFollows`, `envelopePointsAreAddedAndMovedOnTheWaveform`, `anAddedPointDraggedPastItsNeighbourTakesTheRightPoint`, `theGestureGeometryIsExact`: real mouse press/move/release on the real `SampleClipView` in a real `GuiApplication`, the press at the handle's OWN drawn centre; the gain rises above unity and the drawn band's top moves UP, one `control.undo` restores 1.0, a downward drag cuts it and the band moves down; Alt-click adds a point at the pressed tick, a plain drag moves it to a quarter of the clip, one undo each; an Alt-added point dragged past the first point moves the ADDED point, not the tail; the fade hit test does not take the handle's own centre) | 0 |
| " (the drawn curve is the rendered multiplier) | `ctest -R ClipPolarityTest --output-on-failure` (`theDrawnCurveIsTheRenderedMultiplier`:  the curve the view samples compared at 100+ frames with what the production play handle rendered for a clip with -6 dB gain, a 3-point envelope and inverted polarity; every sampled frame is the unedited render times the drawn curve within 2e-3) | 0 |
| Invert one clip's polarity and verify its rendered waveform changes sign without changing the source sample | `ctest -R ClipPolarityTest --output-on-failure` (`invertingOneClipFlipsItsRenderAndLeavesTheSourceAlone`: two clips share one source buffer; inverting one renders every frame as its exact negative, the OTHER clip renders identically, and the shared buffer's samples are unchanged sample for sample) | 0 |
| " (the display half) | `ctest -R ClipWaveformEditTest --output-on-failure` (`thePolarityMenuItemFlipsTheClipThroughTheCommand`: the real Gain and fades menu's checkable Invert polarity item triggers `clip.set_polarity`; the clip reports inverted, the view repaints a different image, the item is checked on the next menu build, one undo restores it) | 0 |
| One undo step per edit, no shadowing no-op | `ctest -R ClipWaveformEditTest --output-on-failure` (`aClickWithoutMovementRecordsNoStep`: a press/release on the handle or on a point without moving leaves `control.undo_depth`'s `depth` unchanged, so the previous real edit is still the next undo) | 0 |
| Verify gain, envelope, polarity, undo and read-back through the agent command surface | `ctest -R ControlClipPolarity --output-on-failure` (`tests/control-clip-polarity.py`: a real headless `zene` over the control socket - `clip.set_gain` -6 dB read back from the command and from `arrangement.get_state`, one undo; `clip.gain_envelope_set`/`get` point for point, one undo; `clip.set_polarity` read back including through `arrangement.get_state`, one undo, and a repeated request leaving no step of its own) | 0 |
| Old projects load/render exactly as before; new data round-trips | `ctest -R ClipPolarityTest --output-on-failure` (`theClipElementCarriesItOnlyWhenInverted`: a neutral clip writes NO `invert` attribute, an inverted one writes `invert="1"`, an element carrying it restores inverted, an element without it restores off even onto a previously inverted clip) and `ControlClipPolarity` (`check_round_trip`, `check_legacy_default`: a real `project.save` -> `project.open` keeps the gain, the envelope point for point and the polarity; a clip created without the attribute reports `inverted: false`) | 0 |

Gates (from the repo root): `python3 tests/doc-figures-gate.py` 0 (canary 846 ids / 76 groups);
`ctest -R ReversibilityContractTest` 0 (published A16 figure re-taken: 855 rows, 424 true_inverse, 42 snapshot,
24 irreversible, 365 not_mutating); `ctest -R ControlCommandsSnapshot` 0 (snapshot regenerated live);
`ctest -R ControlRegistryTest` 0; `ctest -R 'ClipEditsTest|ClipFadesRenderTest|ClipGainEnvelopeTest|ClipEditsMenuTest|LinkedClipEditsTest|ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity|ReversibilityContractTest|ControlRegistryTest'`
0 (10/10); `bash tests/fork-sources-gate.sh` 0 (`tests/all-sources.txt` re-derived from its own header recipe, which
also removed the 1148 stale `plugins/SfzPlayer/sfizz/` entries train-12's merge left listed);
`bash tests/all-sources-reproduce.sh` 0; `bash tests/no-upstream-regression-gate.sh` 0 (every inherited file
this unit touched is a declared divergence); `bash tests/no-tautology-gate.sh` 0.
Gate 4 (complexity) and gate 7 (file length) exit 1 as the train leaves them: the remaining complexity rows are
files this unit did not touch (`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`,
`src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, ...), and the file-length list is
the train's own grandfathered set. The one regression this unit DID introduce - `withGainPoint` at CCN 11 - was
refactored away (`gainPointAllowed` split out) and no longer appears.

## Negative control

Every non-test hunk was reverted while the new tests were kept (`git checkout --` on the 16 product files,
then behaviour reversals in `SamplePlayHandle.cpp` sign, `ClipPreFades.cpp` sign, `ControlEditSupport.cpp`
state key, `SampleThumbnail.cpp` column gain, `SampleClipView.cpp` drag write and `ControlCommandsClipEdits.cpp`
handler write, so the tree still compiled), rebuilt (`cmake --build build --target zene ClipPolarityTest
ClipWaveformEditTest`, build EXIT 0), and run:

```
ctest -R "ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity" --output-on-failure   -> EXIT 8
  ClipPolarityTest::invertingOneClipFlipsItsRenderAndLeavesTheSourceAlone  FAIL (frame 0 renders -0.4, the mirror of -0.4 is 0.4)
  ClipPolarityTest::theCommandSetsReadsBackAndUndoesInOneStep             FAIL
  ClipPolarityTest::theDrawnCurveIsTheRenderedMultiplier                  FAIL
  ClipWaveformEditTest::draggingTheGainHandleSetsTheGainAndTheWaveform... FAIL (the gain drag left gain at 1)
  ClipWaveformEditTest::envelopePointsAreAddedAndMovedOnTheWaveform       FAIL
  ClipWaveformEditTest::thePolarityMenuItemFlipsTheClipThroughTheCommand  FAIL
  ControlClipPolarity                                                     FAIL
```

Restoring the product hunks and rebuilding (`build EXIT 0`) makes the same three tests pass:

```
ctest -R "ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity" --output-on-failure   -> EXIT 0 (100%, 3/3)
```

### Re-verified in this session (r1)

Three defects found while re-checking the earlier work, each fixed and each pinned by a test that fails
without the fix:

1. **The gain handle could not be grabbed where it is drawn.** At unity on a 25 px clip its centre sat in the
   zero-length fade-in handle's hit strip at the same x, and `startFadeDrag` runs first, so the press became a
   fade drag. The earlier GUI test pressed at mid-height, which happened to miss the strip. Fix: the handle is
   held below the fade strip (inside the clip on a very short one), the level line stays at its true dB, a tick
   joins the two. Negative control (old placement restored, `build EXIT 0`):
   `ctest -R ClipWaveformEditTest` -> **EXIT 8** (`a boosted clip paints its waveform at y=8, the same as unity's 8`
   with the handle test still pressing mid-height would have passed; with the handle test pressing its own
   centre it fails `the gain drag left gain at 1`, and `theGestureGeometryIsExact` fails its handle-centre
   assertion). Restored -> **EXIT 0**.
2. **An Alt-added point dragged past its neighbour moved the wrong point.** `m_gainDragPoint` was the pre-sort
   end index; after the sort the added point may sit anywhere. Fix: `withGainPoint` answers the post-sort index.
   Negative control (old append index restored, `build EXIT 0`):
   `ctest -R ClipWaveformEditTest` -> **EXIT 8** (`the tail point sits at tick 95`, not 144). Restored -> **EXIT 0**.
3. **A click without movement recorded a no-op undo step**, shadowing the edit before it. Fix: the drag tracks
   real pointer movement (and a drag that returns to the stored value writes nothing). Negative control
   (`moved` forced true, `build EXIT 0`): `ctest -R ClipWaveformEditTest` -> **EXIT 8**
   (`depth() -> 2`, expected 1). Restored -> **EXIT 0**.

The wider set re-run after the fixes (from `build/tests`):

```
ctest -R 'ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity|ClipEditsTest|ClipFadesRenderTest|ClipGainEnvelopeTest|ClipEditsMenuTest|LinkedClipEditsTest|ReversibilityContractTest|ControlRegistryTest|ControlCommandsSnapshot|AudioEditingTest|NoteBackgroundAudioTest|ControlSocketIntegration|ControlResultSchemaTest' --output-on-failure
  -> EXIT 0 (15/15)
```

## Self-check

Answered in the order `submit_check.py` asks ("?" items), with the commands and exits seen in this session.

1. **Each new test FAILS with the product change reverted, then PASSES restored.** Proven three ways, each with
   a build that still compiled (`cmake --build build --target zene ClipPolarityTest ClipWaveformEditTest`,
   build EXIT 0) and each exit seen:
   - the 16 product hunks reverted plus behaviour reversals in the polarity sign (`SamplePlayHandle.cpp`,
     `ClipPreFades.cpp`), the state key (`ControlEditSupport.cpp`), the drawn column gain
     (`SampleThumbnail.cpp`), the drag write (`SampleClipView.cpp`) and the command write
     (`ControlCommandsClipEdits.cpp`):
     `ctest -R 'ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity'` -> **EXIT 8**;
     restored and rebuilt -> **EXIT 0** (3/3).
   - drawn-gain-alone reversed (`SampleThumbnail.cpp` `columnGain = 1.0f`), everything else kept:
     `ctest -R 'ClipWaveformEditTest|ClipPolarityTest'` -> **EXIT 8**
     (`louderTop < plainTop` FALSE, `the polarity flip did not repaint the waveform`); restored -> **EXIT 0**.
   - the three defects of this session, each with its own reversal (see "Re-verified in this session (r1)"):
     old handle placement -> **EXIT 8**; old pre-sort point index -> **EXIT 8**; `moved` forced true ->
     **EXIT 8**; each restored -> **EXIT 0**.
2. **Every acceptance criterion is proven by a test I ran**, per criterion and with its exit in the acceptance
   table above (all 0): gain drag + envelope add/move + drawn waveform follows (`ClipWaveformEditTest`),
   polarity render and source untouched (`ClipPolarityTest`), the menu display half
   (`ClipWaveformEditTest::thePolarityMenuItemFlipsTheClipThroughTheCommand`), the agent surface
   (`ControlClipPolarity`), persistence/legacy (`ClipPolarityTest::theClipElementCarriesItOnlyWhenInverted`
   plus the socket `check_round_trip`/`check_legacy_default`), and one undo step
   (`ClipWaveformEditTest::aClickWithoutMovementRecordsNoStep` + the socket test). Machine-readable exits:
   `ctest -R 'ClipPolarityTest|ClipWaveformEditTest|ControlClipPolarity|ClipEditsTest|ClipFadesRenderTest|ClipGainEnvelopeTest|ClipEditsMenuTest|LinkedClipEditsTest|ReversibilityContractTest|ControlRegistryTest|ControlCommandsSnapshot|AudioEditingTest|NoteBackgroundAudioTest|ControlSocketIntegration|ControlResultSchemaTest'`
   -> **EXIT 0 (15/15)**.
3. **One undo step per edit, honest reversibility.** Every gesture writes exactly one registered command on
   release and the tests undo once and compare; a stationary press, a drag that returns to the start and a
   repeated `clip.set_polarity` each add no step (`control.undo_depth` depth unchanged; the socket test's
   `repeating the current polarity leaves no undo step of its own`). `clip.set_polarity` is `true_inverse` -
   the flag is part of the Clip's serialized state, so the Clip checkpoint is a live inverse; no new read
   command, the read-back rides the existing `inverted` key. `ReversibilityContractTest` -> **EXIT 0**
   (855 rows: 424 true_inverse, 42 snapshot, 24 irreversible, 365 not_mutating).
4. **Nothing added runs on the audio thread allocating, freeing, locking, waiting, doing I/O or emitting a
   signal.** The diff touches `SamplePlayHandle::applyClipEdits` and `SamplePlayHandle::applyPreFaded`
   (`ClipPreFades.cpp`) only: each reads `m_edits` - the plain `ClipEdits` POD snapshot
   `snapshotClipEdits` already took on the interface thread at construction - multiplies one constant
   `polaritySign()` into the existing per-frame gain, and loops over the caller's buffer. No allocation, no
   free, no lock, no wait, no I/O, no Qt object and no signal is added; the new `gainAt` `std::function` and
   all drag state live on the interface thread (paint and mouse handlers only). TSan does not run on this box;
   this is the same snapshot pattern the gain and fades have used since ED-042, so the change adds no new
   cross-thread access.
5. **Old projects load and render exactly as before; new data round-trips.** A neutral clip writes no `invert`
   attribute (asserted on the saved element), absence resets it off even onto a previously inverted clip, the
   neutral-render identity in `ClipFadesRenderTest` passes (**EXIT 0**), and a real `project.save` ->
   `project.open` keeps gain, envelope point for point and polarity (`ControlClipPolarity` **EXIT 0**). The
   clip-gain and envelope drags use the commands that already round-tripped.
6. **Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API: `QPolygon`, `QPoint`, `QMouseEvent`,
   `QAction`, `QMenu` (all Qt 5.15), `std::clamp`, `std::stable_sort`, `std::equal`, `std::function` (C++20);
   `entry.format`/`clipGainHandleAt` etc. use no reserved macro name (`min`/`max` are not used as identifiers
   in the new code); no `std::filesystem`; no POSIX-only call. `tests/scripted/check-namespace` -> **EXIT 0**.
   Nothing new is Qt-version-guarded because nothing new is Qt-6-only.

Additional evidence:

- **The GUI test drives the GUI, not the handler.** Real `QMouseEvent`s on a real `SampleClipView` found
  through the live arranger in a real `GuiApplication` (`guitest::startGui`), reading the model and the
  rendered `QImage` back; the menu case builds the real Gain and fades menu and triggers the real checkable
  action.
- **Gates.** A patched copy of the stock script (its `wt.parent` log dir cannot be created under this
  sandbox's file policy) reports `feature-check ... RESULT: PASS`: fork-sources 0, all-sources-reproduce 0,
  no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0 (846 ids / 76 groups),
  complexity "no new item in your files", file-length "no new item in your files". The stock
  `feature_check.py`/`submit_check.py` fail on the log-directory `mkdir` alone (EACCES under this session's
  policy), not on a gate.
- **`tests/all-sources.txt`** was stale for the two new test files (they were registered in
  `tests/fork-sources.txt` but missing from the whole-tree manifest); re-derived from its own header recipe,
  which also cleared the stale `plugins/SfzPlayer/sfizz/` entries, and `all-sources-reproduce.sh` now prints
  REPRODUCES (**EXIT 0**).
- **`ControlSocketIntegration`** failed only because the plugin modules it loads were not built in this
  checkout; after `cmake --build build --target amplifier bassbooster tripleoscillator audiofileprocessor
  ladspaeffect lv2effect lv2instrument` (**EXIT 0**) it passes (**EXIT 0**). No product change caused it.
