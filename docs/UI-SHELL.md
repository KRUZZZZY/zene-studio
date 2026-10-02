# The 0.5.0 interface shell

The single-window interface that replaced the MDI workspace and the Focus Desk on the
`0.5.0/ui-train` line. The toolkit decision is recorded in `docs/adr/ADR-0001-UI-TOOLKIT.md`
(Qt Widgets, a new shell, design tokens). The direction comes from the owner (2026-10-02):
a modern single-window interface modelled on Bitwig Studio's layout logic, without its names,
icons or artwork.

## 1. The frame

The main window holds one page, `Shell` (`include/Shell.h`), laid out top to bottom:

| Band | What it holds | Code |
|---|---|---|
| Menu bar | File, Edit, View, Tools, Help. Unchanged: every action still declares its command. | `MainWindow::finalize` |
| Header | The product mark, then Play and Stop (`transport.play` / `transport.stop`), then the song editor's tempo, time, meter and master controls. A stretch follows, then New, Open (with recent projects), Save, Export and the metronome toggle. The strip is `objectName "mainToolbar"`, which the agent-surface gate walks. | `ShellHeader` (`src/gui/shell/ShellChrome.cpp`) |
| Body | Four regions (§2). | `Shell::buildBody` |
| Footer | One switch per panel, grouped by the region the panel opens in, then the view words ARRANGE · MIX · EDIT, then the Browser switch on the far right. | `ShellFooter` |

## 2. Regions, panels and views

The body is a fixed template, not a docking system:

```
+-----------+---------------------------------------+-----------+
|           |  [launcher] |  centre: the view's   |           |
|  LEFT     |             |  main panel           |  RIGHT    |
| Inspector +-------------+-----------------------+  Browser  |
|           |  BOTTOM: one secondary panel          |           |
+-----------+---------------------------------------+-----------+
```

- **A side region (left, bottom, right) shows at most one panel.** Its footer switches are a
  radio group: opening a panel hides the one the region held.
- **Each view has a centre panel that cannot be hidden.** Arrange's is the arranger, Mix's is
  the mixer, Edit's is the note editor.
- **The clip launcher sits beside the centre panel**, in Arrange and in Mix, when it is
  switched on.
- **Each view keeps its own side panels.** Switching views restores the panels you had in
  that view.

The panels (`ShellLayout::panels()`):

| id | Title | Home region | Shortcut | Content today |
|---|---|---|---|---|
| `arranger` | Arranger | centre (Arrange only) | Ctrl+1 | the song editor |
| `launcher` | Clip Launcher | beside the centre (Arrange, Mix) | Alt+L | `SessionGridView` |
| `mixer` | Mixer | bottom (centre in Mix) | Alt+M, Ctrl+5 | `MixerView` |
| `detail` | Note Editor | bottom (centre in Edit) | Alt+N, Ctrl+3 | `NoteEditor`, the reworked piano roll (`docs/NOTE-EDITOR.md`); the classic piano roll floats (`classic_piano_roll`) |
| `devices` | Devices | bottom | Alt+D | a placeholder note until lane `050/ui-panels` |
| `automation` | Automation | bottom | Alt+A, Ctrl+4 | the automation editor |
| `pattern` | Pattern Editor | bottom | Alt+P, Ctrl+2 | the pattern editor |
| `inspector` | Inspector | left | Alt+I | a placeholder note until lane `050/ui-panels` |
| `browser` | Browser | right | Alt+B | `BrowserPanel`: the plugin and file browsers behind one source selector |

The views switch with **Alt+1** (Arrange), **Alt+2** (Mix) and **Alt+3** (Edit). Alt+letter
avoids the menu mnemonics (F, E, V, T, H). The MDI era's Ctrl+1 to Ctrl+5 stay as second
shortcuts on the panels those editors became. Ctrl+6 and Ctrl+7 still toggle the controller
rack and project notes.

All the rules (one panel per region, the centre cannot be hidden, a view's offer list,
refusals with reasons, restoring a saved layout) live in `ShellLayout`. That class has no
widgets, and `ShellLayoutTest` proves the rules without a display. `Shell` renders whatever
`ShellLayout` says.

## 3. One action per command

Each view and panel is one `QAction`, owned by the shell. The View menu, the footer button and
the shortcut all use that same action. The action dispatches `window.set_view` or
`window.toggle` through the registry, the same call an agent makes on the socket (SPEC A11),
and it declares that command in its `controlCommand` property for the agent-surface gate.

## 4. The control surface

- **`window.get_state`** returns:
  - `view`
  - `panels`: `{id, title, region, visible, available, shortcut, mounted}` for each panel
  - `focused`: the panel that last took keyboard focus
  - `launcher`: whether the launcher is beside the centre
  - `editors`: `{editor, visible, panel}` for every `window.toggle` name
