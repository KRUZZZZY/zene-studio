# Instrument view safety — the `InstrumentView::setModel` crash, what it is, and what a user sees

**Branch:** `post-alpha/instrument-view-safety` · **Worktree:**
`projects/lmms-fl-research/zene-pa-instrview` · **Base:**
`post-alpha/instrument-hosting-impl` (`5c69b514f`) · **Product:** Zene Studio (LMMS-derived,
GPL-2.0-or-later).

**This lane answered one question:** the instrument-hosting lane reported that the fallback parameter
grid could not be verified headlessly because `InstrumentView::setModel` crashes inside Qt's
`setWindowIcon` on both the offscreen platform and Xvfb — so is that a documented limitation or "the
user clicked the instrument and the application died"?

**Verdict, in one paragraph.** The crash is real and reproduces exactly as reported — but it is
**not** a screen artefact and **not** reachable through any path the product uses. It is a
**null-pointer dereference on the `InstrumentTrackWindow` the view expects to be parented inside**
(`src/gui/instrument/InstrumentView.cpp:61`), and it fires for *any* caller that parents the view
somewhere else. On the shipping path the parent is `InstrumentTrackWindow::m_tabWidget`
(`InstrumentTrackWindow.cpp:466`), so the window opens normally: **verified by driving the shipped
binary against a project carrying a VST3 instrument track, under Xvfb, on both the pre-fix and the
post-fix build — the grid opens and the process survives in both.** The guard was still landed: it is
contained, it costs nothing, it makes the entry point fail soft for every non-window caller, and it is
what allows the entry point to be regression-tested at all. The plugin's own editor remains
unimplemented (`IPlugView`), which is the only instrument-UI limitation a user actually meets.

**The reproduction the lane reported is two walls, not one.** The crash they saw was wall 1 (the
window-icon dereference); behind it sits wall 2, `Knob` construction, which needs a live
`GuiApplication` and therefore cannot run in a render-only test binary at all. §5 records wall 2
because "the grid does not construct headlessly" is true for that second reason too, and the honest
answer to "is the grid usable" had to come from the running product.

---

## 1. The code path a user reaches

```
Song Editor: the track's label button (TrackLabelButton), toggled
  -> InstrumentTrackView::toggleInstrumentWindow(bool)      src/gui/tracks/InstrumentTrackView.cpp:309
  -> InstrumentTrackView::getInstrumentTrackWindow()        :253  (lazily `new InstrumentTrackWindow(this)`)
  -> InstrumentTrackWindow::InstrumentTrackWindow()         src/gui/instrument/InstrumentTrackWindow.cpp:76
       getGUI()->mainWindow()->addWindowedWidget(this)      :287
       updateInstrumentView()                               :289
  -> m_instrumentView = m_track->m_instrument->createView( m_tabWidget );   :466
  -> Plugin::createView(QWidget*)                           src/core/Plugin.cpp:263-271
       instantiateView(parent) -> new gui::Vst3InstrumentView(...)
       -> InstrumentView::InstrumentView(instrument, parent)  src/gui/instrument/InstrumentView.cpp:34
            setModel(instrument)                              :38
              instrumentTrackWindow()->setWindowIcon( model()->logo()->pixmap() );   ***line 61***
```

`instrumentTrackWindow()` (`:69`) is `dynamic_cast<InstrumentTrackWindow*>(parentWidget()->parentWidget())`.
There is exactly one product call site that reaches it:

```
$ grep -rn "createView(" src/gui src/tracks src/core include | grep -vE "ClipView|gui::TrackView|EffectControlDialog"
src/gui/EffectView.cpp:90                 (an effect's controls -> EffectControlDialog, different hierarchy)
src/gui/editors/TrackContainerView.cpp:282 (Track -> TrackView)
src/gui/instrument/InstrumentTrackWindow.cpp:466   <-- the only Instrument::createView
src/gui/MainWindow.cpp:368                 (ToolPlugin::instantiate(...)->createView(this), tools only)
```

