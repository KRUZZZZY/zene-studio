FIXED

# Cluster new-p17-202-port: Watsyn oscillator parameter caches race with playback

**Verdict: FIXED** (ported by hand from the accepted `bugs/qwen-new-p17-202` line,
`e48a86fd0`..`9990bf3dc`; no merge, no cherry-pick, no rebase).

The finding is REAL on this branch. Watsyn kept its four oscillator gains and four
oscillator frequencies in plain `float` arrays: the editing thread (the `updateVolumes()`
/ `updateFreqA*()` slots) wrote them and the audio thread read them per frame from
`WatsynVoice::renderOutput()` with no synchronisation whatsoever.

The port job's stated reason for the re-do - "fails `PluginPortsMigrationTest` on
`050/m0-train-13`" - does not reproduce: the merge of this fix on train-13
(`a10f9bc5d`) ran the test and the Watsyn rows passed. The one failure in that log is
`migratedPluginsPreserveBehaviour()` on **audiofileprocessor**, a plugin this fix does
not touch. The re-application is done anyway, as instructed.

---

## Decision: REAL

Pre-fix evidence on this branch (`HEAD` = `07d6d02bd`):

- `plugins/Watsyn/Watsyn.h:169-174` - the plain caches:

```cpp
protected:
	float m_lvol [NUM_OSCS];
    float m_rvol [NUM_OSCS];

	float m_lfreq [NUM_OSCS];
	float m_rfreq [NUM_OSCS];
```

- `plugins/Watsyn/Watsyn.cpp:618-662` - the editing thread writes them, with no
  atomic, no lock and no publication step:
  `m_lvol[A1_OSC] = leftCh( a1_vol.value(), a1_pan.value() );` ... `m_rfreq[B2_OSC] = ...`.
  These slots are wired to the models' `dataChanged()` signals in the constructor
  (`plugins/Watsyn/Watsyn.cpp:407-431`), i.e. they run when the user moves a knob.
- `plugins/Watsyn/Watsyn.cpp:129-137`, `147-157`, `163-173`, `190-200` and `269-272` -
  the audio thread reads `m_parent->m_lvol[...]`, `m_parent->m_rvol[...]`,
  `m_parent->m_lfreq[...]`, `m_parent->m_rfreq[...]` per rendered frame, from
  `WatsynVoice::renderOutput()`, which `playNoteImpl()` calls on the audio thread
  (`plugins/Watsyn/Watsyn.cpp:475`).

So a volume, pan or tuning change while a note sounds is a C++ data race (plain writes
concurrent with plain reads) and the reader can observe a torn value, or a set that mixes
two edits (left gain from one volume, right gain from another). That is exactly what the
finding describes, and the intended contract - "publish parameter updates through a
real-time-safe snapshot mechanism" - is not met by the pre-fix code.

## The fix (at the cause)

The accepted fix, re-applied verbatim and checked against the siblings/callers:

- **New** `plugins/Watsyn/WatsynParamHandoff.h` - a real-time-safe publication of one
  immutable `Values` set (4 gains + 4 frequencies). The editing thread fills a private
  set and publishes it with a `seq_cst` pointer exchange
  (`plugins/Watsyn/WatsynParamHandoff.h:204`); the audio thread takes the published set
  with a `seq_cst` load (`:230`) inside a claim window (`:225`/`:243`). Retired sets are
  only recycled when no reader is inside that window (`:307`) and no holder is left
  (`:311`). `read()` never allocates, never locks and never waits. The claim-window hook
  is compiled out of the shipped plugin (`#ifdef LMMS_TESTING`, `:235-240`, `:341-345`).
- `plugins/Watsyn/Watsyn.h:34` includes the hand-off; `:176` replaces the four plain
  arrays with `WatsynParamHandoff<NUM_OSCS> m_paramHandoff`; `:184`/`:186` declare
  `computeParams()` / `publishParams()`; `leftCh()`/`rightCh()` become `const`.
