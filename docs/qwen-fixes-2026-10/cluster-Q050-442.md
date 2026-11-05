IMPLEMENTED

# Q050-442 — High-contrast track movement grip

Round 2. The branch now carries two commits on top of `050/m0-train-25`
(`081630835b`): the fix plus the test that proves it.

## What changed, and why

The track movement handle was drawn from two fixed 6x25 bitmaps
(`data/themes/default/track_op_grip{,_c}.png`) whose dots are alpha 102/255
black/white — i.e. a scaled-down 24 px glyph, not the palette role. Rendered on
the stock dark row it measured **1.06:1**, and on the high-contrast row
**1.05:1**: the handle was effectively invisible in both palettes and no
contrast test saw it.

- `src/gui/tracks/TrackGrip.cpp` now paints ten whole-pixel dots (5 rows x 2
  columns, 2 px released / 3 px grabbed) from `tokens::palette().text` with
  antialiasing off. The widget keeps its inherited 10 px width, and the mouse
  path (grab signals, cursor, Ctrl-drag copy, custom-colour fill, mute check) is
  untouched.
- `include/TrackGrip.h` drops the two cached `QPixmap` pointers.
- `tests/src/gui/TrackGripContrastTest.cpp` (new, registered in
  `tests/CMakeLists.txt`, `tests/fork-sources.txt`, re-derived
  `tests/all-sources.txt`) renders the real row from the real `TrackView` and
  holds every inked pixel of the grip to 3:1 with `a11y::contrastRatio` /
  `meetsComponentContrast` in the standard dark palette, in the high-contrast
  palette chosen from **Customise > Theme > Palette**, and after the `text` role
  is changed through **Customise > Theme > Colours... > Change...**. Each palette
  change is re-read by re-running the executable as a child that loads the config
  the previous start saved (the real next-start lifecycle).
- `tests/upstream-modifications.txt`: ledger rows for the two inherited files.
- `data/manual/index.html`: the Customising paragraph describes the handle.
- `docs/050-features/Q050-442.md`: the unit record.

No command, schema, result field, reversibility class, project format or new GUI
entry point: the palette stays user configuration written by the existing
`window.appearance` / `theme.set_token`, which keep their `true_inverse` rows and
their own one-undo-step behaviour.

## Acceptance

| # | Criterion | Test | Command | Exit |
|---|---|---|---|---|
| 1 | Run the GUI contrast checker against the grip in standard and high-contrast palettes and verify it meets the existing threshold | `TrackGripContrastTest::theGripIsLegibleInTheStandardPalette` (dark, role `#eceef1`, surface `#17191d`, worst **15.14:1**, 40/40 pixels exactly the role colour) and `::theGripIsLegibleInTheHighContrastPalette` (chosen via the Theme menu, child start: role `#ffffff`, surface `#08080a`, worst **20.01:1**, 40/40 exact) | `ctest -R '^TrackGripContrastTest$' --output-on-failure` from `build/tests` (`test-restore-2.log`) | 0 |
| 2 | Change the palette foreground role and confirm the grip updates with it | `TrackGripContrastTest::aChangedForegroundRoleReachesTheGrip` — `Customise > Theme > Colours...` (action declares `theme.get`), the text row's Change... form, `#ffcc00` typed, Run; `theme.get` agrees and says `restart_pending`; the child start draws role `#ffcc00` on `#17191d`, worst **11.64:1**, 40/40 exact. `::theGrabbedGripIsStillDrawnInThePalette` adds the held state (more role-coloured pixels, still >= 3:1, release restores the image) | same `ctest -R` (`test-restore-2.log`), verbose measurements in `run-3.log` | 0 |

`zene` builds (`build-zene.log`, exit 0). Neighbours stay green (`test-neighbours-4.log`,
exit 0): `TrackGripContrastTest`, `TrackIconsLevelBarTest`, `DesignTokensTest`,
`WorkspaceAccessTest`, `ThemeContrastTest`, `TouchLayoutTest`, `CommandFormTest` — 7/7.

Negative control (product hunks reversed to the branch base `081630835b`, tests
kept): `build-negctl-2.log` exit 0, then `./TrackGripContrastTest` exits **4**
(`run-negctl-2.log`): standard renders **1.06:1**, high contrast **1.05:1**, the
changed role **1.05:1**, each with 14 inked pixels and 0 exactly the role colour;
the grabbed case fails on `during.inked > before.inked` (14 vs 14). Restored:
`build-restore-2.log` exit 0 and `ctest -R '^TrackGripContrastTest$'` exit **0**
(`test-restore-2.log`).

Gates (`feature_check.py`, all rows PASS, exit 0; log `feature-check-2.log`):
fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0,
check-namespace 0, no-tautology 0, doc-figures 0 (917 ids / 78 groups, unchanged —
no command was added), complexity and file-length "no new item in your files".

## Self-check

