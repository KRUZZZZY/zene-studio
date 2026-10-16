# Cluster new-p12-229 — Lua object views can dereference deleted project objects

**Verdict:** FIXED
**Difficulty (finding):** hard / needs_context
**Confidence:** high — the hazard is reachable from a script and is now closed for
every wrapper named in the finding.

Round 1 answered NEEDS-CONTEXT; Luna disputed it and was right. The finding is
real, and the fix does not need an architectural rework of `Track` / `MidiClip` /
`Instrument`: every object a Lua view names is a `Model`, hence a `QObject`, so
Qt already ships the liveness-checked handle the finding asks for.

## What changed

The six pointer members of the Lua wrappers became `QPointer`s. `QPointer` clears
itself in the target's `QObject` destructor, so a view whose target was destroyed
reports `isValid() == false` and every accessor takes the branch it already took
for a wrapper that was never bound (the existing null-wrapper sentinel / no-op
behaviour — no new contract, no new Lua-visible value).

- `include/ScriptBindings.h:150` `QPointer<AutomatableModel> m_model` (FloatModel)
- `include/ScriptBindings.h:167` `QPointer<BoolModel> m_model` (BoolModel)
- `include/ScriptBindings.h:185` `QPointer<Instrument> m_instrument` (Instrument)
- `include/ScriptBindings.h:208` `QPointer<InstrumentTrack> m_track`
- `include/ScriptBindings.h:231` `QPointer<Track> m_track`
- `include/ScriptBindings.h:273` `QPointer<MidiClip> m_clip` (PatternClip)

The pointer-taking constructors and the inline `isValid()` bodies moved to
`src/core/ScriptBindings.cpp:550-552, 580-582, 655-657, 683-685, 758-761` and
`src/core/ScriptDawBindings.cpp:563` (the `LuaFloatModel` one, beside its other
definitions): `QPointer<T>` needs `T` complete at the point of the member's
definition, and this header deliberately forward-declares those types (the old
inline bodies were compiled in every translation unit that only includes the
header, e.g. `src/core/ScriptDawEffects.cpp`). `LuaTrack::index()`,
`asInstrumentTrack()`, `volumeModel()` and `LuaPatternClip::addCheckPoint()` now
read through `.data()`, since `dynamic_cast` / `static_cast` cannot take a
`QPointer` operand. No accessor logic changed.

`LuaNote` is untouched: it holds a value copy of `Note`, so it never dangles.

This is the same hazard the DAW bindings already solved a different way —
`src/core/ScriptDawEffects.cpp:52` `resolveLiveEffect()` re-checks that the effect
is still in its chain. A view built by a pool factory has no owner to re-check
against, so a handle that clears itself is the cheaper fit there.

## Evidence (file:line)

1. **A getter flushes and then dereferences.** `src/core/ScriptBindings.cpp:673`
   `LuaTrack::name()` called `flushCommandsForRead()` and then `m_track->name()`
   with a raw pointer: `isValid()` only ever asked whether the pointer was
   non-null, so a freed track still answered "valid".
2. **The flush applies destructive work inside the run.**
   `src/core/ScriptEngine.cpp:727-746` `flushCommandsForRead()` hands the queue to
   the apply side and *waits* for it; `src/core/ScriptCommandBridge.cpp:272-297`
   (`zene.command`) is exactly such a flush, so the deletion happens on the apply
   side while the script — and the saved view — is alive on the Lua stack.
3. **Which targets a script can actually destroy.** Lua track views are views of
   the *PatternStore* container (`src/core/ScriptEngine.cpp:849-857` `trackAt()`
   reads `Engine::patternStore()->tracks()`), and `zene.command('track.add')`
   creates its track in the *Song* container
   (`src/core/ControlCommandsArrangement.cpp:75-79,178-179`) while
   `track.remove` resolves ids against the Song container only
   (`src/core/ControlEditSupport.cpp:136-158` `resolveTrack()`). So the review's
   suggested `track.remove` step cannot reach the object a `zene.patternStore()`
   view names — Luna's recipe (and my round-1 example) named a track no Lua view
   can hold. The reachable destruction paths for these views are the
   project-replacing commands, both allowed to scripts
   (`src/core/ScriptCommandBridge.cpp:69-86` refusal lists):
   `project.new` → `Song::createNewProject()` → `clearProject()`, and
   `project.open`. `src/core/Song.cpp:1524` calls
   `Engine::patternStore()->clearAllTracks()`, which is
   `src/core/TrackContainer.cpp:356-362` `while (!m_tracks.empty()) delete
   m_tracks.front();` — the track, its clips and the models it owns all die, and
   `newPatternStore()`-installed views keep pointing at them.
4. **Both regressions are written against that path**, each saving a
   `LuaInstrumentTrack`, a `LuaTrack`, a `LuaPatternClip` and the volume/mute
   models, then replacing the session from inside the same run and asserting
   every view is invalid plus the full accessor sentinel sweep.

## Instrument view

`LuaInstrument` is covered by the same `QPointer`
(`include/ScriptBindings.h:185`): its target dies with the plugin that owns it
(`InstrumentTrack::loadInstrument()` at `src/tracks/InstrumentTrack.cpp:1260`
deletes the old `Instrument` before instantiating the new one, and the plugin
destructor deletes its parameter models). It is **not** exercised by an automated
regression here: this build's test host cannot load plugin *modules* at all, so
no instrument can be instantiated — the run logs say
`"The plugin \"tripleoscillator\" wasn't found or could not be loaded! Reason:
\"Plugin not found.\""` (`test-3b-direct.log:15`). The same limit makes
`ScriptDawRemovalUndoTest` (LADSPA) and `ControlAutomationScriptTest` (instrument
with parameters) fail in this tree, independently of this change (see below).

