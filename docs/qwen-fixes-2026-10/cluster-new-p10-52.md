FIXED

# Cluster new-p10-52 — BUG 17: an explicit `null` filter bound read as an open bound

Finding (Luna patrol, historical line numbers): `note.transform_where` with `action: remove` and
`filter: {"key_min": null}` could remove every note instead of refusing the malformed bound.

## Decision and evidence

The defect is real on branch `bugs/qwen-new-p10-52` (base `5c1c6715d9`). The finding's cited lines
(231-238, 244-248) have moved: they are now `addNotes`/`restoreTiming`. The defect itself is in the filter reader.

- `src/core/ControlCommandsNoteShape.cpp:273` (before the fix: `if (value.isUndefined() || value.isNull()) { return true; }`):
  `readBound` read an explicit JSON `null` as an omitted bound.
- `src/core/ControlCommandsNoteShape.cpp:282-291` (`readRanges`): `*use = lo.isDouble() || hi.isDouble();`. With
  `key_min: null` and no other key bound, `useKeyRange` stays false, so `NoteTransform::select` does not constrain the key
  and every note matches. `action: remove` then removes all of them.
- The filter is an open `objectProperty()` (`src/core/ControlCommandsNoteShape.cpp` `transform_where` registration, around
  line 607), so schema validation does not refuse the `null`. The handler has to.
- Both callers of `filterFrom` are affected: `select_where` (`:468`) and `transform_where` (`:542`). Each returns the
  refusal before any mutation or journal checkpoint, so a refused call leaves no undo step.
- Sibling with the same hazard: `readScale` (`:316`) also read `null` as absent, so `filter: {"scale": null}` dropped the
  scale channel and matched every note. It is fixed in the same way.
- Deliberately not changed: `readInvert` (`:299`) still reads `null` as "no inversion". That equals an omitted `invert`
  and cannot widen a filter, so changing it would alter behaviour outside the bug.
- No in-tree caller sends a `null` bound or scale: `rg` over `src/` and `tests/` finds none. The GUI menu sends an empty
  filter `{}` (`src/gui/noteeditor/NoteEditorMenus.cpp:267-269`), which still means "all bounds open".

## The change

- `readBound`: only an undefined value (an omitted property) is open. `null` now fails the existing
  `isDouble()` check and is refused with `InvalidArgs` ("a filter bound is a whole number (a JSON number)").
- `readScale`: only an undefined value is absent. `null` fails the existing `isString()` check and is refused
  with `InvalidArgs`.
- No command, schema, description or snapshot changed (no canary movement).

## Regression test

- `tests/src/core/NoteShapeValidationTest.cpp:208`, new slot `anExplicitNullFilterValueIsRefusedNotReadAsOpen()`:
  (a) `{key_min: null}` alone refused, notes unchanged; (b) `{key_max: 61, velocity_min: null}` refused;
  (c) `{scale: null}` refused; (d) `select_where` with `{key_min: null}` refused; (e) control: an omitted bound
  (`{key_max: 61}`) still works and removes exactly the two low notes.
- The test file is already registered (`tests/CMakeLists.txt:880`, `tests/fork-sources.txt:2501`). No new file.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-52.r1/logs`

| # | Command (run from the worktree / `build/tests` as noted) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target NoteShapeValidationTest -j4` | 2 | build-1-red.log (ccache could not write its temp file under `dsh/`; environment, not the test) |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p10-52 cmake --build build --target NoteShapeValidationTest -j4` | 0 | build-2-red.log |
| 3 | RED: `ctest -R '^NoteShapeValidationTest$' --output-on-failure` (in `build/tests`) | **8** | test-1-red.log: `anExplicitNullFilterValueIsRefusedNotReadAsOpen` fails at `NoteShapeValidationTest.cpp:215`, Actual `0` (None, accepted), Expected `3` (InvalidArgs). 10 passed, 1 failed. |
| 4 | GREEN: `cmake --build build --target zene NoteShapeValidationTest -j4` | 0 | build-3-green.log |
| 5 | GREEN: `ctest -R '^NoteShapeValidationTest$' --output-on-failure` | **0** | test-2-green.log: 1/1 passed |
| 6 | 4b: `git diff -- src/core/ControlCommandsNoteShape.cpp > logs/fix.patch` (31 lines) | 0 | — |
| 7 | 4b: `git apply -R logs/fix.patch` | 0 | — |
| 8 | 4b: build (zene + test) | 0 | build-4-unfixed.log |
| 9 | 4b RED-without-fix: ctest | **8** | test-3-unfixed.log: same failure at line 215, Actual `0`, Expected `3` |
| 10 | 4b: `git apply logs/fix.patch` | 0 | — |
| 11 | 4b: build (zene + test) | 0 | build-5-refixed.log |
| 12 | 4b GREEN-with-fix: ctest | **0** | test-4-refixed.log: 1/1 passed |
| 13 | Family build: `cmake --build build --target ControlEditCommandsTest NoteSelectionIdentityTest ControlVerbInverseTest ControlNoteScaleVerbsTest NoteEditingCommandsTest NoteEditRangeTest NoteNoOpEditUndoStepTest ControlNoteNoOpUndoTest NoteEditorTest NoteEditorFlTest -j4` | 0 | build-7-family.log |
| 14 | Family ctest: `ctest -R '^(ControlEditCommandsTest\|NoteSelectionIdentityTest\|ControlVerbInverseTest\|ControlNoteScaleVerbsTest\|NoteEditingCommandsTest\|NoteEditRangeTest\|NoteShapeValidationTest\|NoteNoOpEditUndoStepTest\|ControlNoteNoOpUndoTest\|NoteEditorTest\|NoteEditorFlTest)$' --output-on-failure` | **8** | test-5-family.log: 11 tests run, 10 passed, 1 failed (`ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged`, environment: see below) |
| 15 | Python family test: `ctest -R '^ControlNoteEditor$' --output-on-failure` | 0 | test-6-noteeditor-py.log: 1/1 passed |
| 16 | Gates: `python3 .../qwen/feature_check.py <worktree>` | 1 | Not usable from the write-jail: the script creates `<worktree>-feature-check` next to the worktree, and `mkdir` was denied (PermissionError). No gate ran through that path. |
| 17 | Gates, same loop as `feature_check.py` (same `SIMPLE` / `RATCHET` commands, same base-listing comparison) with logs in the LOG FOLDER: `python3 -I $TMPDIR/gate-driver-p10-52.py <worktree> <logs>/gates` | 0 | gates-2.log, gates/*.log: `RESULT: PASS` |

Gate table (step 17):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS: ratchet script exited 1 on the base as well; 0 new items in the changed files |
| file-length | PASS: ratchet script exited 1 on the base as well; 0 new items in the changed files |

Not caused by this change:

- `ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged` fails with "the take file
  /home/kruzzzzy/Documents/Zene Studio/zene-take-trk40.wav could not be opened for writing". That folder is outside the
  worktree. A probe `touch` from the same shell was denied with Permission denied, so the write-jail blocks the write. The
  test exercises record arming and does not touch the note filter.
- `ccache` needed `CCACHE_TEMPDIR` pointed at `$TMPDIR` (disk, inside the jail); the default temp dir under `dsh/` was denied.

## Files committed

- `src/core/ControlCommandsNoteShape.cpp` (the fix)
- `tests/src/core/NoteShapeValidationTest.cpp` (the regression slot)
- `docs/qwen-fixes-2026-10/cluster-new-p10-52.md` (this report)