- `plugins/Watsyn/Watsyn.cpp:112` - the audio-thread render takes the published set
  **once per rendered period**; `:139`..`:278` read `params.lvol` / `params.rvol` /
  `params.lfreq` / `params.rfreq` instead of the plain arrays.
- `plugins/Watsyn/Watsyn.cpp:627` `computeParams()` reads every model value **once** and
  computes the whole set from those readings, so an oscillator's left and right gain come
  from one volume/pan pair; `:682` `publishParams()` publishes that set.
- `plugins/Watsyn/Watsyn.cpp:693-717` - every one of the five editing slots
  (`updateVolumes`, `updateFreqA1/A2/B1/B2`) now calls `publishParams()`. No caller of the
  old writes remains: `grep -rn 'm_lvol\|m_rvol\|m_lfreq\|m_rfreq'` over `plugins/` and
  `src/` returns only `tests/reference/Watsyn/Watsyn.cpp` (the frozen pre-migration copy
  used for A/B comparison, deliberately untouched).

The hand-off is the same protocol as the wavetable snapshot beside it
(`WatsynWave::claimWave` / `WatsynInstrument::recycleWaves`,
`plugins/Watsyn/Watsyn.cpp:315-350`), so the two published objects stay consistent.

### Why this is not the rejected round-1 shape

Review round 1 rejected a version whose exhausted-retry path could hand out a mixture of
two publications. The accepted, re-applied protocol publishes a pointer to an **immutable**
set, so there is no field-by-field retry to exhaust and no mixed set to hand out; the
regression `WatsynParamHandoffTest::anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive`
scripts the interleaving through the claim-window seam and proves the set a reader observed
is not recycled under it.

## The regression tests

Both live where the area's tests already are.

1. `tests/src/plugins/PluginPortsMigrationTest.cpp:3329`
   `watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff()` - reads the
   plugin's own sources (`WATSYN_PLUGIN_SOURCE_DIR`, `tests/CMakeLists.txt:3507`) and
   asserts: the four plain caches are gone from `Watsyn.h`; `WatsynParamHandoff` /
   `m_paramHandoff` / `publishParams` are present; the `renderOutput()` body contains none
   of the plain caches and does take `m_paramHandoff`; neither the render path nor the
   voice constructor recomputes from the models (`computeParams` appears exactly twice);
   the published pointer's load and exchange are `seq_cst`; the claim window is ordered
   announce-before-observe, observe-before-pin, pin-before-leave; `recycle()` refuses
   while the claimer count is non-zero; and every `m_claimHook` line sits inside
   `#ifdef LMMS_TESTING`.
2. `tests/src/plugins/PluginPortsMigrationTest.cpp:3448`
   `watsynAppliesOscillatorParameterChangesToASoundingNote()` - loads the real `watsyn`
   module, starts a note, renders a period, sets all four volumes to `0` through the
   instrument's own state, renders again (must be silent), restores them and renders a
   third period (must sound again). This is the user-visible path of the finding: a
   parameter change while a note is playing.
3. **New** `tests/src/plugins/WatsynParamHandoffTest.cpp` (4 slots, 6 cases) - the
   hand-off's own behaviour, including the two-thread claim-window case, scripted with a
   barrier/condition variable and the test seam (no clock, no sleep).

`PluginPortsMigrationTest` is the existing file for this area (the finding's own "tests
that already cover this file" list); the hand-off unit test is a new fork-NEW file,
registered in `tests/CMakeLists.txt:606-609`, `tests/fork-sources.txt`,
`tests/all-sources.txt` and `tests/rt-safety-scope.txt`.

## Evidence table

All log paths below are relative to the worktree's `logs/` directory. The brief's log
folder was tried first and **every write there is denied by this round's lane** (see
"Log-folder denial" below), so the logs are new numbered files under `logs/` here.

