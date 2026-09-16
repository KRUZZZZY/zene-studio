# FIX-UP LANE 030/fixup-clap (CLAP dev-id + ClapHost.cpp split) — 2026-09-16

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wfixclap`,
branch **`030/fixup-clap`** (@ `49a40b30c` when this lane started). **Nothing was pushed.** One
build dir in the lane's own tree: `build/` (1.7 GB, kept so the proofs below can be re-run;
`rm -rf build` is safe).

| # | commit | subject |
|---|---|---|
| 1 | `e504c88cd` | `fix(clap): the catalogue publishes id = dev-<n>, the plug-in's own id as clap_id` |
| 2 | `c231c157a` | `refactor(clap): split the CLAP host into core, lifecycle, notes and params TUs` |
| 3 | HEAD at handover | `docs(030w10): the fix-up lane's report` — this file |

## 1 · The CLAP dev-id defect (the one new ctest red wave 10 introduced)

**Cause** (as the train diagnosed it): `controlDeviceJson()` sets `id = dev-<n>` — the only id
`plugin.load` accepts, `ControlDeviceCatalogue.cpp:150` — and the CLAP branch then ran
`out.insert("id", entry.name)`, overwriting it with the plug-in's own CLAP id. So
`plugin.list format=clap` published an id no client could load and never published the catalogue
id; every other format kept its `dev-<n>`.

