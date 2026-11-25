ALREADY-FIXED

# Cluster new-p10-73: a malformed project member index can crash VCA state queries

Both halves of the reported defect are already in this branch's history, before this
lane cut its worktree, and both carry the guard the diagnostic checkpoint asked me to
backport. The checkpoint's own warning names the failure mode: "a fixer cannot commit a
fix for a bug that is already fixed on its base". I verified that from the code and
`git blame` this session, then added the one piece that was missing: the finding's own
reproduction, driven through the command surface (`vca.get_state` / `vca.list`) from a
project file, which no existing test did.

## The two guards, with their introducing commits

1. **The query-side guard** — `src/core/ControlCommandsVcaShared.h:146-148`:

   ```cpp
   for (mix_ch_t member : group->members())
   {
       // A stale position has no live channel to report.
       if (member >= Engine::mixer()->numChannels()) { continue; }
       QJsonObject entry;
       MixerChannel* channel = Engine::mixer()->mixerChannel(member);
   ```

   `git blame -L 144,152 -- src/core/ControlCommandsVcaShared.h` attributes lines 147-148
   to `45d6dac367 qa-fix(new-p21-31): guard shared VCA member state`, which is exactly the
   commit the checkpoint named ("so a later merge of 45d6dac367 is a no-op"). A backport of
   that hunk today would be an empty patch.

