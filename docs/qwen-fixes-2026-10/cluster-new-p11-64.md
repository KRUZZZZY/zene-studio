FIXED

# cluster new-p11-64: authored patcher wiring is not saved with the chain

The finding is **REAL** and is now **FIXED**. `EffectChain::saveSettings` writes an authored wiring as a
`<patchwiring>` child of `<fxchain>`, and `loadSettings` restores it (or the derived wiring when the
element is absent) before the graph rebuild that applies it.

History: an earlier session wrote the fix and the test and saw the test RED, but the host disk was
full and it could not build (commit 7c85ff576f, verdict BLOCKED). This session the disk had 145G
free (`df -h .`), and the build, the GREEN run, the step-4b red/green proof and the gates all ran.
I re-reviewed the committed diff before building it; the only change this session is the
`tests/all-sources.txt` registration line and this report.

## Evidence that the bug is real (read this session)

- `src/core/EffectChain.cpp:158-187` (`EffectChain::saveSettings`) writes `enabled`,
  `numofeffects` and one `<effect>` per device. It never writes `m_patch`, the authored wiring
  (`include/EffectChain.h:136` `patchWiring()`; `:138` `patchActive()` = `!m_patch.isEmpty()`).
- `src/core/EffectChain.cpp:192-272` (`loadSettings`) never restores `m_patch` and never resets
  it. `clear()` (`src/core/EffectChain.cpp:541-577`) resets `m_patchDropped` but not `m_patch`.
  - A reload therefore loses an authored wiring.
  - Loading XML into a chain that already has a wiring keeps the stale wiring.
- No other serializer saves the wiring. The only writers of `m_patch` are `setPatchWiring` and
  `rebuildRoutingGraph` (`src/core/EffectChainPatcher.cpp:168-174`, `:215-246`).
- This was a documented gap, not an intended contract. `docs/PATCHER-GRAPH.md` §5 ("What is NOT
  done") said: "The wiring is session state ... a patch does not survive a save/load".
  - The GUI authors wiring: `src/gui/PatcherCanvas.cpp:258` and the "Wire the Chain..." menu at
    `src/gui/shell/ShellCommandMenus.cpp:451`. So a user loses those edits when saving.
  - It is not on the brief's by-design list. An fff search of AGENTS.md for `wiring` and `patcher`
    found 0 matches.

## The fix

- **`include/PatchWiring.h`, `src/core/PatchWiring.cpp`:**
  - `PatchWiring::saveXml()` writes
    `<patchwiring output="effect:N"><edge from=".." fromport=".." to=".." toport=".."/>..</patchwiring>`.
    An empty (derived) wiring writes nothing, so a chain that is not patched saves exactly as before.
  - `PatchWiring::loadXml()` reads that element.
    - No element (every older file): the wiring is derived.
    - A malformed element: the wiring is derived and the call returns false.
- **`src/core/EffectChain.cpp`:**
  - `saveSettings` calls `m_patch.saveXml()` after the effects.
  - `loadSettings` calls `loadPatchWiring(_this)` before the `rebuildRoutingGraph()` that applies
    it. That rebuild still drops a wiring the loaded effects cannot take, and sets `patchDropped()`,
    as it did before.
  - This runs on the control thread, inside the model-change guard that is already held. The audio
    thread gets no new work.
- **`include/EffectChain.h`, `src/core/EffectChainPatcher.cpp`:** the new private
  `EffectChain::loadPatchWiring()` reports a malformed element through `Song::collectError`.
- **Docs and row text:** `src/core/ControlReversibilityTableRouting.cpp` (the reason text of the
  `patcher.set_wiring` row) and `docs/PATCHER-GRAPH.md` §5 are updated to match. The row stays
  `snapshot`, and no control command is added.
- **Registration:**
  - `tests/fork-sources.txt` lists the new test file.
  - `tests/upstream-modifications.txt` has reasons for `include/EffectChain.h` and
    `src/core/EffectChain.cpp`.
  - `tests/CMakeLists.txt` registers `PatcherWiringPersistenceTest`.
  - `tests/all-sources.txt` lists the new test file (added this session; the `fork-sources` and
    `all-sources-reproduce` gates failed without it, log `30-feature-check.log`).

## Regression test

`tests/src/core/PatcherWiringPersistenceTest.cpp` has four cases:

1. `authoredWiringSurvivesSaveAndLoad`
2. `loadingXmlWithoutWiringRestoresDerivation`
3. `derivedChainSavesNoWiringElement` (guards the old format)
4. `malformedWiringFallsBackToDerivation`

### Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p11-64.r1/logs`. The test runs were
made from `build/tests`.

Earlier session (logs 00-19): the RED run before the fix was `ctest -R PatcherWiringPersistenceTest`
(`02-test-red.log`), EXIT 8. Every GREEN build attempt failed with ENOSPC/SIGBUS (logs 03-17).

This session:

| Step | Command (log) | EXIT | Result |
|---|---|---|---|
| GREEN build | `cmake --build build --target zene PatcherWiringPersistenceTest -j4` (`20-build-green.log`) | 0 | |
| GREEN test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`21-test-green.log`) | 0 | 100% passed out of 1 |
| 4b patch | `git diff HEAD~1 HEAD -- include/EffectChain.h include/PatchWiring.h src/core/EffectChain.cpp src/core/EffectChainPatcher.cpp src/core/PatchWiring.cpp src/core/ControlReversibilityTableRouting.cpp > logs/fix.patch` | 0 | The fix was already committed, so the patch was taken from HEAD~1..HEAD (product files only, no tests). |
| 4b revert | `git apply -R logs/fix.patch` | 0 | |
| 4b RED build | `cmake --build build --target zene PatcherWiringPersistenceTest -j4` (`23-build-red.log`) | 0 | |
| 4b RED test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`24-test-red.log`) | 8 | `FAIL! authoredWiringSurvivesSaveAndLoad() 'loaded.patchActive()' returned FALSE`; `FAIL! loadingXmlWithoutWiringRestoresDerivation() '!chain.patchActive()' returned FALSE`; 4 passed, 2 failed |
| 4b reapply | `git apply logs/fix.patch` | 0 | `git status --short` was empty afterwards |
| 4b GREEN build | same build (`25-build-green2.log`) | 0 | |
| 4b GREEN test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`26-test-green2.log`) | 0 | 100% passed out of 1 |
| Family build | `cmake --build build --target zene ControlArmReversibilityTest ReversibilityContractTest ReversibilityUndoTest ReversibilityUndoDeviceTest PatcherCommandsTest PatcherWiringPersistenceTest PatcherCanvasTest -j4` (`28-build-family.log`) | 0 | |
| Family test | `ctest -R 'ControlArmReversibilityTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|PatcherCommandsTest\|PatcherWiringPersistenceTest\|PatcherCanvasTest\|ControlReversibilityTranscript\|ControlCommandsSnapshot' --output-on-failure` (`29-test-family.log`) | 0 | 100% passed out of 9; this includes the ControlCommandsSnapshot canary |
| Wider build | `cmake --build build --target <23 matching targets> -j4` (`32-build-wide.log`) | 0 | |
| Wider test | `ctest -R 'RoutingGraph\|MissingDevices\|Rack\|MixerRoutingBackwardCompat\|StableTrackIds\|ClipEffects\|MixerStrip\|EffectChain\|ProjectRoundTrip\|SaveLoad' --output-on-failure` (`33-test-wide.log`) | 0 | 100% passed out of 25 |
| Gates (1st) | `python3 .../qwen/feature_check.py <worktree>` (`30-feature-check.log`) | 1 | fork-sources and all-sources-reproduce FAIL: `tests/all-sources.txt` was missing the new test |
| Gates (final) | same command, after adding the line (`31-feature-check.log`) | 0 | every row PASS, `RESULT: PASS` |