## Evidence table

| # | Command (from `build/tests` unless stated) | EXIT | Log |
|---|--------------------------------------------|------|-----|
| 1 | `cmake --build build --target ScriptBindingsTest` (new tests, unfixed code) | 0 | `build-test-2.log` |
| 2 | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (RED) | **8** — 26 passed, 2 failed | `test-2.log` |
| 3 | `cmake --build build --target zene ScriptBindingsTest` (with fix) | 0 | `build-fix-2.log` |
| 4 | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (GREEN) | **0** | `test-3.log` |
| 5 | `./ScriptBindingsTest` (test count for record) | 0 — **28 passed, 0 failed** | `test-3b-direct.log` |
| 6 | `git diff -- include/ScriptBindings.h src/core/ScriptBindings.cpp src/core/ScriptDawBindings.cpp > …/fix.patch && git apply -R …/fix.patch` | 0 | `fix.patch` |
| 7 | `cmake --build build --target zene ScriptBindingsTest` (production reverted) | 0 | `build-red-reverted.log` |
| 8 | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (RED, fix reversed) | **8** — 26 passed, 2 failed | `test-4-reverted-RED.log` |
| 9 | `git apply …/fix.patch` | 0 | — |
| 10 | `cmake --build build --target zene ScriptBindingsTest` (restored) | 0 | `build-green-restored.log` |
| 11 | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (GREEN, restored) | **0** | `test-5-restored-GREEN.log` |
| 12 | `cmake --build build --target zene …ScriptBindingsTest` (final) | 0 | `build-final-1.log` |
| 13 | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (final) | **0** | `test-8-final-GREEN.log` |
| 14 | `bash tests/complexity-gate.sh --check` | **1** — pre-existing, list unchanged by this diff | `gate-complexity-2.log` (before: `gate-complexity-1.log`) |
| 15 | `bash tests/file-length-gate.sh --check` | **1** — pre-existing, list unchanged by this diff | `gate-filelength-2.log` (before: `gate-filelength-1.log`) |
| 16 | `bash tests/fork-sources-gate.sh` | 0 | `gate-forksources-1.log` |
| 17 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-upstream-1.log` |

Both ratchet gates were already red before this change (`gate-complexity-1.log`,
`gate-filelength-1.log`): the regressions are in other files
(`src/core/TakeLane.cpp`, `tests/src/gui/XyPadPairTest.cpp`, …) and the two
`REGRESSION:` lists are byte-identical before and after (`diff` of the two logs
reports no new entry). `src/core/ScriptBindings.cpp` measures 912 code lines
against its 930 baseline; none of the new functions is reported by the
complexity gate. No baseline or gate limit was touched.

### RED proof detail

With the fix in place the two new cases pass. With only the production hunks
reversed (`git apply -R fix.patch`, test file untouched) both fail on the
deterministic liveness assertion, not on a crash:

```
QINFO  : …savedViewsOfAReplacedProjectAreInvalidated() lua: (dead views):14: a Track view outlived the track it named
QINFO  : …savedViewsOfAnOpenedProjectAreInvalidated()   lua: (opened views):12: a Track view outlived the project it named
Totals: 26 passed, 2 failed, 0 skipped, 0 blacklisted
```

### Neighbouring suites

`ctest -R '^(ScriptBindingsTest|ScriptCommandBridgeTest|ScriptDawBindingTest|ScriptDawEffectsTest|SessionCheckpointTest|ControlEditCommandsTest|ControlRegistryTest|ScriptDawRemovalUndoTest|ScriptBudgetActionTest|ControlAutomationScriptTest)$'`
→ **EXIT 8**, 8 of 10 pass (`test-9-final-related.log`). The two failures are
missing-plugin failures in this tree, not view failures: all 13 refusals in the
log are `the LADSPA host plugin 'ladspaeffect' is not in this build`, and
`ControlAutomationScriptTest` cannot find "an instrument track with parameters in
the fresh song". `ScriptDawRemovalUndoTest` was re-run with the production hunks
reverted (`test-7-preexisting-baseline.log`, EXIT 8): the same 11 failures, so it
was already red before this change.

## Files committed

- `include/ScriptBindings.h` — `QPointer` members, out-of-line declarations, the
  comment that states the liveness contract.
- `src/core/ScriptBindings.cpp` — the moved constructors and `isValid()` bodies;
  `.data()` at the three cast sites.
- `src/core/ScriptDawBindings.cpp` — `LuaFloatModel`'s constructor definition.
- `tests/src/core/ScriptBindingsTest.cpp` — `savedViewsOfAReplacedProjectAreInvalidated`,
  `savedViewsOfAnOpenedProjectAreInvalidated`, and `ControlRegistry::setReady`
  in `initTestCase` / `cleanupTestCase` (these are the only cases in the file that
  drive a control command).
- `docs/qwen-fixes-2026-10/cluster-new-p12-229.md` — this report.

No ledger edit: all three product files are already registered as fork-NEW in
`tests/fork-sources.txt`, no file was added, and both ledger gates pass.