`InstrumentView::setModel` has no other caller (`grep -rn "instrumentView->setModel" src/` → nothing;
the ctor is the only entry). So the product's parent chain is `view -> m_tabWidget ->
InstrumentTrackWindow` and the cast succeeds. Any *other* parent — a harness that passes a plain
`QWidget`, an embedding, a future caller — yields `nullptr` and line 61 dereferences it.

### The lane's diagnosis, corrected

The report read the crash as the plug-in's logo pixmap failing to resolve in a test binary. The pixmap
is a red herring: a failed load never returns null.

```
$ sed -n '97,102p' src/gui/embed.cpp
        const auto pixmap = QPixmap::fromImageReader(&reader);
        if (pixmap.isNull()) {
                qWarning().nospace() << "Error loading icon pixmap " << name << ": " << reader.errorString();
                return QPixmap{1, 1};          <-- a valid 1x1 pixmap, not a null one
        }
```

The `qWarning` and the segfault appear together simply because both happen at line 61, in that order.
The null is the **window pointer**, and the exception address proves it: the crash reports
`address 0x0000000000000008` — a member access off a null `this` — and the faulting frame is
`QWidget::setWindowIcon`.

---

## 2. Reproduction, with stacks

### 2.1 The instrument's view, built from the pre-fix source (the lane's crash)

`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`,
`testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow()` — a `QWidget parent` and the base
`InstrumentView` ctor (no plug-in widgets, see §5 for why).

```
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./Vst3InstrumentIntegrationTest \
      testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow; echo BIN_EXIT=$?
QWARN  : ... Error loading icon pixmap "vst3instrument/logo": "File not found"
Received signal 11 (SIGSEGV), code 1, for address 0x0000000000000008
BIN_EXIT=139
```
(log: `tests/evidence/instrument-view-safety/in-test/test-view-prefix-offscreen.log`)

Under a real X server (Xvfb, `QT_QPA_PLATFORM=xcb`) it is byte-for-byte the same:

```
$ DISPLAY=:98 QT_QPA_PLATFORM=xcb ./Vst3InstrumentIntegrationTest \
      testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow; echo BIN_EXIT=$?
