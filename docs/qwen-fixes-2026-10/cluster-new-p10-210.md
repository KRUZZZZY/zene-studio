FIXED

# Cluster new-p10-210 — removing a linked control looked up the layout item by the control id

Finding (Luna patrol, `src/gui/LinkedModelGroupViews.cpp:129` at the historical line): `removeControl()` used one
`key` for two lookups that use different keys. `addControl()` stores the widget under `id` in `m_widgets` and names
the layout widget `display`. When `id != display`, `m_layout->itemByString(key)` returns null and the code
dereferences it.

## Verdict and reachability

FIXED. The defect is real and reproduced (SIGSEGV on the unfixed code, see RED below).

Reachability in this tree is narrower than the finding suggests:
- `LinkedModelGroupView::removeControl` has no production caller. `grep -rn removeControl src include plugins`
  finds only its definition (`src/gui/LinkedModelGroupViews.cpp:124`) and the test.
- The one production `addControl` caller, `src/gui/Lv2ViewBase.cpp:119-123`, passes `id` = port symbol,
  `display` = port name, and `removable = false`. So the crash is latent: any caller that removes a control whose
  id differs from its display name hits it.
- Separate observation, not changed here: the remove button (`addControl` with `removable`, lines 93-108) calls
  `LinkedModelGroup::removeControl`, which emits `modelRemoved` (`src/core/LinkedModelGroups.cpp:101-108`). Nothing in
  `src`, `include`, `plugins` or `tests` connects that signal to a view, so this path never reaches
  `LinkedModelGroupView::removeControl`. That is a different gap from this finding and is left alone.

## Evidence (base `639e1d23e`, line numbers as of the base)

- `src/gui/LinkedModelGroupViews.cpp:89-112` — `addControl`: `box->setObjectName(display)` (line 111), then
  `m_layout->addWidget(box)`.
- `src/gui/ControlLayout.cpp:114-117` — `addItem` keys `m_itemMap` by the widget's objectName.
- `src/gui/ControlLayout.cpp:170-174` — `itemByString(key)` returns null when `key` is absent.
- Base `src/gui/LinkedModelGroupViews.cpp:126` — `m_widgets.find(key)` (the id), and base line 129 —
  `m_layout->itemByString(key)` (the display name). Same `key` for both.
- RED crash location: `LinkedModelGroupView::removeControl+128` (`mov (%rax),%rax` on a null pointer), from
  `LOGS/test-red-1.log` line 10 (SIGSEGV, address 0x0), line 23 (disassembly at `+128`), line 40 (frame `#5`).

## The fix

- `include/LinkedModelGroupViews.h:92-99` — `m_widgets` is now `std::map<std::string, ControlEntry>` keyed by the
  `addControl` id; `ControlEntry` holds the `Control` and the layout name (`display`).
- `src/gui/LinkedModelGroupViews.cpp:74` — `modelChanged` reads `itr->second.control`.
- `src/gui/LinkedModelGroupViews.cpp:115` — `addControl` stores `display` with the control in the same `emplace`, so
  the two stay in step (both are keyed by the same id and erased together).
- `src/gui/LinkedModelGroupViews.cpp:129-131` — `removeControl(id)` finds the entry by id, then looks up the layout
  item by the stored `layoutName`. The `Q_ASSERT`s and the delete order are unchanged.

## Regression test

`tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp`, new slots:
- `removingAControlWhoseIdDiffersFromItsDisplayNameFreesItsLayoutItem` — adds `control-id`/`Control Display` and
  `other-id`/`Other Display`, removes `control-id`, checks via the existing operator-delete probe that the layout item
  for `Control Display` was freed, that its name is gone from the layout, and that `Other Display` is still there.
- `removingAnUnknownControlLeavesTheLayoutAlone` — removing an unknown id, and removing by display name (which is not
  an id), changes nothing. This case passes before and after the fix; it guards against a fix that matches the wrong
  key.

## Commands and exit codes

