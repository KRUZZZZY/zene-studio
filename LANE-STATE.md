# LANE-STATE — `030/scan-and-crash-surface` (feature rows 46 and 54)

Written by the lane that owns this branch. The parent merges; this branch is not
merged and nothing is pushed.

**About this file:** `LANE-STATE.md` is the tracked lane-state scratch file each lane rewrites at its
branch tip (its history shows retro-capture, chain-presets, MIDI clock, the VCA lane and the routing
lane each taking it in turn), so this lane replaced its contents rather than appending to them. The
version at the base tip is `git show 598d4f5c1:LANE-STATE.md` if the merge wants it.

- **Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wscan`
- **Branch:** `030/scan-and-crash-surface`
- **Base tip:** `598d4f5c1` (`release/0.3.0`, the tip the CI run in flight was started for)
- **Branch tip:** see `git log --oneline -1` (the last commit is the one that carries this file)
- **Build:** `build/` in this worktree, configured with the CI's own flag set by
  `bash tools/local-ci.sh --build-dir build --jobs 2` (the pinned VST3 SDK + CLAP headers were
  copied in from `zene-030/build` so the provisioning step verifies rather than fetches)
- **Logs:** `/home/kruzzzzy/zene-030-wscan-598d4f5c1/` (a private per-run directory)

## The two rows, and what "complete" means for each

| row | status now | ids registered | A16 classes |
|---|---|---|---|
| 46 Plugin scan cache and quarantine | **closed on this branch** (engine + group + proofs + limits) | `plugin.scan_cache_get_state`, `plugin.scan_cache_list`, `plugin.scan_cache_lookup`, `plugin.scan_cache_quarantine_add`, `plugin.scan_cache_quarantine_remove`, `plugin.rescan` | 3 × `not_mutating` (the reads), 2 × `snapshot` with a recorded-COMMAND inverse, 1 × `irreversible` (`plugin.rescan`) |
| 54 Crash reporter | **closed on this branch** (engine + group + proofs + limits) | `crash.list_reports`, `crash.acknowledge_report`, `crash.discard_report`, `crash.upload_report` | 1 × `not_mutating` read, 1 × `not_mutating` registered refusal (`crash.upload_report`), 2 × `irreversible` with named fallbacks |

Both engines were ALREADY in the tree at the base tip and are named, with the lines this lane
verified, in the commit message of `a00ec9637` (and in the four-part scope contract):

* row 46 — `include/PluginScanCache.h:53-161`, `src/core/PluginScanCache.cpp`,
  `src/core/PluginFactory.cpp` (`m_scanCache` at `:266`, `planPluginScan()` at `:217`,
  `dropQuarantinedPlugins()` at `:504`, `m_scanCache.load()` at `:435`, save-if-dirty at `:472`),
  proven by the registered ctest `PluginScanCacheTest`.
* row 54 — `include/CrashReporter.h:145-185`, `src/core/CrashReporter.cpp:324-523` (the
  acknowledge/discard pair at `:429`/`:435`), installed from `src/core/main.cpp:886-889` before the
  socket exists, proven by the registered ctest `CrashReporterTest`.

## What is done (all committed on this branch)

1. **The groups are registered and their A16 rows exist.**
   `include/ControlRegistryGroups.h` declares `registerPluginScanCommands`,
   `registerPluginScanEditCommands` and `registerCrashReporterCommands`;
   `src/core/ControlRegistryRegistrations.cpp` calls them;
   `src/core/ControlReversibilityTableScanAndCrash.cpp` holds the ten rows (a NEW row file because
   the passive block is at 477 of the 500 lines the ratchet allows and the live block at 499) and
   `reversibilityRowTable()` joins it. The histogram assertion moved with them:
   `ReversibilityContractTest::documentedHistogram()` is now `{218, 116, 18, 7, 77}` and
   `docs/RELEASE-NOTES-v0.3.0-alpha.md` carries 220 rows / 116 / 18 / 7 / 79 for this configuration.
2. **The engine addition is read-only and additive**, and is the only engine change:
   `PluginScanCache::records()` and `PluginScanCache::record(path)`
   (`include/PluginScanCache.h`, `src/core/PluginScanCache.cpp`), with two new cases in
   `tests/src/core/PluginScanCacheTest.cpp` (the deterministic path order; `record()` vs `lookup()`
   on a replaced file). Nothing else in the engine was touched —
   `CrashReporter.cpp` in particular is **unchanged**, because it is grandfathered at 564 lines in
   `tests/file-length-baseline.tsv` and that ratchet has a zero-line tolerance.
3. **Proofs registered**: `tests/control-plugin-scan-commands.py` (`ControlPluginScanCommands`) and
   `tests/control-crash-reporter.py` (`ControlCrashReporter`), wired in `tests/CMakeLists.txt` inside
   the existing `if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)` block.
4. **Manifests**: the five new sources are in `src/core/CMakeLists.txt`, `tests/fork-sources.txt`
   (with the two `.py` names added to all eight python pathspec lines of its recipe, so the file
   REPRODUCES) and `tests/all-sources.txt` (regenerated from its own recipe, which prints
   REPRODUCES).
5. **Docs**: the UI-absence lines are in `docs/KNOWN-LIMITATIONS.md` (two, one per row) and
   `docs/RELEASE-NOTES-v0.3.0-alpha.md` (a full section per row: what is drivable, the proof, the
   A16 class and its trap, the stated bounds, and the UI-absence lines), plus the ten-row accounting
   paragraph in the histogram section.

## Acceptance results — every command run unpiped, from this worktree

| command | exit | result |
|---|---|---|
| `tools/local-ci.sh --build-dir build --jobs 2` | **1** | `configure EXIT=0`, `build EXIT=0`, `ctest EXIT=8`: **"99% tests passed, 1 tests failed out of 149"** — the ONE failure is `ControlCommandsSnapshot` (the expected red below). Honest deviation: Qt5 development files are absent on this box, so the script adds `-DWANT_QT6=ON` (the CI job configures against Qt5) — printed by the script itself as a DEVIATION. |
| `ctest -R "CrashReporterTest\|PluginScanCacheTest\|ReversibilityContractTest\|ControlPluginScanCommands\|ControlCrashReporter"` (from `build/tests`) | **0** | **5/5 Passed, 100% tests passed, 0 tests failed out of 5** |
| `ctest -R agent_surface` (from `build/tests`) | **0** | `Passed` — and run by hand for the numbers (below) |
| `python3 tests/agent-surface-gate.py build/zene tests/data/agent-control-fixture.mmp --check` | **0** | `PASS: agent surface gate (reflection + ratchet + reverse completeness + headless sweep)` — **220 commands, 219 swept, 1 allowlisted, 0 compiled out, 3.0 s of a 120 s budget**. All ten new ids returned a TYPED result: `crash.list_reports ok`, `crash.acknowledge_report not_found`, `crash.discard_report not_found`, `crash.upload_report refused`, `plugin.rescan ok`, `plugin.scan_cache_get_state ok`, `plugin.scan_cache_list ok`, `plugin.scan_cache_lookup invalid_args`, `plugin.scan_cache_quarantine_add invalid_args`, `plugin.scan_cache_quarantine_remove invalid_args` |
| `tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **0** | `RESULT: PASS — all 6 documented feature(s) match this build on linux` |
| `tests/run-all-gates.sh` | **1** | 10 of 11 gates PASS (3,4,5,6,7,8,9,10,11) and gate 2 is SKIPPED (no `--with-coverage`); **gate 1 (ctest) is the only FAIL, and it fails solely on `ControlCommandsSnapshot`** (see below) |
| `tests/complexity-gate.sh --check` | **0** | PASS (after one real fix: `check_fresh_state` in the crash transcript was CCN 11 — split into `check_fresh_paths` + `check_fresh_state`, and the `and` chains replaced by whole-dict comparisons) |
| `tests/file-length-gate.sh --check` | **0** | PASS — `ControlReversibilityTable.cpp` is exactly at 499/500 lines (the new rows went to a NEW file for that reason) |
| `tests/duplication-gate.sh` | **0** | PASS — `duplicated lines 2.21%` (budget 5%), 418 fork sources scanned |
| `tests/fork-sources-gate.sh` | **0** | PASS — 418 fork-NEW, 1060 inherited, 34 tooling, 0 stale |
| `tests/no-upstream-regression-gate.sh` | **0** | PASS — 409 changed paths declared, nothing undeclared |
| `tests/unregistered-tests-gate.sh` | **0** | PASS — 128 test sources touched, 126 registered, 2 declared not-built |
| `tests/evidence-gate.sh` | **0** | PASS — 6287 files scanned, 0 refused |
| `tests/file-length-gate.sh` on `tests/control-crash-reporter.py` / `control-plugin-scan-commands.py` | — | 336 and 420 lines, both under the 500-line cap |
| tree hygiene | — | `git status --short` clean after the last commit; `src/core/RoutingGraph.cpp` sha256 is `1fc2d8fb3fe015e94468cd77e8fb285805198f6074258e0c7ced217c8632f163`, byte-identical to the mutation gate's own "pristine sha256" — the gate left **no mutant** in this tree |
| manifests | — | `tests/all-sources.txt` and `tests/fork-sources.txt` are regenerated from their OWN recipes and each prints `REPRODUCES` |

