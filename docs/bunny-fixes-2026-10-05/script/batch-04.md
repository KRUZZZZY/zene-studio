# Space Bunny fixer lane `script` — batch-04 report

Branch `bugs/bunny-script-2026-10-05`. Six findings, triaged against the tree at
`dc088cb61`. One FIXED, three already fixed by earlier commits on this branch,
one FALSE-POSITIVE by documented design, one REAL-UNTESTED, one SKIPPED-SCOPE.

### Cluster 5336 — mixer.ids() returns positional IDs instead of channel IDs

Verdict: FIXED

Evidence: `src/core/ScriptDawBindings.cpp:385` (pre-fix) built the list with
`control::channelId(i)`, which names a mixer POSITION, while
`LuaMixerChannel::id()` (`ScriptDawBindings.cpp:405`) and `channelById()`
(`:356`) both use the channel OBJECT's allocated id via `control::channelIdOf`.
Measured on a live session the master is `ch-1` and the first added channel is
`ch-7`, so `ids()` listed `ch-0`/`ch-1`/… and every non-position entry resolved
to an invalid wrapper. The list is now built from
`control::channelIdOf(mixer->mixerChannel(i))` (`ScriptDawBindings.cpp:385-401`).

Commit: 51e7da51f

Test: ScriptDawBindingTest::mixerIdsListsTheChannelsOwnIdsAndEveryOneResolves

Red/green:
- Red: `cd build/tests && ctest -R '^ScriptDawBindingTest$' --output-on-failure` -> EXIT=8
  (`listed 3 unresolved 2` vs expected `listed 3 unresolved 0`)
- Green: same command -> EXIT=0
- Fallout: `ctest -R '^(ScriptStabilisationTest|ScriptBindingsTest|ControlVerbInverseTest)$'` -> EXIT=0
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

Note: `ScriptEngineTest::testInstrumentParameterReadWrite` fails in this headless
build ("dummy" vs "TripleOscillator", `ScriptEngineTest.cpp:531`) both before and
after the change — no instrument plugins are loaded here, and that file never
references `ids()`/`LuaMixer`. Pre-existing environment limit, not fallout.

### Cluster 1663 — Lua wrappers can dereference deleted engine objects

Verdict: SKIPPED-SCOPE

Evidence: the defect class is real but its proposed fix is a handle-invalidation
redesign across every wrapper. `LuaEffect` already has exactly this protection —
`resolveLiveEffect()` at `src/core/ScriptDawEffects.cpp:51-63` re-checks that the
effect is still in its chain on every access, via `LuaEffect::m_effective()`
(`:102`). `LuaMixerChannel` (`ScriptDawBindings.cpp:430` `gain()`,
`ScriptDawEdit.cpp:321` `setGain()`) and `LuaTrack` (`ScriptBindings.cpp:709`
`name()`) still hold a raw pointer and only null-check it, so the reported
scenario (remove a channel with `mixer.remove_channel`, then read the kept
wrapper) is genuinely unguarded. Fixing it means resolving every wrapper through
stable ids on each access or adding an invalidation registry — a design change
well past "change only what the defect needs", and it has no deterministic
headless failing test (it is a use-after-free, which a test can only trip by
crashing). Marked SKIPPED-SCOPE per rules 6/7 rather than half-implemented.

Commit: none

Test: none

Red/green: none

### Cluster 5339 — Long Lua names are silently truncated, sometimes mid-character

Verdict: ALREADY-FIXED

Evidence: `src/core/ScriptDawEdit.cpp:64-71` — `setCommandText()` now returns
false when the UTF-8 name does not fit whole in the fixed 256-byte payload,
leaving the text empty rather than carrying a truncation that may be a broken
UTF-8 sequence. The callers (`LuaMixerChannel::setName` `:339`, and the
track/clip rename paths) turn that false into an explicit logged refusal, not a
silent cut. Fixed earlier on this branch by `4508fc221`
("Lua renames silently truncated overlong UTF-8 names", +41/-8 in this file).

Commit: none (pre-existing, 4508fc221)

Test: none added here; coverage already added by 4508fc221 in ScriptBindingsTest.cpp

Red/green: none

### Cluster 5332 — Removing an effect is not undoable

Verdict: REAL-UNTESTED

Evidence: confirmed real. `applyRemoveEffect()` at
`src/core/ScriptDawEdit.cpp:212-226` removes the effect and schedules
`deleteLater()` with no journalled inverse, unlike its neighbour
`applyLoadEffect()` (`:209`), which records one via
`control::journalEffectLoad`. The correct fix is a one-line call to
`control::journalEffectRemoval(chain, effect, index)` before the removal — the
same helper `plugin.unload` uses (`src/core/ControlCommandsPlugin.cpp:360`,
implementation `src/core/ControlStructuralSupport.cpp:440`) — which re-instantiates
the device from its captured state document on undo. Left untouched: this
headless build's `controlDeviceCatalogue()` lists NO loadable effect module, so
the test can only SKIP; rule 2 of the procedure forbids a product change without
a deterministic failing test. The regression test is landed and ready.

Commit: none

Test: ScriptDawBindingTest::aScriptsRemovedEffectIsUndoneByItsOwnStep (SKIPs here:
"this build lists no loadable effect module"; the pre-existing
aScriptsLoadedEffectIsUndoneOnTheChannelItNames skips for the same reason)

Red/green: none — no deterministic failing test is possible in this build.

### Cluster 5333 — Renaming a mixer channel is not undoable

Verdict: FALSE-POSITIVE

Evidence: true that `applySetChannelName()` at
`src/core/ScriptDawEdit.cpp:238-244` assigns the plain `MixerChannel::m_name`
without journalling. That is a deliberate, documented decision, not an oversight:
`docs/LUA-API-STABILISATION.md:372-373` lists it under "Withheld, one line each"
— "`MixerChannel::m_name` is a plain `QString`, so no inverse exists; the binding
performs the rename (a script that creates a channel must be able to name it)" —
and the source comment at `ScriptDawEdit.cpp:241-242` says the same, claiming no
inverse rather than silently omitting one. The binding makes no reversibility
claim for a rename, so nothing reported is false to a script.

Commit: none

Test: none

Red/green: none

### Cluster 1661 — Effect enabled read-back can return the old value

Verdict: ALREADY-FIXED

Evidence: `src/core/ScriptDawEffects.cpp:141-147` — `LuaEffect::enabled()` calls
`ScriptEngine::instance()->flushCommandsForRead()` before reading
`effect->isEnabled()`, so a `setEnabled()` enqueued earlier in the same script is
already applied by the apply side. Fixed earlier on this branch by `a5de51c1b`
("stale getters after a queued write"), which added exactly this
`flushCommandsForRead()` line (+3 in this file) and the same guard to
`ScriptBindings.cpp`.

Commit: none (pre-existing, a5de51c1b)

Test: none added here; coverage added by a5de51c1b in ScriptBindingsTest.cpp

Red/green: none

Leftovers: Cluster 5332 is real and its fix is a one-line
`control::journalEffectRemoval` call at `ScriptDawEdit.cpp:223`, blocked only by
this build listing no loadable effect module — apply it in a build where
`controlDeviceCatalogue()` has a loadable entry, where the landed test
`aScriptsRemovedEffectIsUndoneByItsOwnStep` will exercise it instead of skipping.
Cluster 1663 remains genuinely unguarded for `LuaMixerChannel`/`LuaTrack`
wrappers and needs the handle-invalidation design decision before it can be
fixed. `ScriptEngineTest::testInstrumentParameterReadWrite` fails in this
headless build for an unrelated reason (no instrument plugins loaded).