| Check | Exact command and result |
| --- | --- |
| Baseline before any change | `cmake --build build --target PluginPortsMigrationTest -j20 > logs/build-1-baseline.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest > logs/test-1-baseline-ppm.log 2>&1` **EXIT=1** - `Totals: 72 passed, 1 failed, 2 skipped`; the one failure is `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"), unrelated and pre-existing. |
| RED - regression added, fix absent | `cmake --build build --target PluginPortsMigrationTest -j20 > logs/build-2-red-tests.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest > logs/test-2-red-ppm.log 2>&1` **EXIT=2** - `FAIL! : ...watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff() '!header.contains( cache )' returned FALSE. (Watsyn.h still declares the plain parameter cache m_lvol)`; `Totals: 74 passed, 2 failed`. The new unit test target did not exist yet: `cmake --build build --target WatsynParamHandoffTest > logs/build-2b-red-handoff.log 2>&1` **EXIT=2** - `fatal error: ../../../plugins/Watsyn/WatsynParamHandoff.h: No such file or directory`. |
| GREEN - fix applied | `cmake --build build --target zene watsyn PluginPortsMigrationTest WatsynParamHandoffTest WatsynSelectedGraphTest -j20 > logs/build-3-green.log 2>&1` **EXIT=0** (0 `error:` lines); `./PluginPortsMigrationTest > logs/test-3-green-ppm.log 2>&1` **EXIT=1** - `Totals: 75 passed, 1 failed, 2 skipped`, the only failure being the same pre-existing LADSPA fixture; both new Watsyn rows PASS (`logs/test-3-green-ppm.log:181-182`). `./WatsynParamHandoffTest > logs/test-3-green-handoff.log 2>&1` **EXIT=0** - `Totals: 6 passed, 0 failed`. `./WatsynSelectedGraphTest > logs/test-3-green-selgraph.log 2>&1` **EXIT=0** - `Totals: 4 passed, 0 failed`. |
| 4b - fix reversed (must FAIL) | `git diff -- plugins/Watsyn/Watsyn.cpp plugins/Watsyn/Watsyn.h plugins/Watsyn/WatsynParamHandoff.h > logs/fix.patch` (3 product files, **EXIT=0**); `git apply -R logs/fix.patch` **EXIT=0**; `cmake --build build --target zene watsyn PluginPortsMigrationTest -j20 > logs/build-4-reverted.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest > logs/test-4-reverted-ppm.log 2>&1` **EXIT=2** - `FAIL! : ...watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff() '!header.contains( cache )' returned FALSE. (Watsyn.h still declares the plain parameter cache m_lvol)`, `Totals: 74 passed, 2 failed`. Rebuilding the unit test with the fix reverted is also RED: `cmake --build build --target WatsynParamHandoffTest > logs/build-4b-reverted-handoff.log 2>&1` **EXIT=2** (missing header). |
| 4b - fix restored (must PASS) | `git apply logs/fix.patch` **EXIT=0**; `cmake --build build --target zene watsyn PluginPortsMigrationTest WatsynParamHandoffTest WatsynSelectedGraphTest -j20 > logs/build-5-restored.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest > logs/test-5-restored-ppm.log 2>&1` **EXIT=1** - `Totals: 75 passed, 1 failed, 2 skipped`, with `watsynPublishes...` and `watsynApplies...` PASS (`:181-182`); `./WatsynParamHandoffTest > logs/test-5-restored-handoff.log 2>&1` **EXIT=0** - `Totals: 6 passed, 0 failed`. |
| Affected family | `cmake --build build --target WatsynSelectedGraphTest -j20 > logs/build-5-restored.log 2>&1` (same command as above) **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'Watsyn\|PluginPortsMigration' --output-on-failure > logs/ctest-3-family-final.log 2>&1` **EXIT=8** - **3 tests run**: `WatsynSelectedGraphTest` Passed, `WatsynParamHandoffTest` Passed, `PluginPortsMigrationTest` Failed only on the same pre-existing `nonRealtimeLadspaEffectsAreInvalid` SWH-Decimator fixture. 0 tests run would have been an error; 3 ran. |
| Gates (real set, log dir redirected) | `python3 logs/run_feature_check.py $PWD > logs/feature-check-redirected.log 2>&1` **EXIT=0** - `RESULT: PASS`, all eight rows: `fork-sources` exit 0, `all-sources-reproduce` exit 0, `no-upstream-regression` exit 0, `check-namespace` exit 0, `no-tautology` exit 0, `doc-figures` exit 0, `complexity` "no new item in your files", `file-length` "no new item in your files". |
| Gates (unmodified, for the record) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD > logs/gates/feature-check-direct.log 2>&1` **EXIT=1** - `PermissionError: [Errno 13] Permission denied: '<worktree>-feature-check'`, before any gate runs. |
| Gate 9 by hand | `bash tests/fork-sources-gate.sh > logs/gates/fork-sources.log 2>&1` **EXIT=0** - `PASS: every tracked source in scope is registered (1979 fork-NEW, 1214 inherited, 48 tooling)`; `bash tests/all-sources-reproduce.sh > logs/gates/all-sources-reproduce.log 2>&1` **EXIT=0** - `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| Real-time safety (whole-tree sweep) | `python3 tests/rt-safety-sweep.py --check > logs/gates/rt-safety.log 2>&1` **EXIT=0** - `PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count`; `WatsynParamHandoff::read` is declared in `tests/rt-safety-scope.txt` and measured. |
| Ratchets, direct | `bash tests/complexity-gate.sh --check > logs/gates/complexity.log 2>&1` **EXIT=1** (7 REGRESSION lines, none in a file this branch changed); `bash tests/file-length-gate.sh --check > logs/gates/file-length.log 2>&1` **EXIT=1** (19 REGRESSION lines, none in a file this branch changed). Both are red on the base train on purpose. |
| Report / status | `git add <explicit paths>` then `git commit`; `git status --porcelain` empty. |

