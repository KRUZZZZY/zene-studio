FIXED

# Cluster new-p10-193: LADSPA descriptors point into an unloaded library

## Verdict: FIXED

The finding is real. `src/core/LadspaManager.cpp:102-110` (original) created a
local `QLibrary plugin_lib(f.absoluteFilePath())`, loaded it, resolved
`ladspa_descriptor`, and passed that function pointer to `addPlugins()`.
`addPlugins()` (`src/core/LadspaManager.cpp:153-205`) stores the pointer in
`LadspaManagerDescription::descriptorFunction` (`include/LadspaManager.h:75-82`)
for every plugin in `m_ladspaManagerMap`. The local `QLibrary` was destroyed at
the end of each scan iteration, while every later lookup
(`getDescriptor()` at `src/core/LadspaManager.cpp:574-588`, used by
`getName()`, `getPortCount()`, `instantiate()`, `run()`, etc.) calls that
stored pointer. The constructor itself calls `getName()` for every key at
`src/core/LadspaManager.cpp:120-124`, after all scan locals are gone. The
contract of `QLibrary` is that destroying the owner unloads the library
unless `PreventUnloadHint` is used; this code never set that hint and never
kept the owner, so the stored pointers dangle by design, regardless of
whether a particular Qt build happens to keep the mapping resident.

Note on a probe: on this Linux/Qt 6.12 build, a standalone probe
(`logs/probe.cpp`, `logs/probe2.log`) showed a destroyed `QLibrary`'s
mapping staying in `/proc/self/maps` and a call through it still returning.
That is a Qt implementation detail (lazy load, global `QLibraryPrivate`
cache), not a contract the manager can rely on, and it differs on
Windows/`FreeLibrary` and across Qt versions. The fix does not rely on it:
the manager now owns the libraries explicitly.

## Fix

- `include/LadspaManager.h`: add `std::vector<std::shared_ptr<QLibrary>> m_loadedLibraries`
  (declared before `m_ladspaManagerMap`, so libraries outlive the
  descriptions that point into them), forward-declare `QLibrary`, include
  `<memory>`/`<vector>`, and add the `LMMS_TESTING`-guarded seam
  `bool isLibraryLoaded(const QString&) const`.
- `src/core/LadspaManager.cpp`: in the constructor scan, load via
  `std::make_shared<QLibrary>(...)`; on a successful resolve, call
  `addPlugins()` and then `m_loadedLibraries.push_back(plugin_lib)`. A
  library whose descriptor resolves to null, or that fails to load, is not
  retained (no plugin points into it). `isLibraryLoaded()` (test seam only)
  reports whether a retained library for the given file name is loaded.
- `tests/upstream-modifications.txt`: append the two inherited-file entries
  for this cluster (file ends with a newline).

No audio-thread, undo, command, save-format, or scan-location behaviour
changes: scanning, filtering, `addPlugins()` logic, and all descriptor
accessors are byte-identical apart from the ownership of the `QLibrary`.

## Regression test

`tests/src/core/LadspaManagerTest.cpp:175-196`
`scannedLibraryStaysLoadedForManagerLifetime()`:

- Copies the real LADSPA fixture module into a temporary working
  directory's `plugins/ladspa` under a unique probe name, with the same
  `ScopedCurrentDirectory`/`ScopedLadspaPath` guards the sibling cases use.
- Constructs `LadspaManager`, requires the probe plugin to be registered
  (`hasPluginForFile`), and requires `manager.isLibraryLoaded(probe)`.

Deterministic seam: `isLibraryLoaded()` is the `LMMS_TESTING` seam added
above; it inspects the manager's own retained-library list, not wall-clock
timing, sleeps, or process memory layout. A `/proc/self/maps` variant of
this test was tried first (`logs/test-2.log`, EXIT=0 without the fix) and
was discarded because Qt 6.12 on Linux keeps the mapping resident after
`QLibrary` destruction, so it cannot distinguish owned from unowned on
this platform; the seam test can and does.

## Evidence table

