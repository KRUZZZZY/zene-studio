BLOCKED

# cluster new-p11-64: authored patcher wiring is not saved with the chain

The finding is **REAL**. I wrote the fix and the regression test, and the test was seen RED before the
fix. I could not run the GREEN build, the red/green proof (step 4b) or the gates: the host filesystem
that holds the worktree, the build tree, `$TMPDIR` and the log folder is out of space (btrfs on
`/dev/nvme0n1p2`, fully allocated). **The product change committed here has not been compiled.** A
later session must build it, run steps 4, 4b and 5, write the proof manifest and change this verdict.

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

## The fix (committed, NOT compiled)

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

## Regression test

`tests/src/core/PatcherWiringPersistenceTest.cpp` has four cases:

1. `authoredWiringSurvivesSaveAndLoad`
2. `loadingXmlWithoutWiringRestoresDerivation`
3. `derivedChainSavesNoWiringElement` (guards the old format)
4. `malformedWiringFallsBackToDerivation`

### Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p11-64.r1/logs`.

| Step | Command (log) | EXIT | Result |
|---|---|---|---|
| RED regen | `cmake --build build --target rebuild_cache` (`00-configure.log`) | 0 | |
| RED build | `cmake --build build --target PatcherWiringPersistenceTest -j4` (`01-build-red.log`) | 0 | |
| RED test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` from build/tests (`02-test-red.log`) | 8 | Case 1 FAIL at line 177: `'loaded.patchActive()' returned FALSE`. Case 2 FAIL at line 198: `'!chain.patchActive()' returned FALSE`. Cases 3 and 4 pass. |
| GREEN build | `cmake --build build --target zene PatcherWiringPersistenceTest PatcherCommandsTest -j4` (`03-build-green.log`) | 2 | `ccache: error: failed to create temporary file ... No space left on device` |
| GREEN retry | same command (`12-build-green.log`) | 2 | `cmake_depends` stuck about 20 min in D state (wchan `wait_reserve_ticket`), then `Bus error (core dumped)` on `mocs_compilation.cpp.o`, `AudioEngine.cpp.o` and `AudioBusHandle.cpp.o` |
| GREEN retry | same command (`14-build-green-retry.log`) | 2 | Same Bus errors |
| GREEN retry | `CCACHE_INODECACHE=false` + same command (`15-build-green-noinodecache.log`) | 2 | `lmmsobjs.dir/depend] Error 2`; the shell reported `no space left on device: .../new-p11-64.r1/tmp/claude-1f99-cwd` |
| GREEN retry | `CCACHE_INODECACHE=false` + same command (`16-build-green.log`) | 2 | My env var was wrong: ccache rejected `"false"` |
| GREEN retry | `CCACHE_NOINODECACHE=true` + same command (`17-build-green.log`) | 2 | `moc_XyPad.cpp:175:1: fatal error: error writing to /home/kruzzzzy/.config/kads/zene/state/runs/new-p11-64.r1/tmp/cc0YYEYR.s: No space left on device` |

Every failure was on a file this change does not touch.

### Why BLOCKED (missing host capability)

- `btrfs filesystem usage -T` on the worktree reports `Device allocated: 949.87GiB` and
  `Device unallocated: 1.05MiB`. `df` shows 66G free, but the volume is fully allocated, so writes
  fail with ENOSPC on and off.
- Other writes failed the same way:
  - `git add -N` → `fatal: unable to write new index file`
  - `tail` → `write error: No space left on device`
  - writing this report failed once with `ENOSPC ... rename`
- `stat -f` / `df` show that the worktree, `build/`, `$TMPDIR` and the log folder are all on this
  volume.
- Fixing this needs root on the host (a btrfs balance, or freeing other lanes' data). That is
  outside this worktree, so I did not attempt it.

**Not run:** the GREEN ctest, the `ctest -R` family, step 4b (`fix.patch` revert/reapply), the
`feature_check.py` gates and `proof.json` (a BLOCKED report needs no proof manifest).

## To finish (next session, once the disk has space)

1. Run `cmake --build build --target zene PatcherWiringPersistenceTest PatcherCommandsTest -j4`,
   then `ctest -R PatcherWiringPersistenceTest` from build/tests. All four cases must pass.
2. `include/EffectChain.h` changed, so rebuild every target that includes it. Then run the family:
   `ctest -R 'Patcher|EffectChain|Reversibility|RoutingGraph|MissingDevices|RackTest|MixerRoutingBackwardCompat|StableTrackIds|ClipEffects|MixerStrip'`.
3. Run step 4b with the product files `include/PatchWiring.h src/core/PatchWiring.cpp include/EffectChain.h src/core/EffectChain.cpp src/core/EffectChainPatcher.cpp src/core/ControlReversibilityTableRouting.cpp`.
4. Run the gates, write `proof.json` and change the verdict.

## Self-check

- **Verdict evidence:** the REAL decision rests on the code citations above. BLOCKED rests on the
  logged build failures and the btrfs allocation output.
- **Regression test first, seen failing:** yes. `02-test-red.log`, EXIT 8, with the two failing
  cases named above.
- **Passes with the fix:** NOT verified, because the build is blocked.
- **Audio thread:** nothing new runs there. The new code is in `saveSettings` and `loadSettings`
  only, on the control thread, and `loadSettings` already holds the model-change guard. Local TSan
  does not run here; this is reasoned from the design.
- **Old projects:** they should load exactly as before. A missing element gives the derived wiring,
  and a derived chain writes no new element. Case 3 checks the save side. Not verified by a run.
- **Control commands added:** none.

Delegation:
- Sonnet wrote `PatcherWiringPersistenceTest` and its registration, and ran the RED build and test
  (logs 00-02).
- Haiku ran the first GREEN build (log 03). It hit ENOSPC and a stalled `cmake_depends` and could
  not continue.
- I ran the later build retries myself as background commands, reading only exit codes and log
  tails.
- DeepSeek was unavailable this session.

## Other findings

- `EffectChain::clear()` (`src/core/EffectChain.cpp:541-577`) keeps `m_patch`. A chain that is
  cleared and then refilled through `appendEffect` re-applies the old authored wiring, or drops it.
  `loadSettings` no longer depends on this, but other `clear()` callers might. I left it unchanged.
