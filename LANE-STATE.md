# LANE-STATE — 030/vca-editgroups (feature row #4, phase-locked multitrack edit groups)

* **Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wvca`
* **Branch:** `030/vca-editgroups` (created from `3956ef589`, the `release/0.3.0` tip; never moved)
* **Tip sha:** _recorded at the end of this file, updated at every commit_
* **Build dir:** `build/` (one worktree, one build dir; deleted at the end and its size reported)
* **Log dir:** `/home/kruzzzzy/zene-030-wvca-3956ef589/` (private per-run; nothing written to `/tmp`)
* **Not done, on purpose:** no merge, no push, no `commands_snapshot.json` regeneration
  (that is a merge-time step; the drift ctest is run and its result recorded below).

## 1. What is done

* **Engine (the edit half, on the existing entity)** — `include/VcaGroup.h`,
  `src/core/VcaGroup.cpp`, `src/core/Mixer.cpp`: `editTracks()` /
  `hasEditTrack` / `addEditTrack` / `removeEditTrack` (stable track ids, ascending)
  and `isPhaseLocked()` / `setPhaseLocked()` (default ON); persistence as the
  `locked` attribute plus one `<edittrack track="N"/>` child per set member on
  the group's own `<vcagroup>` element, read by id with `locked` defaulting to 1.
* **The `vca.*` command group, 14 ids** — `src/core/ControlCommandsVca.cpp`
  (create/remove/list/get_state/rename), `ControlCommandsVcaMix.cpp`
  (set_gain/set_mute/set_solo/assign/unassign), `ControlCommandsVcaEditSet.cpp`
  (set_phase_lock/track_add/track_remove) and `ControlCommandsVcaEdit.cpp`
  (edit_move — the phase-locked move itself), with the shared resolver, the
  `vca-<n>` id formatter and the phase-lock helpers in
  `ControlCommandsVcaShared.h`. Registered by `registerVcaCommands` in
  `src/core/ControlRegistry.cpp`; declared in `include/ControlRegistryGroups.h`.
  Four translation units, not three: `ControlCommandsVcaEdit.cpp` reached 521
  lines once the move and the edit set were written together and Gate 7 refused
  it on the first measured run, so the file was split along the seam that
  already existed (a MOVE — every handler is byte-identical).
* **A16 rows** — 12 `true_inverse` rows in `src/core/ControlReversibilityTableVca.cpp`
  (joined into `reversibilityRowTable()`; declaration in
  `include/ControlReversibility.h`) and the 2 `not_mutating` rows in
  `src/core/ControlReversibilityTablePassive.cpp`.
* **Proof** — `tests/src/core/ControlVcaCommandsTest.cpp` (registered QTest,
  `LMMS_TESTS` + `QT_QPA_PLATFORM=offscreen`) and `tests/control-vca-commands.py`
  (registered ctest `ControlVcaCommands` over a live `--control-socket`).
* **Docs** — `docs/VCA-EDIT-GROUPS.md` (the lane's report: naming decision,
  correspondence rule, per-row A16 argument, limits), the `vca.*` feature section
  and the updated A16 histogram in `docs/RELEASE-NOTES-v0.3.0-alpha.md`, and the
  updated (never deleted) absence line in `docs/KNOWN-LIMITATIONS.md`.
* **Manifests** — the 5 new `src/core` files in both `tests/fork-sources.txt`
  and `tests/all-sources.txt`; the new test in `tests/all-sources.txt` only
  (the same place the other recent fork tests live, e.g. `VcaGroupTest.cpp`), and
  `tests/control-vca-commands.py` added to all 8 python pathspec lines of
  `tests/fork-sources.txt`. `bash tests/fork-sources-gate.sh` → exit 0, and
  the `all-sources.txt` REPRODUCES diff → exit 0, empty.
* **Histogram** — `tests/src/core/ReversibilityContractTest.cpp` constant moved
  from `183/99/14/4/66` to `197/111/14/4/68` (telemetry-off/wasm-off base) and
  the release-notes figure from `185` to `199` rows (`111/14/4/70`), which is
  +14 rows / +12 `true_inverse` / +2 `not_mutating`.

## 2. What is red, with the exact command and exit code

Only ONE thing is red, and it is the collected red this lane was told to expect:

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ControlCommandsSnapshot --output-on-failure
# 1/1 Test #143: ControlCommandsSnapshot ..........***Failed    2.88 sec
# FAIL: the committed snapshot and the built binary do not agree (1 finding(s),
#       14 drifted command id(s)).
# exit 8
```

