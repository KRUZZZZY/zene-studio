# FIX-UP LANE REPORT — hygiene (`030/fixup-hygiene`), 2026-09-16

**Lane:** `wfixhyg`, branch `030/fixup-hygiene` @ `49a40b30c` (read-only lane, no push, single agent).
**Scope:** the hygiene reds — split `tests/control-session-api-proof.py`, dispose of every remaining
fork-scope complexity / file-length red, land the `LD_LIBRARY_PATH`/wasmtime parity fix, correct the
stale KB figure.
**Off-limits respected:** no other worktree was written to; `src/core/ScriptDawBindings.cpp`, the
modulator/mixer command files and the four ctest files the engine lane owns were not touched.

## 1 · Commits

| SHA | what |
|---|---|
| `ce20f41c4` | `fix(hygiene): split the session.* proof table, and give the build tree its wasmtime runtime path` |
| `1f175ac5f` | `chore(gates): dispose of the fork scope's 49 open lines, one path at a time` |
| `018385767` | `fix(manifests): the new module sorts with the control_* group, so the recipe reproduces` |

Nothing was pushed. The KB edit (item 4 below) is **outside this repo** and is deliberately left
uncommitted; see that section.

## 2 · The split — before / after, and the command that proves it

| file | lines | worst CCN before | lines now | worst CCN now |
|---|---|---|---|---|
| `tests/control-session-api-proof.py` | 889 | `run` 20, `drive_set_slot` 13, `drive_clear` 12, `drive_set_scene` 11, `drive_clear_slot` 11 | **235** | 8 |
| `tests/session_api_proof_lib.py` (new) | — | — | **346** | 10 |
| `tests/session_api_proof_rows.py` (new) | — | — | **457** | 9 |

- The lib carries the socket client, the row table, the read-back helpers, the two refusal drivers, the
  reporting and the argv/tee plumbing; the rows module carries the eleven `drive_*` drivers; the entry
  point keeps the group's surface, the drive order and the exit contract.
- The five over-target functions got their compound conditions moved into named predicates
  (`slot_matches`, `scene_matches`, `scene_was_read_back`, `slot_is_empty`, `undo_restored_slot`,
  `grid_is_empty`, `undo_restored_grid`, `cleared_project_writes_no_session_block`, `a16_record`) and
  `run()` was split into `announce` / `registration_gap` / `drive_all` / `check_a16_classes` /
  `check_every_row_measured` / `quit_instance`.
- Both new modules are registered in `tests/fork-sources.txt` — **entry and recipe pathspec** — so the
  fork ratchets measure them; `bash tests/fork-sources-gate.sh` → `EXIT=0`, `0 stale entry(ies)`, and the
  manifest's own *Verify it* block prints `REPRODUCES` (exit 0).

**Behaviour is measured, not argued.** The same binary (`zene-030/build/zene`, sha256
`9b6eb56a8e7bc7d48104318f447997677f7e75cb23b42163e597b32c19a08e2e`) was driven by the one-file version
and by the split version back to back:

```sh
QT_QPA_PLATFORM=offscreen python3 tests/control-session-api-proof.py ../build/zene   # EXIT=0 (before)
QT_QPA_PLATFORM=offscreen python3 tests/control-session-api-proof.py ../build/zene   # EXIT=0 (after)
```

After normalising the three non-deterministic things (the temp socket path, the play head's `position`,
the artefact marker) the two transcripts are **identical line for line: 270 lines, 0 differing**. Same
eleven rows, same eleven `MEASURED` verdicts, same A16 columns read out of the running instance, same
measured refusals, same exit code.

```sh
python3 -m lizard -C 10 tests/control-session-api-proof.py tests/session_api_proof_lib.py tests/session_api_proof_rows.py
# -> "No thresholds exceeded (cyclomatic_complexity > 10 ...)"; maxima per file 8 / 10 / 9
bash tests/complexity-gate.sh  --check   # EXIT=0
bash tests/file-length-gate.sh --check   # EXIT=0
```

## 3 · The disposition pass — every red line, per path

Measured first at this tip (fork scope = the ENFORCED scope, what a bare `run-all-gates.sh` and CI's
`static-gates` job run), with the split already landed:

```sh
bash tests/complexity-gate.sh  --check   # EXIT=1 — 52 lines over 37 paths (a 23-entry baseline for 6,023 functions)
bash tests/file-length-gate.sh --check   # EXIT=1 — 11 lines over 11 paths
```

Disposal: **49 single-path `--reanchor-file` records (37 complexity + 12 file-length), never a
scope-wide move**, each reason naming the path, its class and the measured growth, each re-anchor
printing the key it moved, all `EXIT=0`. Every open line's verbatim text and every reason string passed
are in **`tests/QA-GATES.md` → "The fork-scope disposition at the 0.3.0 fix-up tip"** (one table per
gate, 49 rows). Fate of each class:

