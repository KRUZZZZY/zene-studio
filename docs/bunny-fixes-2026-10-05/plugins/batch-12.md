# Plugins batch-12 — Luna clusters 908, 5478

Branch `bugs/bunny-plugins-2026-10-05`. Two findings, no product code changed:
one SKIPPED-SCOPE (the documented `ProcessMutex` contract, already triaged for
the byte-identical Vestige/Ladspa/Vst shape in batch-08 cluster 5451) and one
REAL-UNTESTED whose observation is accurate but whose proposed fix would break
the reload path, and for which no deterministic headless test exists here.

### Cluster 908 — Zyn can block audio processing on its plugin mutex

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ZynAddSubFx/ZynAddSubFx.cpp:370-373` — `processLock()` is
`m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0)`, and
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:399-408` (`handleMidiEvent()`) takes the
same mutex with a blocking `lock()`. This is the documented host contract, not
an accident: `include/AudioPlugin.h:110-124` states "Some LMMS plugins currently
require a lock around their process method ... Until all process methods are made
lock-free, plugins may override these lock/unlock methods", and the
non-blocking consumption sites are `include/AudioPlugin.h:166` and `:266`. The
same shape was already triaged in
`docs/bunny-fixes-2026-10-05/plugins/batch-08.md:12-27` (cluster 5451), which
names `plugins/ZynAddSubFx/ZynAddSubFx.cpp:370` and `:386-410` explicitly as
byte-identical to the Vestige case.

The observation is factually accurate — the export-time `-1` arm can block the
render thread, and the MIDI arm is a plain blocking lock — but the proposed fix
("hand changes or MIDI events across a nonblocking queue or safe swap point")
requires either new lock-free access to the third-party Zyn DSP objects or a
redesign of the shared `ProcessMutex` contract, with allocation and unbounded
growth risk on the audio thread. That is the rule 6 realtime case and rule 9
known-by-design, i.e. SKIPPED-SCOPE, not a defect to fix in this lane.

Commit: none

Test: none

Red/green: none

### Cluster 5478 — The Zyn hosting checkbox is not bound to the instrument's model

Verdict: REAL-UNTESTED

Evidence: `plugins/ZynAddSubFx/ZynAddSubFx.cpp:729` calls
`m_separateProcess->setChecked( m->m_separateProcessModel.value() )`, and
`include/AutomatableButton.h:58-62` overrides `setChecked()` to
`model()->setValue(_on)`. That `model()` is the checkbox's OWN `BoolModel`,
constructed in `src/gui/widgets/AutomatableButton.cpp:47`
(`BoolModelView( new BoolModel( false, nullptr, _name, true ), this )`) — not the
instrument's `m_separateProcessModel`. So the code-shape claim is right: the
value is copied once at view-attach and never bound, unlike the adjacent MIDI
checkbox, which is bound at `plugins/ZynAddSubFx/ZynAddSubFx.cpp:726`
(`m_forwardMidiCC->setModel( &m->m_forwardMidiCcModel )`). A programmatic
`setHostingMode()` (control surface `oop.set_mode`) or a `loadSettings()` while
the view is open does not refresh the visible checkbox.

The suggested fix (`setModel( &m->m_separateProcessModel )`) must NOT be applied,
because it breaks the toggle. With the button bound to the instrument model, a
user click runs `src/gui/widgets/AutomatableButton.cpp:179-192` `toggle()` →
`model()->setValue(...)` → `update()` → `QPushButton::setChecked()`, which emits
`toggled` only AFTER the instrument model already holds the new value. Then
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:751-762` reads that same model and calls
`setHostingMode( checked )`, which returns false at
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:443-448` because the model already equals
the requested mode — so `reloadPlugin()` would never run and the checkbox would
stop re-hosting. Correct synchronisation has to keep the button's own model as
the click source and mirror the instrument model into it, which is a wider
change than this defect needs and is exactly the settle-point design documented
at `plugins/ZynAddSubFx/ZynAddSubFx.h:108-116`.

No headless test is possible in this tree, so per procedure the product code is
left alone: the Zyn module is not built here (`build/plugins/` holds 47 plugin
modules and no `libzynaddsubfx.so`), the plugin class has no includable header
(it is reached through `QLibrary`/`lmms_plugin_main`), and
`tests/src/plugins/ZynSeparateProcessTest.cpp:557` is `QTEST_GUILESS_MAIN` with
no `QApplication`, so `instantiateView()` cannot be constructed to inspect the
checkbox. `tests/CMakeLists.txt:2386-2392` gates the whole suite behind
`ZYN_PLUGIN_PATH`/`RemoteZynAddSubFx`, which is why `initTestCase()` QSKIPs
(`tests/src/plugins/ZynSeparateProcessTest.cpp:342-360`).

Commit: none

Test: none

Red/green: none

Leftovers: Cluster 5478 is still an open UI-sync gap — a later
`setHostingMode()`/`loadSettings()` leaves the checkbox showing its previous
state. Fixing it needs a mirror connection from `m_separateProcessModel` into
the checkbox's own model (NOT `setModel`, which would disable the reload), plus
a GUI-capable test target: a Zyn module built in the test job and a
`QTEST_MAIN`/offscreen test that instantiates the view and asserts the checkbox
follows a programmatic mode change. Neither the build nor the test-target change
belongs to this lane's targeted-build budget.