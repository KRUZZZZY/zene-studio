# Space Bunny — fixer lane `cmds2` — batch 02 (2026-10-05)

Clusters 5728, 4858, 4871, 4867, 4866, 4865 on branch `bugs/bunny-cmds2-2026-10-05`.

### Cluster 5728 — `patcher.set_wiring` can report `changed: false` after changing wiring state

Verdict: FIXED
Evidence: `src/core/ControlCommandsPatcherEdit.cpp:381` compared only
`before.equals(effectiveWiring(chain))`. `EffectChain::patchActive()` is `!m_patch.isEmpty()`
(`include/EffectChain.h:134`) and `effectiveWiring` (`src/core/ControlCommandsPatcher.cpp:268`)
returns derived linear wiring while the patch is inactive, so authoring the chain's own derived
edges flipped the reported `wiring` to `"authored"` while the handler answered `changed:false`.
The comparison now also accounts for the authored-versus-derived state.
Commit: c99256897
Test: PatcherCommandsTest::switchingToAnIdenticalAuthoredWiringReportsChanged
Red/green:
- RED: `cmake --build build --target zene PatcherCommandsTest > build/bunny-b.log 2>&1` EXIT=0;
  `cd build/tests && ctest -R '^PatcherCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
  (FAIL on the `changed` toBool assertion).
- GREEN: same build EXIT=0; same ctest EXIT=0 (12 passed).

### Cluster 4858 — `patcher.set_wiring` is not registered with undo

Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsPatcherEdit.cpp:399` records the change as an A16 snapshot
transaction (`inverse.applies = "command"`, `reversible:true`), and
`src/core/ControlCommandsControl.cpp:330` `undoByCommand` dispatches exactly that inverse, with
line 412 invoking it for the last command. `EffectChain` is a Model/SerializingObject (see the
comment at `src/core/ControlCommandsPatcherEdit.cpp:394`), not a JournallingObject, so the A16
snapshot record IS the undo mechanism for it — the description at line 440 and the transaction
therefore describe the same action. The existing case
`tests/src/core/PatcherCommandsTest.cpp:325` `controlUndoRestoresTheWiring` passes in the full
suite (running that single case in isolation fails on fixture ordering, because it depends on
`editingTheWiringChangesTheRender` having applied authored wiring first — a fixture artefact, not
a product defect).
Commit: none
Test: PatcherCommandsTest::controlUndoRestoresTheWiring (pre-existing, green in full suite)
Red/green: none.

### Cluster 4871 — PDC reports clamping from total latency instead of delay-line requests

Verdict: FIXED
Evidence: `src/core/ControlCommandsPdc.cpp:164` set `delay_line_clamped = total >= LatencyCompensation::MaxFrames`,
but `Mixer::updateLatencyCompensation` (`src/core/Mixer.cpp:1925`) computes a per-edge `clamped`
verdict and stores the total as `(clamped ? min(alignment, cap) : alignment) + masterChain`, so an
aligned graph can exceed MaxFrames purely through the master chain. The handler now reads the
mixer's own real verdict via the new `Mixer::latencyCompensationClamped()`; the result schema is
untouched (`booleanProperty`), so the ControlCommandsSnapshot canary is unaffected.
Commit: f372bfa2d
Test: PdcMixerTest::theClampFlagFollowsTheCompensationDelaysNotTheTotalLatency
Red/green:
- RED: `cmake --build build --target zene PdcMixerTest > build/bunny-b.log 2>&1` EXIT=0;
  `cd build/tests && ctest -R '^PdcMixerTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
  (failure reported a total of 32768 frames with nothing clamped).
- GREEN: same build EXIT=0; same ctest EXIT=0 (100% passed).

### Cluster 4867 — Instrument replacement has no undo action and its suggested inverse cannot load

Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsPlugin.cpp:236` builds the inverse args as `{target, note}` with no
`device`, while the `plugin.load` schema in the same file requires `{target, device}`, so replaying the
advertised inverse fails argument validation. The handler records `reversible:false`
(`src/core/ControlCommandsPlugin.cpp:241`) and adds no `addStructuralUndoStep`. No product code was
changed because the defect cannot be driven headlessly: the device catalogue is empty in this build
(`plugin.list` with `loadable_only` returns nothing; 10 of 87 LV2 plugins blocked), so no instrument
replacement can be triggered, and driving it would also need a `dev-<n>` index lookup
(`include/ControlDeviceSupport.h`: `controlDeviceCatalogue`, `controlDeviceById`).
Commit: none
Test: none
Red/green: none.

### Cluster 4866 — `plugin.unload` advertises an unusable inverse

Verdict: REAL-UNTESTED
Evidence: the `plugin.unload` inverse in `src/core/ControlCommandsPlugin.cpp:376` omits `device` in both
the reversible and non-reversible branches, and describes chain and position only in the free-text `note`.
`journalEffectRemoval` (`src/core/ControlCommandsPlugin.cpp:360`) does record the real structural undo step,
but the inverse lacks `applies:"command"`. Same testability wall as 4867: with an empty device catalogue no
built-in effect can be unloaded, so the case cannot be made deterministically RED, and per the procedure no
product code was changed.
Commit: none
Test: none
Red/green: none.

### Cluster 4865 — The bypass inverse passes the enabled value as the bypass value

Verdict: FIXED
Evidence: the handler did `effect->setEnabled(!args.value("bypass").toBool())` while the recorded inverse
stored `previouslyEnabled` directly as the `bypass` argument — opposite meanings, so the inverse re-applied
the change instead of undoing it. The inverse now records `{QStringLiteral("bypass"), !previouslyEnabled}`.
Commit: 1a9476a1e
Test: ReversibilityUndoTest::theBypassInverseReplaysToTheStateBeforeTheCommand
Red/green:
- RED: `cmake --build build --target zene ReversibilityUndoTest > build/bunny-b.log 2>&1` EXIT=0;
  `cd build/tests && ctest -R '^ReversibilityUndoTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
  (FAIL: the inverse of bypassing an enabled effect asks to bypass again).
- GREEN: same build EXIT=0; same ctest EXIT=0.

Leftovers: clusters 4866 and 4867 are confirmed real but remain unfixed — both need a populated device
catalogue (a `LMMS_PLUGIN_DIR` this lane must never export) or a new purpose-built test fixture before
product code can be changed safely.