Build environment note: the first build attempt failed inside ccache (`ccache: error: failed to create temporary
file ... Permission denied`, `LOGS/build-red-1.log`, EXIT=2). The ccache temp dir is outside the jail, so every build
below sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` (on disk, inside the allowed area). No compiler flags or
build settings changed.

| Step | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target LinkedModelGroupViewLifetimeTest -j4` | build-red-2.log | 0 |
| **RED** (original code, test added) | `cd build/tests && ctest -R '^LinkedModelGroupViewLifetimeTest$' --output-on-failure` | test-red-1.log | **8** (SEGFAULT in the new case) |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene LinkedModelGroupViewLifetimeTest Lv2ViewBaseLifetimeTest -j4` | build-green-1.log | 0 |
| **GREEN** | `cd build/tests && ctest -R 'LinkedModelGroupViewLifetimeTest\|Lv2ViewBaseLifetimeTest' --output-on-failure` | test-green-1.log | **0** (2/2) |
| Binary, verbose | `cd build/tests && QT_QPA_PLATFORM=offscreen ./LinkedModelGroupViewLifetimeTest` | binary-green-1.log | 0 (5 passed, 0 failed) |
| 4b save patch | `git diff -- include/LinkedModelGroupViews.h src/gui/LinkedModelGroupViews.cpp > LOGS/fix.patch` | — | 0 |
| 4b revert | `git apply -R LOGS/fix.patch` | — | 0 |
| 4b build (unfixed) | `cmake --build build --target zene LinkedModelGroupViewLifetimeTest Lv2ViewBaseLifetimeTest -j4` | build-4b-unfixed.log | 0 |
| 4b **must FAIL** | `cd build/tests && ctest -R '^LinkedModelGroupViewLifetimeTest$' --output-on-failure` | test-4b-unfixed.log | **8** (SEGFAULT) |
| 4b re-apply | `git apply LOGS/fix.patch` | — | 0 |
| 4b build (fixed) | same build as above | build-4b-fixed.log | 0 |
| 4b **must PASS** | `cd build/tests && ctest -R '^LinkedModelGroupViewLifetimeTest$' --output-on-failure` | test-4b-fixed.log | **0** |
| Family build | `CCACHE_TEMPDIR=... cmake --build build --target MidiClipLinkedEditTest -j4` | build-family-1.log | 0 |
| Family | `cd build/tests && ctest -R 'LinkedModel\|Lv2ViewBase\|MidiClipLinked' --output-on-failure` | test-family-1.log | **0** (3/3 executed) |

Affected family: `LinkedModelGroupViewLifetimeTest`, `Lv2ViewBaseLifetimeTest`, `MidiClipLinkedEditTest` = 3 tests
executed, 3 passed. Not rebuilt: the Lv2 plugin headers `plugins/Lv2Effect/Lv2FxControlDialog.h` and
`plugins/Lv2Instrument/Lv2Instrument.h` include `Lv2ViewBase.h`, which includes the changed header. Those plugin
targets were not in the requested target list and were not built here. Nothing they compile touches `m_widgets`
(private), and the member's size is unchanged, but this is unverified by build.

## Gates

`feature_check.py` could not run as designed from this sandbox: it writes its logs to
`<worktree>-feature-check` (`zene-qwen-new-p10-210-feature-check`), a sibling directory outside the write-jail.
`LOGS/gates-1.log`: `PermissionError: ... zene-qwen-new-p10-210-feature-check`, EXIT=1. I did not edit the loop's
script. Instead I ran each gate command that `feature_check.py` lists, from the worktree, logging into the LOG FOLDER:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-reproduce.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-regression.log | 0 (see note) |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-complexity.log | 1 (7 REGRESSION lines, none in a file this branch changed) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-file-length.log | 1 (22 REGRESSION lines, none in a file this branch changed) |

Ratchet note: `feature_check.py` passes a ratchet row only when no REGRESSION line names a file the branch changed.
None of the 29 REGRESSION lines names `include/LinkedModelGroupViews.h`, `src/gui/LinkedModelGroupViews.cpp` or
`tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp`, so those two rows would pass. The non-zero exit is the train's
known red, in other files. Note on no-upstream-regression: it diffs committed history, so its PASS above was taken
before the ledger lines and the commit existed. It is re-run after the commit, below.

Ledger: `tests/upstream-modifications.txt` gains two lines (header, and `.cpp`) with reasons. The header was not in the
ledger before, and at base `01148947` it matched upstream; the `.cpp` already had a `new-p9-226` line.

## Files committed

- `include/LinkedModelGroupViews.h`
- `src/gui/LinkedModelGroupViews.cpp`
- `tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-210.md`

No new source files, no registration changes (the test was already in `tests/CMakeLists.txt` and
`tests/all-sources.txt`), no control commands, no canary or snapshot changes.

## Self-check

- **Regression test fails with the change reverted and passes with it.** Yes. RED: `test-red-1.log` (EXIT 8, SIGSEGV
  in `removeControl`) on the original code. GREEN: `test-green-1.log` (EXIT 0). The 4b pair (`test-4b-unfixed.log`
  EXIT 8, `test-4b-fixed.log` EXIT 0) uses the saved `fix.patch`, reverse-applied and re-applied.
- **Deterministic.** The test uses fixed id/display strings, no sleeps, no timers and no threads. It reads the
  existing operator-delete probe from `new-p9-226`, which records freed addresses and is not timing-dependent.
  Ordering is by the `std::map` key order, not by time.
- **Fix at the cause, and complete.** The cause is the two lookups using one key. Every caller and sibling was
  checked: `removeControl` has no production caller; `addControl` has one (`Lv2ViewBase.cpp:119`), whose signature is
  unchanged; `m_widgets` is private and used only in `LinkedModelGroupViews.cpp` (lines 64, 67, 74, 115, 126-141);
  the two maps' keys are now one map, so they cannot drift. Duplicate ids keep the old behaviour (the second
  `emplace` is refused, as before).
  Residual, pre-existing and not changed: `ControlLayout::addItem` uses `QMap::insert`, so two controls with the same
  display name overwrite one another's layout entry. `removeControl` then removes the entry registered under that
  name. Fixing that would change how names are keyed in the layout and is out of scope for this finding.
- **No collateral damage.** No saved-data, project or persistence format is involved; the view is GUI-only.
  No limit or check was added. `id == display` behaviour is unchanged (`removingAControlFreesItsDetachedLayoutItem`
  still passes). Realtime: the code runs on the GUI thread, not on any audio path; nothing new allocates on an audio
  thread. Undo: none involved.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable (verdict FIXED).
- **submit_check.py** (`LOGS/submit-check-1.log`, run before the commit): FAILs on commits-not-made, dirty tree, the
  missing report, this section, tests-changed (now satisfied), and the gates row. The gates row fails only because
  `feature_check.py` cannot create its log directory from this sandbox (see Gates). The per-gate results above are
  the substitute evidence.