- **`window.toggle {editor}`** accepts every panel id. It also accepts the 0.4.0 editor names,
  which keep working:
  - `song` → `arranger`
  - `piano_roll` → `detail`
  - `clip_launcher` → `launcher`
  - `pattern`, `automation` and `mixer` are unchanged
  - the floating windows `controller_rack`, `project_notes` and `microtuner`

  Showing a panel hides the panel its region held. A view's centre stays visible. A panel that
  the current view does not offer (the arranger or the launcher in Edit) is revealed by
  switching to Arrange. The reply is `{editor, visible, panel, view}`.
- **`window.set_view {view}`** (new in 0.5.0) switches to `arrange`, `mix` or `edit` and replies
  `{view, changed}`.
- **`window.detach_all` and `window.attach_all`** are now compatibility no-ops that always
  answer `detached: true`. Every secondary window floats, and the editors are panels, not
  windows. Both commands will be removed together with the MDI code (§6).
- **`window.screenshot`** renders the whole shell. On the offscreen platform the window is
  given its working size (1600 × 1000) on first show, because the offscreen platform's nominal
  800 × 800 screen would otherwise clamp it. A real screen keeps the user's window state.

`tests/control-shell.py` (ctest `ControlShell`) drives all of the above over the socket.

## 5. Floating windows and project compatibility

- The MDI workspace object still exists but is never shown. It is the parent of every
  `SubWindow`: the instrument and effect windows, plugin editors, the controller rack, project
  notes and the microtuner.
- With `SubWindow::setFloatOutsideWorkspace(true)`, a sub-window becomes its own top-level
  window when shown, centred over the main window. It closes the way the long-standing
  "detached" behaviour closes, and it is never re-attached.
- Editor geometry saved by MDI-era projects (`x`, `y`, `width`, `height`, `visible`,
  `maximized`) is **ignored safely**. For a panel's editor, `restoreWidgetState` finds no
  sub-window and does nothing (`ShellTest::mdiEraGeometryInAProjectIsIgnoredSafely`). The tool
  windows (controller rack, project notes, microtuner) no longer restore visibility from a
  project. The default template saves the controller rack as visible, which would otherwise
  pop up a floating window on every new project. Saving still writes the attributes, so an
  older build can read the file.
- Instrument and sample track windows still restore their open state, because an open plugin
  window is per-object state that the user left open.

## 6. What is not done yet, stated

- **Inspector and Device panel:** placeholder notes only, until lane `050/ui-panels`.
- **Header readouts:** tempo, time and meter are still the song editor's legacy LCD widgets
  in the header strip. They will be replaced with token-painted readouts.
- **Arranger toolbar:** the arranger keeps its own toolbar, which duplicates Play and Stop.
- **Dead MDI code:** `SubWindow`, `MainWindow::workspace()`, the MDI geometry clamp and the two
  detach verbs are still present. The 30 `addWindowedWidget` callers need a `FloatingWindow`
  replacement before they can go.
- **Not verified:**
  - Plugin editors that embed native windows (Vestige VST2 with embedding, Carla parameter
    windows) were not exercised. Vestige marks its window non-detachable, and the float path
    ignores that flag.
  - `InstrumentTrackView::topLevelInstrumentTrackWindow` reads the hidden workspace's
    activation order, which no longer changes. It returns the newest visible instrument window
    rather than the most recently activated one.

## 7. Tests

| Test | Holds |
|---|---|
| `ShellLayoutTest` | region, view and refusal rules; save/restore; corrupt layouts |
| `ShellTest` | the rendered shell: a view switch moves an editor without deleting it; the region rule; reveal by switching view; actions and footer follow the layout; 24 px targets; persistence; MDI geometry ignored; BUGS 11.30's clamp (moved from the retired Focus Desk tests) |
| `DesignTokensTest` | every declared text/surface pair meets AA; the generated stylesheet meets AA; the velocity ramp is ordered by lightness; icons are paths that paint at the screen's scale |
| `AccessibilityHelperTest` | every footer switch and view word is named and Tab-focusable; the chrome bands and panel frames are named |
| `ControlShell` | the socket proof (§4) and three distinct rendered views |

The Focus Desk's four test files (`FocusDeskTest`, `FocusDeskPaneTest`, `FocusDeskActionsTest`,
`FocusDeskWorkspacesTest`) were retired with the Focus Desk. Each guarantee they carried that
still applies is restated above:

- **Every surface is named.** `AccessibilityHelperTest`.
- **The layout survives a restart.** `ShellTest::theLayoutSurvivesARestart`.
- **No clamp into a hidden area.** `ShellTest::aRestoredWindowIsNotClampedIntoAHiddenWorkspace`.
- **An editor handed between hosts is never freed.**
  `ShellTest::aViewSwitchMovesAnEditorWithoutLosingIt`.
- **A module that cannot be shown says why.** The shell gives a typed refusal with a reason, in
  `ShellLayoutTest::aViewRefusesWhatItDoesNotOffer`.

Screenshots taken under offscreen: `docs/ui-logs/050-ui-shell/`.
