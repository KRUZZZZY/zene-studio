# MERGE TRAIN — wave 9 (2026-09-16)

**Worktree** `…/zene-030` · **branch** `release/0.3.0` · **base** `a36e8b0d4` (unpushed,
240 ahead of `product/release/0.3.0` f611c888b) · **final** `6c0ff99a2` · nothing pushed.

Lanes merged **by pinned SHA** (`git merge --no-ff <sha>`, never a branch name):

| lane | pinned sha | forecast (`git merge-tree --write-tree`) | merge result |
|---|---|---|---|
| `030/session-api-proof` | `e8265279f` | CONFLICT in `tests/fork-sources.txt` + `tests/upstream-modifications.txt`, everything else auto-merged | merged as `872193357`, conflicts resolved (see §2) |
| `030/rel2-release-job` | `4e5f2c85f` | clean (`exit 0`) | merged as `cf39690a0`, no conflicts |

`git cherry a36e8b0d4 030/w20-wasm-abi` → `- e292eb694…` (**already present**, nothing to merge).

## 1 · The commits this train added

| commit | what |
|---|---|
| `e57d045bf` | the wave-5 train's **leftover** `docs/SAMPLE-ACCURATE-AUTOMATION.md` change, committed as a labelled leftover (it documents `unaddressable_object_count`, which `src/core/ControlCommandsAutomationRamp.cpp` already implements) |
| `872193357` | **merge** `030/session-api-proof` |
| `727f73da5` | fix-up: the three session-proof artefacts Gate 6 has no class for (§4) |
| `cf39690a0` | **merge** `030/rel2-release-job` |
| `8c136129c` | A16 histogram: merge-tip re-measure recorded (§3) |
| `c46e46a44` | `commands_snapshot.json` regenerated from a live instance (§3) |
| `6c0ff99a2` | fix-up: `fork-sources.txt`'s two recipe blocks now derive the whole entry list (§4) |

## 2 · Conflicts, and the hunks that auto-merged