QWARN  : ... Error loading icon pixmap "vst3instrument/logo": "File not found"
Received signal 11 (SIGSEGV), code 1, for address 0x0000000000000008
BIN_EXIT=139
```
(log: `tests/evidence/instrument-view-safety/in-test/test-view-prefix-xvfb.log`)

Stack, offscreen (gdb, `logs/gdb-prefix-offscreen.log` → committed as
`in-test/gdb-prefix-offscreen.log`) — the reported frame is frame #0:

```
Thread 1 "Vst3InstrumentI" received signal SIGSEGV, Segmentation fault.
0x00007ffff79c2b9b in QWidget::setWindowIcon(QIcon const&) () from /lib/x86_64-linux-gnu/libQt6Widgets.so.6
#0  QWidget::setWindowIcon(QIcon const&)   libQt6Widgets.so.6
#1  lmms::gui::InstrumentView::setModel   src/gui/instrument/InstrumentView.cpp:61   <-- this lane
#2  lmms::gui::InstrumentView::InstrumentView   src/gui/instrument/InstrumentView.cpp:38
#3  lmms::(anonymous namespace)::BareInstrumentView::BareInstrumentView   (the test)
#4  lmms::Vst3InstrumentIntegrationTest::testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow
```

### 2.2 The mechanism, isolated in Qt itself

A four-case probe (`tests/evidence/instrument-view-safety/qt-probe/probe-setwindowicon.cpp`, ~60
lines, Qt 6.4.2, standalone) calls `setWindowIcon(icon)` with the icon `loadPixmap()` returns on a
miss (`QPixmap{1,1}`) on four widget shapes, on both platforms:

| case | widget the icon is set on | offscreen | Xvfb (`xcb`) |
| --- | --- | --- | --- |
| `toplevel` | a top-level `QWidget` | exit 0 | exit 0 |
| `child` | a non-top-level child widget | exit 0 | exit 0 |
| `stacked` | a widget two levels down (the shape an `InstrumentTrackWindow` has) | exit 0 | exit 0 |
| `null` | `c->parentWidget()->parentWidget()`, i.e. what `instrumentTrackWindow()` returns here | **SIGSEGV, exit 139** | **SIGSEGV, exit 139** |

```
$ QT_QPA_PLATFORM=offscreen ./probe-setwindowicon null
case null: derived window pointer = QWidget(0x0)
Segmentation fault (core dumped)          EXIT=139
$ gdb -batch -ex run -ex "bt 3" --args ./probe-setwindowicon null
Program received signal SIGSEGV, Segmentation fault.
#0  QWidget::setWindowIcon(QIcon const&)   libQt6Widgets.so.6
#1  main ()
```

So `setWindowIcon` is not fragile: it is fine for every widget shape with a valid icon, on both
platforms. It dies only when `this` is null.

---

## 3. Headless-only, or real? — **neither: it is parent-shape dependent**

Three measurements decide it, and they agree:

1. **The crash is screen-independent.** Identical address (`0x8`), identical frame, offscreen and on
   a real X server (§2.1, §2.2). If it were a no-screen artefact, `toplevel`/`child`/`stacked` would
   at minimum behave differently between the two platforms. They do not.
2. **The crash is parent-shape dependent, and the crash site says so** — the null pointer is the
   `dynamic_cast` result, not a pixmap, not a `QScreen` (§1, §2.2).
3. **No product path has that shape.** The single Instrument `createView` call site passes
   `m_tabWidget`, which is a `TabWidget` created with the window as its parent
   (`InstrumentTrackWindow.cpp:233`), so `parentWidget()->parentWidget()` *is* the window. This is not
   a structural argument alone — it was run:

**Product run, shipped binary, Xvfb, a project with a VST3 instrument on the "Bass" track**
(`docs/evidence/.../product-run/`, script `run-app-instrwindow.sh`; the project is
`Crunk(Demo).mmp` with that track's `<instrument>` block replaced by the `vst3instrument` element the
product's own writer produces):

| binary | click the VST3 track's instrument button | process | what the instrument window contained |
| --- | --- | --- | --- |
| **pre-fix** (`prefix-2-after.png`) | window opens | **alive** | `Controls for Zene VST3 Test Instrument`, a `Level` knob, `The instrument's own editor is not shown in this version - these are its parameters.` |
| **post-fix** (`postfix-2-after.png`) | window opens | **alive** | identical |

```
$ bash logs/gui-click/run-app-instrwindow.sh prefix 145 215
app pid=710314  log=.../app-prefix.log
main window=4194324 after 3s
title before: vst3-instrument - LMMS 0.1.0-alpha.5+5c69b51
ALIVE after clicking the VST3 instrument track's button: yes (pid 710314)
```

**So: the reported crash is a harness limitation — but not because it is headless.** It is a real,
display-independent process-killing crash that the product happens not to reach, and the honest
statement is "the view's entry point was only safe for callers that parent it inside an
InstrumentTrackWindow". Nothing in the product does otherwise; the guard removes the constraint.

The one thing this box cannot do: a *physical* display. Xvfb is a real X server with a real screen and
a real platform plugin, and the probe covers all four widget shapes on it; the residual difference
between Xvfb and a desktop session is the window manager and the driver, neither of which is in the
crash or the fix. `xvfb` is thus the strongest claim this environment supports, and §6 names what a
GUI session would still add.

---

## 4. The fix: a contained guard, not a removed feature

`src/gui/instrument/InstrumentView.cpp` (upstream file, declared in
`tests/upstream-modifications.txt`):

```diff
 void InstrumentView::setModel( Model * _model, bool )
 {
         if( dynamic_cast<Instrument *>( _model ) != nullptr )
         {
                 ModelView::setModel( _model );
-                instrumentTrackWindow()->setWindowIcon( model()->logo()->pixmap() );
+                // The window icon belongs to the instrument window, and that window is
+                // only reachable while this view is parented inside one
+                // (InstrumentTrackWindow -> m_tabWidget -> view). Any other parent
+                // leaves instrumentTrackWindow() null, and the unconditional call that
+                // used to be here dereferenced it and killed the process inside
+                // QWidget::setWindowIcon. Skipping the icon costs nothing that matters:
+                // the view itself is still built and shown.
+                if( auto * window = instrumentTrackWindow() )
+                {
+                        window->setWindowIcon( model()->logo()->pixmap() );
+                }
                 connect( model(), SIGNAL(destroyed(QObject*)), this, SLOT(close()));
         }
 }
```