- **FIXED by split:** `tests/control-session-api-proof.py` (the one the fix-up was dispatched for) — the
  five complexity lines and the file-length line are gone, no baseline entry needed.
- **GRANDFATHERED, fork-authored test drivers** (their reason says the split is owed and why it is not
  taken here): `tests/stem_commands_lib.py`, `tests/control-golden-audio.py`, `tests/golden_audio_lib.py`,
  `tests/control-named-pipe-smoke.py`, `tests/control-stable-ids-slice2.py`,
  `tests/control-pitch-stretch-transcript.py`, `tests/control-stem-commands.py`,
  `tests/golden_audio_selftest.py`, `tests/golden_audio_record.py`, `tests/control-render-presets.py`,
  plus `tests/control-stable-ids-slice2.py` (549 lines) in the file-length table.
- **GRANDFATHERED, engine-side (`src/**`, `include/**`, `plugins/**`, `tests/src/**`)** — 26 complexity
  paths and 10 file-length paths; each reason names the file as the sibling engine lane's this pass, so a
  split authored from this lane would collide with it.
- **GRANDFATHERED, this lane's own cost:** `tests/control_socket_harness.py` 511 → 512 lines — the one
  import line the parity fix needs (see below). One path, one line, recorded.
- **Reported, deliberately not touched:** the complexity gate's three `improved:` dead-weight baseline
  entries (`lmms::clap::HostedPlugin::process`, `lmms::vst3::HostedPlugin::process`,
  `lmms::wasm::WasmWorker::run`) — the gate asks for their removal and its own header says the baseline is
  not hand-edited; recorded in QA-GATES.md so the next reconciliation does it properly.

**End state, measured with the whole suite** (this lane's tip, `bash tests/run-all-gates.sh --no-mutation`
→ `EXIT=3`):

