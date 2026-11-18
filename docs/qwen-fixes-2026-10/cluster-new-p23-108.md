# Cluster new-p23-108

Verdict: **FIXED**

The finding is real, with a narrower trigger than the finding states. Hovering with no button held is not a live trigger, because Qt drops such moves for a widget without mouse tracking and `Graph` never enables tracking. The live trigger is a drag with a button other than the left one (right or middle): `Graph::mousePressEvent` ignores that press, but `Graph::mouseMoveEvent` still wrote samples while that button was held. The fix guards the sample writes on the left-button stroke state (`m_mouseDown`), which covers both cases.

## Evidence (file:line)

- Before the fix, `Graph::mouseMoveEvent` wrote samples on every move: `changeSampleAt` when `m_lastCursorX < 0` and `drawLineAt` otherwise, with no check of the button state. The pre-fix lines were `src/gui/widgets/Graph.cpp:140-147`.
- After the fix, `src/gui/widgets/Graph.cpp:142-152` wraps both writes in `if( m_mouseDown )`. The position bookkeeping and the cursor update below it are unchanged.
- `m_mouseDown` is set only by a left press (`src/gui/widgets/Graph.cpp:184` and `:207`) and cleared only by a left release (`:312`). The shift-click path and the press path already check `Qt::LeftButton`, so they are unchanged.
- `Graph` never calls `setMouseTracking` (`rg` over `src/gui/widgets/Graph.cpp` and `include/Graph.h` finds nothing), so Qt delivers moves only while a button is held.
- Regression test: `tests/src/gui/SampleEditorDialogTest.cpp:312-378` (`rightButtonDragOnFreshGraphEditsNoSample`, `rightButtonDragAfterLeftStrokeEditsNoSample`, `hoverEditsNoSample`, plus the control `leftButtonDragStillEditsSamples`). The helpers `sendMouse` and `samplesOf` are at `:136-150`, and `TestGraph::deliverMove` at `:134` exposes `mouseMoveEvent` for the hover case only.
- Sibling and caller check:
  - The only `Graph` users are direct `gui::Graph` instances. No class in the product derives from `Graph`; `LfoGraph` and `EnvelopeGraph` derive from `QWidget` (`include/LfoGraph.h`, `include/EnvelopeGraph.h`).
  - Nothing forwards move events into a `Graph` by hand (`rg "mouseMoveEvent\("` over `src` and `include`).
  - `Graph` has no undo or command hooks (`rg -i "undo|journal|Command"` over `Graph.cpp` and `Graph.h` finds nothing).
- Qt-side evidence for the hover case: `hoverEditsNoSample` sent through `QCoreApplication::sendEvent` passed on the unfixed code (`test-red-2.log`, the PASS line for `hoverEditsNoSample`). Only the direct `deliverMove` call exposed it. Qt drops button-less moves before they reach the widget.

## Test matrix

| Path | Test | Unfixed code | Fixed code |
| --- | --- | --- | --- |
| Right-button drag, fresh graph (no last cursor) | `rightButtonDragOnFreshGraphEditsNoSample` | FAIL | PASS |
| Right-button drag after a left stroke (`drawLineAt` path) | `rightButtonDragAfterLeftStrokeEditsNoSample` | FAIL | PASS |
| Move with no button, fresh graph and after a stroke (handler called directly) | `hoverEditsNoSample` | FAIL | PASS |
| Left-button drag still edits (control) | `leftButtonDragStillEditsSamples` | PASS | PASS |

The test uses `sendMouse` with explicit button state, so it does not depend on QTest's internal button tracking. The existing `QTest::mousePress`/`mouseMove` drag tests in the same file still pass.

## Commands and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-108.r1/logs/`.

| Step | Command (abridged) | EXIT | Log |
| --- | --- | --- | --- |
| Register new test file in CMake | `cmake --build build --target GraphMouseEditTest` (new file) | 2 (no such target; configure not re-run) | `build-1.log` |
| Re-configure | `cmake -S . -B build` | 97 (lane guard: "configuring is the parent's job") | `configure-1.log` |
| Build with default ccache temp | `cmake --build build --target SampleEditorDialogTest -j4` | 2 (ccache temp dir permission denied, environment) | `build-3.log` |
| Build with `CCACHE_TEMPDIR` under `~/.cache/zene-tmp` | `cmake --build build --target SampleEditorDialogTest -j4` | 0 | `build-4.log` |
| RED (first, single function) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` from `build/tests` | 8 | `test-red-1.log` |
| RED (split, handler seam not yet used) | build then ctest | 0 / 8 | `build-5.log`, `test-red-2.log` (2 FAIL, hover PASS) |
| RED (hover via `deliverMove`) | build then ctest | 0 / 8 | `build-6.log`, `test-red-3.log` (3 FAIL, 23 passed, control PASS) |
| Fix applied; build | `cmake --build build --target zene SampleEditorDialogTest -j4` | 0 | `build-7-green.log` |
| GREEN | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` | 0 | `test-green-1.log` |
| Proof 4b: diff saved | `git diff -- src/gui/widgets/Graph.cpp > logs/fix.patch` | 0 | `logs/fix.patch` |
| Proof 4b: reverse | `git apply -R logs/fix.patch` | 0 | — |
| Proof 4b: rebuild unfixed | `cmake --build build --target zene SampleEditorDialogTest -j4` | 0 | `build-8-unfixed.log` |
| Proof 4b: RED | ctest as above | 8 (3 FAIL, 23 passed) | `test-red-4b.log` |
| Proof 4b: re-apply | `git apply logs/fix.patch` | 0 | — |
| Proof 4b: rebuild fixed | `cmake --build build --target zene SampleEditorDialogTest -j4` | 0 | `build-9-refixed.log` |
| Proof 4b: GREEN | ctest as above | 0 | `test-green-2.log` |
| GREEN, verbose | `ctest -R '^SampleEditorDialogTest$' -V` | 0 (26 passed, 0 failed) | `test-green-3-verbose.log` |
| Affected family build | `cmake --build build --target EnvelopeGraphTest LfoGraphTest ExportQualityControlsTest ModulatorPanelTest -j4` | 0 | `build-10-family.log` |
| Affected family | `ctest -R '^(SampleEditorDialogTest\|EnvelopeGraphTest\|LfoGraphTest\|ExportQualityControlsTest\|ModulatorPanelTest)$' --output-on-failure` | 0 (5/5 passed) | `test-family-1.log` |
| Gate (shared script, as provided) | `python3 qwen/feature_check.py <worktree>` | 1 (PermissionError creating the sibling log directory `<worktree>-feature-check`, outside the write area) | `feature-check-1.log` |
| Gate (same checks, logs redirected) | `python3 -I <driver> <worktree> <logs>/gates-1` (the driver imports the gate list and verdict logic from `feature_check.py`) | 0 (all 8 rows PASS) | `feature-check-2.log`, `gates-1/` |