Measured inside the two new transcripts (the numbers a reader wants without opening a log):

* `tests/control-plugin-scan-commands.py`: the instance's own scan reports
  `plugin-scan: 64 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 64 scanned,
  59 plugin(s)`; `control.undo` reports `restored_by: plugin.scan_cache_quarantine_remove` and, on the
  second undo, `restored_by: plugin.scan_cache_quarantine_add` with the entry's reason restored
  exactly; the negative control measures `reason: ""` for the path-only re-add; the module-dependent
  checks RAN (the build ships 42 plugin modules and the transcript points `LMMS_PLUGIN_DIR` at
  `build/plugins`), so "a quarantined plugin leaves `plugin.list` after `plugin.rescan` and comes
  back" is measured, not skipped; `control.undo` after the rescan is a typed `irreversible`.
* `tests/control-crash-reporter.py`: `report_directory` is `<workspace>/crash-reports`,
  `offered_marker_path` is `<workspace>/crash-reports/zene-crash-report.offered`,
  `bounds.max_report_bytes` is 4096, `upload.supported` is false; the acknowledge writes the
  sentinel and keeps the report; the discard reports `removed_count: 2` and both files are gone from
  disk; both writers' `control.undo` is a typed `irreversible`; `crash.upload_report` is a typed
  `refused` naming the file to attach by hand.

