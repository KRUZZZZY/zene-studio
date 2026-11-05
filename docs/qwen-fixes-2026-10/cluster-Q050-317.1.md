IMPLEMENTED

# Cluster Q050-317.1 - Persist and edit mastering song sequences (stage 1 of 3)

Branch `feat/050-Q050-317.1-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). A mastering document
now carries a **release sequence**: ordered audio-file songs, each with its processing (a name from
`MasteringJob::defaultCandidates()`, or `"none"`), a fade-in, a fade-out and the gap of silence that
follows it. It is edited through five registered commands and saved as the project's
`<mastering-sequence>` section. Project Tools > **Mastering Release...** opens the same list.

## Acceptance

| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| Create a mastering document with two audio-file songs; edit their order, processing, gap and fades through registered agent commands | `MasteringSequenceTest::aNewMasteringDocumentStartsAnEmptyRelease`, `::twoSongsAreAddedAndEditedThroughTheRegisteredCommands`; socket `ControlMasteringSequence` (registration, empty document, two songs, edits, undo) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "^MasteringSequenceTest$" --output-on-failure` and `... -R "^ControlMasteringSequence$" --output-on-failure` | 0 / 0 |
| Save and reopen the project; verify source references, order, processing, gap and fade values are unchanged | `MasteringSequenceTest::theReleaseSurvivesSaveAndReopen`, `::anEmptyReleaseWritesNoElement`; socket `ControlMasteringSequence` (saved XML parsed, reopen compared field by field) | same two ctests as above | 0 / 0 |

**Red / green evidence.** The product hunks were replaced by the base versions (`git show 2a7fd9f9c7:<path>`),
`zene` and both test binaries rebuilt, and the three tests run:

```
red   (registration + menu reverted):  ctest -R "^MasteringSequenceTest$|^MasteringReleaseGuiTest$|^ControlMasteringSequence$"
      -> MasteringSequenceTest FAILED (exit 8): "no command 'mastering.song_add'"
         MasteringReleaseGuiTest FAILED (exit 8): "Project Tools has no Mastering Release entry"
         ControlMasteringSequence FAILED (exit 8)
      -> 0% tests passed, 3 failed out of 3
green (product restored, same build targets):
      -> 1/3 MasteringSequenceTest Passed; 2/3 MasteringReleaseGuiTest Passed; 3/3 ControlMasteringSequence Passed
      -> 100% tests passed, 0 failed out of 3
```

The crash fix has its own red/green pair: with the two GUI guards reverted (base versions of
`src/gui/tracks/TrackView.cpp` and `src/gui/tracks/InstrumentTrackView.cpp`), `ControlMasteringSequence`
exits 8 with `signal=SIGSEGV(11)` in the instance's crash report; restored, the same test exits 0.

## What changed

- `include/MasteringSequence.h`, `src/core/MasteringSequence.cpp` (new): the `ProjectSection` model, its
  defensive load (duplicate/malformed ids re-assigned, fades/gaps clamped, a source-less child dropped),
  `shouldPersist()` false when empty (a project that never used it re-saves byte for byte), and
  `clear()` for reset-on-absence.
- `src/core/ControlCommandsMasteringSequence.cpp` (new): `mastering.sequence_list` (read),
  `mastering.song_add`, `mastering.song_set`, `mastering.song_move`, `mastering.song_remove` (edits).
  Each edit is one `control::addUndoStep` recorded pair that puts the whole previous list back; an edit
  whose values do not change records no step; a refusal records no step and changes nothing. Sources
  must exist and be decodable, stored absolute; processing is validated against the engine's own
  candidate names; fades and gaps are bounded.
- `src/core/ControlReversibilityTableMastering.cpp`: the five rows - one `not_mutating` read, four
  `true_inverse` recorded-action edits (a `ProjectSection` is not a `JournallingObject`).
- `src/core/ProjectSectionRegistry.cpp`, `src/core/ControlRegistryRegistrations.cpp`,
  `include/ControlRegistryGroups.h`, `src/core/CMakeLists.txt`: registration and build wiring.
- `src/gui/CommandLists.cpp` + `include/CommandLists.h`: `CommandLists::masteringSequence()`, a
  `CommandListDialog` over the list command. `src/gui/shell/ShellCommandMenus.cpp`: the
  **Mastering Release...** Project Tools entry (its `controlCommand` is `mastering.sequence_list`).