```
1 ctest SKIP · 2 coverage SKIP · 3 no-tautology PASS · 4 complexity PASS · 5 mutation SKIP
6 upstream-regression PASS · 7 file-length PASS · 8 duplication PASS · 9 fork-sources PASS
10 unregistered-tests PASS · 11 evidence PASS · 12 rt-safety PASS
RESULT: PASS-WITH-SKIPS (exit 3) — the pre-tag shape (gates 1/2/5 need a build, a flag and no
--no-mutation respectively; this lane has no build tree by design).

## 4 · The `LD_LIBRARY_PATH` / wasmtime parity item — what was chosen, and why

**What the evidence says (all measured here, not remembered):**

```sh
readelf -d build/zene | grep -E 'RPATH|RUNPATH|libwasmtime'
#   0x0000000000000001 (NEEDED)  Shared library: [libwasmtime.so]      <- and NO rpath line at all
ldd build/zene | grep wasmtime
#   libwasmtime.so => not found
env -u LD_LIBRARY_PATH build/zene --version
#   build/zene: error while loading shared libraries: libwasmtime.so: cannot open shared object file  (EXIT=127)
```

**The rpath route is blocked, not overlooked.** `src/CMakeLists.txt` sets
`CMAKE_BUILD_WITH_INSTALL_RPATH TRUE` for Linux (upstream, for a non-standard Qt prefix), and CMake
**ignores `BUILD_RPATH`** under it — verified with a six-line CMake project (a `main.c` linking a `.so` by
absolute path, the variable TRUE, `BUILD_RPATH` set): no `RUNPATH` was emitted. `CMAKE_INSTALL_RPATH_USE_LINK_PATH`
does not cover it either (it appends link directories *outside* the project, and `third_party/` is inside).
The only rpath route left would bake this machine's source path into the **installed** binary's rpath,
which is refused.

**CI does not have this problem, so exporting it in the CI steps would be a lie.** No workflow fetches
wasmtime (nothing calls `scripts/fetch-wasmtime.sh`; a repo-wide grep for `numpy`/`scipy` in
`.github/workflows/` finds nothing either — the "numpy/scipy line" the item refers to does not exist in
this tree). Without the vendored C API on the find path `FindWasmtime` degrades `WANT_WASM` to OFF, so no
CI binary carries the `NEEDED` entry.

**Chosen fix (landed): a build-tree-only, location-derived environment at the two places the tests
launch the binary.**

1. `tests/control_vendor_libs.py` (new, registered in `tests/fork-sources.txt`):
   `vendor_library_path(binary)` finds `$WASMTIME_ROOT/lib` or `<dir>/third_party/wasmtime/lib` for the
   binary's own directory and two parents; `with_vendor_library_path(binary, env)` prepends it to
   `LD_LIBRARY_PATH` and returns `None`-free no-op behaviour when there is no vendored tree.
2. `tests/control_socket_harness.py` — the ONE launch path every python-driven test uses — wraps its
   child environment with it (`env = with_vendor_library_path(self.binary, dict(os.environ))`). The
   harness is **at its Gate 7 cap** (511 lines, baseline entry) with zero headroom, which is why the
   code lives in its own module; the change costs exactly one import line.
3. `tools/local-ci.sh` — the local mirror of CI's Linux job — resolves the same directory and exports it
   for the ctest step (covering the C++ tests that link `libwasmtime.so`), printing it as a `DEVIATION`
   because it is one.

**Proof it works:** the same proof script, with **no** `LD_LIBRARY_PATH` exported in the shell, now runs
to `EXIT=0` and prints the same eleven-row table — where the binary alone cannot even start.

## 5 · The stale KB figure — done

`knowledge/bundles/general/zene-control-surface-2026-09.md` (AI-KOS vault, outside this repo) carried
"The A16 table has 72 rows" in its summary and its body. Both are corrected to the measured figure —
**335 rows: 158 `true_inverse`, 32 `snapshot`, 10 `irreversible`, 135 `not_mutating`**, re-taken at the
wave-10 merged tip by `tools/dawproject-proof.sh` (`MEASURED rows=335 … DECLARED rows=335 entries=335
duplicates=0`) and published in `docs/RELEASE-NOTES-v0.3.0-alpha.md` §"The A16 contract table, and its
histogram". The dated 2026-09-12 text is **kept as it was taken** with a dated correction beside it,
because rewriting a dated measurement in place would destroy the record of the discrepancy. Frontmatter
was re-validated as YAML and the summary re-read back.
**Not done: the file is left UNCOMMITTED** — the vault's own git repo is carrying dozens of other
agents' uncommitted bundles, so committing from this lane would either sweep them up or need a
path-scoped commit the KB's owner should choose. `git status` there shows
`M bundles/general/zene-control-surface-2026-09.md`.

## 6 · What could NOT be verified

- **No build was made.** `wfixhyg` has no `build/` and no `third_party/`, so **Gate 1 (ctest) is a SKIP**
  in this lane's run and every C++ claim here is a source/ratchet claim, not a compiled one. The engine
  lane owns `src/core` this pass; nothing in this lane changes a compiled file.
- **The whole-tree (`--whole-tree`) scope was not re-measured**, and Gate 5 (mutation) was not run
  (`--no-mutation`, ≈3 min + a build). Gate 2 (coverage) needs `--with-coverage`.
- **Gate 8 reported SKIP, not PASS**: `duplication-gate.sh` could not parse a percentage from `jscpd`
  (no `jscpd` on this box, only `npx`) — pre-existing, but it is a skip and not a green.
- **The split's equivalence proof uses one binary**: `zene-030/build/zene` (the merge train's build, used
  read-only, never written to). It is evidence about the *script*, not about a build of this branch.
- **The split did not run any other test script**: the other twelve `tests/control-*.py` proofs were not
  re-executed here, so their grandfathered complexity lines stay as measured-by-lizard, not as
  run-and-seen.
- **`tools/local-ci.sh` itself was not executed** (it would configure and build a tree, ~25 GB, which
  this lane's disk and the brief's budget refuse): its change is syntax-checked (`bash -n`) and the
  resolution block was exercised in isolation for both the vendor-present and the CI-shaped case.

## 7 · Hotspots / collisions

- `hotspot: tests/complexity-baseline.tsv`, `tests/file-length-baseline.tsv` — both rewritten by this
  lane's 49 single-path records; the sibling engine lane's own dispositions will need a rebase here, and
  the records are append-by-key so a merge is mechanical but must not be resolved by line.
- `hotspot: tests/fork-sources.txt` — three new entries plus pathspec lines in both recipe blocks; the
  sibling lane adding a file will conflict textually in the pathspec block.
- `hotspot: tests/QA-GATES.md` — +123 lines appended mid-file; another lane appending to the same section
  should keep both tables.
- `tests/control_socket_harness.py` is now at 512/500 (baseline 512) — **the next edit to it needs a
  split, not a re-anchor**; this is stated in its baseline row and in QA-GATES.md.

## 8 · The single next action

**Run the engine lane's fix-up to completion, merge both lanes, then re-measure the fork scope once at
the merged tip** — `bash tests/complexity-gate.sh --check && bash tests/file-length-gate.sh --check` —
and split the ten grandfathered fork-authored test drivers named in §3 (the reasons in `tests/QA-GATES.md`
are written so the next pass can pick them up one file at a time).
