FIXED

# cluster new-p16-133: soloing a VCA member ignored the group's solo link and suspension

## Verdict

**REAL.** `Mixer::toggledSolo()` expanded a soloed VCA member to its whole group with no check of the group's solo link or suspension. `applyGroupSolo` already applied that check, so the two paths disagreed. A group with its Solo link off, or suspended, was still soloing its siblings when a member was soloed. The fix adds the same condition to the member path (`src/core/Mixer.cpp:1192-1195`). The change is 3 insertions and 1 deletion in `src/core/Mixer.cpp`, plus a regression test in `tests/src/core/VcaGroupTest.cpp`.

## Evidence (file:line)

- **Old unconditional expansion:** the base commit had `if (group != nullptr)` at the call site in `toggledSolo()`. The diff replaces it with `if (group != nullptr && group->linksSolo() && !group->isSuspended())` at `src/core/Mixer.cpp:1195`. The comment at `:1192-1193` states the contract.
- **Existing guard in the sibling path:** `applyGroupSolo` returns early when `!group->linksSolo() || group->isSuspended()` at `src/core/Mixer.cpp:1351`.
- **Links and suspension contract:**
  - `include/VcaGroup.h:158-160` defines `linksSolo()`, `isSuspended()` and `setLinks(...)`.
  - `src/gui/VcaStripView.cpp:117-118` shows the "Solo" link ("This group's Solo solos its members") and "Suspend All" ("Turn the level, mute and solo links off for now").
  - `src/core/ControlCommandsVcaMix.cpp:182` uses the same condition (`group->linksSolo() && !group->isSuspended()`) to choose the active solo group.
- **Additive solo is not affected:** `src/core/Mixer.cpp:1166-1167` returns from `toggledSolo()` through `applyAdditiveSolo()` before the expansion at `:1194`.
- **Bounded loop:** the expansion iterates `group->members()` (`src/core/Mixer.cpp:1197`). The loop length is the membership count, so there is no queue or exhaustion dimension.
- **Diff scope:** `git diff --stat` shows 3 files changed, 83 insertions and 1 deletion. This is this fix's working-tree diff (`src/core/Mixer.cpp`, `tests/src/core/VcaGroupTest.cpp`, `tests/upstream-modifications.txt`). It is not the branch base..HEAD diff that `submit_check` sees, which covers 188 changed files (`feature-check-1.log` header). The one deletion is the old `if (group != nullptr)` line.

## Test matrix (`tests/src/core/VcaGroupTest.cpp:704-780`, `soloingAMemberRespectsTheGroupsSoloLinkAndSuspension`)

| Case | What it checks | Result (log) |
|---|---|---|
| Normal link-on solo (S1, `soloingAMemberSoloesItsGroup`) | Soloing member 4 makes 2, 4, 6 audible and the non-member silent | PASS in `test-3.log`, `test-4v.log` (`VCA_EVIDENCE solo_member soloed=4 group_audible=2,4,6 non_member_silent=1`) |
| Link off (phase A) | Member 4 soloed, siblings 2 and 6 stay muted, render equals member-4-only render | FAIL before the fix (`test-3.log:25`), PASS after (`test-4v.log`) |
| Suspension (phase B) | Link on but suspended: same assertions as phase A | Covered by the same test; passes in `test-4v.log` |
| Control (unsuspended, link on) | Soloing member 4 solos the whole group | Covered by the same test; passes in `test-4v.log` |
| Restore / reconfiguration | `clearingSoloRestoresMutesAfterTheSoloLinkIsTurnedOff`, `soloingTheGroupMakesExactlyItsMembersAudible` | PASS in `test-3.log`, `test-4v.log` |
| Restore inside the new test | The new slot's own restore loops (`VcaGroupTest.cpp:757-760` and `:773`) check channels return to unmuted after the solo clears | Ran in the green 20 passed/0 failed run (`test-4v.log`), not only in the older restore tests |
| No exhaustion or queue dimension | Loop is bounded by the membership list (`Mixer.cpp:1197`) | Not a test dimension |
| Control thread only | `toggledSolo()` is the solo-change handler; the diff adds only a boolean condition, with no allocation, lock, undo or command call | Verified by reading the diff |
| Platform-neutral | The new assertions use QVERIFY/QCOMPARE and the Mixer API (grep of the range). Not checked on other platforms here; the run was linux only | Partly verified (linux run only) |

## Commands and exit codes

Commands were run with output redirected to the named logs. The reversed ctest was EXIT=8 and the restored ctest was EXIT=0, with both builds EXIT=0. The p1 and p2 codes are handoff-recorded. The logs contain the `Totals:` lines, not an `EXIT=` line, so the Totals lines are quoted. The exact command strings for the p1 and p2 build and ctest calls are listed in the p1 and p2 bullets below, labelled as the p1/p2 handoff commands; their logs (`build-1.log`, `test-1.log`, `build-2.log`, `test-2.log`) contain the redirected output, not the command line.