Affected-family test count: `SampleEditorDialogTest` runs 26 functions, 26 passed (verbose log). Family ctest run: 5 tests, 5 passed.

The three families named in the finding (`MultitapEchoControlDialog`, `WaveShaperControls`, `DynamicsProcessorControls`) have no configured test targets in this build tree (`ctest -N` reports 0 tests), so they could not be run. The family above is the set of configured tests I found that drive `gui::Graph` mouse input (`rg` over `tests/src`).

## Files changed

- `src/gui/widgets/Graph.cpp`: guard the sample writes in `mouseMoveEvent` on `m_mouseDown`.
- `tests/src/gui/SampleEditorDialogTest.cpp`: four regression functions, two helpers, one `TestGraph` seam (`deliverMove`), and the `<QMouseEvent>` include.
- `tests/upstream-modifications.txt`: ledger lines for the two changed inherited files.
- `docs/qwen-fixes-2026-10/cluster-new-p23-108.md`: this report.

## Self-check

1. **Each new test FAILS with the product change reverted and PASSES with it.** Yes. Proof 4b: with `Graph.cpp` reverted, `test-red-4b.log` shows FAIL for `rightButtonDragOnFreshGraphEditsNoSample`, `rightButtonDragAfterLeftStrokeEditsNoSample` and `hoverEditsNoSample` (EXIT 8). With the fix re-applied, `test-green-2.log` is EXIT 0. The control `leftButtonDragStillEditsSamples` passes in both states.
2. **Acceptance criteria proven by a test I ran.** The finding asks for one behaviour: no sample writes without a left-button stroke, and left strokes still edit. Both are covered by the functions above, run in `test-green-3-verbose.log`.
3. **Each edit is one undo step; reversibility.** Not applicable: `Graph` writes the model directly and has no undo or command path (see Evidence). No command was added or changed.
4. **Nothing added runs on the audio thread.** The change is in GUI-thread event handling (`mouseMoveEvent`). It removes a call (`changeSampleAt` / `drawLineAt`) for non-left moves; it adds no allocation, lock, wait, log or signal. The audio path is not touched.
5. **Old projects still load and render exactly as before.** No load, save or render code is touched. The guard changes only what a non-left-button move does.
6. **Qt 5.15 / MSVC / macOS portability.** The test uses the six-argument `QMouseEvent` constructor, the same form used in `tests/src/plugins/SlicerTWaveformTest.cpp:65`. No Qt-6-only API was added in product code.
7. **Deterministic test.** Yes. Events are constructed with explicit button state and sent with `QCoreApplication::sendEvent`; there is no timing, sleep or window exposure wait. The hover case calls the handler through `TestGraph::deliverMove`.
8. **Fix at the cause; siblings checked.** The guard is at the single place that writes samples on a move. The press paths were already left-only. No `Graph` subclass exists in the product, and nothing forwards moves into a `Graph`.
9. **No collateral damage.** The position bookkeeping and cursor update in `mouseMoveEvent` are unchanged, so a non-left drag still moves the cursor as before (see Residual below). The existing drag tests in `SampleEditorDialogTest` (`graphDrawAtFivePixelHeightKeepsSamplesFinite`, `firstShiftClickEditsOnlyTheClickedSample`) and the rest of the family pass.
10. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE,** so no claim-only evidence is needed.

## Residual and environment notes

- A non-left drag still moves the pointer: `mouseMoveEvent` still calls `QCursor::setPos` when the position changes, and this fix deliberately leaves it alone. Only the sample writes were the bug.
- If a left release is ever lost (for example, a modal dialog takes it), `m_mouseDown` stays true and a later right-button drag would edit again. That state is pre-existing and was not changed.
- The lane build tree refused re-configuration, so the new test file could not be registered and built. The regression was placed in the existing `SampleEditorDialogTest` instead.
- `CCACHE_TEMPDIR` was set to `~/.cache/zene-tmp/ccache-tmp-new-p23-108` for the builds, because the default ccache temp dir was not writable from this lane.
- The shared `feature_check.py` could not create its log directory beside the worktree. Its gate list and verdict logic were run unchanged from a driver in `~/.cache/zene-tmp/gate-driver-new-p23-108/`, with logs redirected to the LOG FOLDER.
