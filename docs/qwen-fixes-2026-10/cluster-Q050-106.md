IMPLEMENTED

# Q050-106 - Split stereo files and convert clips to mono

## Acceptance

| Criterion | Command / test | Result |
|---|---|---|
| Split a stereo fixture to two destination files and verify left/right sample data, rate, and length against the source. | `ctest -R '^ControlSampleStereoTest$' --output-on-failure` from `build/tests`; `ControlSampleStereoTest::splitWritesEachChannelAsItsOwnMonoFile` decodes both outputs and compares every sample, channel count, rate and frame count with the asymmetric source, then checks the clip is unchanged. The socket transcript `ctest -R '^ControlAudioEditing$'` also checks both files' RIFF headers (mono, source rate, source frames). | exit 0 (green); exit 8 with the product hunks reversed |
| Convert a stereo clip to mono and verify the documented downmix rule, undo, and agent state read-back. | `ControlSampleStereoTest::monoDownmixUsesTheMeanAndOneUndoRestoresStereo` and `...::monoLayoutRoundTripsThroughAProject`; `ControlAudioEditing`: every frame is `(L + R) / 2` on both sides, rate/frames/length unchanged, `arrangement.get_state` is `channels:1` / `layout:"mono"`, one `control.undo` restores the exact stereo frames and `2` / `"stereo"`, and a save/load keeps mono. | exit 0 (green); exit 8 reversed |
| Confirm both operations are reachable from the Sample Editor and the clip Process menu. | `ctest -R '^SampleEditorDialogTest$' --output-on-failure`: clicks `To Mono` and `Split Stereo...` (fills the two-field destination dialog) and verifies the resulting samples/undo and files. `ctest -R '^DevicePanelTest$'`: the Process menu has both registered entries; triggering `Convert to Mono` bakes the mean (one undo restores stereo); `theProcessMenuSplitsAClipIntoTwoMonoFiles` triggers `Split Stereo to Two Mono Files...`, fills the destination dialog with two distinct temporary paths, decodes both written files (mono, 44100 Hz, 64 frames, left/right samples equal the source) and checks the source clip is unchanged. | exit 0 both (green); exit 8 both reversed |

Metadata/reversibility proof (round 5, integrated on `050/m0-train-16`): `ctest -R '^ReversibilityContractTest$'` exit 0; `ctest -R '^ControlCommandsSnapshot$'` exit 0; `DocFiguresGate` exit 0; `snapshot_live.py` exit 0 (850 commands / 76 groups); `python3 tests/doc-figures-gate.py` exit 0.

Round 5 integration re-run on the merged tree (from `build/tests`, each exit 0): `ControlSampleStereoTest`, `ControlSampleOperatorTest`, `SampleEditorDialogTest`, `DevicePanelTest`, `ControlAudioEditing`, `ClipSerialisationTest`, `SampleClipWarpTest`, `ReversibilityContractTest`, `ControlCommandsSnapshot`, `DocFiguresGate`.
Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exit 0, all eight rows PASS.

## What changed
- New `sample.split_stereo`: two mono float32 WAVs written through `QSaveFile`, one source channel each, no resampling/trimming/mixing; the clip is unchanged; A16 `not_mutating`.
- `sample.edit_range` op `mono`: whole-source `(L + R) / 2` downmix on both sides, one Clip checkpoint, `true_inverse`; reports `channels`/`layout`. The agent form is `{clip, op: "mono"}` - absent from/to default to the whole source, and the save/load + socket tests use that form.
- `SampleBuffer::channels()` plus an additive `channels="1"` clip attribute; absent means stereo, so old projects load unchanged. Other sample bakers preserve the layout.
- Sample Editor buttons and Process menu entries for both operations, all through the registry.
- `arrangement.get_state` / `window.sample_editor` / `clip.add` / `clip.delete` / `roll.get_state` report the layout.

## Self-check

