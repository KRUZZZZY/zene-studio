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
| Header | The product mark, then Play and Stop (`transport.play` / `transport.stop`), then the readouts - tempo, meter, position, time, master level, transposition and engine load - each a caption over a value that drags, scrolls or takes typing (`TransportReadouts`; typing a position is `transport.seek`). A stretch follows, then New, Open (with recent projects), Save, Export and the metronome toggle. The strip is `objectName "mainToolbar"`, which the agent-surface gate walks. | `ShellHeader` (`src/gui/shell/ShellChrome.cpp`) |
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
| `devices` | Devices | bottom | Alt+D | `DevicePanel`: the selected track's devices as cards, then its mixer channel's inserts with their modulators (§8) |
| `automation` | Automation | bottom | Alt+A, Ctrl+4 | the automation editor |
| `pattern` | Pattern Editor | bottom | Alt+P, Ctrl+2 | the pattern editor |
| `inspector` | Inspector | left | Alt+I | `InspectorPanel`: the selected track's, clip's or mixer channel's properties (§8) |
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
  - `selection`: what the Inspector and the Device panel show (`window.select`'s reply, §8)
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

### Panels in their own windows: the tear-off (0.5.0 G, theme 8)

Any panel can leave the shell for a window of its own, which can sit on another monitor, and
come back. Every route ends in `window.detach` or `window.attach` (`Shell::dispatchFloat`):

- **Drag the header out.** Past the drag distance a preview of the window follows the
  pointer; pulled more than a header's height off the header (or past its ends) and released,
  the panel floats there, as a browser tab tears off its strip. Released on the header band,
  or with Esc, nothing changes (`ShellTearOff`). The header shows an open hand; its buttons keep
  the arrow.
- **Drag it back.** Dragging a floating panel's header moves its window. Over the main window
  a dashed outline marks the panel's dock zone; inside it the zone fills and says "Release to
  dock", naming the panel it will replace when its region already shows one. A panel docks
  into its own home (the template of §2 gives every tool one home): its side region, or the
  centre when it is the view's main panel. Dragging by the system title bar moves the window
  but cannot dock it (the program never sees that release); over the zone a short note says
  to drag the header instead.
- **Dock button, key, menu.** The header's pop-out button reads **Dock** while the panel
  floats. **Ctrl+Shift+F** floats or docks the focused panel (from either window).
  Double-clicking the header, or right-clicking it, does the same.
- While the view's main panel floats, the centre says where it went and offers **Show Its
  Window** and **Dock It Here**. A floating panel's footer switch stays lit and brings its
  window forward. Closing the window from its title bar docks the panel; a program closing
  every window (File > Quit) leaves it floating, so the next start brings it back.
- A saved layout (`window.layout_save`, the persisted `ui/shell.layout`) lists the floating
  panels with their screen and geometry (`floating: [{panel, screen, x, y, width, height}]`).
  Applying it floats those and docks the rest; a window whose screen is gone opens on the
  primary one. A layout saved before 0.5.0 has no `floating` and leaves the windows as they are.
- On Wayland a program can neither place its windows nor see the pointer outside them: the
  header drag hands the move to the compositor and docking is the Dock button's.

## 6. What is not done yet, stated

- **Device cards draw at most 16 parameters** (`DeviceCard::MaxParameters`); the rest are
  in the device's own editor, one click away. A built-in effect's own control dialog, opened
  from a card, still shows that plugin's inherited artwork.
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
| `DevicePanelTest` | the selection's rules; the Inspector edits through `track.rename` / `track.set_mute` and undoes; the Device panel draws a channel's inserts and the modulator beside the one it drives, its power button is `plugin.bypass`, and a device removed over the socket takes its card at once |
| `ControlDevices` | `window.select` over the socket (§8): a track's devices in chain order, a clip and its track, a channel with its insert and modulator, the panel following a removal and its undo, a typed refusal |

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

## 8. The Inspector and the Device panel

Both panels follow one selection, `ShellSelection` (`include/ShellSelection.h`): a track, a clip
on it, or a mixer channel on its own. A press on a track header selects the track, a press on a
clip selects the clip and its track, a press on a mixer strip selects that channel, and opening a
clip in the note editor selects it. One application-wide event filter reads those presses and
never consumes one. `window.select {track | clip | channel}` is the agent's click; its reply is
what the two panels draw.

**The Inspector** (`InspectorPanel`) shows the track's colour and name (typing a name is
`track.rename`), Mute and Solo (`track.set_mute` / `track.set_solo`), volume and pan knobs and a menu of the
mixer channel it plays into (`track.set_mixer_channel`); then the clip's name, start, length and, by kind, its note count with
**Edit notes** (`window.note_editor_set {clip, fit}`) or its gain (`clip.set_gain`). A selected
mixer channel shows its name, mute, solo, volume and pan.

**The Device panel** (`DevicePanel`) draws, left to right: the track's instrument, the devices on
the track's chain, then - when the track plays into a channel of its own, not the master - that
channel's inserts, with each modulator's card placed right after the device it drives. A card
(`DeviceCard`) is built from the device's own parameter models, in the order and with the names
`dsp.get_state` reports, so a user and an agent read the same list: a flat knob per number, a
toggle per switch, a menu per choice, a readout per integer. The header's power button is
`plugin.bypass`, the close button `plugin.unload`, and the editor button opens the plug-in's own
window (`plugin.editor_open`) or, for a built-in effect, its control dialog. **Add device** lists
the built-in effects (`plugin.load`).

A chain edited from anywhere - a rack, the socket, an undo - redraws the panel. The chain's own
signals drop the cards *before* a device is deleted, so no control ever paints a deleted
parameter; a 2 Hz check while the panel is visible catches what sends no signal (a modulator
added over the socket).

Screenshots: `docs/ui-logs/050-visual-overhaul/`.
