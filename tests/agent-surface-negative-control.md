# The `agent_surface` gate — how it bites, and how to satisfy it

SPEC-zene-studio.md **A15** / task **#620**. The gate is the ctest `agent_surface`
(`tests/CMakeLists.txt`) running `tests/agent-surface-gate.py` against the real
binary and `tests/data/agent-control-fixture.mmp`.

It is deliberately a Python test over the control socket rather than a test
binary: the thing it must see — every action in every menu and toolbar — only
exists inside a running instance, so it reflects the live `MainWindow` the same
way an agent would reach it. `control.surface_report` (registered by
`src/core/ControlCommandsSurface.cpp`) walks `menuBar()` recursively and the
`mainToolbar` widget and returns one entry per user-visible action.

```sh
cd build/tests && ctest -R agent_surface --output-on-failure     # the gate
python3 tests/agent-surface-gate.py build/lmms tests/data/agent-control-fixture.mmp   # same, directly
python3 tests/agent-surface-gate.py --self-test                  # the logic, no app needed
```

## What a failure means, and the three ways to fix it

| message | what it means | fix |
|---|---|---|
| `NEW action without a registered command: '<key>'` | a menu/toolbar action was added and nobody gave it a command id | give the action a command (below), **or** delete the baseline line if you are removing an action |
| `STALE baseline entry: '<key>' resolves to a command now` | the ratchet: an action that was grandfathered has been registered, so its baseline line is obsolete | delete that line from `tests/agent-surface-baseline.txt` |
| `STALE baseline entry: '<key>' is no longer in the surface at all` | the action was renamed or removed | delete that line (a rename changes the key) |
| `HARD: '<key>' declares command '<id>', which is not in the registry` | an action declares a command id that does not exist — worse than declaring nothing | fix the id; this cannot be grandfathered |
| `HANG:` / `CRASH/PROTOCOL:` | a command did not answer, or the process died while it ran | fix the command; an allowlist entry cannot cover a crash |
| `allowlist entry '<id>' is not justified` | the command declares no `requires`, so it has to be swept | drop the allowlist entry, or make the command declare what it needs |
| `--reanchor refused` | a re-anchor was attempted on a dirty run | fix the real problem first; the valve records, it does not bury |