**`tests/fork-sources.txt`** (both sides edited the file's own recipe and entry list). Ours as
skeleton; the lane had appended its new file's name to **all 40 python pathspec lines** it
touched (4 in `Regenerate with`, 4 in `Verify it`, and every tail note-block copy) and inserted
its entry in byte order. Resolved by mirroring exactly that on ours (matched line-by-line by
path-set overlap, min Jaccard 0.978 on all 40) and inserting the entry in sorted position:
625 entries, globally sorted, no duplicates, no markers.

**`tests/upstream-modifications.txt`**: ours verbatim plus the lane's inserted clause appended to
the `tests/CMakeLists.txt` entry (`; session.* per-id audit proof (030/session-api-proof, board
task #642…)`, ours first). 457 ledger entries; `bash tests/no-upstream-regression-gate.sh` EXIT=0.

**Auto-merged files read, not assumed** (a clean merge is not evidence):
`tests/CMakeLists.txt` — exactly ONE `add_test(ControlSessionApiProof …)` + its property block,
inside the existing `if(LMMS_HAVE_SESSION_VIEW)` block; `ControlSessionApiProof` occurs twice
(add_test + set_tests_properties), no duplicate block. `docs/RELEASE-NOTES-v0.3.0-alpha.md` — one
new section. Merge 2 (`build.yml`): ours had not touched the file since the lane's base, so the
merge is the lane's change verbatim — verified in the merged file: **six** tag-only upload
conditions, `always()` on `release-gate` (`if: ${{ always() && (startsWith(github.ref,
'refs/tags/') || github.event_name == 'workflow_dispatch') }}`), one `release-gate` job key, YAML
parses (7 jobs; `release.yml` 2 jobs), and all five new `tests/release-*.sh` scripts pass `bash -n`.
No C++ moved in either merge (the only `.cpp`/`.h` path in `a36e8b0d4..HEAD` is
`tests/src/core/ReversibilityContractTest.cpp`, comment-only).

## 3 · Re-measurements at the merged tip

**A16 histogram** — `bash tools/dawproject-proof.sh` (part 2: the probe compiled against this
tree's `ControlReversibilityTable*.cpp` with this build's own flags) printed, verbatim:

```
MEASURED rows=334 true_inverse=158 snapshot=32 irreversible=10 not_mutating=134
```

Identical to the constant already in `tests/src/core/ReversibilityContractTest.cpp`, so **no digit
changed**; the re-measure is recorded in the test's comment (`8c136129c`) and corroborated by the
ctest slot `theTableHistogramIsTheDocumentedOne()` — **PASS**. (Configuration: `ZENE_TELEMETRY_ENABLED`,
`WANT_WASM=ON`, `WANT_STEM_SPLIT=OFF` — the release configuration.)

**Command snapshot** (derived; the brief forbids hand-editing) — regenerated from a **live**
instance of the merged tip (`build/zene`, started through `tests/control_socket_harness.py`,
`--control-socket`), `python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>`:

```
wrote tools/mcp-zene-control/zene_control/commands_snapshot.json: 334 commands, proto 1,
version 0.2.1-alpha.450+cf39690 · surface: 334 id(s) across 52 group(s),
ids_sha256 7dc75a5ff846671889fdbfc18d35640271771784ad557cbce223997c81f2bab2
SNAPSHOT_TOOL_EXIT=0
```

**334 ids**, unchanged id set — but the regenerated file **is** different from the committed one:
`automation.ramp_get`'s result schema now carries `unaddressable_object_count` and a new
description, which the previous capture (`2026-09-15T22:03:26Z`, `version 0.2.1-alpha.431+…`)
predated. The snapshot was genuinely stale; only the provenance block and that schema moved.

## 4 · Defects this train found and fixed while merging (each its own commit)

1. **Gate 6 went red on three fork-authored paths the lane added** (`727f73da5`). The classifier
   admits `*.md` under `docs/` and the ROOT `.gitignore`, and nothing else there:
   * `docs/reports/SESSION-API-PROOF-transcript-2026-09-15.txt` → `…-TRANSCRIPT-2026-09-15.md`
     (`R100`, content byte-identical). `docs/reports/README.md`'s own rule names a lane transcript
     `<LANE>-TRANSCRIPT.md`; the two citations moved with it.
   * `.proof-bin/.gitignore`, `.proof-ctest/.gitignore` untracked (files kept on disk) with an
     ignore block in the root `.gitignore` recording what the two directories were — the same
     treatment the `/.merge-logs-*/` block gives the trains' working directories.
   Re-run after: `bash tests/no-upstream-regression-gate.sh` **EXIT=0** (421 changed paths declared;
   457 ledger entries).
2. **`tests/fork-sources.txt` did not reproduce** (`6c0ff99a2`). Neither of the file's own two
   command blocks produced the entry list it documents: `Regenerate with` emitted 616 of 625 and
   `Verify it` 611, so the file's verification command failed with 13 missing lines. Cause: seven
   `.py` proof harnesses (`control-detect-commands`, `control-mcp-group-coverage`,
   `control-midi-reconnect`, `lua-api-surface`, `mcp_stdio_session`, `midi_reconnect_flows`,
   `midi_reconnect_probe`) and two `tests/src/core` sources (`ImportDetectionTest.cpp`,
   `ScriptDawBindingTest.cpp`) were in the ENTRY LIST but in no pathspec line or awk allow-list of
   either block — each name lived only in the tail note-block its own lane had appended, and
   `mcp-group-coverage`/`mcp_stdio_session` in no recipe line anywhere; the golden-audio pathspec
   line was in `Regenerate with` and absent from `Verify it`. Repair is comment-only (no entry
   changed): the seven names appended to the eight python pathspec lines of the two blocks, the two
   names added to both blocks' awk allow-list, and `Verify it` gains the golden-audio line.
   **Verified unpiped: `Regenerate with` now emits 625 lines byte-identical to the entry list
   (0 missing / 0 extra, EXIT=0) and `Verify it` prints `REPRODUCES` (EXIT=0).** Note Gate 9 does
   not run the recipe (the file says so itself), which is why this survived.
3. **The session ctests need `LD_LIBRARY_PATH`** (not a fix, a finding): this build is
   `WANT_WASM=ON` and links `libwasmtime.so` from `third_party/wasmtime/lib` with **no rpath**, so
   every ctest that starts the binary fails with `exit code 127` in a clean environment. Exported
   (`LD_LIBRARY_PATH=$PWD/third_party/wasmtime/lib`) the session family is 3/3 green. Every
   measurement in this report was taken with it exported; the wave-5 train's gate run was not,
   which is why its ctest numbers look different from a clean-env run.

## 5 · Proofs (command → exit code, unpiped, `LD_LIBRARY_PATH` exported)

| proof | command | result |
|---|---|---|
| session.* per-id ctest (registration: **Test #163 of 205**) | `cd build/tests && ctest -R '^ControlSession'` | **EXIT=0**, 3/3 Passed — `ControlSessionApiProof` 5.57 s, `ControlSessionLifecycleTranscript` 5.16 s, `ControlSessionLaunch` |
| rel2 red/green self-test | `bash tests/test-release-ref-fitness.sh` | **EXIT=0** — "the oracle refuses a deliberately red ref, passes its base, refuses an unmeasurable leg, and the staging-path policy gate has been seen red" |
| rel2 oracle, RED ref | `bash tests/release-ref-fitness.sh --ref rel2/red-ref-proof` | **EXIT=1** — "RESULT: REFUSED — 3 leg(s) red on b776daa16" |
| rel2 oracle, GREEN ref | `bash tests/release-ref-fitness.sh --ref rel2/green-ref-proof` | **EXIT=0** — "RESULT: FIT — every required leg ran and passed on e7afbbffb" (14 legs, incl. the staging-path policy) |
| A16 histogram slot | `cd build/tests && ctest -R '^ReversibilityContractTest$'` | histogram slot **PASS**; the file is a KNOWN red later (`SIGABRT`), see §6 |
| golden audio | `cd build/tests && ctest -R '^ControlGoldenAudio$'` | **EXIT=0**, PASS 24.24 s — the committed record holds, so **no re-record was made** (a record rewritten while green is a disabled test); the deep command if one is ever owed: `QT_QPA_PLATFORM=offscreen python3 tests/control-golden-audio.py build/zene --write-record --runs 5` |

## 6 · Gate suite — `bash tests/run-all-gates.sh` → `RESULT: FAIL`, **EXIT=1** (not 3)

Log: `.merge-logs-030w9/gates-w9.log` (gitignored). Not one new C++ failure is attributable to
this train:

| gate | result | measured |
|---|---|---|
| 1 ctest | **FAIL** | **41 of 205 failed** (164 passed). Against the wave-5 log (`.merge-logs-030w5/gates-w5.log`, 42 of 204): **0 failures new, 1 fixed** (`SampleAccurateAutomationTest` now passes). The four named known reds are all in the list (`ControlProjectArchiveTest`, `ControlNoteScaleVerbsTest`, `SmfInterchangeTest`, `SmfInterchangeRoundTripTest`), beside the inherited classes: 6 subprocess aborts (`Script*` family + `ReversibilityContractTest` + `ControlAutomationScriptTest`), 1 SEGFAULT (`SafeStartLoadPathTest`), and the control-surface transcript reds (`ControlSocketIntegration`, `ControlUndoStructuralTranscript`, `ControlVcaCommands`, `ControlPdcCommands`, `ControlBusCommands`, `ControlCrashReporter`, `ControlCommandsSnapshot`) |
| 2 coverage | SKIP | needs `--with-coverage` |
| 3 no-tautology | PASS | |
| 4 complexity | **FAIL** | fork scope 5858 functions, 78 over CCN 10; tools scope 570 functions, 13 over. **This train's own share: 5 new over-target functions**, all in `tests/control-session-api-proof.py` (the merged lane's proof script: `run` CCN 20, `drive_set_slot` 13, `drive_clear` 12, `drive_set_scene` 11, `drive_clear_slot` 11). The other new-looking lines (`ControlAutomationSupport.cpp`, `ControlCommandsAutomationRamp.cpp`) moved in `a36e8b0d4` itself — wave-5's last commit, after its own gate run — not here |
| 5 mutation | PASS | kill score ≥ 80 % (30 mutants) |
| 6 upstream-regression | PASS | 421 changed paths declared, 457 ledger entries (after `727f73da5`) |
| 7 file-length | **FAIL** | **11 regressions** (wave-5 had 10); the one this train added is `tests/control-session-api-proof.py (889)`. Named, never re-anchored |
| 8 duplication | PASS | |
| 9 fork-sources | PASS | 625 fork-NEW, 1103 inherited, 40 tooling (after `6c0ff99a2`) |
| 10 unregistered-tests | PASS | |
| 11 evidence | PASS | 6600 files scanned, 0 refused |

The brief expected exit 3; the measured exit is 1, because gates 1/4/7 are red — the state the
wave-5 tip was already in (its own report says so). This train neither re-anchored nor weakened
anything to change that.

## 7 · What could not be verified

* **Gate 2 (coverage)** — not run (`--with-coverage` not passed); no coverage claim is made.
* **The whole-tree scope** (`gates 4/7/8 --scope all`) — not run; the enforced fork+tools scope is
  what CI's `static-gates` job runs and what is reported above.
* **CI** — `release/0.3.0` is local and unpushed, so no GitHub run exists for any commit here; the
  rel2 job is verified only through its own local oracle and self-test.
* **The other 40 ctest reds** were not re-diagnosed; each is named in §6 with its wave-5 status.
* `rel2/red-ref-proof` and `rel2/green-ref-proof` are **local scratch refs** the lane left for
  re-runs (delete with `git branch -D …` when they are no longer wanted).

## 8 · Hotspots

* `tests/fork-sources.txt` + `tests/upstream-modifications.txt` — every lane edits both; the
  fork-sources entry list is now derivable again and the upstream ledger's `tests/CMakeLists.txt`
  clause is ~6 000 characters of concatenated lane clauses (wave-5's own finding). A per-wave
  companion file is the honest next move, not a blanket re-anchor.
* `tests/control-session-api-proof.py` — 889 lines / 5 over-CCN functions: the fix-up pass's
  material, with the two ratchet lines named in §6.
* `LD_LIBRARY_PATH` — any ctest run of this build outside a shell that exports it will report ~all
  binary-driven tests as failed (exit 127). Run the suite with it exported.
* `tests/CMakeLists.txt`, `include/ControlRegistry*.h`, `src/core/ControlRegistry*.cpp` — the
  standing shared-file caps (lane brief §5); nothing here exceeded them further.

## 9 · The single next action

Take the merged tip into the **fix-up pass** with the ratchet reds named above as its material:
gate 1's `ControlProjectArchiveTest` / `ControlNoteScaleVerbsTest` / `SmfInterchange*` expectations
must be decided against the engine (never edited to match), and
`tests/control-session-api-proof.py`'s five over-CCN functions plus its 889-line length are the one
ratchet regression this train landed, to be split or accepted with a recorded reason — **never by
`--reanchor`**.