- **p1 (handoff-recorded):** build `EXIT=0`; ctest of `VcaGroupTest` `EXIT=8` (`test-1.log`). `Totals: 19 passed, 1 failed`, same FAIL line as below, `264 - VcaGroupTest (Failed)`.
  - p1 handoff commands (logs contain redirected output, not the command line): `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && cmake --build build --target VcaGroupTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/build-1.log 2>&1; echo EXIT=$?` (EXIT=0) and `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133/build/tests && ctest -R '^VcaGroupTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/test-1.log 2>&1; echo EXIT=$?` (EXIT=8).
- **p2 (handoff-recorded):** build `EXIT=0`; ctest of six tests `EXIT=0` (`test-2.log`, `100% tests passed out of 6`).
  - p2 handoff commands (logs contain redirected output, not the command line): `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && cmake --build build --target zene VcaGroupTest VcaRefreshAtomicityTest MixerStripTest MixerStripFeaturesTest MixerRoutingBackwardCompatTest MixingRoutingTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/build-2.log 2>&1; echo EXIT=$?` (EXIT=0) and `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133/build/tests && ctest -R '^(VcaGroupTest|VcaRefreshAtomicityTest|MixerStripTest|MixerStripFeaturesTest|MixerRoutingBackwardCompatTest|MixingRoutingTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/test-2.log 2>&1; echo EXIT=$?` (EXIT=0).