## The one RED, and it is the expected one

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wscan/build/tests
ctest -R ControlCommandsSnapshot --output-on-failure        # EXIT=8
```

The failure text, in the test's own words:

```
snapshot ids      210 command(s), captured 2026-09-14T12:10:49Z, proto 1
live ids          220 command(s), proto 1
bridge live       220 generated tool(s) (222 with the bridge tools), source live
                  vs the binary it just asked: 0 missing, 0 extra
bridge offline    210 generated tool(s) (212 with the bridge tools), source snapshot
```

So the drift is EXACTLY the ten ids this lane adds, in one direction, and the bridge's live mode
already generates a tool for each of them (the per-feature bridge work is automatic, as the spine
says). `tools/mcp-zene-control/zene_control/commands_snapshot.json` is **not** regenerated and **not**
hand-edited here: it is a merge-time step owned by the merge lane. This is a **collected red**, not a
stop.

## The single next action (for whoever picks this branch up)

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wscan
bash tools/local-ci.sh --build-dir build --jobs 2      # 148/149, the one red being the snapshot above
```

## VERBATIM replacement row text for rows 46 and 54

`docs/FEATURE-LIST-0.3.0.md` lives on `030/audit` and this lane **did not** edit it. The text below
is what replaces the two rows in section 10 ("Plugin hosting") of that file, in the table's own
column order (`| # | Feature | Command group / ids | Status | List it comes from |`):

| 46 | Plugin scan cache and quarantine | `plugin.*` scan group, 6 ids: `plugin.scan_cache_get_state`, `plugin.scan_cache_list`, `plugin.scan_cache_lookup`, `plugin.scan_cache_quarantine_add`, `plugin.scan_cache_quarantine_remove`, `plugin.rescan` | **in the tree** — engine `include/PluginScanCache.h` + `PluginFactory`'s scan, proof `PluginScanCacheTest` (extended with the two enumeration cases the group needed); the group is registered and proven by `ControlPluginScanCommands` — the cache FILE is read back off disk as well as off the wire, the recorded inverse restores a quarantined entry WITH its reason (and the negative control measures the loss a path-only inverse would cause), and a quarantined plugin really leaves `plugin.list` after `plugin.rescan`. The hand-edit route the audit named is retired: the two quarantine verbs write the list through the engine's own API. Stated limits: the cache's CONTENTS became reachable only with this group (`PluginScanCache::records()` / `record(path)` are new and read-only); nothing in `src/gui/` shows a scan record, a cache hit or a quarantine entry, nor offers to add one; a missing, corrupt or wrongly-versioned cache file still means a full scan, by the engine's own contract | audit Table B #9 |