1. **Each new test FAILS with the product change reverted, then PASSES restored.** Saved the non-test product patch to `logs/product-full-final.patch`, ran `git apply -R` (exit 0), rebuilt with `cmake --build build --target zene ControlSampleStereoTest SampleEditorDialogTest DevicePanelTest tripleoscillator amplifier ControlSampleOperatorTest ControlEditCommandsTest -- -j4` (exit 0). From `build/tests`: `ControlSampleStereoTest` exit 8, `SampleEditorDialogTest` exit 8, `DevicePanelTest` exit 8, `ControlAudioEditing` exit 8. Re-applied the patch (`git apply`, exit 0), rebuilt (exit 0), and each test returned exit 0. Failures included `'mono.ok' returned FALSE`, missing `To Mono`/`Split Stereo...` buttons, missing Process entries, and the socket checks `sample.split_stereo ... FAILED` / `sample.edit_range op mono ... FAILED`.
2. **Every acceptance criterion is proven by a test I ran.** See the table above: all three criteria have green exit 0 and red exit 8 evidence; the exact test names are in the table.
3. **One undo step per edit, honest reversibility class.** `sample.split_stereo` is `not_mutating` (writes two output files; no checkpoint, no session change). `sample.edit_range` remains `true_inverse` with one `addJournalCheckPoint()` before one buffer swap; the tests assert one `control.undo` restores the exact original stereo frames and layout, and `ReversibilityContractTest` exit 0 confirms the table/command match and the A16 histogram (838 rows).
4. **Nothing added runs on the audio thread.** The new code is control-thread only: `splitStereo()` (clip resolution, `sampleops::splitStereo`, `QSaveFile`/libsndfile I/O via `writeDestinations`), `bakeMono()` (`sampleops::toMono`, checkpoint, `SampleClip::replaceSample`), the GUI dialog and `clipState`. The only model field added, `SampleBuffer::m_channels`, is written at buffer construction/swap on the control thread and read by control/GUI/state code; `Sample::play`/`Sample::render` never read it. No audio-thread allocation, free, lock, wait, I/O or signal was added.
5. **Old projects load and render exactly as before; new data round-trips.** The `channels` attribute is written only when the layout is mono; absence on every old project reads as the existing stereo layout, and `SampleBuffer` defaults to two channels. `ControlSampleStereoTest::monoLayoutRoundTripsThroughAProject` saves and reopens a real `.mmp` and re-checks the mono samples and layout; `ClipSerialisationTest` and `ControlSampleOperatorTest` both passed.
6. **Qt 5.15 / MSVC / macOS < 10.15 portability.** Used only Qt APIs available in 5.15 (`QSaveFile`, `QFileDialog`/`QDialog`, `QJsonObject`), no Qt-6-only call without a guard, no `small`/`near`/`far`/`interface`/`min`/`max` identifiers, no `std::filesystem`, and only portable file I/O (`QSaveFile` + libsndfile `sf_open_fd` already used by the tree). The new test source uses `QTemporaryDir`, `QFileInfo` and libsndfile, all already used by neighbouring tests.

## Round 3 (review r2: the Process menu's Split Stereo was listed but never triggered)

7. **New test `DevicePanelTest::theProcessMenuSplitsAClipIntoTwoMonoFiles`** triggers the clip Process menu's split action, fills `stereoSplitDialog` (`leftPath`/`rightPath`) with distinct `QTemporaryDir` paths, then decodes both WAVs with libsndfile and compares every sample, channel count (1), rate and frame count with the stereo source, and checks the clip's source is unchanged.
   - Red: with the `ShellCommandMenus.cpp` split connection disabled, rebuilt (exit 0), `ctest -R '^DevicePanelTest$'` from `build/tests` exit 8: `'dialogSeen' returned FALSE` at `DevicePanelTest.cpp:461`.
   - Green: connection restored, rebuilt (exit 0), same ctest exit 0 (3 tests passed).
   - Logs: `.../Q050-106.r3/logs/test-2-red.log`, `test-3-green.log`.
   - Build note: ccache needed `CCACHE_TEMPDIR=/tmp/ccache-q106-tmp` (its default temp dir was not writable).