- **p3 (command strings from the p3 packet; logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs`):**
  - `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && git diff -- src/core/Mixer.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/fix.patch; echo REVERSE_EXIT=$?`: `REVERSE_EXIT=0`.
  - Reversed build: `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && cmake --build build --target zene VcaGroupTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/build-3.log 2>&1; echo EXIT=$?`: `EXIT=0` (`build-3.log`, no errors, links `VcaGroupTest`).
  - Reversed ctest: `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133/build/tests && ctest -R '^VcaGroupTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/test-3.log 2>&1; echo EXIT=$?`: `EXIT=8` (`test-3.log`).
  - `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/fix.patch; echo APPLY_EXIT=$?`: `APPLY_EXIT=0`.
  - Restored build: `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && cmake --build build --target zene VcaGroupTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/build-4.log 2>&1; echo EXIT=$?`: `EXIT=0` (`build-4.log`).
  - Restored ctest / verbose run (`test-4v.log`, `test-4.log`): `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133/build/tests && ctest -R '^VcaGroupTest$' -V > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/test-4v.log 2>&1; echo EXIT=$?` gives `EXIT=0`, `264: Totals: 20 passed, 0 failed`.
- **RED (p3):** `test-3.log`, ctest of `VcaGroupTest`. Recorded EXIT=8. The log shows `Totals: 19 passed, 1 failed`, with `FAIL! : VcaGroupTest::soloingAMemberRespectsTheGroupsSoloLinkAndSuspension() 'mixer->mixerChannel(2)->m_muteModel.value() == true' returned FALSE. (member 2 is audible although the group's solo link is off)`. ctest reports `264 - VcaGroupTest (Failed)`.
- **GREEN (p3, verbose):** `test-4v.log`. Recorded EXIT=0. The log shows `264: Totals: 20 passed, 0 failed`, `100% tests passed out of 1`.
- **GREEN (ctest, not verbose):** `test-4.log`. Recorded EXIT=0. Same run as `test-4v.log`. ctest hides the output of passing slots, so the verbose log is the one that shows the per-test PASS lines.
- **Family and related tests (p2):** `test-2.log`, ctest of 6 tests, `100% tests passed out of 6`: MixerRoutingBackwardCompatTest (#174), VcaGroupTest (#264), MixingRoutingTest (#273), MixerStripTest (#274), VcaRefreshAtomicityTest (#286), MixerStripFeaturesTest (#492).
- **Direct run:** `vca-direct.log` shows `Totals: 20 passed, 0 failed` for VcaGroupTest.
- **Report-correction checks (p4b):** `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 && git diff --check; echo EXIT=$?` gave `EXIT=0`. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 new-p16-133 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/submit-check-p4b.log 2>&1; echo EXIT=$?` gave `EXIT=1`. The only FAIL item is `working tree clean - M src/core/Mixer.cpp`, which is expected while the fix is uncommitted.
- **Feature gates (this session):** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-133 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-133.r1/logs/feature-check-1.log 2>&1; echo EXIT=$?` gave `EXIT=0`. The table is `RESULT: PASS`, with all 8 gates PASS.

## Self-check

**1. The regression test fails with the product change reverted and passes with it (exit codes).**
RED: `test-3.log` shows `Totals: 19 passed, 1 failed` and the member-2 FAIL, recorded EXIT=8. GREEN: `test-4v.log` shows `Totals: 20 passed, 0 failed`, recorded EXIT=0. The RED run is the p3 log. I did not re-run the revert in this session, so the RED exit code is recorded, not re-observed.

**2. The test is deterministic.**
- The signal is a fixed integer-arithmetic signal in `[-0.5, 0.5)` (`VcaGroupTest.cpp:102`).
- The periods and members are constants: `kPeriods = 16` (`:89`) and `kMembers = {2, 4, 6}` (`:94`).
- `renderFeed` (`:165`) renders by model writes only, with no sleeps or clocks.
- The range 704-780 contains no `sleep`, `chrono`, `QElapsed`, `QTime` or `rand`.
- The bit-exact comparisons are `QCOMPARE` on the rendered byte arrays (`VcaGroupTest.cpp:734` and `:755` are the byte-equality checks), so there is no tolerance or race. Line `:719` is a `QVERIFY2` peak check (`peakOf(renderFeed(...)) > 1.0e-6f`) that confirms the baseline signal is non-vacuous; it is not a byte comparison.

**3. The fix is at the cause and complete (every caller and sibling checked).**
- Cause: the unguarded expansion in `toggledSolo()` (`Mixer.cpp:1194-1195`).
- Sibling `applyGroupSolo` already checks the same condition (`Mixer.cpp:1351`).
- The UI and control paths use the same condition (`VcaStripView.cpp:117-118`, `ControlCommandsVcaMix.cpp:182`).
- Additive solo returns before the expansion (`Mixer.cpp:1166-1167`), so it cannot expand a group.
- Multiple membership is supported: `VcaGroup::addMember(channel, alongsideOthers=true)` lets a channel join another group too (`include/VcaGroup.h:114-115`; `ControlCommandsVcaMix.cpp:415-416`, the `also` argument). `refreshGroups` multiplies the gains of the groups a channel sits in (`src/core/Mixer.cpp:1315`).
- `vcaGroupForChannel` (`Mixer.cpp:1257`) still returns only the first group containing the channel, so member-solo expansion follows only that group. This is a pre-existing limitation, unchanged by this fix and out of scope; it is listed under Other findings.
- Sibling commands: `submit_check`'s `app.*` and `arrangement.*` list comes from command files changed earlier on this branch, between its base and HEAD (the feature-check header shows 188 changed files). It does not come from this fix. No command file is in this fix's diff, and none of those commands enters `Mixer::toggledSolo`. The only path into it is the channel solo `BoolModel` connection at `src/core/Mixer.cpp:1123`.

**4. No collateral damage.**
- Serialization is unchanged: the only product hunk is in `Mixer::toggledSolo`. `Mixer::saveSettings`, `Mixer::loadSettings` and the `<vcagroup>` format are unchanged, so saved data round-trips as before. (`src/core/Mixer.cpp` contains those functions, but the diff does not touch them.)
- Legacy and save/load tests are green: `legacyProjectLoadsAndRendersUnchanged` and `groupingSurvivesSaveAndReload` PASS in `test-3.log` and `test-4v.log`. The save/load evidence is `state_identical=1 audio_identical=1`.
- The six family tests are green in `test-2.log` (`100% tests passed out of 6`).
- The guard only narrows expansion for groups whose solo link is off or that are suspended. For linked, live groups the behaviour is unchanged; the control phase of the test passes in `test-4v.log`.

**5. Verdict ALREADY-FIXED or FALSE-POSITIVE.**
Not applicable. The verdict is REAL.

**6. Audio thread: no allocation, lock, undo or command change.**
The diff adds only a boolean condition to a control-thread handler. It adds no allocation, lock, undo entry or control command. `theVcaMultiplyOnTheAudioPathAllocatesNothing` PASSES in `test-1.log` and `test-3.log`.

## Other findings

- **Pre-existing additive-solo group-expansion gap (not fixed here):** additive solo returns through `applyAdditiveSolo()` (`src/core/Mixer.cpp:1166-1167`) and never expands a soloed member to its group. This is a separate gap and is out of scope for this packet.
- **Multiple-membership single-group solo limitation (not fixed here):** a channel can belong to several groups (`VcaGroup::addMember(channel, alongsideOthers=true)`, `include/VcaGroup.h:114-115`), but `vcaGroupForChannel` (`src/core/Mixer.cpp:1257`) returns only the first group that contains the channel. Member-solo expansion therefore follows only that group. This predates the fix and is unchanged by it.