Why this shape and not a fail-soft stub in the plug-in:

- It is the **contained** fix, so the feature is untouched: when the window exists, the identical
  `setWindowIcon` call runs as before; when it does not, the view is still built and shown without
  an icon. Nothing is deleted, and the parameters stay reachable.
- It matches the file's own style — `~InstrumentView()` already guards the same call
  (`if( instrumentTrackWindow() )`, pre-fix `:47`).
- `model()->logo()` needs no second guard: all 57 plug-in descriptors in the tree carry a non-null
  logo loader (`grep -rh --include=*.cpp "Plugin::Type::\(Effect\|Instrument\),$" plugins/ | wc -l`
  → 57, and none is followed by a null logo — the one single-line descriptor, `Xpressive.cpp`, has
  `new PluginPixmapLoader("logo")`).
- A fail-soft *entry point* (an inert control plus "this format's editor is unavailable") would have
  been the wrong medicine here: the entry point was never the problem, and the grid is not inert —
  it works. That wording belongs to `IPlugView`, which is §7's open item in the hosting report, not
  to this crash.

### The regression test

`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`,
`testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow()` — it builds the module's instrument on
a track (the shipped `vst3instrument.so`, through `InstrumentTrack::loadInstrument`), constructs the
view with a plain `QWidget` parent, and asserts we get past the entry point
(`view.model() == instrument`, the view is parented, `instrumentTrackWindow() == nullptr`).

```
pre-fix  : SIGSEGV, exit 139  (stack in §2.1)      <- the negative control
post-fix : PASS, all 9 cases, exit 0, offscreen and Xvfb
```

The test uses the base `InstrumentView` (a 6-line subclass in the test file) rather than
`createView()`, because the plug-in's own view builds `Knob`s — see §5.

---

## 5. Wall 2: the generated grid cannot be built in this test binary at all

With the guard in place the test binary gets past line 61 and dies one wall later, in the plug-in's
own view. This is a **harness** limit, and it is worth recording because it is the reason the
hosting lane's "the grid's construction was not verified" was true, and the reason this report's
grid evidence comes from the product instead:

```
Thread 1 "Vst3InstrumentI" received signal SIGSEGV, Segmentation fault.
#0  lmms::gui::SimpleTextFloat::SimpleTextFloat   include/GuiApplication.h:65   <-- getGUI()->mainWindow(), getGUI() == null
#1  lmms::gui::FloatModelEditorBase::initUi   src/gui/widgets/FloatModelEditorBase.cpp:76
#3  lmms::gui::Knob::Knob   src/gui/widgets/Knob.cpp:47
#5  lmms::gui::Vst3InstrumentView::Vst3InstrumentView   plugins/Vst3Instrument/Vst3InstrumentView.cpp:64
#8  the test
```
gdb log: `tests/evidence/instrument-view-safety/in-test/gdb-wall2-knob-needs-gui-offscreen.log`

`src/gui/widgets/SimpleTextFloat.cpp:39` is `QWidget(getGUI()->mainWindow(), Qt::ToolTip)`, and
`getGUI()` is null because the test binary runs `Engine::init(true)` (render-only) — no
`GuiApplication`, no `MainWindow`. Every `Knob` in LMMS needs it, which is also why no test in this
repository constructs one. Making the test boot a whole GUI is not a reasonable price for this lane,
so the grid is verified where it actually runs (§3, product run) and the test asserts the entry point
with widget-free views.

---

## 6. What is NOT proven

1. **No physical display was used.** Offscreen and Xvfb; §3 explains why the fix and the crash are
   indifferent to which, and what a desktop session would add (window manager, drivers, real DPI).
   A GUI session can re-run §2.2's four probe cases and §3's product run unchanged.