| 54 | Crash reporter | `crash.*`, 4 ids: `crash.list_reports`, `crash.acknowledge_report`, `crash.discard_report`, `crash.upload_report` | **in the tree** — engine `include/CrashReporter.h`, proof `CrashReporterTest`; the group is registered and proven by `ControlCrashReporter` — the report and the `offered` sentinel are measured ON DISK as well as on the wire, and both writers are asserted to make `control.undo` fail typed `irreversible` with a named fallback. Stated limits: `crash.upload_report` is a REFUSAL by name (this build has no upload and no network code of any kind in the module); `crash.acknowledge_report` / `crash.discard_report` are `irreversible` with named fallbacks (nothing un-writes the sentinel, nothing writes a report from a caller's bytes); there is no `crash.enable` / `crash.disable` (`main()` installs the reporter before the socket exists and the module has no uninstall); nothing in `src/gui/` shows a report, its state or its directory, and there is no way to send one | audit Table B #10 |

And the two rows of **Table B** (the "not drivable" table, `| # | Feature (tree anchor) | Its tests | Why it is not drivable | Row above |`) become:

| 9 | Plugin scan cache + quarantine | `PluginScanCacheTest` | drivable through `--control-socket` with the `plugin.*` scan group (6 ids: the three reads, the two quarantine writes and `plugin.rescan`); the JSON hand-edit route is retired — `tests/control-plugin-scan-commands.py`. What is absent is the INTERFACE: no scan record, cache hit or quarantine entry is shown, and no view offers to add one | 46 |

| 10 | Crash reporter | `CrashReporterTest` | drivable through `--control-socket` with `crash.list_reports` and the module's two real operations (`crash.acknowledge_report`, `crash.discard_report`, both `irreversible` with named fallbacks); `crash.upload_report` is a typed REFUSAL because the module has no network code — `tests/control-crash-reporter.py`. What is absent is the INTERFACE: no report, state or directory is shown, and there is no way to send one | 54 |

## The limits lines this lane wrote (verbatim)

`docs/KNOWN-LIMITATIONS.md` (in "Where the quality bars are not met yet", both dated 2026-09-14):

- **The plugin scan cache and its quarantine list are drivable, and there is no interface for either — added 2026-09-14.** … `plugin.scan_cache_get_state` / `plugin.scan_cache_list` / `plugin.scan_cache_lookup` … **operable** with `plugin.scan_cache_quarantine_add` / `plugin.scan_cache_quarantine_remove` — with `plugin.rescan` to apply an edit — but **nothing in `src/gui/` shows a scan record, a cache hit or a quarantine entry, and no view offers to add one** … (the full paragraph is in the file; the sentences above are its load-bearing ones)
- **The crash reporter is drivable, and there is no way to see or send a report from the interface — added 2026-09-14.** … `crash.list_reports` … `crash.acknowledge_report` … `crash.discard_report` … **nothing in `src/gui/` shows a report, its state or its directory**, and **there is no way to send one**: `crash.upload_report` is registered and REFUSES every call by name …

`docs/RELEASE-NOTES-v0.3.0-alpha.md` carries the same two sentences in its
"The plugin scan cache, the quarantine list and the crash reporter (rows 46 and 54)" section, and
the histogram figures this lane measured.

## Merge notes for the parent

* **The A16 histogram is a union-by-id merge point.** This lane moved
  `documentedHistogram()` from `{208, 116, 16, 4, 72}` to `{218, 116, 18, 7, 77}` and the same
  figures in `docs/RELEASE-NOTES-v0.3.0-alpha.md` (220 / 116 / 18 / 7 / 79 for this configuration).
  If another lane also added rows, take the UNION and re-measure — do not side-pick either number.
* **`reversibilityRowTable()`'s join list** gained `reversibilityScanAndCrashRowTable` on one
  existing line (the file is at 499 of the 500 lines gate 7 allows; it must not grow).
* **The snapshot is the merge lane's step** (see the expected red above).
* This lane did **not** touch `docs/FEATURE-LIST-0.3.0.md`, `tools/mcp-zene-control/**`,
  `release/0.3.0`, the `zene-030` worktree, or any `zene-021*` / `zene-pa-*` worktree.