## Log-folder denial

The brief's log folder
(`~/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r1/logs`) was tried first and
**every write there is denied by this round's lane**:

```
$ touch ~/.local/share/zene-bunny-fix/qwen/runs/new-p17-202-port.r1/logs/probe.log
touch: cannot touch '.../logs/probe.log': Permission denied     # EXIT=1
$ python3 -c "open('.../logs/probe-python.log','w')"
PermissionError: [Errno 13] Permission denied                   # EXIT=1
$ touch <worktree>/../probe
touch: cannot touch '<worktree>/../probe': Permission denied    # EXIT=1
```

So every build/test log named above is a new numbered file in the worktree's `logs/`
directory (`.gitignore:3 /logs/`, outside git), and the gate logs are under `logs/gates/`.
Nothing in this report claims a file was written to the denied path.

The same denial hits `feature_check.py`, whose log directory is `<worktree>-feature-check`
(a sibling of the worktree). `logs/run_feature_check.py` re-uses that script's own
`SIMPLE`/`RATCHET` command lists, its `base_listings()` cache and its exact comparison
rule; only the per-gate log directory changes. No gate command, limit or baseline was
touched.

## Manifest repair (a base-train defect this branch had to fix to make Gate 9 green)

`tests/all-sources.txt` publishes the command that generates it ("Regenerate with") and
`tests/all-sources-reproduce.sh` runs that command on every gate run. The base commit
`07d6d02bd` ("chore(train-12): ... all-sources re-derived") added the 1148 vendored
`plugins/SfzPlayer/sfizz/...` paths this manifest's own recipe excludes, so from that
commit on the manifest could not reproduce and Gate 9 was red on the base:

```
git show 07d6d02bd^:tests/all-sources.txt | grep -c 'SfzPlayer/sfizz'   # 4
git show 07d6d02bd:tests/all-sources.txt  | grep -c 'SfzPlayer/sfizz'   # 1152
git show HEAD:tests/all-sources.txt       | grep -c 'SfzPlayer/sfizz'   # 1152
```

