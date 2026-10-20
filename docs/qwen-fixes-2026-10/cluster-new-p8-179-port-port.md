FIXED

"Stopping a pending port map can open the port afterward" was real, and is fixed on this branch.
The product fix was committed in 3cff36c59 and reworked in 9f0b18c3d; this round added the two
PortMapping-level regressions that were still missing and re-proved RED/GREEN from scratch.

## Evidence (file:line, current tree)

- `src/core/PortMapping.cpp:151` `PortMapping::stop()` sets `m_stopped = true` (line 154) before it cancels the
  mappers, so a completion that belongs to the canceled attempt cannot be mistaken for a live one.
- `src/core/PortMapping.cpp:163` `onPmpFinished()` returns early when the mapping was stopped: the late answer
  neither reports a success nor starts the UPnP fallback.
- `src/core/PortMapping.cpp:171` `onUpnpFinished()` returns early for the same reason.
- `src/core/PortMappingPmp.cpp:110` `PmpMapper::unmap()` cancels a pending attempt (the retry timer stops and the
  step goes back to `Idle`, so `onReadyRead()` at line 183 drops the late answer) and expires best effort only what
  that attempt could have created.
- `src/core/PortMappingUpnp.cpp:251` `UpnpMapper::unmap()` bumps the generation counter (line 253), stops the search
  timer and clears the search, so the HTTP callbacks of the canceled attempt (lines 156, 201, 236) are ignored.
- `include/PortMapping.h:156` (`m_active`, `m_generation`) and `include/PortMapping.h:219` (`m_stopped`) hold that state.

Before the fix (base 2da2ed356): `PmpMapper::unmap()` returned at once when nothing was mapped yet
(`if (!m_result.ok) { return; }`), so the pending request kept its retry timer, the late answer completed the
mapper with a success, and `onPmpFinished()` / `onUpnpFinished()` had no check at all.

## Regression tests

- `tests/src/core/PortMappingOrchestratorTest.cpp:201` `aLateAnswerAfterStopIsIgnored()` - a mapper's completion
  after `stop()` must report nothing, must not leave the host busy and must not plan a renewal.
- `tests/src/core/PortMappingOrchestratorTest.cpp:225` `aLateRefusalAfterStopDoesNotStartTheUpnpFallback()` - a late
  refusal after `stop()` must not start the UPnP fallback (the search timer stays off, nothing reaches the search
  endpoint).
- Already committed: `tests/src/core/PortMappingCodecTest.cpp` (`pmpIgnoresLatePcpReplyAfterUnmap`,
  `stopCancelsAPendingPortMapping`, `upnpUnmapCancelsSearchTimeout`, `staleAnswerAfterStopAndRestartCannotPoisonTheNewAttempt`)
  and `tests/src/core/UpnpMapperFakeRouterTest.cpp` (`staleAddAfterStopIsTakenBack`,
  `staleAddAfterStopAndRestartKeepsTheNewerMapping`).

