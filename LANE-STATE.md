# LANE-STATE — 030/audit, surface re-measure at the 0.3.0 release tip

**Lane:** re-measure the Zene Studio 0.3.0-alpha control surface at `3956ef589` and correct the list of
record where — and only where — all of a row's own probes are genuinely satisfied.
**Branch written to:** `030/audit` (worktree `zene-030-audit`).
**Measurement target:** `release/0.3.0` @ `3956ef589` (worktree `zene-030`), **not modified**.
**Mode:** non-building. No compile, no long build. **One** headless instance of the already-built binary
was started for a live reading and reaped by explicit PID.

## Commits

| commit | branch | what |
|---|---|---|
| `42f70eb4f` | `030/audit` | the corrected `docs/FEATURE-LIST-0.3.0.md` |
| `2c8e21140` | `030/audit` | new `docs/COVERAGE-REMEASURE-2026-09-14.md`; `docs/COVERAGE-MATRIX-2026-09-13.md` marked superseded |
| `e890c9f` | root (`master`, workspace repo only) | regenerated `V0.3-FEATURE-TRACKER.{md,tsv}` |

Nothing was pushed. No tag moved or deleted. `release/0.3.0` was not moved: `git -C zene-030 rev-parse
HEAD` → `3956ef58969140b26628a434a10b8aeadb534a89` before and after, and its `git status -s` shows only
untracked directories that were already there.

## What changed, and the evidence for each change

### The surface itself — `33 groups / 185 ids`, measured twice and agreeing

```
$ grep -c '\.id = QStringLiteral' src/core/ControlCommands*.cpp | awk -F: '{s+=$2} END {print s}'
185                      # 54 ControlCommands*.cpp translation units
$ grep -h -oE '\.id = QStringLiteral\("[a-z0-9_.]+"\)' src/core/ControlCommands*.cpp \
    | sed 's/.*("//;s/")//' | sort -u | wc -l          -> 185
    ... | sed 's/\..*//' | sort -u | wc -l             -> 33
$ python3 verification/ctl.py --socket <lane dir>/audit.sock commands
# 185 command(s)                                        -> 185 ids, 33 prefixes
```

Set comparison of the two: **185 == 185, both differences empty.** The tracker's own extraction returns
**192 / 35**; the 7 extra are reconciled exactly — 5 gate-compiled `wasm.*` (absent from the build) and 2
false positives of its part-B regex, which matches the **verb** field
`cmd.verb = QStringLiteral("tag.add")` at `src/core/ControlCommandsBrowserTags.cpp:175` (and `:202`) as
if it were an id. No row on the list declares `tag.add` or `tag.remove`, so no verdict is affected. The
defect is **reported, not patched**: the tracker is a shared instrument and fixing it moves numbers other
lanes are reading.

### The live reading, and the reap

```
$ pgrep -a zene
1319566 /home/kruzzzzy/.../zene-030/build/zene --control-socket /home/kruzzzzy/zene-030-audit-auditor/audit.sock
$ ctl.py ping   ->  "version": "0.2.1-alpha.159+571016f", "engine_ready": true, "proto": 1
$ kill 1319566            # explicit PID; never pkill -f
$ pgrep -a zene ; echo EXIT=$?
EXIT=1                    # reaped, nothing left
```

The binary is built at `571016ff8`, the tip's **parent**. That is safe for this measurement because the
tip changes no source: `git diff --name-only 571016ff8..3956ef589 -- src/core/` → **empty**, and
`git diff --stat 571016ff8..3956ef589` → two test files only
(`tests/control-freeze-commands-transcript.py`, `tests/freeze_bounce_evidence.py`). So the live registry
is the tip's registry. **This is an inference from the diff, not a rebuild** — see *Unverifiable*.

### Six rows corrected in `docs/FEATURE-LIST-0.3.0.md` (plus the header, base-of-record, row 47 and *Reconciliation* 6)

| # | was | now | the measurement |
|---|---|---|---|
| 5 | to build | **in the tree** | 9 folder ids registered — `ControlCommandsTrackFolder.cpp:283,310,333,361,386` (`track.set_folder`, `track.folder_set_collapsed`, `track.set_routing`, `track.set_pinned`, `track.folder_get_state`) and `ControlCommandsTrackFolderSets.cpp:273,298,322,344` (the four `visibility_set_*`); `include/TrackFolder.h` + `src/tracks/TrackFolder.cpp` present; `TrackFolderTest` and `ControlTrackFolderTranscript` registered |
| 15 | to build | **in the tree** | `midi.retro_capture_arm` / `_status` / `_to_clip` at `ControlCommandsMidi.cpp:163,214,360`; `include/RetroMidiCapture.h` + `src/core/RetroMidiCapture.cpp` present; `MidiRetroCaptureTest` + `ControlRetroCapture` registered |
| 41 | to build | **in the tree** | `chain.*` 6 ids at `ControlCommandsChain.cpp:198,248,282` and `ControlCommandsChainEdit.cpp:147,189,266`; `ControlChainPresets` + `ControlChainPresetTest` registered. **This is the row the queue was sending the fleet at.** Its proof is named as `ControlChainPresetTest` because that is a name the instrument can see (`^[A-Z][A-Za-z0-9_]*Test$`) |
| 62 | to build | **in the tree** | `track.set_routing` at `ControlCommandsTrackFolder.cpp:333` — the routing mode that sums the children through the folder's own mixer channel |
| 74 | to build | **partial** | `clock.*` 3 ids at `ControlCommandsClock.cpp:209,240,268`; `MidiClockTest` + `ControlClockCommands` registered. **MTC stays recorded ABSENT**: `clock.get_state` reports `mtc: "absent"` (`ControlCommandsClock.cpp:21,219`; `docs/KNOWN-LIMITATIONS.md:566`) |
| 47 | 31 groups / 170 ids | 33 groups / 185 ids | the source and the live reading above |