The 14 drifted ids ARE this group (`vca.create` … `vca.edit_move`). The committed
offline snapshot is regenerated **once, at merge time, from a live instance of
the merge tip** — this lane is explicitly forbidden to regenerate or hand-edit
`tools/mcp-zene-control/zene_control/commands_snapshot.json`, so this red is
expected and is collected, not fixed. It is the only red in the whole suite:

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -j2
# ctest totals: 99% tests passed, 1 tests failed out of 143
# The following tests FAILED: 143 - ControlCommandsSnapshot
# exit 8
```

`bash tools/local-ci.sh --build-dir build --jobs 2` therefore reports
`build EXIT=0` and `local-ci: overall exit=1` (its ctest step is the run above).

## 3. The next exact command

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wvca/build/tests
QT_QPA_PLATFORM=offscreen ctest -R 'ControlVcaCommands|ControlVcaEditGroups' --output-on-failure
```

then, for the merge itself (the parent's step, not this lane's):
regenerate `commands_snapshot.json` from a live instance of the merge tip and
re-run `ControlCommandsSnapshot`.

## 4. The remaining acceptance list (unpiped exit codes, logged in the run dir)

| # | command | result |
|---|---|---|
| 1 | `bash tools/local-ci.sh --build-dir build --jobs 2` | configure **EXIT 0**, build **EXIT 0**, ctest **99% / 1 failed** (snapshot drift only) → overall exit 1 |
| 2 | `ctest -R 'ControlVcaCommands\|ControlVcaEditGroups\|VcaGroupTest\|ReversibilityContract\|ReversibilityUndo\|ControlRegistryTest'` | **EXIT 0** — all six PASS (the transcript among them, 48 checks) |
| 3 | `ctest -R ControlCommandsSnapshot` | **EXIT 8** — 14 drifted ids, expected while unmerged (§2) |
| 4 | `bash tests/run-all-gates.sh` | **EXIT 1** — gates 3/4/5/6/7/8/9/10/11 **PASS**, gate 2 (coverage) SKIP (not run without `--with-coverage`), and gate 1 (ctest) FAIL on the ONE expected red: `99% tests passed, 1 tests failed out of 143` → `143 - ControlCommandsSnapshot`. So the exit 1 is entirely the merge-time snapshot drift of §2 and nothing else; it is NOT pre-existing (it is this group's 14 ids) and it is not fixable in this lane by design |
| 5 | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **EXIT 0** — "RESULT: PASS — all 6 documented feature(s) match this build on linux" |
| 6 | `bash tests/complexity-gate.sh --check` | **EXIT 0** — PASS (after the `check_ids` split; CCN 12 → under target) |
| 7 | `bash tests/file-length-gate.sh --check` | **EXIT 0** — PASS (after the `ControlCommandsVcaEdit.cpp` and test-file splits) |
| 8 | `bash tests/duplication-gate.sh` | **EXIT 0** — PASS, duplicated lines 2.00% (budget 5%) |
| 9 | `bash tests/fork-sources-gate.sh` | **EXIT 0** — PASS (398 fork-NEW / 1060 inherited / 34 tooling; 0 stale) |
| 10 | `bash tests/no-upstream-regression-gate.sh` | **EXIT 0** — PASS (409 changed paths declared) |
| 11 | `bash tests/unregistered-tests-gate.sh` | **EXIT 0** — PASS (127 sources: 125 registered, 2 declared-not-built) |
| 12 | `bash tests/evidence-gate.sh` | **EXIT 0** — PASS (6264 files scanned, 0 refused) |
| 13 | `diff <(grep -vE '^[[:space:]]*(#\|$)' tests/all-sources.txt) <(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \| grep -vE '^(src/3rdparty/\|tests/reference/\|plugins/NeuralAmp/(rtneural\|nam\|tests)/\|plugins/RnnoiseDenoiser/rnnoise/)' \| LC_ALL=C sort)` | **EXIT 0**, empty — REPRODUCES |
| 14 | `tests/fork-sources.txt`'s own "Verify it" command | **REPRODUCES** (extracted from the file and run by `fork_verify.py`) |

## 5. The exact replacement text for row 4 of `docs/FEATURE-LIST-0.3.0.md`

**DO NOT EDIT THAT FILE FROM THIS LANE** — the `030/audit` lane owns it this
fire. The parent applies the row below once these ids are merged into the tip.
Supporting evidence per part is in §1 and §6.

Current text at `030/audit` (`docs/FEATURE-LIST-0.3.0.md:79`):

```
| 4 | Phase-locked multitrack edit groups | none yet | **partial** — the group *entity* landed (`include/VcaGroup.h`, `Mixer::createVcaGroup`, `VcaGroupTest`) but there is no `vca.*` group to drive it and the edit-group half is to build; a group can only be created by editing the project file | ladder row for OWNER-31 item 11; audit Table B #4 |
```

Replacement:

```
| 4 | Phase-locked multitrack edit groups | `vca.*`, 14 ids: `vca.create`, `vca.remove`, `vca.list`, `vca.get_state`, `vca.rename`, `vca.set_gain`, `vca.set_mute`, `vca.set_solo`, `vca.assign`, `vca.unassign`, `vca.set_phase_lock`, `vca.track_add`, `vca.track_remove`, `vca.edit_move` | **in the tree** — the group entity (`include/VcaGroup.h`, `Mixer::createVcaGroup`, `VcaGroupTest`, task #622) is now driven by a registered group that also carries the EDIT half the row owed: an edit set of tracks by stable `trk-<n>` id and a phase lock (ON by default) under which `vca.edit_move` moves a named clip and every other member's clips that overlap its pre-command span by the SAME delta, so a multitrack take slides as one object and stays sample-aligned (one `control.undo` returns every moved clip). Proof `ControlVcaCommandsTest` + the registered ctest `ControlVcaCommands` (`tests/control-vca-commands.py`), which proves the edit set through a real save/open round trip; the entity half is exercised on a scratch `Mixer` in the same test (`tests/src/core/VcaGroupTest.cpp` is grandfathered at 1022 lines with `FILE_LINE_TOLERANCE=0`, so extending it would regress Gate 7 in the whole-tree scope). Stated limits: **one** media edit is propagated (a clip move — trim, slip, split and fades are not); a track deleted while it is in an edit set stays in it and is reported as `missing_tracks`/`skipped_tracks` until `vca.track_remove`; a clip id is index-derived, so `vca.edit_move`'s inverse is the checkpoint and not a replayed `clip-<n>`; `vca.set_solo`'s undo does not restore the transient `MixerChannel::m_muteBeforeSolo`. UI-absent: no strip, no group menu, no member list, no lock toggle, no Lua binding — the socket and the MCP bridge are the only way in | ladder row for OWNER-31 item 11; audit Table B #4; `docs/VCA-EDIT-GROUPS.md` |
```

## 6. Files added and modified

**Added (11)**

| file | what |
|---|---|
| `src/core/ControlCommandsVca.cpp` | create/remove/list/get_state/rename + the registration point |
| `src/core/ControlCommandsVcaMix.cpp` | set_gain/set_mute/set_solo/assign/unassign |
| `src/core/ControlCommandsVcaEdit.cpp` | edit_move — the phase-locked move |
| `src/core/ControlCommandsVcaEditSet.cpp` | set_phase_lock/track_add/track_remove — the edit set and the lock switch |
| `src/core/ControlCommandsVcaShared.h` | the `vca-<n>` rule, `resolveGroup`, `groupState`, the lock's correspondence helpers |
| `src/core/ControlReversibilityTableVca.cpp` | the group's 12 `true_inverse` rows |
| `tests/src/core/ControlVcaCommandsTest.cpp` | the registered surface + entity proof |
| `tests/control-vca-commands.py` | the socket transcript (ctest `ControlVcaCommands`) |
| `docs/VCA-EDIT-GROUPS.md` | the lane's report |
| `LANE-STATE.md` | this file |

**Modified (14)**

`include/VcaGroup.h`, `src/core/VcaGroup.cpp`, `src/core/Mixer.cpp`,
`include/ControlRegistryGroups.h`, `src/core/ControlRegistry.cpp`,
`include/ControlReversibility.h`, `src/core/ControlReversibilityTable.cpp`,
`src/core/ControlReversibilityTablePassive.cpp`, `src/core/CMakeLists.txt`,
`tests/CMakeLists.txt`, `tests/src/core/ReversibilityContractTest.cpp`,
`tests/fork-sources.txt`, `tests/all-sources.txt`,
`docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`.

**Not touched (deliberately):** `tests/src/core/VcaGroupTest.cpp` (§5),
`tools/mcp-zene-control/zene_control/commands_snapshot.json`,
`docs/FEATURE-LIST-0.3.0.md`.

## 7. Limits lines written (verbatim, for the parent's cross-check)

`docs/KNOWN-LIMITATIONS.md` — the bullet is still headed "**No VCA groups in the
interface.**" and now reads "... and since 2026-09-14 the **whole group is
drivable through `--control-socket`, which is still the only way to reach one:
nothing in the interface creates a group, names one, assigns a member, locks it
or edits through it.**" followed by the `vca.*` id list, the edit-half
description and the three limits (one edit propagated; a deleted track's id
stays and is reported missing; `m_muteBeforeSolo` not restored), and ends
"There is still no Lua binding for any of it, and a group's audibility is proved
by `VcaGroupTest`'s rendered dB delta, not by the socket transcript."

`docs/RELEASE-NOTES-v0.3.0-alpha.md` — the new section "Phase-locked multitrack
edit groups (`vca.*`, OWNER-31 item 11) — added 2026-09-14" with a "**UI absence
— one line:**" bullet and a "**Stated limits**" bullet carrying the same four
limits as `docs/VCA-EDIT-GROUPS.md` §4.