2. **The non-null icon branch is not unit-tested.** Setting a window icon on a real
   `InstrumentTrackWindow` needs a `MainWindow`, i.e. a booted GUI. It is unchanged code (the same
   call, now inside an `if`), and it is exercised by every instrument window in the product — the
   pre-fix product run in §3 is itself evidence that the branch works, because that run took it.
3. **The subject is the in-tree MIT fixture.** No third-party VST3 instrument was available
   (inherited limitation, `docs/VST3-INSTRUMENT-HOSTING.md` §9.1).
4. **The guard is upstream code.** It is declared in `tests/upstream-modifications.txt` with its
   reason; Gate 6 passes on it (§7).
5. **`QGtkStyle`/Wayland/other platforms** were not exercised; the crash and the guard contain no
   platform-specific code.

---

## 7. Build, test and gate evidence (exit codes unpiped)

Build root: this worktree's own `build/` (VST3 SDK copied in from the hosting lane's checkout, tag
`v3.8.1_build_84`, `LICENSE.txt` present). Logs: `tests/evidence/instrument-view-safety/build/`.

**Baseline, the brief's command** — `JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4`:

```
configure EXIT=0   (build/configure.log — "-- Found VST3 SDK 3.8 (MIT) at .../build/vst3sdk", NOT "skipped")
build EXIT=0       (build/build.log)
ctest EXIT=0   ctest totals: 100% tests passed, 0 tests failed out of 25
local-ci: overall exit=0
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
```

25 tests is the baseline set: `WANT_VST3` was AUTO→ON (the SDK checkout is detectable) but the fixture
option was still off. `-j4` was used with ~20 GB available; no `Terminated signal` lines appeared, so
the sibling lane's OOM did not recur here.

**Feature configuration, the same build directory** (`-DWANT_VST3_TEST_INSTRUMENT=ON`):

```
cmake -S . -B build -DWANT_VST3_TEST_INSTRUMENT=ON   CONFIGURE_EXIT=0
cmake --build build -j 4                             BUILD_EXIT=0   (fixture: build/tests/data/vst3-test-instrument/vst3-test-instrument.vst3)
cd build/tests && ctest --output-on-failure -j 2     CTEST_EXIT=0
100% tests passed, 0 tests failed out of 28
```

**The test this lane changed, by name, both platforms:**

| build | platform | command | exit |
| --- | --- | --- | --- |
| pre-fix | offscreen | `Vst3InstrumentIntegrationTest testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow` | **139** (SIGSEGV) |
| pre-fix | Xvfb `xcb` | same | **139** (SIGSEGV) |
| post-fix | offscreen | same | **0** — `9 passed, 0 failed` |
| post-fix | Xvfb `xcb` | same | **0** — `9 passed, 0 failed` |

Post-fix the new case reports its own evidence:
`instrument view built without an instrument window: model=0x… parent=0x… window=0x0`.

**Gates** (`bash tests/run-all-gates.sh --no-mutation`):

```
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP   (--no-mutation run; not run with --with-coverage — see below)
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 SKIP   (--no-mutation)
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS
RESULT: PASS — every executed gate passed
RUN_ALL_GATES_EXIT=0
```

`tests/no-upstream-regression-gate.sh` alone: `GATE6_EXIT=0`. **Gate 2 (coverage) was not run with
`--with-coverage`** and **Gate 5 (mutation) was skipped by flag**, so this is a PASS-WITH-SKIPS, not a
pass on those two axes.

Both were re-run **after** the three commits landed, with the committed tree, because Gate 6 reads
`git diff <base>..HEAD` and an uncommitted working tree hides a file from it:
`GATE6_FINAL_EXIT=0` (32 files in the ledger, 0 violations) and `RUN_ALL_GATES_FINAL_EXIT=0` with the
same PASS/SKIP table. The committed evidence for both runs is
`tests/evidence/instrument-view-safety/gates/{gate6-final.log,run-all-gates-final.log}`. The first
Gate 6 run in this lane (before the commit) did catch a real mistake worth recording: the evidence
tree was originally under `docs/evidence/`, and Gate 6 classifies only `*.md` under `docs/` as
documentation, so a `docs/evidence/**` tree of logs, screenshots and scripts is reported as an
undeclared change to upstream-inherited code. It moved to `tests/evidence/` (`tests/**` is allowed),
and the reason is written down in §9 so the next lane does not repeat it.

