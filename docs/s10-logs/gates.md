# Lane S10 — `WANT_SESSION_VIEW` removal + docs/tsv/test unwrap — proof log

Base: `484368844` (train tip), branch `040/arch4-s10`. All exit codes recorded
UNPIPED (`cmd > file 2>&1; echo EXIT=$?`). Raw logs live in `/tmp/s10-*.log`
(not committed: Gate 6/11 reject `.log`/`.txt` under `docs/`).

## What was removed (original locations at base)

**Build system**
- `CMakeLists.txt:126-133` — `OPTION(WANT_SESSION_VIEW ... ON)` + comment block
- `CMakeLists.txt:176-182` — `IF(WANT_SESSION_VIEW) SET(LMMS_HAVE_SESSION_VIEW 1) ENDIF` + comment
- `src/CMakeLists.txt:121-128` — `IF(NOT LMMS_HAVE_SESSION_VIEW)` SKIP_AUTOMOC block
- `src/lmmsconfig.h.in:42` — `#cmakedefine LMMS_HAVE_SESSION_VIEW`
- `src/core/CMakeLists.txt:79` (+ its `ENDIF`) — `IF(LMMS_HAVE_SESSION_VIEW)` around `LMMS_SESSION_SRCS`
- `src/core/CMakeLists.txt:613-615` — "EMPTY without LMMS_HAVE_SESSION_VIEW" comment

**Code guards unwrapped (on-branch kept, off-branches deleted)**
- `include/Song.h:54` (include guard), `:535` (accessors), `:796` (members;
  deleted the `#else` `m_preservedSessionXml` member)
- `src/core/Song.cpp:282` (launch scheduling), `:458`/`:466` (session-track
  take-over in `processNextBuffer`), `:1263` (`clearProject` `#else`), `:1755-1774`
  (whole `appendPreservedSessionXml` `#ifndef` helper — deleted), `:1934`+`#else`
  (`saveProjectFile` preservation call — deleted), `:2285`+`#else` (loader
  "session-blind" claim block — deleted)
- `src/core/ControlRegistryRegistrations.cpp:138-152` — session group registrations
  (4 call sites + guard + comments)
- `src/core/ControlReversibilityTablePassive.cpp:327-342` — launch-row guard
- `src/core/ControlReversibilityTableAction.cpp:252-268` — `session.*` rows guard
- `src/core/ControlReversibilityTableSessionView.cpp:59-132` — guard + the
  `#else` empty-table/`kSessionViewRowCount = 0` branch
- Header comments corrected: `include/ControlRegistry.h:458-461`,
  `include/ControlReversibility.h:347-349`, `include/ControlRegistryGroups.h:719-723`,
  `include/SessionModel.h:52-54`, `include/UnclaimedElements.h:58-60`

**Test registrations / test unwraps**
- `tests/CMakeLists.txt:610` + `ENDIF` — session test list
- `tests/CMakeLists.txt:2274` + `:2322` `endif()` — ControlSessionLaunch /
  LifecycleTranscript / ApiProof block (now unconditional; 77-returns removed
  from the two python scripts; `SKIP_RETURN_CODE 77` properties left dormant)
- `tests/CMakeLists.txt:3343-3345` — snapshot `--compiled-out session.` call site #1
- `tests/CMakeLists.txt:3480-3482` — MCP `--compiled-out session.` call site #2
- `tests/src/core/ProjectOpenIntegrityTest.cpp:97/:125/:218/:271` — guards and
  both `#else` (session-blind) assertion sets deleted; method renamed
  `sessionBlockSurvivesARoundTripThroughASessionBlindBuild` →
  `sessionBlockSurvivesALegacySessionRoundTrip` (pointer updated in
  `docs/SCENES-NATIVE.md:114-118`)
- `tests/src/core/ProjectVersionTest.cpp:31/:65/:118/:385` — guards deleted (no
  `#else` existed; §3 of SPEC-ARCH-4: unwrap, never delete the test)
- `tests/control-session-api-proof.py` — docstrings + `registration_gap()` no
  longer has the 77 no-group skip; a missing group is now a recorded problem
- `tests/control-session-lifecycle-transcript.py` — docstring + runtime: no group
  now returns 1 (FAIL), not 77 (Skipped)
- `tests/control-commands-snapshot.py` — CONFIGURATION paragraph + `--compiled-out`
  help text now describe only `ZENE_TELEMETRY`

