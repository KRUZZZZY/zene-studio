# Cluster new-p10-199-port: the bundled LADSPA search path

**Verdict: FIXED**

The finding (`src/core/LadspaManager.cpp:58`, the scan list of the constructor) is real: a LADSPA module that sits in
`plugins/ladspa` **relative to the working directory** was never scanned, because the
bundled entry of the scan list was the single string `"plugins:ladspa"`. The port writes
the fix the way this line can carry it: the working-directory location is **added** as a
second bundled entry, and the existing `"plugins:ladspa"` entry stays.

## Why the port is additive and not the accepted branch's replacement

`"plugins:ladspa"` is not a typo for `"plugins/ladspa"`: it is Qt's `QDir` search-path
syntax, and it is **load-bearing**. `PluginFactory::setupSearchPaths()`
(`src/core/PluginFactory.cpp:283`) registers the prefix `plugins` for the plugin
directory relative to the application (`src/core/PluginFactory.cpp:305`), and
`QDir("plugins:ladspa")` then resolves to `<application dir>/plugins/ladspa` - the
directory the bundled LADSPA modules are built into
(`plugins/LadspaEffect/CMakeLists.txt:5` sets `CMAKE_LIBRARY_OUTPUT_DIRECTORY` to
`.../plugins/ladspa`; this build has `build/plugins/ladspa/cmt.so`,
`foverdrive_1196.so`, `amp_1181.so`, `decimator_1202.so`).

Replacing the entry with the working-directory-relative `"plugins/ladspa"` therefore
**loses** the bundled modules, exactly as the merge train reported. Measured on this tree
with the accepted branch's one-line change:

| Build | Root84 fixture load | `ControlGoldenAudio` |
| --- | --- | --- |
| accepted branch's change (`"plugins:ladspa"` -> `"plugins/ladspa"`) | `loaded_with_errors: true`, `error_count: 4` ("Unknown LADSPA plugin freeverb3 / amp_stereo / foverdrive / fbdelay_1s requested") | **FAILED** - "the bundled fixture opens with no load errors ... errors=True count=4" |
| this port (both entries) | `loaded_with_errors: false`, `error_count: 0` | **Passed** |

So the port adds the missing location and keeps the one that works:

- `src/core/LadspaManager.cpp:64` - `"plugins:ladspa"` (unchanged): the LADSPA directory
  of the `plugins` search path, i.e. relative to the application.
- `src/core/LadspaManager.cpp:65` - `"plugins/ladspa"` (new): the LADSPA directory
  relative to the working directory, which is what the finding asks for.

The order keeps the existing priority: for a module file name that exists in both
locations, the application-relative one is still the one that is registered (the map key
is the file name, `src/core/LadspaManager.cpp:164`, and the first registration wins).

## The regression

`tests/src/core/LadspaManagerTest.cpp` (a fork source, `tests/fork-sources.txt:2288`):

- `:128` `bundledLadspaDirectoryIsScanned()` - the finding's case. It creates a
  temporary working directory, copies the real LADSPA fixture module
  (`tests/src/core/LadspaPortCountFixture.cpp`) into `<temp>/plugins/ladspa` under a name
  of its own, points the process at that working directory, clears `LADSPA_PATH` and
  requires `LadspaManager` to register the copied module. **RED without the fix.**
