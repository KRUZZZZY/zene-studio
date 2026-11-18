# GUI batch-04 — Luna findings for `src/gui`

Worktree `zene-bunny-gui`, branch `bugs/bunny-gui-2026-10-05`. Six findings triaged:
2 FIXED, 3 FALSE-POSITIVE, 1 REAL-UNTESTED. No control command was added, removed or
renamed, and no params/result schema changed.

### Cluster 4846 — Detaching to another screen can report no change

Verdict: REAL-UNTESTED

Evidence: `src/gui/ControlCommandsLayouts.cpp:212-218` — `detachTo()` sets
`*changed = s->detachPanel(...)` and then, when `screen >= 0`, calls
`s->moveFloatingToScreen(panel, screen)` without folding that result into `*changed`.
`movePanel()` returns that `*changed` verbatim as `changed` (`:220-236`), and
`Shell::moveFloatingToScreen` (`src/gui/shell/ShellFloating.cpp:302-313`) does return
a meaningful bool. The claim about the code is correct, but no deterministic headless
regression exists: `screenArg` (`:200-208`) validates the index against
`QGuiApplication::screens().size()`, and the offscreen platform reports exactly ONE
screen (verified by running a 6-line `QGuiApplication` probe under
`QT_QPA_PLATFORM=offscreen`), so "detach to screen 0, then move to screen 1" cannot be
staged in this lane. Per procedure step 2, product code left unchanged.

Commit: none

Test: none

Red/green: none (probe only: `QT_QPA_PLATFORM=offscreen ./sc` -> `screens: 1`)

### Cluster 5678 — GUI commands call widgets directly from the command handler thread

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlRegistry.cpp:223-234` — `ControlRegistry::runOnUiThread`
marshals onto the app thread with
`QMetaObject::invokeMethod(app, ..., Qt::BlockingQueuedConnection)` whenever the caller
is not already on it, and `invoke()` wraps every handler in that call at
`src/core/ControlRegistry.cpp:270`. The handlers named in the finding
(`src/gui/ControlCommandsNoteEditor.cpp:142-143`,
`src/gui/ControlCommandsMixerView.cpp:98,114`) therefore already execute on the GUI
thread; the finding does not describe the dispatch path that exists.

Commit: none

Test: none

Red/green: none

### Cluster 4960 — Shortcut undo can alter pre-existing conflicts

Verdict: FALSE-POSITIVE

Evidence: the non-clashing inverse at `src/gui/ControlCommandsShortcuts.cpp:119-121`
advertises `shortcuts.set ... force:true`, but `undoByCommand`
(`src/core/ControlCommandsControl.cpp:328-345`) only replays an advertised inverse when
`inverse.applies == "command"`, and the `uistore` payload built by `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) sets no `applies`. Undo therefore falls
through to `undoThroughJournal`, which runs the step `UiConfigStore::recordWrite`
registered (`src/gui/UiConfigStore.cpp:53-63`) — a restore of the whole previous
override object, so no pre-existing binding can be lost. The regression case written
for it passes against unfixed code.

Commit: none

Test: `ShortcutsCommandsTest::undoingASetLeavesTheOtherBindingsAlone`

Red/green: none (case passes both before and after; run via
`cd build/tests && ctest -R '^ShortcutsCommandsTest$' --output-on-failure`)

### Cluster 4014 — Resetting an unknown shortcut id succeeds without resetting anything

Verdict: FIXED

Evidence: `src/gui/ControlCommandsShortcuts.cpp:132` (pre-fix) called
`overrides.remove(id)` for any non-empty id; an unknown id removed nothing and the
command answered `{"reset": 0}` as a success. A non-empty id is now resolved against
the live `sm::bindings(menus)` first and refused with `ControlErrorKind::NotFound`; the
empty-id "reset everything" path is untouched.

Commit: d127c2fc5

Test: `ShortcutsCommandsTest::resettingAnActionNoMenuHasIsRefused` (plus
`aKnownActionAndEveryActionStillReset`, which guards both reset paths)

Red/green: `cd build/tests && ctest -R '^ShortcutsCommandsTest$' --output-on-failure`
-> EXIT=8 (2 failed) before; -> EXIT=0 after. `ShortcutsPageTest`,
`AccessibilityWalkTest`, `WorkspaceAccessTest` -> EXIT=0.

### Cluster 4959 — Shortcut import treats non-string values as an unbind

Verdict: FIXED

Evidence: `src/gui/ControlCommandsShortcuts.cpp:213` (pre-fix) read each override with
`it.value().toString()`, which renders `null`, numbers, booleans and objects as `""` —
and `sm::portable` (`src/gui/ShortcutMap.cpp:111-118`) accepts the empty sequence as a
valid unbind, so `{"File ▸ Save": null}` silently unbound Save instead of being
reported. Each value must now be a JSON string before `sm::portable` sees it; a
non-string lands in `invalid` and is left out, while a real `""` still unbinds.

Commit: 181251015

Test: `ShortcutsCommandsTest::anImportedValueThatIsNotKeyTextLeavesTheActionBound`
(plus `anImportedEmptyStringStillUnbinds`, which guards the documented `""` unbind)

Red/green: `cd build/tests && ctest -R '^ShortcutsCommandsTest$' --output-on-failure`
-> EXIT=8 (2 failed) before; -> EXIT=0 after. `ShortcutsPageTest`,
`AccessibilityWalkTest`, `WorkspaceAccessTest` -> EXIT=0.

### Cluster 4914 — Saved selections can point to a different clip after reordering

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlVocabulary.cpp:148-156` — `clipIdOf` returns
`clipId(clip->id())` and `clipId(int)` formats `clip-<id>` from the creation-assigned
`Clip::id()`, not an ordinal. `resolveClip` (`src/core/ControlEditSupport.cpp:162-190`)
resolves saved ids by that id and carries an explicit comment (`:171-179`) that there is
deliberately NO positional fallback, so a moved or reordered clip keeps its id and a
dead-but-well-formed id is reported NotFound rather than silently resolving elsewhere.

Commit: none

Test: none (an existing selection/arranger test already covers id-based apply)

Red/green: none

Leftovers: cluster 4846 is real in code but untestable in this lane — the offscreen Qt
platform exposes one screen. It wants
`*changed |= s->moveFloatingToScreen(panel, screen)` in
`src/gui/ControlCommandsLayouts.cpp:216`, which needs a multi-screen (real X/Wayland or
a virtual-display) run to regression-test.