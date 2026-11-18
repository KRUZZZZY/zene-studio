# Space Bunny fixer lane `cmds2` — batch 01

Worktree `zene-bunny-cmds2`, branch `bugs/bunny-cmds2-2026-10-05`, base `6d1a1143d`.
Six Luna clusters, all in the `oop.*` (out-of-process hosting) command surface —
`src/core/ControlCommandsOutOfProcess{Edit,Support}.cpp`. Two fixed, one duplicate,
one false positive, one real-but-untestable, one skipped as a design change.

Summary: 2 FIXED, 1 DUPLICATE, 1 FALSE-POSITIVE, 1 REAL-UNTESTED, 1 SKIPPED-SCOPE.

---

### Cluster 4861 — `mode_before` misreports an exited separate-process device

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsOutOfProcessEdit.cpp:73-78` — `modeOf` maps only the exact
string `separate-process` to separate and falls through to `in-process` for anything else.
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:416-425` does return a third state,
`separate-process-exited`, which therefore falls through. `mode_before`/`mode_after`
(Edit.cpp:191-192) and the transaction fallback (Edit.cpp:201-210) inherit the wrong value.
The defect is real, but the state can only be produced by a ZynAddSubFx instance whose
remote client has exited. No deterministic headless test is achievable: no `RemoteZynAddSubFx`
binary exists in this build, so `tests/CMakeLists.txt:1121-1130` does not even define
`OUTOFPROC_ZYN_*` paths and `OutOfProcessHostClientLoopTest` QSKIPs its real-client cases.
Per the rule that untestable findings get no product change, the code is left as it is.

Commit: none

Test: none

Red/green: none

---

### Cluster 2005 — An exited separate-process mode is reported as in-process

Verdict: DUPLICATE (of cluster 4861)

Evidence: the cited location `src/core/ControlCommandsOutOfProcessSupport.cpp:72-76` is
`invokeHostingState`, the raw invokable probe — it returns whatever string the plugin reports
and is correct. The `modeOf` comparison the report describes lives at
`src/core/ControlCommandsOutOfProcessEdit.cpp:73-78`, and the report names
`separate-process-exited` as the misreported value, which is the identical defect and the
identical fix. Same finding, reported twice from two call sites.

Commit: none (covered by 4861's verdict)

Test: none

Red/green: none

---

### Cluster 4863 — `oop.set_mode` has no undo step for a saved mode change

Verdict: FALSE-POSITIVE

Evidence: `oop.set_mode` is explicitly declared `RC::Irreversible` with a stated mechanism and
fallback at `src/core/ControlReversibilityTableOutOfProcess.cpp:81-95`. Every writer in this
group reports `reversible:false`, an empty `inverse` and a `__transaction` block by design —
`transactionBlock` at `src/core/ControlCommandsOutOfProcessEdit.cpp:111-127` is shared by all
three writers and exists precisely to declare that. `oop.set_mode` additionally records
`mode_before` in its `before` block and names the manual fallback call in its mechanism string
(Edit.cpp:201-210), so the caller is told exactly how to go back. Nothing in the response
implies standard `control.undo` covers the mode, which is the harm the report describes.

Commit: none

Test: none (existing coverage: OutOfProcessHostTest::theGroupRegistersItsFiveIds asserts the
single A16 row per id)

Red/green: none

---

### Cluster 3906 — `oop.reset_crashes` reports clients as cleared when they had no crashes

Verdict: FIXED

Evidence: `handleResetCrashes` selected its `cleared` names from the AFTER record alone —
`after.crashes == 0 && after.exits > 0` — so any record with exits and zero prior crashes was
announced as having had a crash count cleared. The fix records the crash count per client
BEFORE the reset and clears a name only when that before-count was greater than zero
(`src/core/ControlCommandsOutOfProcessEdit.cpp:305-336`); the empty-client scope keeps the
same comparison in a `QHash` snapshot. A client that genuinely crashed is still reported.

Commit: 06aac795b

Test: OutOfProcessHostTest::onlyClientsWithACrashCountAreReportedCleared

Red/green:
- red: `cmake --build build --target zene OutOfProcessHostTest > build/bunny-b.log 2>&1` EXIT=0;
  `cd build/tests && ctest -R '^OutOfProcessHostTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
  — `FAIL!  onlyClientsWithACrashCountAreReportedCleared() '!cleared.contains(clean)' returned FALSE.`
- green: same two commands after the fix EXIT=0 / EXIT=0 (11 passed, 0 failed)

---

### Cluster 4860 — Crash history makes an in-process device appear exited

Verdict: SKIPPED-SCOPE

Evidence: `hostedDeviceState` at `src/core/ControlCommandsOutOfProcessSupport.cpp:220-246`
resolves in four steps, and the crash-history step is deliberately placed ABOVE the plugin's
own answer with the comment at lines 227-229 stating why: a plugin re-instantiated after a
crash reports a healthy mode and would hide the crash that emptied the slot. The report asks
for that precedence to be inverted and history reported separately — a change to the state
resolution and refusal semantics that would let a crashed client be re-hosted silently, and
which also changes what `refused-crash-loop` means. No synthetic crash record can be attached
to a real device family headlessly, so it is not testable here either. Design change, out of
scope for this lane.

Commit: none

Test: none

Red/green: none

---

### Cluster 3905 — Out-of-process state reports refusal inconsistently

Verdict: FIXED

Evidence: `hostedDeviceJson` at `src/core/ControlCommandsOutOfProcessSupport.cpp:248-280` set
`hosting.refused`/`refused_reason` from `tracker.refusedByCrashLoop` alone, while
`hosting.state` in the same object came from `hostedDeviceState`, which can resolve a family
with no client to `refused-no-client` (Support.cpp:241-244). So the existing test
`aFamilyWithNoClientIsRefusedTyped` already saw `state == "refused-no-client"` alongside
`refused == false` and an empty reason. The refusal fields are now derived from the resolved
state: a crash-loop refusal still quotes the tracker, `refused-no-client` quotes the family's
own reason. No `state` value and no result schema changed, and no consumer depended on the old
value (`countHostedDevices` in `ControlCommandsOutOfProcess.cpp:173-185` reads only `state`).

Commit: 23f17c4d4

Test: OutOfProcessHostTest::theRefusalFieldsAgreeWithTheState

Red/green:
- red: `cmake --build build --target zene OutOfProcessHostTest > build/bunny-b.log 2>&1` EXIT=0;
  `cd build/tests && ctest -R '^OutOfProcessHostTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
  — `FAIL!  theRefusalFieldsAgreeWithTheState() Compared values are not the same.`
- green: same two commands after the fix EXIT=0 / EXIT=0

---

Leftovers: cluster 4861 (and its duplicate 2005) is a real defect left unfixed — `modeOf` at
`src/core/ControlCommandsOutOfProcessEdit.cpp:73-78` still maps `separate-process-exited` to
`in-process`; the one-line fix is to treat the exited state as separate, but it needs a build
with a `RemoteZynAddSubFx` client binary to regression-test. Cluster 4860's precedence
question is left as designed.