**Gate 9 does not exist on this branch** — `bash tests/fork-sources-gate.sh` →
`GATE9_EXIT=127` (`No such file or directory`), exactly as the brief anticipated. No result is claimed
for it.

---

## 8. Proposed wording for the release documents

**Not applied here** (`docs/KNOWN-LIMITATIONS.md` and the release notes are the orchestrator's to
edit). The hosting lane's §7 draft is compatible with this and is *extended*, not replaced: it says
the grid is what a user gets instead of the plug-in's own editor, and its §9.2 left the grid's
construction unverified because of this crash. That open item is now closed — what a user meets is the
editor limitation alone.

> **VST3 instruments: opening the instrument window shows LMMS' generated parameter grid, not the
> plug-in's own editor.** A VST3 instrument loads onto an instrument track, plays MIDI with
> sample-accurate timing and saves its state with the project. Clicking the instrument button on the
> track opens the instrument window as usual — the track's mixer channel, pitch range and a knob per
> VST3 parameter, drawn by Zene Studio, with the line *"The instrument's own editor is not shown in
> this version — these are its parameters."* What does not open is the instrument's own GUI:
> `IPlugView` is not implemented, so a plug-in's custom editor (and any control it has that is not a
> VST3 parameter) is not available. Instruments that ship no user interface at all are unaffected.

Optional, one line for the release notes' "fixed" list if the orchestrator wants the internal detail:

> Fixed: opening an instrument's window could crash the application when the instrument view was
> created outside the track window (a null window pointer dereferenced inside Qt's
> `setWindowIcon`). The product's own path was not affected; the view now degrades to "no window
> icon" instead of terminating (`docs/INSTRUMENT-VIEW-SAFETY.md`).

---

## 9. Reproducing all of it

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-instrview

# 0. the SDK must be at <build>/vst3sdk (LICENSE.txt present) for WANT_VST3=AUTO to switch on
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4          # configure/build/ctest, exit 0
cmake -S . -B build -DWANT_VST3_TEST_INSTRUMENT=ON && cmake --build build -j 4

# 1. the Qt-level mechanism (four widget shapes, two platforms)
cd tests/evidence/instrument-view-safety/qt-probe
g++ -fPIC probe-setwindowicon.cpp -o probe-setwindowicon $(pkg-config --cflags --libs Qt6Widgets)
QT_QPA_PLATFORM=offscreen ./probe-setwindowicon all               # exit 139 on the "null" case
Xvfb :98 -screen 0 1280x1024x24 & DISPLAY=:98 ./probe-setwindowicon all   # same

# 2. the in-test crash (pre-fix) and the pass (post-fix)
cd ../../../../build/tests
QT_QPA_PLATFORM=offscreen ./Vst3InstrumentIntegrationTest \
    testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow    # exit 0 at HEAD (the guard is in)

# 3. the product run under Xvfb (~60 s; needs Xvfb and xdotool)
cd ../..
bash tests/evidence/instrument-view-safety/product-run/run-app-instrwindow.sh postfix 145 215
#   -> "ALIVE ... yes", and product-run/postfix-2-after.png shows the "Bass" window with
#      "Controls for Zene VST3 Test Instrument", the Level knob, and the disclosure line
```

Evidence index (`tests/evidence/instrument-view-safety/`): `qt-probe/` (probe source + four-case
outputs + gdb), `in-test/` (pre/post-fix runs on both platforms, both gdb stacks), `product-run/`
(Xvfb transcript, both before/after screenshot pairs, the driver script, the crafted project and the
config used), `gates/`, `build/` (local-ci and fixture-configure logs).

The evidence lives under `tests/` rather than `docs/` on purpose: Gate 6 allows everything under
`tests/**` but classifies only `*.md` under `docs/` as documentation, so a `docs/evidence/**` tree of
logs, screenshots and scripts is reported as an undeclared change to upstream-inherited code. Raw
scratch (the sweep screenshots and the full build logs) stays in the untracked `logs/` directory in the
worktree.
