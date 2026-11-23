FIXED

# Cluster 917 - Bounce waits synchronously for up to ten minutes

## Verdict and what was real

`src/core/BounceInPlace.cpp:167` still waits for the render child with
`waitForStarted(30000) && waitForFinished(600000)`, and the comment at
`src/core/BounceInPlace.cpp:162-166` says why: the synchronous verbs keep their
legacy result contract for agents, and the cancellable `RenderQueue` is the
responsive path. That engine wait is intended (see
`docs/RENDER-CHILD-WAIT.md` and `docs/m0-design/render-jobs.md`, "Legacy
synchronous replies"), and this branch already built the asynchronous path for
cluster 917 (`render.queue_bounce` / `render.queue_add`; commits `f6498a18f3`
and `5e7b5d037a`).

The reported **symptom** was nevertheless real on this branch: the normal GUI
entries for bounce, freeze, flatten and range bounce still called the
synchronous verbs, so choosing one blocked the UI thread inside
`waitForFinished()` for the render's whole duration. One of them was also
misdirected: `TrackRenderMenu.cpp:67-77` sent `render.queue_bounce` the
output-only operation `bounce`, which that command refuses (its enum excludes
`bounce`; `tests/src/core/ControlRenderJobTest.cpp` asserts the refusal), so
"Bounce in place" did nothing.

The fix therefore is at the GUI cause: every menu render entry now starts the
cancellable queue job (`render.queue_bounce`, or `render.queue_add` for the
output-only bounce, whose honest class is `not_mutating`). The synchronous verbs
are untouched and keep their documented compatibility contract.

| GUI path (line after this fix) | Verb it ran before | Now |
|---|---|---|
| `src/gui/shell/ShellCommandMenus.cpp:281-290` (Render submenu) | `bounce.land`, `bounce.in_place`, `freeze.track` | `render.queue_bounce` (`bounce_land`, `freeze_track`), `render.queue_add` (`bounce`, default out path) |
| `src/gui/shell/ShellCommandMenus.cpp:504` (Project Tools) | `range.bounce` | `render.queue_bounce` (`range_bounce`) |
| `src/gui/LibraryCommandMenus.cpp:265` | `track.flatten` | `render.queue_bounce` (`flatten`) |
| `src/gui/MenuBarExtras.cpp:141` (Navigate > Range) | `range.bounce` | `render.queue_bounce` (`range_bounce`) |
| `src/gui/TrackRenderMenu.cpp:69-101` | broken `render.queue_bounce` with `bounce` | `render.queue_add` with the same default path `bounce.in_place` generated (`control::resolveOutPath`, `ControlBounceSupport.cpp:113`) |

No command, schema, undo class, save format or engine code changed; the queue
commands and their reversibility classes already existed.

## Regression test

`tests/src/gui/RenderMenuQueueTest.cpp` builds each real menu, triggers its
entry, and asserts the queue recorded the job *before any event processing*.
The legacy synchronous verbs record no queue job, so the assertion is about
what the menu did, not about how fast the render was. The render child is the
test binary pretending to be `zene render` (`argv[1] == "render"`, the same
seam `tests/src/core/ControlRenderJobTest.cpp` uses), so no real engine start
is paid and there is no timing assertion, sleep or race in the oracle.

Cases: the track menu's Bounce in place (output-only, `bounce`), the shell
Render menu (Bounce in Place, Bounce to New Track, Freeze), the library
Flatten entry, and Navigate > Range > Bounce to a New Track.

## Commands and exit codes (logs in the run's log folder)

| Step | Command | EXIT |
|---|---|---|
| RED (before the product change) | build `RenderMenuQueueTest` - `build-red-1.log` | 0 |
| RED | `ctest -R '^RenderMenuQueueTest$' --output-on-failure` - `test-red-1.log` | 8 (4 cases FAIL, Actual 0 jobs) |
| GREEN build | `cmake --build build --target RenderMenuQueueTest zene -j4` - `build-green-1.log` | 0 |
| GREEN test | `ctest -R '^RenderMenuQueueTest$' --output-on-failure` - `test-green-1.log` | 0 |
| 4b revert | `git diff -- src/gui/TrackRenderMenu.cpp src/gui/shell/ShellCommandMenus.cpp src/gui/LibraryCommandMenus.cpp src/gui/MenuBarExtras.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 |
| 4b RED build | `cmake --build build --target RenderMenuQueueTest -j4` - `build-red-3.log` | 0 |
| 4b RED test | `ctest -R '^RenderMenuQueueTest$' --output-on-failure` - `test-red-3.log` | 8 (all four cases: Actual 0 jobs) |
| 4b restore | `git apply .../fix.patch` | 0 |
| 4b GREEN build | `cmake --build build --target RenderMenuQueueTest -j4` - `build-green-4.log` | 0 |
| 4b GREEN test | `ctest -R '^RenderMenuQueueTest$' --output-on-failure` - `test-green-4.log` | 0 |
| family | `ctest -R '^(RenderMenuQueueTest\|TrackRenderMenuTest\|ControlRenderJobTest\|ControlBounceRenderTest\|MixerBusFreezeMenuTest\|RenderRangeMenuTest\|LibraryMenusTest\|BatchFreezeTest)$'` - `test-family-1.log` | 0 (8/8) |
| gates | `python3 .../feature_check.py <worktree>` - `gates-1.log` | 0 (all eight rows PASS) |
| GUI harness | `window.screenshot` of `build/zene` - `gui-screenshot-1.log`, `cluster-917-window.png` | 0 |

The screenshot is 1600x1000 PNG, 130265 bytes,
sha256 `1fdbebbc5708fbfb873d1f63f00e6eb07c0e8f4169ba9986629e7990fd91696c`.

## Self-check

- **The regression test fails with the product change reverted and passes with
  it.** Proven twice: the initial RED run (`test-red-1.log`, EXIT 8) and the
  4b reversed-patch run (`test-red-3.log`, EXIT 8), where all four cases report
  `Actual 0` queue jobs against `Expected 1`. Restored: `test-green-4.log`,
  EXIT 0. The patch and both `git apply` calls exited 0.
- **The test is deterministic.** The oracle is `render.queue_list` immediately
  after `QAction::trigger()`, before any `processEvents()`: either the menu
  invoked a queue command (one job recorded synchronously) or it did not. There
  is no wall-clock bound, sleep or race in the assertion. The fake child is
  selected by `argv[1] == "render"` and the bounded drain after each case is
  cleanup only (`ControlRenderJobTest`'s existing pattern).
- **The fix is at the cause and complete for the reported path.** All five GUI
  entries that called a blocking render verb are listed above and now use the
  queue; the two duplicate shell menus (Inspector and the track's More menu),
  the track operations menu, the library track section and Navigate > Range are
  covered. The synchronous verbs are deliberate compatibility wrappers
  (`docs/m0-design/render-jobs.md`) and are left unchanged, as are their agent
  callers. The wider family was checked: `freeze.tracks` was already async
  (`BatchFreeze`), `render.queue_bounce` already covers freeze region, flatten,
  bounce-land and range bounce, and the only render entry with no async queue
  operation left is `freeze.bus` (see Other findings).
- **No collateral damage.** The output-only bounce passes the same default path
  `bounce.in_place` produced (`control::resolveOutPath`), so the file name and
  audio are unchanged; `render.queue_add` is `not_mutating`, exactly the class
  the design gives an output bounce. Project files, save data, schemas and
  command ids are untouched. No limit was added.
- **Every acceptance item is covered by the test and its exit code.** The four
  cases map one-to-one to the five product edits (the shell case checks its
  three entries).
- **Undo and realtime.** A queued bounce-land/freeze/flatten/range bounce is
  still one `ProjectJournal` step recorded on the main thread by
  `RenderQueue::applyFinishedBounce`; the output-only bounce records none. The
  diff is GUI menu construction only: nothing runs on the audio thread, and no
  allocation, lock, wait or log was added to a render/process path.
- **Old projects and saved data** are unaffected: no model, serialisation or
  command code changed.
- **GUI test drives the visible control.** The test calls
  `QAction::trigger()` on actions obtained from the real menu builders
  (`addTrackRenderActions`, `ShellCommandMenus::populateTrack`,
  `LibraryCommandMenus::populateTrack`, `installNavigateMenu`) and observes the
  registered command's effect (`render.queue_list`), not a handler called
  directly.
- **Portability.** Only existing Qt 5/6-compatible APIs (`QAction`, `QMenu`,
  `QProcess`-backed queue) are used; no Qt-6-only call, `windows.h` name or
  `std::filesystem` was added.
- **Sibling commands.** No command file, schema or reversibility table was
  changed. The diff only changes which already-registered id and arguments a
  GUI action passes: `app.*`, `arrangement.*` and every unrelated command's
  handler, arguments and class are untouched. The render siblings
  (`bounce.in_place`, `bounce.land`, `freeze.track`, `freeze.region`,
  `track.flatten`, `range.bounce`) keep their synchronous compatibility
  contracts and remain the verbs an agent gets when it sends them; the queue
  siblings (`render.queue_bounce`, `render.queue_add`) keep their existing
  enum, required arguments and classes (`true_inverse` / `not_mutating`).

## Other findings (not changed here)

- `src/gui/MixerBusFreezeMenu.cpp:44` still calls the synchronous `freeze.bus`
  from the bus strip's context menu. There is no render-queue operation for a
  bus freeze, so routing it would need a new queue operation and a regenerated
  command snapshot canary; out of scope for this fix.
- A control-surface agent that sends the synchronous `bounce.in_place`,
  `freeze.track`, `freeze.region`, `bounce.land`, `track.flatten` or
  `range.bounce` verb directly still waits for the child. That is the documented
  legacy result contract (`docs/m0-design/render-jobs.md`); the deferred-reply
  transport in `docs/RENDER-CHILD-WAIT.md:128-141` is its own lane.