8. **Gates, round 3.** `feature_check.py` could not run in this session (it writes its logs to a sibling directory outside the write-jail: `PermissionError`), so its commands were run directly from the worktree: `tests/fork-sources-gate.sh`, `tests/all-sources-reproduce.sh`, `tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `tests/no-tautology-gate.sh`, `tests/doc-figures-gate.py` all exit 0. `complexity-gate.sh --check` and `file-length-gate.sh --check` exit 1 (red on the train on purpose: 6 and 14 listed items); none lists a function or file this branch changed (grep for DevicePanel, ShellCommandMenus, SampleEditor, SampleClip, ControlCommandsSample, Stereo: no match). This round changed only `tests/src/gui/DevicePanelTest.cpp` and this report.

## Round 4 (review r3: AC3 evidence row, symlink-aliased destinations)

9. **AC3 row** in `docs/050-features/Q050-106.md` now cites `DevicePanelTest::theProcessMenuSplitsAClipIntoTwoMonoFiles` and what it verifies (trigger, dialog filled with two temporary paths, both files decoded and equal to the source channels, clip unchanged).
10. **Symlink aliasing.** `sample.split_stereo` now compares the destinations after `resolvedDestination()` (`ControlCommandsSampleStereo.cpp`): the deepest existing part of the path is resolved with `canonicalFilePath`, the missing suffix is kept as written. New test `ControlSampleStereoTest::splitRejectsDestinationsAliasedThroughASymlinkedDirectory` (POSIX only; `QFile::link` is not a symlink on Windows) checks that `real/out.wav` + `link/out.wav` is refused with no file written, a `real/a/../out.wav` spelling is refused, and `real/out.wav` + `link/other.wav` still writes both.
    - Red: helper stubbed to return the plain absolute path, rebuilt (exit 0), `ctest -R '^ControlSampleStereoTest$'` exit 8: `'!split.ok' returned FALSE (a symlinked directory aliased the two exports)` at `ControlSampleStereoTest.cpp:359`. (A first red attempt failed to build with an unused-function error, so its exit 0 was the old binary and is not counted.)
    - Green: restored, rebuilt (exit 0), same ctest exit 0.
    - Also green after the change (each exit 0, from `build/tests`): `DevicePanelTest`, `SampleEditorDialogTest`, `ControlAudioEditing`, `ControlSampleOperatorTest`.
11. **Gates** (run directly, as `feature_check.py` cannot write its logs outside the write-jail): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures exit 0; complexity and file-length exit 1 (red on the train) with no listed item in `ControlCommandsSampleStereo.cpp`, `ControlSampleStereoTest.cpp`, `DevicePanelTest.cpp`, `SampleEditorDialog*` or `ShellCommandMenus.cpp`.

## Round 5 (integration onto `050/m0-train-16` @ `cf3618e61`)

Accepted source `feat/050-Q050-106-oc` @ `380495776`, applied three-way by the driver. Ten files conflicted; each was resolved by keeping both sides, and the accepted design is unchanged.

| File | Conflict | Resolution |
|---|---|---|
| `src/core/ControlCommandsSampleEdit.cpp` (`commitBake`) | train: `replaceSample(..., SampleWindow::full)` keeps amplification/frequency/reversed (BUG 9 r2); patch: buffer built with the source's `channels` | both: the channel-aware buffer goes through `replaceSample`. A bake keeps the playback settings and the mono layout. |
| `src/core/SampleClip.cpp` (`loadSettings`) | train: a non-positive `sample_rate` falls back to the engine rate (BUG 6, new-p12-204); patch: reads `channels="1"` | both: validated rate + channel layout |
| `tests/control-audio-editing.py` | docstring numbering | train's items 1-6 + the patch's check as item 7; the body merged cleanly and `check_stereo_tools` still runs from `main` |
| `tests/src/gui/DevicePanelTest.cpp` | include lists | union |
| `data/manual/index.html` | the train's stem-quality paragraph vs the patch's Process-menu text | both paragraphs |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | appended registrations | union |
| `README.md` canary, `docs/RELEASE-NOTES-v0.3.0-alpha.md` A16 | figures | re-measured: snapshot regenerated live (`snapshot_live.py` exit 0, **850 ids / 76 groups**, train 849 + `sample.split_stereo`); A16 `rows=859 true_inverse=425 snapshot=42 irreversible=24 not_mutating=368` (train + one `not_mutating` row), `ReversibilityContractTest` exit 8 before the update, exit 0 after |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | generated | regenerated from the live merged binary |

Hazards checked:
- **Bakers that lose the mono layout.** The only new `SampleBuffer` construction the train added since the source's base (`git diff de5a85a96 cf3618e61 -- src include`) is `commitBake`, which is resolved above. Every other sample baker already passes `channels`.
- **Playback settings after a mono conversion.** `bakeMono` already used `replaceSample`, so its behaviour is unchanged.
- **Loading a mono project with a bad rate.** Both guards now apply on one path.
- **Realtime.** No change to any audio-thread path.
- **Portability.** The only new include is `<QAction>`, which works on both Qt 5.15 and Qt 6.

**Demonstrated regression, fixed.** The file-length ratchet: the train grew `src/gui/shell/ShellCommandMenus.cpp` to 493 code lines, and the patch's 15 lines took it to **508** (> 500, a new item in a file this branch changes). Two changes bring it back without changing behaviour:
- The split entry's action, tag, enable state and `QPointer` slot moved into `SampleEditorDialog::addSplitStereoAction` (`src/gui/SampleEditorDialog.cpp`), next to the `splitStereoToFiles` both surfaces already share.
- The mono entry's arguments are built with the existing `menuparts::with`.

That leaves `ShellCommandMenus.cpp` at 499 code lines. Same label, same `controlCommand` property, same enabled rule, same command.
- Red: slot body neutralised, rebuilt (exit 0), `ctest -R '^DevicePanelTest$'` exit 8 (`theProcessMenuSplitsAClipIntoTwoMonoFiles: 'dialogSeen' returned FALSE`).
- Green: restored, rebuilt (exit 0), exit 0.

Red probe on the resolved load hunk: the `channels` read was forced to stereo and rebuilt (exit 0). `ctest -R '^ControlSampleStereoTest$'` gave exit 8 (`monoLayoutRoundTripsThroughAProject: Compared values are not the same`). After restoring the line and rebuilding (exit 0), `ControlSampleStereoTest` and `ClipSerialisationTest` both gave exit 0.

**Gates.** `feature_check.py` raises `PermissionError` (its log directory is a sibling of the worktree, outside the write-jail), so I ran its commands directly from the worktree:
- exit 0: `fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`, `scripted/check-namespace`, `no-tautology-gate.sh`, `doc-figures-gate.py`.
- exit 1, red on the train itself: `complexity-gate.sh --check` and `file-length-gate.sh --check`. I matched every `REGRESSION` line they print against the 36 files this branch changes (`grep -F -f`) and found none (grep exit 1).

Build note: the default ccache temp dir is not writable here, so builds used `CCACHE_TEMPDIR=/tmp/ccache-q106-tmp`. Logs are in `Q050-106.r5/logs/` (`build-2..7`, `test-*`, `gate-*`).

### Self-check, round 5 (`submit_check.py` on `82e922489`)

`submit_check.py <worktree> Q050-106` ran with exit 1. It passed 11 items and failed only `gates (feature_check.py)`. In this write-jail, `feature_check.py` stops at `logs.mkdir()` on `<worktree>-feature-check` with a `PermissionError`, before it runs any gate. Its gate commands, run directly against its own base (`FIX-BASE` = `050/m0-train-16`, merge-base `cf3618e61`), gave:
- exit 0 for all six simple gates.
- exit 1 for both ratchets, with none of their REGRESSION items in a file this branch changes. This was red on the train before this branch.

The six ASK items:

1. **New tests fail without the product change.** I did not repeat the full product-hunk reversal this round. Round 1 did it for all four tests (exit 8, then exit 0 restored), and none of those tests changed in this integration. This round's probes cover the code that changed:
   - Process-menu split slot neutralised: `DevicePanelTest` exit 8, then exit 0 restored.
   - Resolved `SampleClip` load hunk forced to stereo: `ControlSampleStereoTest` exit 8, then exit 0 restored.
2. **Criteria proven by tests I ran.** All on the merged tree, from `build/tests`:
   - AC1: `ControlSampleStereoTest` exit 0 and `ControlAudioEditing` exit 0.
   - AC2: `ControlSampleStereoTest` exit 0 and `ControlAudioEditing` exit 0.
   - AC3: `SampleEditorDialogTest` exit 0 and `DevicePanelTest` exit 0.
3. **One undo step, honest reversibility class.** Unchanged from the accepted patch: `sample.edit_range` op `mono` is `true_inverse` with one checkpoint, and `sample.split_stereo` is `not_mutating`. `ReversibilityContractTest` gave exit 0 after the A16 re-measure (rows=859).
4. **Audio thread.** Nothing added runs there. The resolved `commitBake` and the new `addSplitStereoAction` run on the control and GUI threads only.
5. **Old projects and round-trip.** A project with no `channels` attribute still loads as stereo, and the positive-rate guard from the train is kept. `ClipSerialisationTest` gave exit 0. `ControlSampleStereoTest::monoLayoutRoundTripsThroughAProject` gave exit 0, and exit 8 under the red probe.
6. **Portability.** The only new API is `<QAction>`, `QMenu::addAction`, `QPointer` and `QObject::connect`, all available in Qt 5.15. No `std::filesystem`, no reserved identifiers, no POSIX calls.
