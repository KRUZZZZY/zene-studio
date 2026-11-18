# Plugins lane — batch 02 (2026-10-05)

Six findings, all in the CLAP host (`plugins/ClapEffect`). Three fixed with regression tests,
three left as SKIPPED-SCOPE with evidence.

### Cluster 5540 — A plug-in resize request never resizes the host window

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ClapEffect/ClapHostInternals.h:287-294` — `guiRequestResize` stores
`guiRequestedWidth/Height` and forwards to the plug-in's own `set_size`, which is what CLAP asks
for: the plugin tells the host the size it wants and the host relays it. The window that would have
to follow is owned one layer up, in `src/gui/PluginNativeEditor.cpp:155-176`, whose `Hooks` struct
(`include/PluginNativeEditor.h:63-68`) exposes exactly three callbacks — `attach`, `detach`, `size`
— and no resize, show or hide channel at all. Wiring one up means a new public hook on a GUI class
plus a second observer path in a module this lane does not own; it is a cross-module design change,
not a defect patch in the host.

Commit: none

Test: none

Red/green: none

### Cluster 5541 — GUI show and hide requests are reported as successful but ignored

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ClapEffect/ClapHostInternals.h:296-297` — both callbacks are `return true`. The
reported symptom is accurate but the fix has nowhere to land, for the same reason as 5540: there is
no show/hide channel from `HostedPlugin` to the native editor window
(`include/PluginNativeEditor.h:63-68`). Additionally, the CLAP contract these callbacks serve is
satisfied: `request_show`/`request_hide` are the *plugin* asking the host to show or hide *its own*
window, and this host already owns the visible state through `openEditor()`/`closeEditor()` in
`plugins/ClapEffect/ClapHostGui.cpp:40-104`. Adding the routing needs a new cross-module hook.

Commit: none

Test: none

Red/green: none

### Cluster 5539 — A plug-in closing its editor leaves the host believing it is still created

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostInternals.h:299-302` — `guiClosed` took no
`was_destroyed` and only set `guiClosedByPlugin`, leaving `guiCreated` true. `openEditor()` returned
true on its first line (`ClapHostGui.cpp:43`) without showing anything, so `editorOpen()` stayed
false after a "successful" reopen, and `closeEditor()` went on to `hide` + `destroy` a GUI the
plug-in had already destroyed. `guiClosed` now clears `guiCreated` when `was_destroyed` is true, and
`openEditor` shows an existing-but-hidden GUI instead of returning early.

Commit: 45f7aa2f7

Test: `ClapHostTest::testAPluginClosedEditorIsReopened` (both `was_destroyed` cases, via a new
`CLAP_TEST_GAIN_CLOSE_GUI=hide|destroy` trigger in `tests/data/clap-test-plugin/clap-test-gain.c`)

Red/green: red `ClapHostTest testAPluginClosedEditorIsReopened` → `FAIL! ... 'plugin.editorOpen()'
returned FALSE` at ClapHostTest.cpp:1021; green `cd build/tests && ctest -R '^ClapHostTest$'
--output-on-failure` → EXIT=0 (23 passed, 0 failed), plus `ctest -R
'^ClapLoaderErrorTest$|^ClapEffectIntegrationTest$|^ClapBusMapTest$'` → EXIT=0 (3/3).
Build `cmake --build build --target zene ClapHostTest ClapEffectIntegrationTest ClapLoaderErrorTest`
→ EXIT=0.

### Cluster 3744 — Non-finite sample rates pass validation

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostLifecycle.cpp:54` guarded only `sampleRate <= 0.0`. Every
comparison against NaN is false, so NaN — and an infinity — passed the guard and reached
`clap_plugin.activate()` as the plug-in's sample rate. The check is now `!std::isfinite(sampleRate) ||
sampleRate <= 0.0 || maxBlockSize <= 0` (with `<cmath>` added).

Commit: 2b2261421

Test: `ClapHostTest::testANonFiniteSampleRateIsRefused`

Red/green: red `ctest -R '^ClapHostTest$'` → EXIT=8, `'!plugin.prepare(NaN, TestBlockSize, &error)'
returned FALSE. (a NaN sample rate reached activate())`, 20 passed 1 failed; green same command →
EXIT=0, then 23 passed 0 failed after the later fixes landed. Build `cmake --build build --target zene
ClapHostTest` → EXIT=0.

### Cluster 1894 — Preparing can race with the live MIDI producer while resetting the queue

Verdict: SKIPPED-SCOPE

Evidence: the observation is technically correct — `noteQueue.reset()`
(`plugins/ClapEffect/ClapNoteQueue.h:202-211`) rewrites every cell's sequence number and zeroes both
indices, and the queue is documented as multi-producer (`ClapNoteQueue.h:108-121`, the producers
being the audio thread and the MIDI/GUI thread for live input), while `prepare()`'s comment at
`ClapHostLifecycle.cpp:78-80` accounts only for the audio *consumer* being stopped. But there is no
quiesce protocol in the host (`rg 'quiesc|generation|inReset'` over `plugins/ClapEffect/` finds
nothing), and adding one means either a lock or a generation counter on the push path, which is the
audio thread — rule 6 (no locking on audio-thread paths) forbids the former, and the latter is a
redesign of the ring's protocol across two modules. Left alone deliberately.

Commit: none

Test: none

Red/green: none

### Cluster 5545 — Note-on velocity is quantized to 7 bits

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostNotes.cpp:83` multiplied the public `double` velocity by 127
into the `std::int16_t` field of `NoteEventIn`, and `toClapEvent()` divided it by 127 again on the
way out — so `setNoteOn(..., 0.5, ...)` delivered 63/127 (the instrument fixture recorded 496 millis,
not 500). `NoteEventIn::velocity` is now a `double` and the value is carried unrounded, still clamped
to CLAP's 0..1.

Commit: 8474fb2b8

Test: `ClapHostTest::testANoteVelocityKeepsItsFraction` (0.5/0.25/0.1/1.0/0.0 plus the out-of-range
clamps, checked in the fixture's own millis)

Red/green: red `./build/tests/ClapHostTest testANoteVelocityKeepsItsFraction` → `Actual
(state.lastVelocityMilli): 496 / Expected (expectedMilli): 500`; green `ctest -R '^ClapHostTest$'`
→ EXIT=0 (22 passed, 0 failed), plus the three sibling CLAP suites EXIT=0. Build `cmake --build
build --target zene ClapHostTest ClapEffectIntegrationTest ClapLoaderErrorTest` → EXIT=0.

Leftovers: none — clusters 5540, 5541 and 1894 are left unchanged on purpose; the first two need a
new cross-module GUI hook in `include/PluginNativeEditor.h` (not this lane's to design), the third
needs a producer-quiesce protocol that cannot be added without locking or redesigning the audio
thread's push path.