Final gate table (`31-feature-check.log`):
```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **Verdict evidence:** REAL rests on the code citations above. Before the fix, `saveSettings`
  never wrote `m_patch` and `loadSettings` never restored or reset it. FIXED rests on logs 24
  (EXIT 8 without the fix) and 26 (EXIT 0 with it).
- **Regression test fails without the fix, passes with it:** yes. The two cases named in the 4b RED
  row fail, and all four cases pass with the fix. The test is deterministic: no timing, sleeps or
  threads. The dummy audio device is stopped in `initTestCase`.
- **Cause and completeness:** the fix is at the chain's own serializer. That serializer is the only
  `<fxchain>` writer and reader, and every chain owner (mixer channels, instrument tracks, clip
  effects) goes through it. No other writer saves the wiring. `setPatchWiring` and the
  `patcher.set_wiring` command (`src/core/ControlCommandsPatcherEdit.cpp:330-354`) always give a
  non-empty wiring an output, so `loadXml`'s required `output` attribute cannot reject a wiring
  the app saved.
- **Old projects:** an `<fxchain>` without `<patchwiring>` loads as the derived wiring, as before.
  A derived chain writes no new element (case 3, `derivedChainSavesNoWiringElement`). The
  `<effect>` loop in `loadSettings` only reads `effect` elements, so the new child does not disturb
  it. Older readers ignore the unknown element. The wider round-trip, missing-devices and
  backward-compat tests pass (25/25).
- **Audio thread:** nothing new runs there. The save and load code runs on the control thread, and
  `loadSettings` already holds the model-change guard (`src/core/EffectChain.cpp:201`) across the
  new call and the rebuild. Local TSan does not run here; this is reasoned from the design.
- **Undo and commands:** no control command was added, and the ControlCommandsSnapshot canary
  passes. The `patcher.set_wiring` row stays `snapshot`, with its inverse-command undo, and only
  its reason text changed. The Reversibility tests pass.
- **Header changed:** `include/EffectChain.h` (a private non-virtual declaration) and
  `include/PatchWiring.h`. `zene` and every test target built this session were rebuilt
  (logs 20, 25, 28, 32).
- **Proof manifest:** `logs/proof.json`, with a `regression-test` artifact. No `src/gui` file
  and no Song/Record/Undo/Transport/Take/AudioEngine file changed, so it needs no screenshot or
  state-sequence.

Delegation:
- Earlier session: Sonnet wrote `PatcherWiringPersistenceTest` and its registration. Haiku made
  the first (failed) GREEN build attempt.
- This session: Haiku #1 ran the GREEN build and test and the whole step-4b red/green proof
  (logs 20-26). Haiku #2 ran the Patcher/Reversibility/canary family and the first gate run
  (logs 27-30). Haiku #3 ran the wider save/load/routing family (logs 31-33).
- I reviewed the diff, made the one-line `tests/all-sources.txt` fix, reran the gates and wrote
  the report and manifest. Sonnet and DeepSeek were not needed this session, because the
  engineering was already written and only verification remained.

## Other findings

- `EffectChain::clear()` (`src/core/EffectChain.cpp:541-577`) keeps `m_patch`. A chain that is
  cleared and then refilled through `appendEffect` re-applies the old authored wiring, or drops it.
  `loadSettings` no longer depends on this, but other `clear()` callers might. I left it unchanged.