The repair is the recipe's own output - exactly what the gate's message asks for - and the
only non-vendored entries that change are this branch's two new files:

```
diff <(git show HEAD:tests/all-sources.txt | grep -vE '^[[:space:]]*(#|$)') \
     <(grep -vE '^[[:space:]]*(#|$)' tests/all-sources.txt) | grep '^[<>]' | grep -v SfzPlayer/sfizz
# > plugins/Watsyn/WatsynParamHandoff.h
# > tests/src/plugins/WatsynParamHandoffTest.cpp
```

Nothing was hand-edited and no path the recipe derives was dropped. The re-derivation is
recorded in `tests/upstream-modifications.txt`.

## Collateral damage / siblings

- **Old projects and saved data.** No model, XML attribute or save/load path changed: the
  fix only replaces the in-memory transport of values that were already derived from the
  models. `saveSettings()`/`loadSettings()` are untouched (the `selgraph` lines stay as
  this branch had them; the diff contains no `selgraph` hunk).
- **Behaviour outside the bug.** Pre-fix and post-fix renders of a Watsyn note are
  bit-identical: `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()` reports
  `watsyn ... max|delta|=0` against the pre-migration reference (`logs/test-3-green-ppm.log:147`),
  exactly as on the baseline (`logs/test-1-baseline-ppm.log:147`).
- **Siblings of the changed values.** Every writer of the four arrays was an editing slot;
  all five now publish the whole set, so a left gain and its right gain always come from
  one reading of the models. Every reader was `WatsynVoice::renderOutput()`; it now takes
  the published set once per period. There is no other reader or writer in the product
  tree.
- **No new limit or check** was added that another caller could exceed; the hand-off
  allocates only on the editing thread (first publication) and reuses retired sets.
- **Realtime.** `read()` does one `seq_cst` claim increment, one `seq_cst` load, one
  relaxed pin, one `seq_cst` claim decrement, a plain struct copy and one release decrement
  - no allocation, no lock, no wait, no logging, no Qt call. The whole-tree sweep passes
  with `WatsynParamHandoff::read` declared.
- **Undo / commands.** No command was added or changed; no undo step is created. This is a
  rendering-internal fix.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** Yes. `git diff -- <three product files> > logs/fix.patch` (EXIT=0),
  `git apply -R logs/fix.patch` (EXIT=0), rebuild (EXIT=0), test **EXIT=2** with
  `Watsyn.h still declares the plain parameter cache m_lvol`; `git apply logs/fix.patch`
  (EXIT=0), rebuild (EXIT=0), test **EXIT=1** where the only failure is the pre-existing
  LADSPA fixture and both new Watsyn rows PASS. The new unit test target is a compile-time
  RED without the header (EXIT=2) and 6/6 GREEN with it (EXIT=0).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The source-shape
  regression reads files and compares strings - no threads at all. The behaviour
  regression drives one note on one thread through `playNote`/`restoreState` and compares
  rendered buffers. The hand-off's cross-thread case scripts the interleaving with
  `std::condition_variable` handshakes through `WatsynParamHandoff::setClaimHook` (the
  reader pauses inside the claim window; the publisher retires and tries to recycle, then
  releases the reader); the publication-window cases use `setPublishHook`. No sleep, no
  clock, no unscripted race.
- **The fix is at the cause and complete.** Every writer (five editing slots), every
  reader (the one render path) and every sibling of the plain arrays was checked; the
  container change is declared in `Watsyn.h` and defined in `Watsyn.cpp` in the same
  commit, and every target that includes the header (`zene`, `watsyn`,
  `PluginPortsMigrationTest`, `WatsynParamHandoffTest`, `WatsynSelectedGraphTest`) was
  rebuilt.
- **No collateral damage.** Old projects load and render as before: no serialisation
  changed, and the migrated-vs-reference A/B render of Watsyn is `max|delta|=0`. No limit
  or check was added.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE**, so no evidence for those is
  needed; the pre-fix code above is the evidence the bug was present.
