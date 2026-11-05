IMPLEMENTED

# Q050-231.1 - Cross-track masking report (stage 1/3): deterministic cross-track masking report

Branch `feat/050-Q050-231.1-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). The design, its limits and the
reversibility figures are in `docs/050-features/Q050-231.1.md`.

**What was built:**

- **`mixer.masking_report`** (`src/core/ControlCommandsMixerMasking.cpp`), `not_mutating`. For each selected channel
  it builds a long-term spectrum from the unmuted audio clips on the tracks that play into it, through the channel's
  strip EQ, in 24 log bands from 40 Hz to 16 kHz.
  - Each run of bands that two channels share is a clash. A clash reports the pair (`a`/`b`), `low_hz`/`high_hz`,
    `target`/`protect`, `reason`, `cut_db` (-1..-6) and `strip_eq` (the exact `mixer.channel_eq` arguments).
  - The target is the lower priority of front/middle/back. At equal priority it is the channel quieter in that
    range, and at equal level too, the later channel.
- **Mixer View > Masking Report...** (`src/gui/MixerMaskingReport.cpp`): tick channels, set each one's priority,
  press Report, and read the clashes table. Each Report is one call to the same registered command.

## Acceptance

| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| For a fixed multi-track test signal and priority assignment, repeated reports return the same clashing bands. | `MixerMaskingReportTest::repeatedReportsAreIdentical` (4 reports, compact JSON equal); `ControlMaskingReport` (real `zene` over the socket, 3 repeats byte-identical) | `ctest -R '^(MixerMaskingReportTest\|MixerMaskingReportWindowTest\|ControlMaskingReport)$' --output-on-failure` from `build/tests` (log `test-13.log`) | 0 |
| Each reported clash identifies its channel pair and proposed cut range and target; reporting leaves strip EQ and undo depth unchanged. | `MixerMaskingReportTest::clashesNameThePairTheRangeAndTheLowerPriorityTarget`, `equalPriorityCutsTheQuieterChannel`, `theStripEqShapesTheChannel`, `reportingChangesNoEqAndNoUndoDepth`, `badArgumentsAreRefused`; `MixerMaskingReportWindowTest::theMaskingReportWindowListsTheClashAndChangesNothing` (triggers the menu action, ticks, sets the priority combos, clicks Report twice); `ControlMaskingReport` (EQ of every channel and `control.undo_depth` unchanged) | same run | 0 |

Neighbouring and contract tests on the final tree: `ctest -R '^(MixerMaskingReportTest|MixerMaskingReportWindowTest|MixerWindowsTest|ControlMaskingReport|MixerToolsTest|ReversibilityContractTest|ControlCommandsSnapshot)$'`
gave exit 0, 7/7 (`test-12-restored.log`). The only source change after that run is the `int` cast in
`MixerMaskingReport.cpp`; I rebuilt after it, and the three new tests gave exit 0 (`test-13.log`).

## Self-check

- **Each new test fails with the product change reverted, then passes restored.**
  - Reverted the registration call (`ControlRegistryRegistrations.cpp`), the reversibility row and the
    Masking Report menu action (`MixerWindows.cpp`).
  - Rebuilt `zene MixerMaskingReportTest MixerMaskingReportWindowTest`: build exit 0 (`build-10-reverted.log`).
  - `ctest -R '^(MixerMaskingReportTest|MixerMaskingReportWindowTest|ControlMaskingReport)$'` gave **exit 8**,
    0/3 passed (`test-11-reverted.log`): 6 failing `MixerMaskingReportTest` cases, `open != nullptr` in the window
    test, and the socket driver failed.
  - Restored with `git checkout --` on those three files and rebuilt (exit 0): **exit 0**, 7/7
    (`test-12-restored.log`).
  - An earlier run of the same proof, made before the GUI test moved out of `MixerWindowsTest`, also gave
    exit 8, then exit 0 (`test-7-reverted.log`, `test-8-restored.log`).
- **Every acceptance criterion is proven by a test I ran.** See the table: both rows exit 0 on the final binary.
- **One undo step per edit, honest reversibility class.** The command makes no edit. It is registered
  `mutating: false` and classed `not_mutating`. `reportingChangesNoEqAndNoUndoDepth` and the socket driver measure
  `control.undo_depth` unchanged, and the song's modified flag is unchanged too.
  - `ReversibilityContractTest`: exit 0 after updating the A16 block in `docs/RELEASE-NOTES-v0.3.0-alpha.md` to
    `rows=888 ... not_mutating=387`. It had failed (exit 8) before that update, asking for it.
  - Snapshot regenerated live with `snapshot_live.py` (exit 0; 879 ids / 77 groups, `wasm.*` out; only
    `mixer.masking_report` was added). `tests/doc-figures-gate.py` exit 0 after updating the README canary.
    `ControlCommandsSnapshot` exit 0.
- **Nothing on the audio thread.** The handler runs on the control (main) thread. It reads the clips' immutable
  shared `SampleBuffer` data and the EQ `FloatModel` values, as `mixer.eq_compare` already does. It reads no mixer
  period buffer and adds nothing to any audio callback. Its allocations (FFT scratch, JSON) all happen in the handler.
  TSan does not run on this box, so the race argument is by design only.
- **Old projects load and render as before; new data round-trips.** No project data was added: priorities are
  command arguments and are not saved, and the project format and loaders are untouched. So there is nothing new to
  round-trip and old projects are unaffected. Not separately rendered.
- **Portability.**
  - No Qt-6-only API. `qsizetype` values are cast to `int` (`indexOf`, the `%n` count). `QJsonObject::insert(QLatin1String, ...)` exists since Qt 5.14.
  - No `std::filesystem`, no POSIX calls, and no `small`/`near`/`far`/`interface`/`min`/`max` identifiers.
  - `M_PI` in the tests follows `AudioAnalysisTest.cpp`.
  - Built only with this box's Qt 6/GCC. Hosted CI was not run.
- **Gates.** `feature_check.py` and `submit_check.py`'s gate row cannot run in this lane's write-jail: the script
  makes `<worktree>/../zene-feat-q050-231.1-feature-check`, which gives `PermissionError`. I ran its exact gate list
  by hand from the worktree, with logs in `Q050-231.1.r1/logs/gate2-*.log`:
  - `fork-sources` 0, `all-sources-reproduce` 0, `no-upstream-regression` 0, `check-namespace` 0, `no-tautology` 0,
    `doc-figures` 0.
  - `complexity` and `file-length`: the raw gates exit 1, as they do on the train by design. Compared with the cached
    base listing (`gate-baselines/2a7fd9f9c7….json`) using `feature_check`'s own `key`/`item_file` filter, there are
    **no new items in changed files**. The first pass flagged `analyse`, `addClip` and the driver's `main` over the
    complexity target, and `MixerWindowsTest.cpp` over 500 lines. I split the functions and moved the GUI test into
    its own file (`0d993deffb`).
- **Environment note.** The ccache temp directory from the config (`dsh/home/ccache-tmp-r6`) was not writable, so
  builds ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q231`.

## Known limits (stage 1)

- Only audio clips are analysed. Instrument tracks are listed in `unanalysed_tracks`.
- The audio is taken before the inserts and the fader; only the strip EQ is applied. Sends into a channel are not
  followed.
- The spectrum is long-term per clip and ignores when parts overlap in time.
- Applying a proposal as one undo step (stage 2) and the full musician workflow (stage 3) are left for their own units.