- `:152` `bundledSearchPathDirectoryIsScanned()` - the other half of the contract: the
  `plugins` search path entry is still a scan location (this is the half the accepted
  branch's replacement broke). It passes before the fix, and **fails** with the accepted
  branch's replacement.
- `tests/CMakeLists.txt:1905` gives the test the fixture module's directory at build
  time (`LADSPA_MANAGER_FIXTURE_DIR`), so the cases do not depend on the environment.

## Evidence table

| Check | Exact command (from the worktree root; ctest from `build/tests`) | Result |
| --- | --- | --- |
| RED before the fix | `ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | **EXIT=8** - `bundledLadspaDirectoryIsScanned` FAILED ("the LADSPA module in the working directory's plugins/ladspa was not scanned"), 4 passed / 1 failed |
| GREEN after the fix | same ctest command, output to `.../logs/test-13.log` | **EXIT=0** - 1/1 ctest target passed, 5/5 QtTest cases passed |
| Reversed fix (step 4b) | `git diff -- src/core/LadspaManager.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` (**EXIT=0**), rebuild (**EXIT=0**), ctest -> `.../logs/test-12.log` | **EXIT=8** - the regression fails again with the product change reverted |
| Restored fix (step 4b) | `git apply .../logs/fix.patch` (**EXIT=0**), rebuild (**EXIT=0**), ctest -> `.../logs/test-13.log` | **EXIT=0** - 1/1 ctest target passed |
| Affected family | `ctest -R 'Ladspa\|PluginPorts\|ControlGoldenAudio' --output-on-failure > .../logs/test-14.log 2>&1; rc=$?; echo EXIT=$rc` | **EXIT=0** - **5 ctest targets executed, 5/5 passed** (`LadspaChannelCountsTest`, `LadspaManagerTest`, `LadspaPortDialogTest`, `PluginPortsMigrationTest`, `ControlGoldenAudio`) |
| Merge train's reason, reproduced | the accepted branch's one-line change, rebuild, `ctest -R '^LadspaManagerTest$'` -> `.../logs/test-8.log`; the same build's Root84 open -> `.../logs/root84-open-upstream-replacement.log`; `ctest -R '^ControlGoldenAudio$'` -> `.../logs/test-golden-audio-upstream-replacement.log` | `bundledSearchPathDirectoryIsScanned` **FAILED**; Root84 `loaded_with_errors: true` (4 errors); `ControlGoldenAudio` **FAILED** |
| Merge train's reason, avoided (this port) | `.../logs/root84-open-fix.log`; `ctest -R '^ControlGoldenAudio$'` -> `.../logs/test-golden-audio.log` | Root84 `loaded_with_errors: false`, `error_count: 0`; `ControlGoldenAudio` **Passed** |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> `.../logs/gate-feature-check.log` | **EXIT=0** - every row PASS, complexity and file-length "no new item in your files" |
| Ratchet gates, raw | `bash tests/complexity-gate.sh --check` -> `.../logs/gate-complexity.log`; `bash tests/file-length-gate.sh --check` -> `.../logs/gate-file-length.log` | **EXIT=1** and **EXIT=1** - red on the train: every `REGRESSION:` line names a file this change does not touch (`src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/SmfInterchangeReader.cpp`, `tests/src/core/VideoTrackTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, ...). The comparison the gate is meant for is green: `feature_check.py` reports "no new item in your files" for both. |

## Commands and exit codes