## Evidence table (exact commands, exit codes as run)

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| RED revert | `git apply -R .../logs/fix.patch` (product diff vs base 2da2ed356, 4 product files) | 0 | `logs/fix.patch` |
| RED build | `cmake --build build --target zene PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest CollabPolicyTest -j 12` | 0 | `logs/build-9-red.log` |
| RED run | `cd build/tests && ctest -R 'PortMapping\|UpnpMapper\|CollabPolicy' --output-on-failure` | **8** (3 of 4 targets failed, 7 failing cases) | `logs/test-8-red.log` |
| GREEN restore | `git apply .../logs/fix.patch` | 0 | - |
| GREEN build | `cmake --build build --target zene PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest CollabPolicyTest -j 12` | 0 | `logs/build-10-green.log` |
| GREEN run | `cd build/tests && ctest -R 'PortMapping\|UpnpMapper\|CollabPolicy' --output-on-failure` | **0** (4 of 4 targets, 100 % passed) | `logs/test-9-green.log` |
| wider family | `cd build/tests && ctest -R 'Collab' --output-on-failure` | **0** (4 of 4 targets, 100 % passed) | `logs/test-10-collab.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` | **0** (RESULT: PASS; complexity and file-length: "no new item in your files") | `logs/gates-2.log` |
| ratchet 1 | `bash tests/complexity-gate.sh --check` | 1 (train-wide items only, none in this cluster's files) | `logs/gate-complexity.log` |
| ratchet 2 | `bash tests/file-length-gate.sh --check` | 1 (train-wide items only, none in this cluster's files) | `logs/gate-file-length.log` |
| stability | `ctest -R '^PortMappingOrchestratorTest$'` x10 | 0 every run | `logs/flaky-summary-2.log` |

The failing cases in the RED run (`logs/test-8-red.log`):

```
FAIL!  : PortMappingCodecTest::pmpIgnoresLatePcpReplyAfterUnmap()            Actual (finished.count()): 1, Expected 0   (line 386)
FAIL!  : PortMappingCodecTest::stopCancelsAPendingPortMapping()               Actual (finished.count()): 1, Expected 0   (line 410)
FAIL!  : PortMappingCodecTest::upnpUnmapCancelsSearchTimeout()                the search timer was still active             (line 427)
FAIL!  : PortMappingOrchestratorTest::aLateAnswerAfterStopIsIgnored()         Actual (finished.size()): 1, Expected 0     (line 211)
FAIL!  : PortMappingOrchestratorTest::aLateRefusalAfterStopDoesNotStartTheUpnpFallback()  the fallback search had started (line 242)
FAIL!  : UpnpMapperFakeRouterTest::staleAddAfterStopAndRestartKeepsTheNewerMapping()   Actual (finished.count()): 2, Expected 1 (line 527)
FAIL!  : UpnpMapperFakeRouterTest::staleAddAfterStopIsTakenBack()             Actual (DeletePortMapping count): 0, Expected 1 (line 548)
```

## Files changed in this round

- `tests/src/core/PortMappingOrchestratorTest.cpp` (two new regression cases; the only file touched now)
- `docs/qwen-fixes-2026-10/cluster-new-p8-179-port-port.md` (this report)

The product fix itself is in the earlier commits of this branch: `include/PortMapping.h`,
`src/core/PortMapping.cpp`, `src/core/PortMappingPmp.cpp`, `src/core/PortMappingUpnp.cpp`.

## Round 6 re-verification (no product change)

Luna's round-5 feedback section was empty, so this round changed no product or test code. It re-proved the fix from
scratch on the current tree (logs in `runs/new-p8-179-port-port.r6/logs/`):

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| product diff | `git diff 2da2ed356 -- include/PortMapping.h src/core/PortMapping.cpp src/core/PortMappingPmp.cpp src/core/PortMappingUpnp.cpp > logs/fix.patch` | 0 | `fix.patch` |
| RED revert | `git apply -R logs/fix.patch` | 0 | - |
| RED build | `cmake --build build --target zene PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest CollabPolicyTest -j4` | 0 | `build-2-red.log` |
| RED run | `cd build/tests && ctest -R 'PortMapping\|UpnpMapper\|CollabPolicy' --output-on-failure` | **8** (same 7 cases fail) | `test-2-red.log` |
| GREEN restore | `git apply logs/fix.patch` | 0 | - |
| GREEN build | same cmake command, -j4 | 0 | `build-3-green.log` |
| GREEN run | same ctest command | **0** (4 of 4 targets, 100 % passed) | `test-3-green.log` |
| gates, by hand | `fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`, `check-namespace`, `no-tautology-gate.sh`, `doc-figures-gate.py` | 0 each | `gate-*.log` |
| ratchets, by hand | `tests/complexity-gate.sh --check` / `tests/file-length-gate.sh --check` | 1 / 1 (train-wide) | `gate-complexity.log`, `gate-filelen.log` |

The ratchet items listed are `ControlCommandsShortcuts.cpp` (`importedOverrides`, CCN 11) and
`tests/src/core/CollabTransportTest.cpp` (846 lines), among others: none is in a file this branch changed
(no `PortMapping*`/`UpnpMapper*` line in either log).

**`feature_check.py` could not run in this session.** It writes its gate logs to `<worktree>-feature-check/`, a
sibling folder outside this lane's write-jail; it died with `PermissionError: [Errno 13]` on
`.../zene-qwen-new-p8-179-port-port-feature-check/fork-sources.log` (`logs/gates-1.log`, exit 1) before running any
gate. I therefore ran the same eight gate commands by hand (table above) and compared the ratchet listings with the
files this branch changed, which is what the script does. The loop's own run of `feature_check.py` outside the jail is
the authoritative result; it passed with exit 0 in the previous round (`logs/gates-2.log` of that round).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Reverting the
  product diff vs base 2da2ed356 (`git apply -R logs/fix.patch`) and rebuilding gives ctest exit **8** with the seven
  failing cases listed above (the two new ones fail on `finished.size()` 1 != 0 and on the UPnP fallback search
  having started); restoring it (`git apply logs/fix.patch`) and rebuilding gives ctest exit **0**, 4 of 4 targets.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The two new cases inject the mappers' answer
  through the existing test seam (`emit pmp->finished(...)` / `emit upnp->finished(...)`, the seam the file already
  documents) and read the state synchronously right after the emission: the UPnP fallback is observed through the
  search timer the mapper arms (`upnp->findChild<QTimer*>()->isActive()`), not through a wait. The search endpoint is
  pointed at a loopback socket, so no packet leaves the machine. The older cases use held replies, a fake gateway and
  `QCoreApplication::processEvents()` loops instead of sleeps. 10 consecutive runs were green.
- **The fix is at the cause and complete: every caller and sibling was checked and stays consistent.** `stop()` is
  called by `PortMapping::start()` (line 117), by `~PortMapping()` (line 112) and by `CollabSession::resetToIdle()`
  (`src/core/CollabSession.cpp:163`, the only production caller, which then destroys the object); `start()` clears
  `m_stopped` again (line 120) so a fresh attempt is not ignored. `onPmpFinished()`/`onUpnpFinished()` are only
  connected in the constructor (lines 106-107). `PmpMapper::unmap()` and `UpnpMapper::unmap()` are the two cancel
  paths and both were reworked so that a stale answer is ignored at its source (the PCP/NAT-PMP step and the UPnP
  generation) as well as at the orchestrator. `renew()` can only run from the timer that `stop()` stops.
- **No collateral damage: old projects and saved data still load as before, and no new limit was added.** The change
  touches no saved data, no command, no serialization and no project format; the only behaviour that changed is that
  a completion belonging to a canceled attempt is ignored, which is what the finding asks for. The expiry that
  `PmpMapper::unmap()` sends is scoped to the stage the attempt had reached (PCP or NAT-PMP map), so it cannot delete
  a mapping the attempt never created. The UPnP fallback deletes only the entry of the attempt it belongs to.
- **Verdict ALREADY-FIXED / FALSE-POSITIVE?** Not applicable: the verdict is FIXED. The bug was real at base
  2da2ed356 (`src/core/PortMappingPmp.cpp` at the base: `PmpMapper::unmap()` returned at once when nothing was mapped
  yet, so the pending request kept retrying and the late answer completed the mapper), which the RED run above shows
  in the failing assertions.
- **Realtime, undo and commands.** Nothing in this cluster runs on the audio thread: the changed product files are
  the port-mapping orchestrator and its two mappers, all on the GUI/control thread, and no new allocation, lock, wait
  or log was added there. No control command was added or changed (the `ControlCommandsSnapshot` canary is untouched),
  and no undo step exists in this area.
