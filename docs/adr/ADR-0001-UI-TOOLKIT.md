# ADR-0001: the 0.5.0 interface is a new Qt Widgets shell, not a Qt Quick/QML port

- **Status:** accepted, 2026-10-02 (train `0.5.0/ui-train`, lane `050/ui-shell`)
- **Context:** the owner's direction of 2026-10-02: rip out the MDI interface and build a
  single-window interface modelled on Bitwig Studio, with a reworked piano roll
  (`plans/PROMPT-UI-REDESIGN-BITWIG.md` in the program workspace).

## Decision

The new interface is written in **Qt Widgets**. It has a new shell (one window, fixed regions,
data-driven panels and views), a design-token system, and new custom-painted widgets. It does not
use Qt Quick or QML. "Modern" comes from the design system, which sets layout, density, colour,
type and vector painting. Bitwig is in the same position: its interface is custom-painted, not
built from a stock widget toolkit.

## Evidence

1. **The toolkit is not on the build machines.** This box has Qt 6.4.2 with Widgets, Svg,
   SvgWidgets, OpenGLWidgets and Test, but no `Qt6Quick` or `Qt6Qml` CMake packages
   (`ls /usr/lib/x86_64-linux-gnu/cmake | grep Qt6`, 2026-10-02). Part of the hosted matrix
   still builds against Qt 5.15 (`.github/workflows/build.yml:153` sources `/opt/qt5*`). A QML
   interface would need new dependencies on seven platforms and would have to run on two Qt
   major versions.
2. **Plugin interfaces are Widgets.** 95 headers under `plugins/` declare widget-based
   instrument views, effect dialogs or tool views (`grep -rln` for `InstrumentView`,
   `ToolPluginView` and `EffectControlDialog`). In a QML shell each of them would sit in a
   `QWidget` window container or need porting. The Device panel shows these views, so this cost
   lands on the panel the owner asked for.
3. **The guarantees are bound to widgets.** These are written against `QWidget` objects:
   50 GUI tests (`tests/src/gui/`), the accessibility walk (`AccessibilityWalkTest`, which walks
   `QAccessible` over real widgets), the `agent_surface` gate (which reads `setData` and
   `controlCommand` declarations on `QAction` and `QWidget`), and `window.screenshot`
   (`QWidget::grab`). A QML port would have to rebuild all of them before it could show they
   still hold.
4. **The realtime and lifetime lessons stay where they were learned.** BUGS 11.27 (a view
   outliving its model) and 11.28 (engine-thread signals queued to the GUI thread) were fixed in
   widget code, and their regression tests drive widgets.

## What "modern" means here, concretely

- **One token source** (`DesignTokens`): colour, spacing, radius, type scale and component
  sizes. New widgets paint from the tokens. The stylesheet for standard widgets is generated
  from the same tokens, so there is no second palette to drift.
- **Flat, dark, high contrast.** Every text/surface pair the tokens declare is held to WCAG AA
  (4.5:1) by a test, alongside the existing `ThemeContrastTest` and `WcagContrastTest`.
- **Resolution independence.** Icons are painted vectors (`QPainterPath`, or SVG through
  QtSvg, which is already a dependency). Geometry is in device-independent pixels. Hairlines
  are drawn at a physical pixel on HiDPI screens.
- **Bitwig's layout logic, not its trade dress.** A fixed frame of header, body and footer.
  The body has left (Inspector), centre, bottom and right (Browser) regions. Each region holds
  one panel at a time, chosen by a radio group of footer buttons. Views (Arrange, Mix, Edit)
  are named bundles of panels. None of Bitwig's names, icons or artwork are used.

## Consequences

- Every existing editor keeps working on day one: the shell mounts the existing widgets
  (song editor, mixer, clip launcher, automation editor) in panels. New editors replace them
  one at a time, starting with the piano roll.
- The MDI workspace is retired as the main surface. Secondary windows (instrument and effect
  dialogs, controller dialogs, plugin editors) become floating windows. This reuses the
  existing detach mechanism (`SubWindow::detach`) until the `SubWindow` class itself is
  replaced.
- The Focus Desk is superseded by panels and views. Its tests leave with it, and each
  guarantee they carried is restated as a shell test. The list is in the lane's commit and in
  `docs/UI-SHELL.md`.
- The cost of this choice: the interface is limited to what QPainter and Widgets do well. There
  are no GPU-composited animations. Smooth zoom and scroll in the editors comes from painting
  only what is visible and caching per zoom level, measured by `PianoRollBenchmarkTest`.

## Rejected

- **A QML port.** Rejected for evidence items 1 to 3. This can be revisited if Qt Quick becomes
  a dependency on every platform for some other reason, and if the plugin views have a
  migration path.
- **Restyling the MDI interface.** Rejected because the owner asked for a rip-out, and because
  restyling keeps the defect class behind BUGS 11.30 (windows clamped into a workspace the user
  cannot see).