**Docs / tsv**
- `tests/advertised-features.tsv:114` — the `session-view WANT_SESSION_VIEW ON` row
  deleted in the same commit (SPEC-ARCH-4 §3.3 step 2: "the gate does not need a
  row to be changed — it needs the row and the build to agree"); header comment
  rewritten to record why there is no row any more; "All six rows" → "All five rows"
- `README.md:126-127` — feature bullet no longer cites the option
- No `docs/specs/SPEC-SESSION-VIEW*` file exists in the tree (checked by glob and
  find at program root and in the worktree) — nothing to unwrap there.

Left alone deliberately: historical evidence docs (`docs/SAVELOAD-INTEGRITY.md`,
`docs/COVERAGE-RUN`, transcripts) and `tests/upstream-modifications.txt` rows —
they record past states; the gates accept them (both gates green, below).

## Seven text gates (source-level, run pre-build-finish)

| gate | exit |
|---|---|
| `bash tests/file-length-gate.sh --check` | 0 |
| `bash tests/complexity-gate.sh --check` | 0 |
| `bash tests/duplication-gate.sh --check` | 0 |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `bash tests/evidence-gate.sh` | 0 |
| `bash tests/unregistered-tests-gate.sh` | 0 |

## Proof 1 — full ctest (house rules: from `<build>/tests`, unpiped)

- First driver attempt (`tools/local-ci.sh --jobs 2 --build-dir build`) died at
  97% with **ENOSPC**: `/` was at 100% (machine state, `286G` total, `202M` free).
  Fix taken (briefing-sanctioned strip path): `strip --strip-debug` over
  `build/**/*.o|*.a|*.so*`, the 217 test executables and `build/zene` — debug
  sections only, no code/symbols; **43G freed** (now 85%). No compiled source was
  edited after the strip, so no touch-sweep was required.
- Direct full run: `cd build/tests && ctest -j2 --output-on-failure`
  → **235/236 passed, `CTEST_EXIT=8`, the single failure is
  `231 - ControlCommandsSnapshot`** (the known stale-by-design red).
  `ProjectVersionTest` PASSED in this run and 10/10 in a repeat probe
  (see driver notes); one earlier driver run flagged it transiently.
- `bash tools/local-ci.sh --jobs 2 --build-dir build` final driver run:
  **`LOCAL_CI3_EXIT=1`, ctest 235/236 — the single failure is
  `231 - ControlCommandsSnapshot`** (the known stale-by-design red; exit 1 is
  exactly that one failure).

## Proof 2 — honesty gate

`./build/zene --version > /tmp/s10-version.txt; echo EXIT=$?` → `ZENE_VERSION_EXIT=0`
`bash tests/release-honesty-gate.sh --dump /tmp/s10-version.txt --artifacts build/plugins; echo EXIT=$?`
→ **`HONESTY_EXIT=0` — PASS: all 5 documented features match** (vst3-hosting,
vst3-instrument, clap-hosting ON with modules present; wasm-sandbox OFF,OFF;
stem-separation OFF,OFF). The gate checks 5 rows because the `session-view` row
left the manifest in this commit, as §3.3 requires.

## Proof 3 — snapshot / MCP drift checks

- `ControlMcpGroupCoverage` — **Passed** (in the full run and the driver runs):
  the declared MCP surface and `--compiled-out` handling agree with the binary.
- `ControlCommandsSnapshot` — **Failed, expected**: 0 missing / 7 extra, and all
  seven are `mts.get_state, mts.load_keymap, mts.load_scale, mts.master_set,
  mts.reset, mts.set_note, mts.set_tuning` — the master-tuning surface. Evidence
  this is pre-existing train drift, not S10: (a) `git grep mts.set_note 484368844`
  finds `src/core/ControlCommandsMts.cpp` — the group existed at base; (b) the
  committed snapshot at base is byte-identical (`base snapshot ids: 343` =
  current 343); (c) this lane's diff touches **zero** id literals. Not
  hand-edited, per the house brief (train final-tip REGEN).

## Proof 4 — the id-set proof (contract's "340 ids unchanged")

Live dump (`control.commands_list` through the tree's own harness,
`/tmp/s10-id-dump.py`, `ID_DUMP_EXIT=0`):

- **Live control-surface id set now = 350 unique ids, of which 17 are `session.*`.**
- **S10's delta on that set = 0 ids**, three ways:
  1. `git diff` id-literal check: `^[+-]\s*"<group>.<name>"` lines changed = **0**;
     no `R("...")` registration line changed; `include/ControlRegistryGroups.h`
     diff is comment-only (0 string lines); the committed snapshot file untouched.
  2. The registration edits are guard/comment only, and the guards were **ON in
     the base configuration** (`OPTION` defaulted ON, no job passes it), so the
     base binary registered the same session group (4 registration calls), the
     same A16 tables (the deleted `#else` tables were the dead branch) and the
     same `--compiled-out` empty list as this build.
  3. Base arithmetic matches: committed snapshot at base = 343, the 7 `mts.*`
     ids were already in base source → base live = 343 + 7 = **350** = live now.
- **Where the contract's 340 comes from**: `verification/CRASH-TESTING-INVENTORY.md`
  §1 "Live registry: 340 ids / 53 groups", measured **2026-09-17** on a different
  tree (`zene-030` @ `f16baff79`, release/0.3.0) — its own snapshot figure there
  is 332 vs this train tip's 343. The spec row's literal predates the train tip's
  lanes; the acceptance that matters — *the id set unchanged by S10* — holds
  exactly (0-id delta, 17/17 session ids intact). Counting from this tip: 350 live
  / 343 committed snapshot / 359 tracked source `R(` id literals, and the
  tracked source-literal set is **identical to base** (`comm -3` base vs now = 0
  lines; 17 of those literals are option-gated OFF in both configs: `stem.*`,
  `com.grame.jackserver.*`, etc.). The raw grep shows 361 only because
  provision-fetched `src/3rdparty/jack2` (`CFSTR("...")` regex false positives)
  sits untracked in the working tree at both ends.

## Notes / deviations

- `strip --strip-debug` extended beyond `.o`/`.a` to the linked test executables
  and `build/zene` because the executables were the 43G hog; debug sections only,
  behavior untouched (ctest ran fully green on the stripped tree apart from the
  known snapshot red).
- One driver run showed `ProjectVersionTest` FAILED; it passed standalone, in the
  full direct run, and in a 10x repeat probe (`PVT_REPEAT_10_FAILS=0`) — recorded
  as a transient, not a regression.
- No source file was edited after the strip (only `.md`), so the trap-2 touch
  rule was not triggered.