**Fix applied (the train's option 1).** `src/core/ControlDeviceCatalogue.cpp`: the CLAP branch now
inserts `file` (module path) and **`clap_id` = the plug-in's own CLAP id**, and leaves `id` as the
catalogue's `dev-<n>` — the same shape the LADSPA `label`, LV2 `uri` and VST3 `class` entries
already have (each has its own key for the plug-in's own identity inside the module).

* `tests/src/core/ControlDeviceCatalogueTest.cpp`, case `clapInstrumentListsAndLoadsThroughTheSurface`:
  it read `id`, asserted `id == name` (it was written against the collision) and handed that value
  to `plugin.load`. It now asserts the entry's `id` **starts with `dev-`** (the regression guard:
  the published id must be one `plugin.load` takes), asserts `clap_id == name` (the `(file, clap_id)`
  pair the host's `load()` keys on) and loads with the catalogue id.
* `docs/RELEASE-NOTES-v0.3.0-alpha.md`, CLAP section: the socket-shape sentence now names the keys
  (`id` = the catalogue `dev-<n>`; `file` = module path; `clap_id` = the plug-in's own CLAP id).

**PROOF — the first green run of the catalogue test** (unpiped, `cd build/tests`):

```
$ QT_QPA_PLATFORM=offscreen ./ControlDeviceCatalogueTest > /tmp/fixclap-catalogue.log 2>&1; echo CATALOGUE_EXIT=$?
CATALOGUE_EXIT=0
QINFO  : …::clapInstrumentListsAndLoadsThroughTheSurface() plugin.list format=clap: dev-1 ->
         plugin.load on trk-3 -> …/build/tests/data/clap-test-plugin/clap-test-instrument.clap;
         plugin.host_notes ports=1 audio=0/2
PASS   : ControlDeviceCatalogueTest::clapInstrumentListsAndLoadsThroughTheSurface()
Totals: 8 passed, 0 failed, 2 skipped, 0 blacklisted, 1457ms
```

`0 failed` where the train measured `9 passed, 1 failed`; the CLAP slot is the one that was red and
is now green, and the entry it drove is `dev-1` — the catalogue id — through `plugin.load` onto an
instrument track, with `plugin.host_notes` reporting the note port and the 0-in / 2-out audio layout.
The 2 skips are this build's configuration, not failures: `lv2EntriesCarryTheirUri` (no `lv2effect`
module in the target set built here) and `vst3ClassesListAndLoadThroughTheSurface` (configured
`-DWANT_VST3=OFF`); both are the test's own documented `QSKIP`s and neither branch is touched by the
CLAP-branch change.

```
$ cd build/tests && ctest -R 'ClapHostTest|ControlDeviceCatalogueTest'; echo CTEST_EXIT=$?
    Start  30: ControlDeviceCatalogueTest .......   Passed    1.45 sec
    Start 145: ClapHostTest .....................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 2
CTEST_EXIT=0
$ cd build/tests && ctest -R 'Clap'; echo CTEST_CLAP_EXIT=$?     # the whole CLAP family
144 ClapBusMapTest · 145 ClapHostTest · 146 ClapLoaderErrorTest · 147 ClapEffectIntegrationTest
100% tests passed, 0 tests failed out of 4
CTEST_CLAP_EXIT=0
```

## 2 · The split of `plugins/ClapEffect/ClapHost.cpp` (1158 lines)

The file was 953 → 1158 (wave-10 CLAP instrument work), a live gate-7 regression. It is now
**470 lines** — at or below the 500-line target — with four new fork-NEW files beside it:

| file | lines | what it owns |
|---|---|---|
| `ClapHost.cpp` | **470** (was 1158) | the host core: thread answers, `describe()`, construction, the accessors, the transport/re-prepare flags, `load()`/`unload()`, `process()`/`runChunk()`, the chunking stats |
| `ClapHostInternals.h` | 346 | `EventListState`, `HostedPlugin::Impl`, the `clap.thread-check` thread answers (`extern thread_local`), and the shared `setError()` / `normalize()` (`inline`) |
| `ClapHostNotes.cpp` | 235 | the note path: `clap.note-ports` discovery, the bounded queue, the drain into the input event list |
| `ClapHostParams.cpp` | 155 | the parameter surface and the state blob (`clap.params`, `clap.state`) |
| `ClapHostLifecycle.cpp` | 151 | the main-thread ladder around `process()`: `prepare()`/`release()` and the module scan (`listClasses()`) |

The code is **relocated verbatim** (the split was generated by slicing the original at function
boundaries, then checked: all 43 function definitions of the old file are present **exactly once**
across the five files). Every target that compiles the host gained the new TUs in the same commit
(`plugins/ClapEffect/CMakeLists.txt`, `plugins/ClapInstrument/CMakeLists.txt`, and the three test
targets in `tests/CMakeLists.txt`); the four new sources are declared in `tests/fork-sources.txt`
and `tests/all-sources.txt`, and the edit declarations are in `tests/upstream-modifications.txt`
(all in the same commit, file ends with a newline).

**`load()`, `process()`/`runChunk()` and `describe()` deliberately stay in `ClapHost.cpp`**: they are
the only functions of the old file that exceed CCN 10, and `tests/complexity-baseline.tsv` keys them
by `function@path` — moving one of them would have added a gate-4 line. The split therefore adds
**no** gate-4 line (measured, below).

**PROOF — the build and the gate check** (unpiped):

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_WERROR=ON \
        -DUSE_COMPILE_CACHE=ON -DWANT_CLAP=ON -DWANT_VST3=OFF \
        -DLMMS_CLAP_PATH=…/lmms-fl-research/vendor/clap-195b42a0     # CONFIGURE_EXIT=0
$ cmake --build build -j4 --target zene clapeffect clapinstrument clap-test-gain \
        clap-test-instrument ClapHostTest ControlDeviceCatalogueTest
  Built target zene · clapeffect · clapinstrument · clap-test-gain · clap-test-instrument ·
              ClapHostTest · ControlDeviceCatalogueTest            # BUILD_EXIT=0 (USE_WERROR=ON)
$ cmake --build build -j4 --target ClapBusMapTest ClapLoaderErrorTest ClapEffectIntegrationTest
                                                                   # BUILD_EXIT=0 — every target
                                                                   # that compiles the host is green
$ bash tests/file-length-gate.sh --check; echo GATE7_EXIT=$?
file-length-gate: 646 fork-scope sources measured; 20 exceed 500 lines
improved: plugins/ClapEffect/ClapHost.cpp is now at or below 500 lines
GATE7_EXIT=1        # FAIL is the gate's PRE-EXISTING red set (11 inherited REGRESSION lines naming
                    # other lanes' paths — include/ControlRe*.h, Vst3Host.cpp 800→891,
                    # ControlCommands*.cpp, tests/*.py …); none names a path this lane touched.
                    # The count of files over 500 dropped 21 → 20 (the train's number → this run's).
```

The note path is not just linked but still working after the split — `./ClapHostTest`:
`Totals: 14 passed, 0 failed, 0 skipped`, including `testInstrumentNotePath` *"256-frame request
into a 64-frame block: 4 plug-in calls, 1 note(s) delivered, audio level 0.250000"* — the same
measurement the train recorded before the split.

## 3 · The gate picture at `c231c157a` (fork scope — the scope CI's static-gates job runs)

| gate | exit | what it says here |
|---|---|---|
| 3 no-tautology | **0** | `PASS: every registered test file has test slots and real assertions` |
| 4 complexity | 1 | **57 REGRESSION lines — the train's own 57, none naming a path this lane touched** (verified: `grep '^REGRESSION' … | grep -i clap` → nothing). Moving `load`/`describe` out would have added 2 lines; they stay, so this gate is unchanged, not re-anchored |
| 6 upstream-regression | **0** | `PASS … (422 changed path(s) declared; the ledger holds 460 entries)` |
| 7 file-length | 1 | `646 fork-scope sources measured; 20 exceed 500 lines` (was 642/21); `improved: plugins/ClapEffect/ClapHost.cpp is now at or below 500 lines`. The 11 REGRESSION lines name other lanes' paths only |
| 8 duplication | **0** | `PASS: duplicated lines 0.53% (budget 5%)` |
| 9 fork-sources + all-sources reproduce | **0** | `PASS: every tracked source in scope is registered (647 fork-NEW, 1104 inherited, 40 tooling, 0 stale)`; `REPRODUCES: the entry list in all-sources.txt is the recipe's own output` |
| 10 unregistered-tests | **0** | `PASS: every test source under tests/src/ is registered` (169 scanned) |
| 11 evidence | **0** | `PASS: no committed evidence file types and nothing over the cap` (6640 files) |
| 12 rt-safety | **0** | `PASS: every hit on a declared audio-thread path is allowlisted with a reason and at its allowed count` (the audio-thread path `process()`/`runChunk()` stayed in `ClapHost.cpp`; the relocation is verbatim) |
| 1 ctest | not run in full | the CLAP family and the catalogue test are green (above); the full suite is the parent's re-check |
| 2 coverage / 5 mutation | not run | need `--with-coverage` / the mutation sweep |

## 4 · What this lane could NOT verify

1. **The VST3 and LV2 catalogue cases did not run here** (`QSKIP`): the VST3 host is off in this
   lane's configuration (`-DWANT_VST3=OFF`, no SDK path passed) and the `lv2effect` module is not in
   the target set this lane built. Neither branch is touched by the CLAP-branch change; the parent's
   full-configuration run is where those two slots must be seen to pass.
2. **The full `ctest` suite and gates 1/2/5** were not run (the lane's build covers the core plus the
   CLAP modules and tests, not every module).
3. **The file-length baseline still carries the stale `plugins/ClapEffect/ClapHost.cpp 953` entry**
   (in `tests/file-length-baseline.tsv` and `-all.tsv`). `--check` never writes (`improved:` is
   reported), and the gate's own convention is that the ratchet-down is recorded by a *ratchet-mode*
   run — which also rewrites every other path's entry, so this lane deliberately did **not** run it
   (that would grandfather the other lanes' unreviewed over-500 files). No re-anchor was taken.
4. **`-DWANT_WASM`/`-DWANT_VST3` and the CI's exact `CMAKE_OPTS`** were not reproduced (leaner local
   configuration, stated above); `tools/local-ci.sh` is the way to reproduce CI byte for byte.

## 5 · Hotspots

* `hotspot: tests/CMakeLists.txt` — its `tests/CMakeLists.txt` ledger line in
  `tests/upstream-modifications.txt` grew again (the split's three test targets); the file is over
  500 lines and every CLAP lane edits it.
* `hotspot: plugins/ClapEffect/ClapHostLifecycle.cpp` (151) and `ClapHostParams.cpp` (155) — small
  now, but `ClapHost.cpp` is at 470/500: **28 lines of headroom** before it re-crosses the target.
* `hotspot: docs/RELEASE-NOTES-v0.3.0-alpha.md` — the CLAP section is edited by every CLAP lane and
  carries the A16 histogram block the merge tip must re-measure.

## 6 · The single next action

Re-run the train's §5 command on the merged tip with the release configuration
(`-DWANT_VST3=ON -DWANT_VST3_TEST_INSTRUMENT=ON -DWANT_CLAP=ON`): `cd build/tests &&
./ControlDeviceCatalogueTest` — expect **10 passed, 0 failed** (both skips above are fixtures of
this lane's leaner configuration only), then `ctest -R 'ClapHostTest|ControlDeviceCatalogueTest'`.