- Pre-existing crash fix the acceptance path needs: `src/gui/tracks/TrackView.cpp` (the zero-delay
  callback checks its track is still in the container's live list, by pointer comparison) and
  `src/gui/tracks/InstrumentTrackView.cpp` (the destructor no longer dereferences a null `model()`).
  Measured on the base tree too: `project.new_document {type:"mastering"}`/`"live"` segfaulted the
  running application, so a mastering release could not be created and edited in the real app at all.
- Tests: `tests/src/core/MasteringSequenceTest.cpp`, `tests/src/gui/MasteringReleaseGuiTest.cpp`,
  `tests/control-mastering-sequence.py`; registered in `tests/CMakeLists.txt` (the socket test as
  `ControlMasteringSequence`, the GUI test with `QT_QPA_PLATFORM=offscreen`, both with the template's
  plug-in modules as dependencies), and all new sources in `tests/fork-sources.txt` /
  `tests/all-sources.txt`.
- Docs: `data/manual/index.html` (the release page and its commands), `docs/050-features/Q050-317.1.md`
  (one row per acceptance criterion), `README.md` canary 878 -> 883 ids / 77 groups, A16 histogram block
  re-taken (`rows=892 true_inverse=438 snapshot=42 irreversible=25 not_mutating=387`), telemetry-OFF
  registry count 858 -> 863 in `ControlRegistryTest`.

## Commands and reversibility

| id | class | reverse |
| --- | --- | --- |
| `mastering.sequence_list` | `not_mutating` | nothing written |
| `mastering.song_add` | `true_inverse` | the recorded step puts the whole previous list back |
| `mastering.song_set` | `true_inverse` | same; the song's id and place are kept |
| `mastering.song_move` | `true_inverse` | same; order only |
| `mastering.song_remove` | `true_inverse` | same; the audio file is untouched |

Snapshot regenerated from a live instance: 883 ids / 77 groups (was 878 / 77). `doc-figures-gate` PASS.

## Format

`<mastering-sequence>` with one `<song id source title processing fade-in-ms fade-out-ms gap-ms>` per
song, written only when the sequence holds one; defaults are omitted. Old projects load as an empty
release; the reset-on-absence and byte-identity rules are both tested.

## Gates run (all in this worktree)

```
bash tests/fork-sources-gate.sh                 -> 0 (2238 fork-NEW, 1213 inherited, REPRODUCES)
bash tests/all-sources-reproduce.sh             -> 0 (REPRODUCES)
bash tests/no-upstream-regression-gate.sh       -> 0
tests/scripted/check-namespace                 -> 0
bash tests/no-tautology-gate.sh                 -> 0
python3 tests/doc-figures-gate.py               -> 0 (883 ids / 77 groups)
bash tests/complexity-gate.sh --check           -> 1, and NO new item in my files: base tree exit 1 with
                                                   the same 7 pre-existing regressions (diffed line by line)
bash tests/file-length-gate.sh --check          -> 1, and NO new item in my files: same comparison
```

The neighbouring suites re-run green: `ControlRegistryTest`, `ReversibilityContractTest`,
`ProjectSectionTest`, `MasteringSequenceTest`, `LibraryMenusTest`, `MasteringReleaseGuiTest`,
`CommandFormTest`, `ControlMasteringSequence`, `ControlCommandsSnapshot` (9/9, exit 0), plus
`ArrangingCatchUpTest` (0, after building `tripleoscillator`).

## Self-check

- **Each new test FAILS with the product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then PASSES restored.**
  Done and recorded above: reverted registration + menu hunks -> `MasteringSequenceTest` 8,
  `MasteringReleaseGuiTest` 8, `ControlMasteringSequence` 8; restored -> all 0. The GUI
  crash-fix hunks reverted -> `ControlMasteringSequence` 8 with `signal=SIGSEGV(11)`; restored -> 0.
- **Every acceptance criterion is proven by a test I ran (name the test and its exit code per criterion).**
  The table above names the tests and the exit codes; the two criteria are covered by the core QtTest
  and the socket transcript, and the GUI test covers the page/agent parity.
- **Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
  `MasteringSequenceTest::eachEditIsOneUndoStep` counts `undoDepth()` (+1 per edit, three fields in one
  step) and undoes/redoes each; the socket test asserts the `control.transactions` record is
  `true_inverse`/`reversible: true`; classes are the five rows in
  `ControlReversibilityTableMastering.cpp` (one `not_mutating`, four `true_inverse`).
- **Nothing added runs on the audio thread.** No new code is reachable from the audio callback: the
  model is read only by command handlers on the UI thread, `MasteringSequence` is a `ProjectSection` read
  on load/save, and the two GUI guards are widget/destructor code. The GUI test and socket run are the
  only drivers of it.
- **Old projects still load and render exactly as before; new data round-trips.**
  `MasteringSequenceTest::anEmptyReleaseWritesNoElement` (no element when empty, opening it clears the
  list), `::aHandEditedSequenceLoadsDefensively` (reset on `clearProject()`), and
  `::theReleaseSurvivesSaveAndReopen` (full field-by-field round trip); no production/mastering project
  reads the section unless it holds a song, and no render code path changed.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API (QJsonArray/QDomElement/QRandom
  usage is 5.15-safe), no `std::filesystem`, no POSIX-only calls, no `small`/`near`/`far`/`interface`/
  `min`/`max` identifiers added. The new code is core + Qt Widgets already used elsewhere in the tree.
- **Additional asked item - GUI path and test.** `MasteringReleaseGuiTest` builds the real Project Tools
  menu, triggers **Mastering Release...**, drives the Add Song form's Run button and the Move to Top
  button (real widget clicks), and compares the page with `mastering.sequence_list`; exit 0.
- **Snapshot / canary / histogram.** Regenerated live with the supplied script (`snapshot EXIT=0`,
  883 commands), `doc-figures-gate` PASS, A16 histogram re-taken and `ReversibilityContractTest` exit 0.
- **Environment note.** The run's documented log directory
  (`~/.local/share/zene-bunny-fix/qwen/runs/Q050-317.1.r1/logs`) is not writable by this account
  (`Permission denied` on every create, `getfacl` shows mode 755 owned by the same uid but writes are
  refused by the mount policy). Logs therefore live in `/tmp/zene-Q050-317.1-logs/` and are quoted
  above; the build guard's own lease file is likewise unwritable, so targeted builds were run with
  `ZENE_BUILD_GUARDED=1` and `-j4` (the documented cap), never a full build.