**Registering an action** (the declaration contract, also in the gate's header):
set ONE of these on the `QAction` / `QToolButton` to a `"group.verb"` id that the
registry declares — `objectName()`, the dynamic property `controlCommand`, or
(for a `QAction` only) `data()`. `data()` is the weakest channel: it is already
used for template paths and config keys, so it only counts when it resolves to a
live command. Example (`src/gui/MainWindow.cpp`, the A11 wiring this lane landed):

```cpp
	// A11/A15: these two actions declare the registry commands they implement.
	m_undoAction->setData(QStringLiteral("control.undo"));
	m_redoAction->setData(QStringLiteral("control.redo"));
```

## What the reflection cannot see (stated, not hidden)

- **A declaration is not proof of routing.** The public Qt API cannot report
  which slots a `QAction` is connected to, so the gate proves the declaration
  exists and names a live command; it cannot prove the slot calls the registry.
  A11's "one action, one implementation" is what makes the declaration true.
- **Menus generated at run time are out of scope**, listed with a reason each in
  `tests/agent-surface-exempt.txt`: `RecentProjectsMenu` (the user's own files),
  `TemplatesMenu` (template files found on disk) and the `Tools` menu (whatever
  Tool plugins this build ships — measured here: *Tap Tempo*, *LADSPA Plugin
  Browser*). Their entries are counted and printed, never silently dropped, and
  the exemption is narrow enough that the self-test proves an unregistered
  action next to an exempt one still fails (`exemption is not a blanket`).
- **Actions created after start-up** (a menu built lazily on `aboutToShow`, a
  plugin's own menu) are only visible if they exist when the report is taken.
  The report is taken once, right after `engine_ready`; `m_viewMenu` is built in
  the `MainWindow` constructor and is therefore fully covered, but a menu built
  strictly on first show would be invisible. Nothing in this product does that
  today — `updateViewMenu()` is called explicitly at construction — and if one
  appears, the reflection has to be extended, not the gate's scope quietly
  widened.
- **The toolbar scope** is the `mainToolbar` widget (LMMS's toolbar is a plain
  `QWidget` of `ToolButton`s, not a `QToolBar`) plus any `QToolBar` descendant.
- **The sweep reaches a typed result, not every happy path.** `ControlSocket-
  Integration` holds the happy paths of this slice; this gate proves each
  command answers on the wire without crashing or hanging.

## Negative control — the gate bites, and names the action

Phase A: a menu action with no command id was injected into
`MainWindow::finalize()` (`src/gui/MainWindow.cpp`), then rebuilt (`-j4`, exit 0):

```cpp
	project_menu->addAction(embed::getIconPixmap("project_new"),
		tr("Negative control action"), this, &MainWindow::createNewProject);
```

`cd build/tests && ctest -R agent_surface --output-on-failure` → **exit 8**, verbatim:

```
    Start 28: agent_surface
1/1 Test #28: agent_surface ....................***Failed   10.93 sec
agent_surface: 47 reflected, 2 exempt (generated menus)
  reflection : 0 registered, 45 unregistered (44 grandfathered, 1 new)
  ratchet    : baseline 44 entries, 0 stale
  reverse    : 24 commands, 24 swept, 0 allowlisted
  budget     : 10.7 s of 120 s

---- 1 problem(s) ----
FAIL: NEW action without a registered command: 'menu:File/Negative control action' (surface=menu container=File)

FAIL: agent surface gate (1 problem(s))

0% tests passed, 1 tests failed out of 1

Total Test time (real) =  10.93 sec

The following tests FAILED:
	 28 - agent_surface (Failed)
Errors while running CTest
```

Note `44 grandfathered, 1 new`: the 44 known-unregistered actions stayed quiet
and the one new action was named — which is exactly the property the baseline
exists for. Phase B: the injection removed, rebuilt (exit 0), same command:

```
    Start 28: agent_surface
1/1 Test #28: agent_surface ....................   Passed    9.97 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   9.98 sec
```

## The ratchet bites too (a grandfathered action that becomes registered)

With the tree green at 44 baseline entries, `Edit → Undo` and `Edit → Redo` were
given their commands (the three-line A11 wiring above). Nothing else changed.
`ctest -R agent_surface` → **exit 8**, verbatim:

```
agent_surface: 46 reflected, 2 exempt (generated menus)
  reflection : 2 registered, 42 unregistered (42 grandfathered, 0 new)
  ratchet    : baseline 44 entries, 2 stale
  reverse    : 24 commands, 24 swept, 0 allowlisted

---- 2 problem(s) ----
FAIL: STALE baseline entry: 'menu:Edit/Redo' resolves to a command now - delete that line from the baseline, or re-anchor with --reanchor "reason"
FAIL: STALE baseline entry: 'menu:Edit/Undo' resolves to a command now - delete that line from the baseline, or re-anchor with --reanchor "reason"

FAIL: agent surface gate (2 problem(s))
```

Those two lines were deleted from `tests/agent-surface-baseline.txt` (44 → **42
entries**) and the run went green (`1/1 Test #28: agent_surface ... Passed
10.77 sec`). The baseline can only be *shrunk* by hand; it can never be grown
except by the deliberate, recorded valve:

```
$ python3 tests/agent-surface-gate.py ... --reanchor "reason"
RE-ANCHORED: baseline written from the live surface (42 entries)
reason: reason
```

An empty reason is refused before anything is read or written (`exit 2`), and a
re-anchor on a run that still has a real problem is refused as well, so the valve
cannot be used to bury a hang, a crash or a declaration that resolves to nothing.

## The rules themselves are tested (`--self-test`)

Fifteen cases drive the same decision core the live run uses (`evaluate()` in
`tests/agent_surface_lib.py`) with synthetic surfaces, so a rule that stops
biting is caught without starting the app (exit 0, verbatim):

```
  new unregistered action is named   ok
  grandfathered action passes        ok
  registered baseline entry is stale ok
  vanished action is stale           ok
  unknown declared command is hard   ok
  unaccounted command                ok
  unjustified allowlist entry        ok
  allowlist names unknown command    ok
  justified allowlist entry passes   ok
  hang is a failure                  ok
  crash is a failure                 ok
  generated menu is exempt           ok
  exemption is not a blanket         ok
  duplicate keys are refused         ok
  clean surface passes               ok
self-test: 15/15 rules bite
```

## What the sweep must never do: write into the repository

Found by this lane's own first full sweep: `project.save` with no arguments saves
back over whatever file is currently open — which is the fixture the gate had just
opened — so a sweep without an explicit path *edited its own test fixture*
(`tests/data/agent-control-fixture.mmp` came back 248 lines longer). Two fixes
landed with the gate:

1. `live_overrides()` always passes a path for `project.save`, pointing into the
   throwaway temp directory, alongside `render.render`'s output path;
2. the gate hashes the fixture before the sweep and again after it, and fails with
   *"the gate wrote to its own fixture project"* if the hash moved — so the next
   command that writes a file by default is caught by the gate rather than
   discovered in a dirty `git status`.

## Measured state of the surface at this landing (2026-09-11)

46 reflected, 2 exempt (generated menus), **2 registered** (the A11 wiring
above), **42 grandfathered**, 24 registered commands all swept to a typed result
in ~10 s of a 120 s budget. `mixer.set_pan` is the one typed refusal
(`refused`) — an honest limit of this tree recorded in AGENT-TOOLING.md §6, not a
gate failure.

The 42 entries are the size of the drift the command registry has to catch up
with: this product's UI is reachable by agents one command at a time, and every
one of those lines is a feature an agent cannot yet drive. The gate's job is to
make sure that number only ever goes down, and that nobody adds a fifty-first
without noticing.
