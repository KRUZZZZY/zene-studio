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
| Drag the clip gain handle and add/move envelope points directly on the waveform; verify the drawn waveform follows the gain curve | `ctest -R ClipWaveformEditTest --output-on-failure` (QtTest `draggingTheGainHandleSetsTheGainAndTheWaveformFollows`, `envelopePointsAreAddedAndMovedOnTheWaveform`, `theGestureGeometryIsExact`: real mouse press/move/release on the real `SampleClipView` in a real `GuiApplication`; the gain rises above unity and the drawn band's top moves UP, one `control.undo` restores 1.0, a downward drag cuts it and the band moves down; Alt-click adds a point at the pressed tick, a plain drag moves it to a quarter of the clip, one undo each) | 0 |
| " (the drawn curve is the rendered multiplier) | `ctest -R ClipPolarityTest --output-on-failure` (`theDrawnCurveIsTheRenderedMultiplier`:  the curve the view samples compared at 100+ frames with what the production play handle rendered for a clip with -6 dB gain, a 3-point envelope and inverted polarity; every sampled frame is the unedited render times the drawn curve within 2e-3) | 0 |
| Invert one clip's polarity and verify its rendered waveform changes sign without changing the source sample | `ctest -R ClipPolarityTest --output-on-failure` (`invertingOneClipFlipsItsRenderAndLeavesTheSourceAlone`: two clips share one source buffer; inverting one renders every frame as its exact negative, the OTHER clip renders identically, and the shared buffer's samples are unchanged sample for sample) | 0 |
| " (the display half) | `ctest -R ClipWaveformEditTest --output-on-failure` (`thePolarityMenuItemFlipsTheClipThroughTheCommand`: the real Gain and fades menu's checkable Invert polarity item triggers `clip.set_polarity`; the clip reports inverted, the view repaints a different image, the item is checked on the next menu build, one undo restores it) | 0 |
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

## Self-check

- **Does the GUI test really drive the GUI?** Yes - it sends real `QMouseEvent`s to a real `SampleClipView`
  found through `getGUI()->songEditor()->m_editor->findChildren<SampleClipView*>()` in a real `GuiApplication`
  (`guitest::startGui`), and reads the model and the rendered `QImage` back. It does not call the handler
  directly. Command: `ctest -R ClipWaveformEditTest --output-on-failure` -> 0.
- **Is every user-facing action one registered command?** Yes: the gain drag calls `clip.set_gain`, the
  envelope drag calls `clip.gain_envelope_set`, and the Invert polarity item calls `clip.set_polarity` - the
  same three the socket test and the agent surface take. The `ControlClipPolarity` transcript proves the
  command side; `ClipWaveformEditTest` proves the GUI side reaches the same commands.
- **One undo step per edit?** Yes - each test does exactly one `control.undo` after each gesture/call and
  compares the model; a repeated `clip.set_polarity` records no step, so the next undo reaches the previous
  real edit.
- **Reversibility classes honest?** `clip.set_polarity` is `true_inverse` (part of the Clip's serialized state,
  so the Clip checkpoint is a live inverse); no new read command was added, and the read-back rides on the
  existing commands' `inverted` key. `ReversibilityContractTest` 0 after the A16 re-take (855 / 424 / 42 / 24 /
  365).
- **Realtime absolute?** The audio thread only reads `m_edits` - the POD snapshot `SamplePlayHandle` already
  took at construction (`snapshotClipEdits`) - and multiplies one constant `polaritySign()` into the existing
  per-frame gain loop. No allocation, free, lock, wait, I/O or Qt signal was added; the drag preview lives on
  the interface thread in `m_dragEdits`. TSan does not run locally; this is the same snapshot pattern the gain
  has used since ED-042, so no new cross-thread access exists.
- **Old projects and round-trip?** The neutral element writes no `invert`; absence resets it off; the neutral
  render identity in `ClipFadesRenderTest` still passes (0). A real save/reopen round-trips it.
- **Portability?** No Qt 6-only API added (the new code uses `QPolygon`, `QAction`, `std::clamp`,
  `std::stable_sort`); `polarityInverted` and `inverted` are not reserved identifiers; no `std::filesystem`
  and no POSIX-only call was added. The `std::function` in `VisualizeParameters` is standard C++20.
- **Snapshot/docs?** Regenerated live from this build: 846 ids / 76 groups (`README.md` canary updated
  845 -> 846); the A16 block re-taken to 855 rows. `docs/050-features/Q050-125.md`, `data/manual/index.html`
  and `docs/KNOWN-LIMITATIONS.md` describe the behaviour, the format and the known limits.
- **Pre-existing gate state (not this unit's).** `tests/complexity-gate.sh --check` and
  `tests/file-length-gate.sh --check` exit 1 on the train itself; neither names a file this unit added after
  the `withGainPoint` refactor. `tests/all-sources.txt` was stale at `07d6d02bd` (1148 `plugins/SfzPlayer/sfizz/`
  entries the recipe excludes); this unit re-derived it from its own documented recipe, which is why
  `all-sources-reproduce.sh` and `fork-sources-gate.sh` are now 0.
- **Sandbox note.** The harness refused file creation outside this worktree during reconnaissance (EACCES in
  the repo parent, `research/`, `docs/` and the run-log dir), so the stock `feature_check.py`/`submit_check.py`
  sibling-log paths could not be created; the submitted tree itself is complete and every gate above was run
  from inside the worktree, with logs under `build/qwen-logs/` (git-ignored).
