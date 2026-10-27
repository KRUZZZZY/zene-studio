FIXED: XyPad with null models is now inert instead of dereferencing them (cluster new-p15-51, finding "BUG 7")

## Verdict and evidence

Real bug. Verified on base `639e1d23e` (the branch's starting point; `git show 639e1d23e:<path>`).

- The constructor accepted null models: `plugins/Delay/DelayControlsDialog.h:79` (base) declared
  `XyPad( QWidget *parent = 0, FloatModel *xModel = 0, FloatModel *yModel = 0 );`.
- The constructor connected unconditionally: `plugins/Delay/DelayControlsDialog.cpp:90-91` (base).
- Painting dereferenced them: `plugins/Delay/DelayControlsDialog.cpp:107-109` (base, `m_xModel->value()` etc.).
- Dragging dereferenced them: `plugins/Delay/DelayControlsDialog.cpp:149-154` (base, `m_xModel->setValue`, `m_yModel->setValue`).
- Measured before the fix: the regression test raised SIGSEGV (see RED below). Qt also warned
  `QObject::connect: Cannot connect (nullptr)::dataChanged() to lmms::gui::XyPad::update()` at construction.

The finding's line 52 is historical; the same code is now at `plugins/Delay/DelayControlsDialog.h:79-82`
and `plugins/Delay/DelayControlsDialog.cpp:84-142`.

Only one production caller exists: `plugins/Delay/DelayControlsDialog.cpp:76` passes all three arguments.

## The fix

1. `plugins/Delay/DelayControlsDialog.h:79-81`: the constructor's defaults are removed, so a pad cannot be
   built without arguments (C++ requires `parent` to lose its default too, since the models are no longer
   defaulted). The declaration gets a `//!` comment saying a null model leaves the pad inert.
2. `plugins/Delay/DelayControlsDialog.cpp:90-91`: each `connect` runs only when its model is non-null.
3. `plugins/Delay/DelayControlsDialog.cpp:99` (paintEvent) and `:141` (mouseMoveEvent): return early when
   either model is null, next to the existing size guard. Press and release never touch a model, so they
   need no change.

An explicit `nullptr` still compiles (the test passes it), so the runtime guards are needed; the removed
defaults are the compile-time part.

## Test

`tests/src/gui/XyPadPairTest.cpp:257-274` (comment 257-259, slot `delayXyPadWithMissingModelIsInert` at 260-274). It is in the existing
registered test (`XyPadPairTest`) which already includes the Delay pad source.

Test matrix:
- both models missing (normal construction path through show/repaint and a full press/move/release): paints and takes input without a crash (RED without the fix, passes with it);
- one model missing (x present, y null): input is ignored, and the present model is unchanged (`QCOMPARE(xModel.value(), 0.2f)`);
- normal path: the 17 existing `XyPadPairTest` slots (existing drag, mapping, macro and pointer tests) pass unchanged;
- audio-thread, realtime and undo: not applicable (GUI widget, no audio path, no command added);
- platform: `QT_QPA_PLATFORM=offscreen`, as the test's ctest `ENVIRONMENT` property already sets.

I first wrote this as three slots (both-null paint, both-null drag, one-missing). Those are superseded by the
single slot above, because the file was at 484 code lines and three slots would push it past the
500-line file-length limit (`tests/file-length-gate.sh`). The RED for the three-slot form was also SIGSEGV
(`ctest -R '^XyPadPairTest$'`, EXIT=8, log `test-red-1.log`); the single slot is the one below.

## Commands and EXIT codes (logs in the run folder `logs/`)

Build note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied`
(`build-red-1.log`, EXIT=2) because ccache's temp dir is outside this lane's writable area. Every later build
sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` (on disk, in the TMPDIR); the logs show no ccache errors.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| build (RED, test before fix) | `CCACHE_TEMPDIR=… cmake --build build --target XyPadPairTest -j4` | 0 | `build-red-2.log` |
| RED (final test, fix reverted) | `git apply -R fix.patch` then `cmake --build build --target XyPadPairTest -j4` then `./XyPadPairTest delayXyPadWithMissingModelIsInert` | **139 (SIGSEGV)** | `build-red-prove-2.log`, `run-red-compact-1.log` |
| GREEN (fix re-applied) | `git apply fix.patch` then `cmake --build build --target XyPadPairTest delay DelayControlsTest DelayLfoAutomationTest MonoDelayTest StereoDelay partc_ref_delay -j4` then `./XyPadPairTest delayXyPadWithMissingModelIsInert` | **0** | `build-green-4.log`, `run-green-compact-2.log` |
| full binary GREEN | `./XyPadPairTest` (run in `build/tests`) | **0**, `Totals: 20 passed, 0 failed` | `run-green-compact-1.log` |
| affected family | `ctest -R 'Delay|XyPad|partc_ref_delay' --output-on-failure` (in `build/tests`) | **0**, 5/5 passed (`MonoDelayTest`, `StereoDelay`, `XyPadPairTest`, `DelayControlsTest`, `DelayLfoAutomationTest`) | `test-family-2.log` |
| ctest, earlier three-slot version | `ctest -R '^XyPadPairTest$' --output-on-failure` | 8 (SEGFAULT) with the fix reverted; 0 with it | `test-red-1.log`, `test-green-prove-1.log` |

The fix patch was saved first as `fix.patch` (`git diff -- plugins/Delay/DelayControlsDialog.h plugins/Delay/DelayControlsDialog.cpp`, EXIT=0) and applied in reverse, then forward again (both EXIT=0). Before the reversal, the live product diff was compared with the saved patch (`diff -q`, no difference, `SAME_PATCH`).

PluginPortsMigrationTest (`build` target built, run in `build/tests`): `ctest -R '^PluginPortsMigrationTest$'`
EXIT=8. One slot fails, `nonRealtimeLadspaEffectsAreInvalid`, with `'description != nullptr' ... (SWH Decimator test
fixture was not loaded)`; the SWH Decimator LADSPA plugin is not installed on this machine (no `decim` match in
`/usr/lib/ladspa` or `/usr/local/lib/ladspa`). The other 82 slots pass (2 skipped), including `delayLfoControlDescribesItsPeriod`,
which reads `DelayControlsDialog.cpp` as text and still passes. The slot is unrelated to the XY pad, and I did
not build the base to prove it fails there too, so this is an unverified environment failure.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not run here: it writes its logs
to `<worktree>/../<name>-feature-check`, which is outside this lane's writable area, and it raised
`PermissionError` (`feature-check-1.log`, EXIT=1). I ran each gate it calls directly, from the worktree, with
the same commands:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train, as the brief says. The only item they list in a file this branch
changed is `dotCentreAlong@87-131 … (CCN 16)` in `tests/src/gui/XyPadPairTest.cpp`. That function is unchanged
from base (`sed -n 87,131p` on both trees: identical md5 `7f71fa3b5e3a7da2562e38f55a923eb1`), so it is in base's
listing and is not new. The file-length item for `XyPadPairTest.cpp` is not reported: base had 484 code lines,
now 499 (measured with that awk), under the 500 limit (the gate's count is code lines, excluding blank and comment-only lines; the same awk as `code_lines` in the gate script).

Registration: `plugins/Delay/DelayControlsDialog.h` and `.cpp` are inherited files, so both have new
reason lines in `tests/upstream-modifications.txt` (appended at the end, same commit). The test file was
already registered in `tests/fork-sources.txt` and `tests/CMakeLists.txt`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes:
  reverted, `./XyPadPairTest delayXyPadWithMissingModelIsInert` → EXIT 139 (SIGSEGV at address `0x8c`, the
  paint/mouse dereference); re-applied → EXIT 0, `PASS`. The commands are in the table above.
- **The test is deterministic.** Yes. No timing, sleeps or races: it uses `FloatModel` values with fixed
  inputs, synchronous `show()`/`repaint()`, and `QTest` mouse events with fixed coordinates. Its assertion is
  `QCOMPARE` on a model value.
- **The fix is at the cause and complete.** The cause is the defaulted null models plus unguarded use. I
  checked every caller: the production one (`DelayControlsDialog.cpp:76`) passes all three arguments; the test
  fixture passes them explicitly. The sibling paths are paintEvent, mousePressEvent, mouseReleaseEvent and
  mouseMoveEvent; press and release never touch a model, so they are unchanged. The other XY pad, `src/gui/XyPad.cpp`
  (`XyPadArea`), is a different class and is not touched.
- **No collateral damage.** The change is UI-only: nothing is serialised, so old projects and saved data are
  unaffected. The only behaviour change is for a pad whose model is missing, which previously crashed. The new
  guards add no limit that a legitimate caller could exceed. The historical reference copy
  `tests/reference/Delay/DelayControlsDialog.h` (with its own declaration) is not the product code and is not
  edited.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so the code citation requirement above is met by the base
  lines `plugins/Delay/DelayControlsDialog.h:79`, `.cpp:90-91`, `:107-109`, `:149-154`.