| Check | Exact command | Result |
| --- | --- | --- |
| RED (seam present, library not yet retained) | `cmake --build build --target LadspaManagerTest -j4 > .../logs/build-3.log 2>&1; echo EXIT=$?` then `ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` (ctest from `build/tests`) | build **EXIT=0**; ctest **EXIT=8** - `scannedLibraryStaysLoadedForManagerLifetime` FAILED: `manager.isLibraryLoaded(probe)` returned FALSE, 5 passed / 1 failed |
| GREEN (fix applied) | `cmake --build build --target LadspaManagerTest -j4 > .../logs/build-4.log 2>&1; echo EXIT=$?` then `ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-4.log 2>&1; echo EXIT=$?` | build **EXIT=0**; ctest **EXIT=0** - 1/1 target passed |
| Affected family | `cmake --build build --target LadspaPortDialogTest LadspaGlobalLinkTest -j4 > .../logs/build-6.log 2>&1; echo EXIT=$?` then `ctest -R 'Ladspa' --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?` | build **EXIT=0**; ctest **EXIT=0** - **4 ctest targets executed, 4/4 passed** (`LadspaChannelCountsTest`, `LadspaManagerTest`, `LadspaPortDialogTest`, `LadspaGlobalLinkTest`) |
| Production build (header change, no `LMMS_TESTING`) | `cmake --build build --target zene -j4 > .../logs/build-7.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Step 4b full revert (builder evidence) | `git diff -- include/LadspaManager.h src/core/LadspaManager.cpp > .../logs/fix.patch; echo EXIT=$?` (**EXIT=0**), `git apply -R .../logs/fix.patch; echo EXIT=$?` (**EXIT=0**), `cmake --build build --target LadspaManagerTest -j4 > .../logs/build-8-revert.log 2>&1; echo EXIT=$?` | revert **EXIT=0**; rebuild **EXIT=2** - `isLibraryLoaded` was removed with the test seam, so this compile failure alone is not runtime RED evidence |
| Step 4b restore | `git apply .../logs/fix.patch; echo EXIT=$?` (**EXIT=0**), `cmake --build build --target LadspaManagerTest -j4 > .../logs/build-9.log 2>&1; echo EXIT=$?` (**EXIT=0**), `ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/test-9.log 2>&1; echo EXIT=$?` | **EXIT=0** - 1/1 target passed |
| Independent QA runtime RED (2026-11-06) | Temporarily removed only `m_loadedLibraries.push_back(plugin_lib)` while keeping the test seam; `cmake --build build --target zene LadspaManagerTest -j12 > .../logs/qa-build-12-red.log 2>&1; echo EXIT=$?`, then `cd build/tests && ctest -R '^LadspaManagerTest$' --output-on-failure > .../logs/qa-test-12-red.log 2>&1; echo EXIT=$?` | build **EXIT=0**; ctest **EXIT=8** - the runtime assertion `manager.isLibraryLoaded(probe)` failed (5 passed / 1 failed) |
| Independent QA restore (2026-11-06) | `git checkout -- include/LadspaManager.h src/core/LadspaManager.cpp` (**EXIT=0**), `cmake --build build --target zene LadspaManagerTest LadspaPortDialogTest LadspaGlobalLinkTest LadspaChannelCountsTest -j12 > .../logs/qa-build-13-restore.log 2>&1; echo EXIT=$?`, then `cd build/tests && ctest -R 'Ladspa' --output-on-failure > .../logs/qa-test-13-restore.log 2>&1; echo EXIT=$?` | build **EXIT=0**; ctest **EXIT=0** - all 4 LADSPA-family tests passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-193 > .../logs/gates-2.log 2>&1; echo EXIT=$?` | **EXIT=0** - PASS on all rows; complexity and file-length "no new item in your files" |

Files committed: `include/LadspaManager.h`, `src/core/LadspaManager.cpp`,
`tests/src/core/LadspaManagerTest.cpp`, `tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p10-193.md`.

## Self-check

- The builder's full step 4b revert (`logs/build-8-revert.log`, EXIT=2)
  removed the test seam, so its compile error was not runtime RED evidence.
  Independent QA removed only the owner insertion while retaining the seam:
  `logs/qa-build-12-red.log` EXIT=0 and `logs/qa-test-12-red.log` EXIT=8
  at the expected ownership assertion. Restoring both product files and
  rebuilding gave `logs/qa-build-13-restore.log` EXIT=0 and
  `logs/qa-test-13-restore.log` EXIT=0 (4/4 LADSPA-family tests).
- The test is deterministic: no wall-clock timing, sleeps or races. It
  uses the `LMMS_TESTING` seam `LadspaManager::isLibraryLoaded()`
  (`include/LadspaManager.h`, `src/core/LadspaManager.cpp`), which
  inspects the manager's retained `QLibrary` list synchronously after
  construction in the same thread.
- The fix is at the cause and complete: the single scan site in the
  constructor is the only place a `QLibrary` is created for LADSPA
  descriptors (searched `QLibrary` in `src/core/LadspaManager.cpp` /
  `include/LadspaManager.h`); every descriptor consumer goes through
  `getDescriptor()` and the stored `descriptorFunction`, all of which now
  point into a library the manager owns. Libraries that fail to load or
  resolve to null are not retained, since nothing points into them.
  `m_loadedLibraries` is declared before `m_ladspaManagerMap`, so
  descriptions are destroyed before their libraries.
- No collateral damage: old projects and saved data still load as before;
  no limit or check was added, no scan location, filter, or
  `addPlugins()` logic changed. All 4 Ladspa-family ctest targets pass
  (`logs/test-6.log` EXIT=0), and the `zene` target builds (`logs/build-7.log`
  EXIT=0).
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE, so the
  code/contract item for those verdicts does not apply; the real-bug
  evidence is the local-`QLibrary` destruction at the original
  `src/core/LadspaManager.cpp:102` with the stored pointer in
  `include/LadspaManager.h:75-82` and the post-scan `getName()` calls at
  `src/core/LadspaManager.cpp:120-124`.
