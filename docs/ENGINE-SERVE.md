# Serve mode: the engine as a process of its own

`zene --engine --control-socket <path> [project.mmpz | --import <file>]`

Runs the engine with the control server and an event loop and nothing else: no `gui::GuiApplication`,
no main window, no widget. A front end in another process (the Rune UI, an agent, a script) is then the
only interface there is. Added by Q050-517 (Rune engine item E1). The Qt application is unchanged.

## What starts

| | Application (`zene`) | Serve mode (`zene --engine`) |
|---|---|---|
| Qt application object | `gui::MainApplication` (a `QApplication`) | `QCoreApplication` |
| Interface | `gui::GuiApplication`: main window and every editor | none |
| Engine | `Engine::init(false)`: configured audio and MIDI devices | the same |
| Control server | only with `--control-socket` | required; refused without it |
| Initial project | recovery prompt, project argument, `--import`, last project, or new | recovery (no prompt), project argument, `--import`, or new. It does not reopen the last project: a front end says what to open |
| Ends on | the window closing, `control.quit` | `control.quit`, SIGTERM, SIGINT |

`lmms::isEngineServe()` (`include/UnattendedRun.h`) is the one predicate. While it is true
`hasDisplay()` is false and `isUnattendedRun()` is true whatever the desktop offers, so:

- a command that declares `requires: display` is refused with the typed `requires` error by the
  registry's existing check, before its handler runs;
- `window.*` commands that need the main window answer `requires` ("this instance has no main
  window"); the ones that only read stored state (`window.layout_list`, `window.appearance`) answer;
- no code path can open a dialog: every prompt in the core is already gated on `isUnattendedRun()`.

`control.ping` reports `interface`: `"none"` in serve mode, `"qt"` in the application.

## Quitting

- `control.quit` ends the process through the same path as in the application: exit code 0, the
  socket unlinked, the bounded last-resort guard armed and then cancelled.
- `control.quit {save:true}` saves the project before the process ends. In the application the main
  window's close event does that save; serve mode does it when the event loop winds down
  (`engineserve::saveOnQuit`). A save that fails is reported on stderr.
- SIGTERM and SIGINT are `control.quit {save:false}`. A supervisor needs no socket client to stop the
  engine cleanly.

## What the suite found

Running the existing control-socket suite with `--engine` found three defects, fixed with this mode:

- `project.import` of a MIDI file crashed (SIGSEGV): the importer built a `QProgressDialog` on the
  main window unconditionally. The dialog is now made only where there is an interface.
- A save dropped the timeline (loop points, punch region): only a process with a GUI wrote and read
  `<timeline>`. A served engine now writes and reads it; render and `--run-script` runs are unchanged.
- An audio device that could not open, and an unconfigured first start, were announced on stderr only
  by the main window. Serve mode prints the same lines.

It also answers `control.ping` while the engine starts (`engine_ready:false` with the reason), as the
application does.

## What serve mode does not do

- **Autosave.** The autosave timer belongs to `MainWindow`, so a served engine writes no
  `recover.mmp`. The front end must save. (It does recover a recovery file an application session
  left behind, without a prompt, as an unattended application start does.)
- **Plug-in editors.** They are native windows and need a `QApplication` and a display; they are
  refused typed here. Q050-522 owns editors in serve mode.
- **Windows.** The flag is parsed on every platform, but SIGTERM handling and the tests are POSIX.
  Q050-524 owns the named-pipe transport and hosted runs on Windows and macOS.
- **A smaller binary.** It is the same executable, so the dynamic linker still maps QtWidgets and
  QtGui; no QtWidgets or QtGui *object* is constructed. The measurements below say what that buys.

## Tests

| Test | What it holds |
|---|---|
| `EngineServeTest` (`tests/src/core/EngineServeTest.cpp`) | the start refusals, the quit-time save rule, the no-display rule |
| `ControlEngineServe` (`tests/control-engine-serve.py`) | the real binary: ready with no display at all (and the application in the same environment is not), interface commands typed, edit/undo/save/open, `control.quit`, `save:true`, SIGTERM, the start refusal, a project argument |
| `tests/serve-mode-suite.py <build>` (hosted: the `Serve-mode control suite` step of the linux job) | the whole control-socket suite again with `--engine` (`ZENE_HARNESS_ENGINE_SERVE=1`), minus the tests named with a reason in `tests/serve-mode-exclusions.txt`; `--audit` fails on an exclusion that is no longer needed |

## Measurements

`python3 tests/serve-mode-measure.py build/zene --runs 5` prints this table. The figures below are
from the commit that added this page; they are a measurement of one machine under load, not a budget.

| Mode | ready, s | resident, MB | threads | shared objects | Qt libraries mapped |
|---|---|---|---|---|---|
| offscreen application | 2.52 (2.46-2.54) | 269.30 (269.18-269.71) | 29.00 (29.00-29.00) | 450.00 (450.00-450.00) | libQt6Core, libQt6DBus, libQt6Gui, libQt6Network, libQt6Pdf, libQt6PrintSupport, libQt6Svg, libQt6Widgets, libQt6Xml |
| serve mode (--engine) | 0.33 (0.33-0.34) | 201.32 (200.97-201.50) | 22.00 (22.00-22.00) | 399.00 (399.00-399.00) | libQt6Core, libQt6DBus, libQt6Gui, libQt6Network, libQt6Svg, libQt6Widgets, libQt6Xml |

Median of 5 (min-max), 2026-11-28, RelWithDebInfo, Qt 6.12, Dummy audio device, load average about 10
on 20 threads. Phase 0 measured the offscreen application at 266 MB and 2.5 to 3.0 s on train 35.
Serve mode is ready about 2.2 s sooner and holds about 68 MB less; QtWidgets and QtGui stay mapped
because it is the same executable (see above).