2. **The load-side guard** — `src/core/Mixer.cpp:3160-3205`, the `<member channel="…"/>`
   reader. It reads the attribute wide, refuses a value the file does not back up, reports
   it through `Song::collectError` (the same way an unresolvable edit track is reported in
   `groupState`'s `missing_tracks`), never calls `allocateChannelsTo()`, and requires
   `0 < channel < m_mixerChannels.size()` before `group->addMember(...)`:

   ```cpp
   const QString channelAttr = memberDom.attribute( "channel" );
   int channel = 0;
   const ProjectIndex memberKind =
       parseProjectChannelIndex( channelAttr, describedChannels, channel );
   if( memberKind != ProjectIndex::Ok ) { … reportUnsupportedIndex( … ); continue; }
   if( channel <= 0 ) { continue; }
   if( static_cast<std::size_t>( channel ) >= m_mixerChannels.size() ) { continue; }
   group->addMember( static_cast<mix_ch_t>( channel ) );
   ```

   `git log -S'parseProjectChannelIndex' -- src/core/Mixer.cpp` → `9c1e5276a7 Fix cluster
   new-p10-248-port-port: bound project mixer channel indices`. So a malformed attribute
   cannot resize the mixer, and `-1` never becomes `65535`.

## Siblings checked (rule 3)

Every other place that turns a member position into a channel is guarded the same way, so
the query fix is not one site behind a still-crashing sibling:

| site | guard |
|---|---|
| `src/core/ControlCommandsVcaShared.h:148` (`groupState`, read by `vca.get_state`, `vca.list` and every mutating vca verb) | `member >= numChannels()` → skip |
| `src/core/Mixer.cpp:1197-1200` (`toggledSolo` member expansion) | `member < m_mixerChannels.size()` |
| `src/core/Mixer.cpp:1369-1372` (`applyGroupSolo`) | `member < m_mixerChannels.size()` |
| `src/core/ControlCommandsMixerTools.cpp:414-417` (`vca.color_members`) | `member >= mixer()->numChannels()` → skip |
| `src/core/Mixer.cpp:2837-2841` (`saveSettings`) | writes the position out, reads no channel |
| `src/core/Mixer.cpp:1305-1326` (`refreshGroups`) | iterates channels by position, never members |

## What this lane adds

`tests/src/core/ControlVcaCommandsTest.cpp` — one new slot,
`aStaleMemberIndexNeverReachesAChannelQuery`. The file already had
`staleMembersAreSkippedFromStateReads` (added by `45d6dac367`), which covers a stale member
added in memory; the new slot is the finding's own repro, which that one does not cover:

1. **Load path.** A `<mixer>` `QDomDocument` holding `<vcagroup><member channel="-1"/><member
   channel="70000"/></vcagroup>` is loaded through `Engine::mixer()->loadSettings()`, then
   `vca.get_state` and `vca.list` are run: no crash, `member_count == 0`, the mixer keeps the
   two channels the file describes (70000 allocated nothing, -1 did not wrap), and the file's
   own `vca-0` id survives.
2. **Query path.** `group->addMember(static_cast<mix_ch_t>(65535))` next to a live member,
   then both reads: `member_count == 1`, the stale position skipped, the live member still
   reported with its `ch-<n>` id and its 0.5 published gain, and
   `Mixer::takeRefusedLookupsForTesting() == 0` — under `LMMS_TESTING`, `Mixer::mixerChannel()`
   counts an out-of-range read and answers `nullptr` (`include/Mixer.h:655-663`), so a zero
   count proves no lookup happened at all rather than that a null answer was tolerated.
3. **No collateral damage.** The live membership round-trips through
   `Mixer::saveSettings()`/`loadSettings()` and still reports the same group id, the same
   `ch-<n>` and the same published gain. The slot restores the mix it inherited (saves the
   mixer element first and reloads it at the end), so the other slots keep their four channels.

## Commands and exit codes (copied from the logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-73.r7/logs`)

- `cmake --build build --target ControlVcaCommandsTest -j4` (`build-1.log`) → **EXIT=0**
- `cd build/tests && ctest -R '^ControlVcaCommandsTest$' --output-on-failure` (`test-1.log`) → **EXIT=0**,
  `100% tests passed out of 1`
- `build/tests/ControlVcaCommandsTest aStaleMemberIndexNeverReachesAChannelQuery` (`test-2-slot.log`) → **EXIT=0**,
  `PASS`, evidence line
  `VCA_EDIT_EVIDENCE malformed_members=2 allocated_channels=0 member_count=0 stale_member_skipped=1 refused_lookups=0 roundtrip_member=ch-6 gain=0.5`
- **RED.** Product code on this branch already carries the guard, so `git diff -- src/` is
  empty and there is nothing of mine to reverse. To prove the test catches the bug I removed
  the *already-present* guard instead:
  `git diff -- src/core/ControlCommandsVcaShared.h > …/logs/unguard.patch` (`DIFF_EXIT=0`,
  patch is the 2-line hunk) →
  `cmake --build build --target ControlVcaCommandsTest -j4` (`build-2-unguarded.log`) → **BUILD_EXIT=0** →
  `cd build/tests && ctest -R '^ControlVcaCommandsTest$' --output-on-failure` (`test-3-red.log`) → **TEST_EXIT=8**,
  `292 - ControlVcaCommandsTest (SEGFAULT)`; the crash landed in the pre-existing slot, so I ran
  mine alone: `./ControlVcaCommandsTest aStaleMemberIndexNeverReachesAChannelQuery`
  (`test-4-red-slot.log`) → **EXIT=139**, `Received signal 11 (SIGSEGV) … aStaleMemberIndexNeverReachesAChannelQuery`.
- **Restore.** `git apply -R …/logs/unguard.patch` → **APPLY_R_EXIT=0**; `git diff --stat -- src/` prints nothing.
- **GREEN.** Rebuild (`build-3-restored.log`) → **BUILD_EXIT=0**; ctest (`test-5-green.log`) → **EXIT=0**;
  the slot alone (`test-6-green-slot.log`) → **EXIT=0**, `Totals: 3 passed, 0 failed`.
- **Family.** `cmake --build build -j4 --target MixerRoutingBackwardCompatTest VcaGroupTest
  VcaRefreshAtomicityTest ControlVcaEditGroupsTest ControlVcaNestTest ControlVcaLockedEditsTest
  MixerToolsTest VcaStripLifetimeTest VcaStripViewTest` (`build-4-family.log`) → **EXIT=0**;
  `ctest -R 'Vca|MixerRoutingBackwardCompat|MixerTools' --output-on-failure` (`test-8-family.log`) →
  **EXIT=0**, `100% tests passed out of 11` (includes the socket proof `ControlVcaCommands` #904).

## Test matrix

| path | case | covered by |
|---|---|---|
| normal | live member reported with its `ch-<n>` id and published gain; membership survives save→load | slot parts 2 and 3 |
| failure | `<member channel="-1"/>` and `<member channel="70000"/>` in a project file | slot part 1 |
| failure | a stale position a live group still holds (`addMember(65535)`) | slot part 2 |
| exhaustion | `70000` must not allocate channels (count stays at what the file describes); no out-of-range lookup is even attempted | slot part 1 and `takeRefusedLookupsForTesting()` |
| reconfiguration | `Mixer::loadSettings` replaces the whole group set (`clear()` → `clearVcaGroups()`), and the reload re-creates the same `vcagroup` id | slot parts 1 and 3 |
| audio thread | not applicable: the diff is one test file; `groupState` runs on the control thread and the audio path reads only `MixerChannel::vcaGain()` (`Mixer::refreshGroups`, `Mixer.cpp:1305-1326`) | — |
| platform | the test uses `QDomDocument`, `QJsonArray`/`QJsonObject` and QtTest only (Qt 5.15 and Qt 6 both have these); no Qt-6-only API, no POSIX path | build EXIT=0 above |

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  (`gates-1.log`) → **EXIT=1**, and it printed no table: its baseline cache has no entry for
  this lane's base (`9913d36d53`), so it runs `git checkout -q --detach` in its own
  `gate-baselines/tree`, and the lane guard refuses that in any cwd:
  `bunny-guard: 'git -C …/gate-baselines/tree checkout -q --detach 9913d36d53…' is not allowed in a Bunny lane`.
  I did not write that cache by hand: a complexity baseline computed here would be empty (see
  the next line) and would make later lanes report inherited over-target functions as new.
- The same gates run one by one in the worktree, each logged:

  | gate | command | exit |
  |---|---|---|
  | fork-sources | `bash tests/fork-sources-gate.sh` (`gate-fork-sources.log`) | **0** — `PASS: every tracked source in scope is registered (2781 fork-NEW, 1213 inherited, 49 tooling)` |
  | all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (`gate-all-sources.log`) | **0** |
  | no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (`gate-no-upstream-regression.log`) | **0** |
  | check-namespace | `tests/scripted/check-namespace` (`gate-manual-tests_scripted_check-namespace.log`) | **0** |
  | no-tautology | `bash tests/no-tautology-gate.sh` (`gate-no-tautology.log`) | **0** |
  | doc-figures | `python3 tests/doc-figures-gate.py` (`gate-doc-figures.log`) | **0** — `RESULT: PASS` |
  | file-length (gate 7) | `bash tests/file-length-gate.sh --check` (`gate-file-length-mine.log`) | **0** — `PASS (check mode: no regressions)`; this file is not among the 4 sources over 500 code lines |
  | complexity (gate 4) | `bash tests/complexity-gate.sh --check` (`gate-complexity-mine.log`) | **2** — `complexity-gate: lizard is not installed`; `pip install --user lizard` is refused by PEP 668 (`pip-lizard.log`, EXIT=1) and I did not force it into the system Python |

  The new slot is straight-line: no `if`/loop/`?:` in it, so its CCN is 1 by inspection. I
  could not have the gate measure that.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it.**
RED: the guard removed (`unguard.patch`), `build-2-unguarded.log` BUILD_EXIT=0,
`test-3-red.log` TEST_EXIT=8 (SEGFAULT) and `test-4-red-slot.log` EXIT=139 with
`Received signal 11 (SIGSEGV) … aStaleMemberIndexNeverReachesAChannelQuery`. GREEN:
`git apply -R` (APPLY_R_EXIT=0), `build-3-restored.log` BUILD_EXIT=0, `test-5-green.log`
EXIT=0, `test-6-green-slot.log` EXIT=0 (`Totals: 3 passed, 0 failed`). The RED run crashed in
`groupState`'s `channel->vcaGain()` on the `nullptr` the `LMMS_TESTING` seam returns, which is
the dereference the finding describes (`src/core/ControlCommandsVcaShared.h:150`).

**2. The test is deterministic.** No `sleep`, clock, `QElapsedTimer`, `rand` or thread. The
stale indices are literals (`-1`, `70000`, `65535`), the channel count is the file's own, and
the only "seam" used is `Mixer::takeRefusedLookupsForTesting()` (`include/Mixer.h:664`), which
returns and resets a counter — no timing, no scheduling.

**3. The fix is at the cause and complete.** The verdict is ALREADY-FIXED, so the "fix" is the
two guards already in history (`ControlCommandsVcaShared.h:146-148`, `Mixer.cpp:3160-3205`),
and the sibling table above shows every other member→channel site carries the same check.
My diff adds no product code, so nothing was changed at a second call site.

**4. No collateral damage.** `git diff --numstat` is `139 0 tests/src/core/ControlVcaCommandsTest.cpp`
— one test file, no product hunk, so old projects load and saved data reads exactly as before.
The slot itself asserts the non-bug path (live membership, same `ch-<n>`, same gain, one
save→load round trip) and restores the shared mix before it returns, and the family run is
11/11 green (`test-8-family.log`).

**5. ALREADY-FIXED: the code that shows it.** `src/core/ControlCommandsVcaShared.h:146-148`
(guarded member read, added by `45d6dac367`) and `src/core/Mixer.cpp:3160-3205` (the loader
refuses and reports an out-of-range `<member>`, added by `9c1e5276a7`). Both are ancestors of
this branch's base (`9913d36d53`), so neither is mine to fix again.

**6. Gates.** Six of the eight gate rows pass here with exit 0 (table above); file-length
passes; complexity cannot be measured because `lizard` is absent and installing it needs
`--break-system-packages`, which I did not do. `feature_check.py` itself cannot print its
table in this session because the guard denies the `git checkout --detach` its baseline cache
needs; its exact output is quoted under Gates and in `gates-1.log`.

**7. Audio thread, undo, commands.** No product change, no control command added or changed,
no undo entry touched, nothing on the audio path. `vca.get_state` and `vca.list` remain
`not_mutating` (`theGroupRegistersItsFourteenIds` and `everyIdHasAContractRow` still PASS in
`test-5-green.log`).

## Other findings

- `bash tests/complexity-gate.sh --check` exits 2 — `lizard is not installed` — in this
  environment, and `pip install --user lizard` is refused by PEP 668. Gate 4 therefore reports
  nothing (a vacuous pass) for every lane cut from this base until someone installs it.
- `feature_check.py`'s baseline cache cannot be filled under the lane guard, because its
  `base_listings()` needs `git checkout --detach` (denied unconditionally by
  `~/.local/share/zene-bunny-fix/bunny/shims/git`). Any lane whose base commit has no cached
  listing cannot run the gate suite at all.
- `VcaGroup::addMember()` (`src/core/VcaGroup.cpp:104-120`) still accepts any non-zero index,
  including one past the end of the mixer. That is deliberate for this bug's fix — the queries
  are the guard — but it means the invariant "a member names a live channel" is not enforced
  at the model, only at the loader and at every reader. Not changed here.