- **Each new test FAILS with the product change reverted, PASSES restored.**
  Reversed only the two non-test product files to `081630835b`, rebuilt
  (`build-negctl-2.log`, exit 0) and ran the binary: exit **4**, with the
  standard palette at 1.06:1 and the high-contrast start at 1.05:1
  (`run-negctl-2.log`). Restored the product files, rebuilt
  (`build-restore-2.log`, exit 0) and ran `ctest -R '^TrackGripContrastTest$'`:
  exit **0**, 1/1 passed (`test-restore-2.log`).
- **Every acceptance criterion is proven by a test I ran.** Criterion 1:
  `TrackGripContrastTest::theGripIsLegibleInTheStandardPalette` and
  `::theGripIsLegibleInTheHighContrastPalette`, exit 0. Criterion 2:
  `::aChangedForegroundRoleReachesTheGrip` (with
  `::theGrabbedGripIsStillDrawnInThePalette` for the held state), exit 0. The
  verbose run (`run-3.log`) records 15.14:1, 20.01:1 and 11.64:1 with 40/40
  pixels exactly the role colour in each case.
- **One undo step per edit, honest reversibility class.** No command was added or
  changed, so no class changed: `theme.set_token` and `window.appearance` keep
  their `true_inverse` rows (`src/core/ControlReversibilityTableW3Custom.cpp:43`,
  `src/core/ControlReversibilityTableDevices.cpp:151`). The test relies on that:
  it calls `control.undo` once after the palette change and once after the colour
  change and checks each restored state, which is what a one-step inverse means.
  The grip's own edit is a drag (track move), untouched by this unit.
- **Nothing added runs on the audio thread.** The diff touches only
  `TrackGrip::TrackGrip` / `mousePressEvent` / `mouseReleaseEvent` / `paintEvent`
  (GUI thread widget code), a header, a test and docs. No allocation, lock, wait,
  I/O or signal emission was added to any render path; `paintEvent` reads
  `tokens::palette()` and draws, as before. `submit_check`'s audio-hint regex does
  not match the diff.
- **Old projects load and render as before; new data round-trips.** No project
  reader/writer, serialization key or format changed. The palette is in the
  config file (`ui/theme`), not the project; `theme.set_token` / `window.appearance`
  still write it exactly as before (unchanged code). A track's own colour and the
  mute path behind the grip still paint as before (unchanged lines), checked by
  `theGrabbedGripIsStillDrawnInThePalette` rendering the real row.
- **Portability.** The only new API use is `tokens::palette()`, `QPainter::drawRect`,
  `QColor` and `QWidget` geometry — all Qt 5.15-and-6 APIs used elsewhere in the
  same files. No Qt-6-only API, no `small`/`near`/`far`/`interface`/`min`/`max`
  identifier, no `std::filesystem`, no POSIX-only call. The test uses
  `QProcessEnvironment` and `QTemporaryDir`, as its neighbours do; nothing
  platform-specific. `check-namespace` exits 0. Syntax-only builds of the two
  changed sources against both installed Qt versions pass:
  `src/gui/tracks/TrackGrip.cpp` with Qt 5.15.19 (`qt5-syntax-grip.log`) and
  Qt 6.12.0 (`qt6-syntax-grip.log`), and `tests/src/gui/TrackGripContrastTest.cpp`
  with each version's `moc` output (`qt5-syntax-test.log`, `qt6-syntax-test.log`),
  all exit 0.
- **GUI and agent, the same command.** The unit adds no user-facing action, so it
  adds no GUI entry point or agent command. The GUI paths it drives are existing
  ones that already run registered commands: the Theme > Palette action declares
  `window.appearance`; the Theme > Colours... action declares `theme.get` and the
  row's Change... form runs `theme.set_token` (asserted by property in the test);
  the agent surface runs the same two ids.
- **Snapshot / docs.** No command metadata changed, so the live snapshot is
  unchanged (`doc-figures-gate.py` exit 0, 917 ids / 78 groups before and after).
  `data/manual/index.html` describes the handle; `docs/050-features/Q050-442.md`
  has one row per criterion with its exit code.

Logs for every command above are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/q050-442-r2-logs/`
(the assigned `runs/Q050-442.r2/logs` directory is not writable from this
session — every write there is refused by the harness sandbox, so logs were kept
beside it instead).

`submit_check.py`'s last row, "gates (feature_check.py)", fails in this session
for a harness reason, not a gate reason: `feature_check.py` writes its logs to
`<worktree>/../<name>-feature-check`, and that parent directory is not writable
either (`mkdir ../zene-feat-q050-442-feature-check` → `Permission denied`, the
same sandbox refusal). Run with only that output directory redirected
(`/tmp/q442/feature_check.py`, `CACHE` and `ROOT` untouched), the identical gate
set returns **PASS, exit 0** for all eight rows — log `feature-check-2.log`. Every
gate was also run individually from the worktree with exit 0 (see the table
above).

