# Scenes native — ARCH-4 lane S8

Contract row (design/specs/SPEC-ARCH-4-DOCUMENT-MODEL-DRAFT.md:537, verbatim):

> **Scenes native**: scene cells over lanes; `<session>` reader becomes the upconverter | part of
> 11, 6 | Depends on S7; the 17 `session.*` ids keep their behaviour | The launch test
> (`ControlSessionLaunch`) green against the new model

This file is the lane's design record and Gate 6/11 evidence. Proof results are appended in
§3 with unpiped exit codes once the final run is captured; nothing here is claimed as
measured until its proof line says so.

## 1. Rules the design obeys

- **Compatibility rule (SPEC §2):** new state ONLY in a top-level namespaced section — never a
  child of an existing element. An older build's `loadTrack` materialises an unrecognised
  `<track>` child as a phantom Clip (§5.2 risk 1), so the scenes writer puts nothing under
  `<track>`, nothing under `<session>`, and no new attribute on any existing element: one
  `<z:scenes>` top-level section bound through the same `z` namespace attribute S7's
  `<z:lanes>` uses (`DocumentIndex.h` — ONE spelling, idempotent re-set).
- **Upconversion map (SPEC §2, line 403):** `<session version tracks scenes
  launchquantisation>` + `<scenes>` + `<clips>` → `<z:scenes>` + `<z:cell>`; slot refs
  `pattern`/`src` keep their exact meaning (`src/core/SessionClip.cpp` never moved).
- **Addressing rule (SPEC row :507):** `track * sceneCount + scene` is KEPT as the addressing
  rule so the 17 `session.*` handlers and their tests survive — the native cell carries the
  same two coordinates (`track`, `scene`) and the same attribute vocabulary the legacy block
  used. No id, no schema and no handler changed.
- **Additive rule (SPEC acceptance, line 630):** a project that uses none of scenes re-saves
  byte-identically — the section is written only when `SessionModel::shouldPersist()` says
  the model holds state, so no section, no `session` block, no `xmlns:z` binding.
- **S7 pattern:** one writer + one claim in `Song.cpp`'s `saveProjectFile` /
  `restoreNamedSection`; the claim accepts only the exact shape the writer produces and
  answers false otherwise, so 1.6.1 preserves the element verbatim as unclaimed.
- **Id stability:** the 17 `session.*` control ids (`include/ControlRegistryGroups.h` +
  `src/core/ControlCommandsSession*.cpp`) are untouched by this lane — no renames, no schema
  changes; `commands_snapshot.json` is stale-by-design and its count is unchanged (this lane
  registers no command).

## 2. Where the model lives

- **Native writer:** `src/core/SessionModel.cpp` `SessionModel::saveScenesSection` — builds
  `<z:scenes v="1" tracks scenes launchquantisation>` with direct `<z:scene index=...>` rows
  (only modified scenes) and `<z:cell track scene ...>` children (only non-empty slots), in
  two fixed document-order passes so save/load/save is byte-stable. A preserved
  unknown-version `<session>` block is re-emitted verbatim instead (the legacy reader's
  "newer build" policy, unchanged).
- **Native claim:** `src/core/SessionModel.cpp` `SessionModel::restoreScenesSection` —
  claim-or-preserve, validated into a candidate first (a failure at any point leaves the
  model untouched): `v="1"`, both dimension attributes present and within the grid clamp,
  every child a `<z:scene>`/`<z:cell>` with in-range coordinates, nothing else. The
  shape-check helpers live next to the other upconverter serialisers in
  `src/core/SessionModelPrivate.h` (`sessionSerialization`).
- **The upconverter:** `src/core/Song.cpp` `Song::restoreNamedSection` — the
  `z:scenes` branch claims the native section; the `session` branch below it is unchanged
  in behaviour and now serves as the UPCONVERTER: an old block fills the same
  `SessionModel` the native section fills, and the next save writes `<z:scenes>` — the
  legacy form is the migration's input, never its output. `SessionModel::saveState` /
  `restoreState` keep their bytes because they are still the undo checkpoint payload's shape
  (`ControlCommandsSession.cpp` `sessionBlockXml`) and the legacy reader itself.
- **Session-blind builds** (`LMMS_HAVE_SESSION_VIEW` off) never see the claim branch: a
  native section from a session-aware build is unclaimed and rides 1.6.1's preserved tail;
  an old `<session>` block is preserved exactly as before.