```text
cmake --build build --target LadspaManagerTest > .../logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc   # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - INTERMEDIATE: the probe copy was named "....probeso" (QFileInfo::suffix() has no dot), so neither new case could pass; fixed before the RED run below
cmake --build build --target LadspaManagerTest > .../logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc   # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - RED (bundledLadspaDirectoryIsScanned failed, the other four cases passed)
cmake --build build --target zene LadspaManagerTest > .../logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - GREEN
cmake --build build --target LadspaChannelCountsTest LadspaPortDialogTest PluginPortsMigrationTest > .../logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R 'Ladspa|PluginPorts' --output-on-failure > .../logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - the SWH modules were not built in this tree yet (PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid); not a regression, fixed by building them
cmake --build build --target amp_1181 decimator_1202 cmt foverdrive_1196 > .../logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R 'Ladspa|PluginPorts' --output-on-failure > .../logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - 4/4
git diff -- src/core/LadspaManager.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene LadspaManagerTest > .../logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - RED with the fix reverted
QT_QPA_PLATFORM=offscreen python3 /tmp/opencode/root84_open_check.py build/zene data/projects/shorties/Root84-TrancyLoop.mmpz > .../logs/root84-open-original.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - the original code loads the fixture without errors
# the accepted branch's one-line change, measured
cmake --build build --target zene > .../logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
QT_QPA_PLATFORM=offscreen python3 /tmp/opencode/root84_open_check.py build/zene data/projects/shorties/Root84-TrancyLoop.mmpz > .../logs/root84-open-upstream-replacement.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=3 - loaded_with_errors: true, error_count: 4
cmake --build build --target LadspaManagerTest > .../logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-8.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - bundledSearchPathDirectoryIsScanned FAILED with the replacement
git checkout -- src/core/LadspaManager.cpp && git apply .../logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0 - the port's fix restored
cmake --build build --target zene LadspaManagerTest > .../logs/build-9.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-9.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - GREEN
QT_QPA_PLATFORM=offscreen python3 /tmp/opencode/root84_open_check.py build/zene data/projects/shorties/Root84-TrancyLoop.mmpz > .../logs/root84-open-fix.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - loaded_with_errors: false
ctest -R 'Ladspa|PluginPorts' --output-on-failure > .../logs/test-10.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - 4/4
# the accepted branch's one-line change, measured through the golden-audio programme
cmake --build build --target zene > .../logs/build-10.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^ControlGoldenAudio$' --output-on-failure > .../logs/test-golden-audio-upstream-replacement.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - "the bundled fixture opens with no load errors ... errors=True count=4"
# the port's fix, final form
git diff -- src/core/LadspaManager.cpp > .../logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0
git apply -R .../logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene LadspaManagerTest > .../logs/build-12.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-12.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8 - RED with the final fix reverted
git apply .../logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene LadspaManagerTest > .../logs/build-13.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-13.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - GREEN with the final fix
ctest -R 'Ladspa|PluginPorts|ControlGoldenAudio' --output-on-failure > .../logs/test-14.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - 5/5 ctest targets
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-feature-check.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 - every gate PASS
bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=1 - pre-existing train items only (see the table)
bash tests/file-length-gate.sh --check > .../logs/gate-file-length.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=1 - pre-existing train items only (see the table)
```

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-199-port.r1/logs`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED
  `ctest -R '^LadspaManagerTest$'` **EXIT=8** (`.../logs/test-2.log`, and again with the
  final patch reverted: `.../logs/test-12.log`), GREEN **EXIT=0**
  (`.../logs/test-13.log`). The reverted run fails exactly on
  `bundledLadspaDirectoryIsScanned`; the other four cases pass in both states.
- **The test is deterministic.** No clock, no sleep, no race: it copies a real module
  file into a `QTemporaryDir`, sets the working directory, clears `LADSPA_PATH` and reads
  `LadspaManager::getSortedPlugins()` - a pure function of the directory listing. The
  environment the case touches (current directory, `LADSPA_PATH`, the `plugins` search
  path) is restored by RAII guards (`ScopedCurrentDirectory`, `ScopedLadspaPath`,
  `ScopedSearchPaths`, `tests/src/core/LadspaManagerTest.cpp:33`), so the cases cannot
  leak into each other or into the process. The fixture module's location is injected at
  build time (`LADSPA_MANAGER_FIXTURE_DIR`), not read from the environment.
- **The fix is at the cause and complete.** The cause is the scan list of
  `LadspaManager::LadspaManager()` (`src/core/LadspaManager.cpp:47`): it named one
  bundled location and the finding's location was missing. The entry is added to that
  list, so every consumer of the list (the `QDir` scan loop at
  `src/core/LadspaManager.cpp:75`) sees it. The siblings of the changed line were
  checked: the other entries of the same list (`LADSPA_PATH`, the configured
  `ladspaDir()`, `applicationDirPath()+LIB_DIR+"ladspa"`, `/usr/lib/ladspa` and friends)
  are untouched, and the two bundled entries keep their relative order, so for a module
  file name present in both locations the application-relative one is still the one
  registered (the map key is the file name, `src/core/LadspaManager.cpp:164`, and the
  first registration wins - `src/core/LadspaManager.cpp:165`). The other consumer of the
  `plugins` search path, `src/core/RemotePlugin.cpp:268`, is a different path (an
  executable, not a LADSPA module) and is not affected.
- **No collateral damage.** The change only adds a directory to the scan list: no
  project format, no saved data, no command and no limit is touched, and the existing
  entries keep their priority. The project that the merge train named, the bundled
  `Root84-TrancyLoop.mmpz`, opens with **no load errors** and the golden-audio programme
  passes (`.../logs/root84-open-fix.log`, `.../logs/test-golden-audio.log`), while the
  accepted branch's replacement makes both fail (`.../logs/root84-open-upstream-replacement.log`,
  `.../logs/test-golden-audio-upstream-replacement.log`).
- **Realtime, undo and commands.** The change is in a constructor that runs once, before
  the engine starts; it adds one `QString` to a list that is scanned once. Nothing on the
  audio thread allocates, frees, locks, waits or logs, no undo step is added, and no
  control command is registered or changed (the `ControlCommandsSnapshot` canary is
  untouched).
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable - the verdict is FIXED. The
  finding's premise about the colon is imprecise (`"plugins:ladspa"` is Qt's search-path
  syntax, and it does resolve), but the behaviour it reports - a module in
  `plugins/ladspa` relative to the working directory is not scanned - was true on this
  branch and is fixed now.

## Files committed

- `src/core/LadspaManager.cpp` - the fix.
- `tests/src/core/LadspaManagerTest.cpp` - the two regression cases.
- `tests/CMakeLists.txt` - the fixture module's directory at build time.
- `tests/upstream-modifications.txt` - the divergence ledger entries.
- `docs/qwen-fixes-2026-10/cluster-new-p10-199-port.md` - this report.