Every one of these six rows now carries a `measured at 3956ef589: <command> -> <result>` block in the
file itself. Header, *Base of record*, *Reconciliation* 6, row 47's figures and *What this means* are
updated to match.

**Rows 12, 13 and 20 were verified and left unchanged** — their statuses already read *in the tree* and
their named proofs `ControlPunchTranscript`, `ControlRecordingRecovery`,
`ControlFreezeCommandsTranscript` are all still registered (140 registered ctests in
`build/tests/CTestTestfile.cmake`).

**Rows 4, 27, 28 and 29 were not touched, by instruction.** `030/vca-editgroups` and `030/routing-surface`
are two builds in flight adding exactly those groups. Measured at `3956ef589`: a strict
`QStringLiteral("vca\.")` / `("pdc\.")` / `("routing\.")` / `("bus\.")` scan over
`src/core/ControlCommands*.cpp`, `src/core/*Control*.cpp` and `src/core/*Control*.h` returns **0** in all
four — so they are not yet on `release/0.3.0` and must not be declared here.

**A proof that a name is not enough:** the grep that returns `routing.` = 23 hits and `bus.` = 1 is a
false positive of an unescaped dot — it matches `QStringLiteral("routing")` (a schema property) and
`QStringLiteral("busy")`. The strict `\.` forms return 0. Recorded because it is the same class of error
as the tracker's `tag.*` defect.

### The regenerated queue

```
$ python3 scripts/zene-feature-tracker.py --write
features 89 · ADDED 18 · THIN-100 10 · IN-PROGRESS 18 (mean 55.6 %) · NOT-STARTED 43 · NO-PROBE 0
open 71   (was: ADDED 13 · THIN-100 10 · IN-PROGRESS 18 · NOT-STARTED 48 · open 76)
```

The five rows that moved are exactly 5, 15, 41, 62 and 74. **No gate, baseline or manifest was
weakened**: every moved row moved because a group, a proof or a limits line was measured present, and
each moved row's own prose keeps whatever genuinely remains (row 74 keeps MTC absent; row 5 keeps the
layout/workspace-presets half out, `grep -rniI 'workspace preset' src include` → 0 hits).

## What is genuinely still open — 71 of 89 by the instrument, 68 by the list's prose

Instrument state: `IN-PROGRESS 18 + NOT-STARTED 43 + THIN-100 10 = 71`.
List prose: `partial 27 + to build 41 = 68` (89 − 21 *in the tree*).
The two differ for measured reasons, stated in `docs/COVERAGE-REMEASURE-2026-09-14.md` §7: a registered
**refusal stub** scores 100 on the `ids` part (rows 10 and 14 read ADDED while their prose records a half
that is absent), and a row that names a **decision or a programme** rather than an id/test/file measures
0 with nothing to probe (rows 52, 53, 55, 56, 58, 59). Neither figure is an estimate.

Per-row detail, including one clause for each of the **83 rows not touched**, is
`docs/COVERAGE-REMEASURE-2026-09-14.md` §8 and §9.

## What I could not verify from this box

1. **The tip's own build.** The live registry was read from a binary built at `571016ff8`, the tip's
   parent. I did not compile `3956ef589` (this lane does not build, and the box is at its two-build
   limit). The equivalence rests on `git diff --name-only 571016ff8..3956ef589 -- src/core/` being empty,
   which is a strong but indirect proof.
2. **Any ctest actually passing.** I read which ctests are *registered* in
   `build/tests/CTestTestfile.cmake` (140) and that the named proofs' sources are present. I ran **no**
   test: a registered proof is not a green proof. The rows say "registered", never "passing".
3. **The binary's version string is not 0.3.0.** The live `control.ping` reports
   `0.2.1-alpha.159+571016f` at the 0.3.0 release tip. That is a finding in its own right and is **not**
   resolved here — it is either the version stamp this line has not yet bumped or a real defect, and
   deciding which needs the release lane, not this one.
4. **The A16 row count** (139 / 142 / 127 / 150 / 155 / 157). Only a run of `ReversibilityContractTest`
   on the merged tree settles it; row 89 says so and this lane did not guess.
5. **The `0ee78abed` tip** some documents attribute to `docs/COVERAGE-MATRIX-2026-09-13.md`. It appears
   nowhere in that file (whose own header pins `ddf5f171d`), so it is recorded as unverified rather than
   adopted.
6. **Anything about the two in-flight lanes' work** (`030/vca-editgroups`, `030/routing-surface`). Their
   branches are not `release/0.3.0`, so nothing they add is declared in a row here.

## The single next action

**Run `python3 scripts/zene-feature-tracker.py --write` again after `030/vca-editgroups` and
`030/routing-surface` merge into `release/0.3.0`, and correct rows 4, 27, 28 and 29 from that
measurement** — they are the four rows this pass was forbidden to touch, they hold the `vca.*`,
PDC/sidechain, routing-graph and audio-port/`AudioBus` groups, and they are the largest remaining block of
the queue's `IN-PROGRESS` count. Until that merge the queue is accurate as of `3956ef589` and no row on it
sends a lane at a feature the tree already has.