## 3. Proofs

- [ ] **Full suite (run 4):** `bash tools/local-ci.sh --jobs 2 --build-dir build` —
      provision **EXIT=0**, configure **EXIT=0**, build **EXIT=0**, ctest **EXIT=8**:
      **234/236 passed**, and the two failures are PROVEN INHERITED (see below).
      **`ControlSessionLaunch` PASSED (5.38 s) against the new model** — its step 2 now
      asserts the native section (`<z:scenes` + four `<z:cell` children); every other step
      (grid build, reopen, transport, launch at the next bar, readback) is untouched.
      `ScenesUpconvertTest` PASSED, `SessionModelTest` PASSED,
      `ProjectOpenIntegrityTest` PASSED.
- [ ] **Inherited reds, proven at pristine base.** Both failures reproduce with this
      lane's changes stashed (`git stash push -u`, tree = train tip `4344327cb`):
      `BASE_BUILD=0`, `BASE_CTEST=8`, **2/2 failed**:
      1. `SessionTuningTest` SEGFAULT inside
         `loadScaleWhileTransportPlaysRetunesSoundingNotes` (a #712 Microtuner/MTS test
         method; null+0x5e deref). The test does not include any file this lane touched.
      2. `ControlCommandsSnapshot`: live binary **350** ids vs committed snapshot
         **343** — the 7 missing ids are exactly `mts.*` (get_state, load_keymap,
         load_scale, master_set, reset, set_note, set_tuning), registered by base commit
         `c2534a336`, whose own message says *"No build/ctest captured: the lane stalled
         before its proof phase."* **Count change noted, not hand-merged:**
         commands_snapshot.json stays stale-by-design until the final tip regenerates it
         through `snapshot_commands.py --socket <live>`; this lane registers no command
         and does not touch the snapshot.
- [ ] **Seven text gates, all EXIT=0** on the tree as committed (re-run post-commit):
      `file-length-gate.sh --check` · `complexity-gate.sh --check` ·
      `duplication-gate.sh --check` · `fork-sources-gate.sh` ·
      `no-upstream-regression-gate.sh` · `evidence-gate.sh` ·
      `unregistered-tests-gate.sh`. (Gate 6 was red AT the train tip — the same
      `c2534a336` vendored `thirdparty/mts-esp` without declaring it; this lane declared
      the five paths with the provenance `thirdparty/mts-esp/README.md` records, and
      extended the `src/core/Song.cpp` and `tests/CMakeLists.txt` rows in place.)
- [ ] **Byte identity:** `ScenesUpconvertTest::aProjectThatUsesNoneOfThisWritesNoScenesSection`
      green inside the suite (no section, no block, no `xmlns:z`, bytes equal across
      save/load/save). The observe()-at-id-write fix (ccfbe4f99) needed no invocation:
      scene cells introduce no engine-born ids.
- [ ] **Negative control (§5.2 risk 1):** seen RED with a `<z:cell>` planted under
      `<track>`, reverted to GREEN without touching the test — full transcript in
      `docs/s8-logs/negative-control.md`.

### Test changes made, with justification (plumbing the contract allows)

- `tests/control-session-m1.py` step 2: `"<session" in text` / four `<clip ` children →
  `"<z:scenes" in text` / four `<z:cell ` children inside the section. The assertion IS the
  external readback of the saved file; only its element names moved with the form. Every
  other step (grid build, reopen, transport, launch at the next bar, readback) is untouched.
- `tests/src/core/SessionModelTest.cpp` `songProjectRoundTripsSessionBlock`: DOM shape
  assertions moved from `session`/`clips/clip` to `z:scenes`/`z:cell`; the test's claims
  (every field intact, block bytes stable across save/load/save) are asserted against the
  new section. Runtime slots unchanged.
- `tests/src/core/ProjectOpenIntegrityTest.cpp`
  `sessionBlockSurvivesALegacySessionRoundTrip` (renamed from
  `...ThroughASessionBlindBuild` by ARCH-4 S10, which removed the build option and
  with it the flag-OFF half): asserts the legacy `<session>` block's data survived
  into `<z:scenes>` (S8's upconversion) and that the legacy block is not
  re-emitted alongside it.
