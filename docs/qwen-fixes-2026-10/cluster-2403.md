ALREADY-FIXED

# Cluster 2403 — Rescanning can unload libraries used by active plug-in instances

The finding describes `m_pluginInfos = pluginInfos` releasing the factory's
`shared_ptr<QLibrary>` references while live plug-in objects still execute module
code. On branch `bugs/qwen-2403` a plug-in instance already owns the very same
`QLibrary` reference, so a rescan can no longer unload a module that a live
instance is using. The fix landed as commit `8dab701ac5` ("fix(plugins): keep
modules loaded by live instances (cluster 2403)") with its regression test, and
that test fails when the retention is removed (proof below).

## Evidence

- `include/PluginFactory.h:57` — `PluginInfo` holds
  `std::shared_ptr<QLibrary> library`; a rescan drops the factory's reference.
- `src/core/PluginFactory.cpp:277` — `m_pluginInfos = pluginInfos;` (synchronous
  scan commit). The background-scan commit replaces the same list at
  `src/core/PluginFactoryBackgroundScan.cpp:393`
  (`m_pluginInfos = m_backgroundInfos;`). Both are the replacement the finding
  names; both now hand their libraries to live instances instead of dropping
  the last reference.
- `include/Plugin.h:316` — every plug-in object carries
  `std::shared_ptr<QLibrary> m_library;`.
- `src/core/Plugin.cpp:312` — `inst->m_library = pi.library;` takes a reference
  after the module's `lmms_plugin_main()` has produced the object. The
  destruction order makes this sufficient: the derived destructor (module code)
  and the derived members are destroyed before the base `Plugin`'s members, so
  `m_library` — and therefore `dlclose` — is released only after the instance's
  own module code has finished running.
- Funnel check (every path into a plug-in object): `Plugin::instantiateWithKey`
  (`src/core/Plugin.cpp:198`) and `Effect::instantiate`
  (`src/core/Effect.cpp:384`) both end in `Plugin::instantiate`
  (`src/core/Plugin.cpp:224`); the safe-start early return there returns a host
  `DummyPlugin` and touches no module code. No other translation unit calls
  `lmms_plugin_main`; `ControlDeviceSupport.cpp:200-204` only *probes*
  `pi.library->resolve("lmms_plugin_main")` through a `PluginInfo` copy, which
  itself keeps the library loaded for the duration of the call.
- Other holders of a catalogue entry inherit the same protection, because
  `PluginInfoAndKey` (`pluginSupportingExtension()`) and every `PluginInfo`
  copy carry the `shared_ptr`; the descriptor consumers
  (`PluginBrowser`, `EffectSelectDialog`, `ControlDeviceCatalogue`,
  `ImportFilter`, `MainMenu`, ...) iterate a fresh `descriptors()` list rather
  than caching descriptor pointers across a scan.
- Regression test: `tests/src/core/PluginScanCacheTest.cpp:920`
  `testPluginInstanceRetainsLibraryAcrossRescan` — scans the real
  `tripleoscillator` module, keeps a `weak_ptr` to its `QLibrary`, instantiates
  the plugin, rescans, and asserts the module is still alive and is released
  exactly when the instance is destroyed.

## Reproduction and proof (this session, all logs in the run's log folder)

The test matrix this covers: the normal path (scan, instantiate, rescan),
the failure path (a rescan that no longer lists the plugin), the audio-thread
constraint (the fix holds a reference only; it adds nothing to `process()`,
allocates nothing on the audio thread, takes no lock), and the supported-platform
constraint (the test's Windows half is the pre-existing, documented QSKIP: a
Windows test host cannot load plugin modules that import `zene.exe`).

Baseline with the fix in place:

1. `cmake --build build --target PluginScanCacheTest -j4 > .../logs/build-1.log 2>&1` — EXIT=0
2. `cd build/tests && ctest -R PluginScanCacheTest --output-on-failure > .../logs/test-1.log 2>&1` — EXIT=0
3. `cd build/tests && ./PluginScanCacheTest testPluginInstanceRetainsLibraryAcrossRescan > .../logs/test-2.log 2>&1` — EXIT=0
   (`PASS … Totals: 3 passed, 0 failed, 0 skipped`; the three `plugin-scan: 1 file(s) … 1 scanned` lines show three real scans, so the case is exercised, not skipped)

Proving the test catches the bug — the product-side retention removed:

4. Edit `src/core/Plugin.cpp`: delete the three comment lines and
   `inst->m_library = pi.library;` (the behavioural half of `8dab701ac5`; the
   header member is left in place so that no module ABI rebuild is involved and
   only the retention is under test).
5. `git diff -- src/core/Plugin.cpp > .../logs/red-revert.patch` — EXIT=0
6. `cmake --build build --target PluginScanCacheTest -j4 > .../logs/build-2-red.log 2>&1` — EXIT=0
   (`[  6%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Plugin.cpp.o` … `Built target PluginScanCacheTest`)
7. `cd build/tests && ./PluginScanCacheTest testPluginInstanceRetainsLibraryAcrossRescan > .../logs/test-3-red.log 2>&1` — **EXIT=1**
   ```
   FAIL!  : PluginScanCacheTest::testPluginInstanceRetainsLibraryAcrossRescan() '!library.expired()' returned FALSE. (a live plugin instance must keep its code module loaded after rescan)
   Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 503ms
   ```
8. `git apply -R .../logs/red-revert.patch` (restore the fix) — EXIT=0, `git status --short` empty
9. `cmake --build build --target PluginScanCacheTest -j4 > .../logs/build-3-green.log 2>&1` — EXIT=0
10. `cd build/tests && ./PluginScanCacheTest testPluginInstanceRetainsLibraryAcrossRescan > .../logs/test-4-green.log 2>&1` — EXIT=0
    (`PASS … Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted`)

Family and gates:

11. `cd build/tests && ctest -R PluginScan --output-on-failure > .../logs/test-5-family.log 2>&1` — EXIT=8: `PluginScanWorkerTest (Not Run)` — the binary had not been built in this tree yet.
12. `cmake --build build --target PluginScanWorkerTest -j4 > .../logs/build-4-worker.log 2>&1` — EXIT=0
13. `cd build/tests && ctest -R PluginScan --output-on-failure > .../logs/test-6-family.log 2>&1` — EXIT=0 (4/4: `PluginScanCacheTest`, `PluginScanWorkerTest`, `ControlPluginScanCommands`, `ControlPluginScanBackground`)
14. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1.log 2>&1` — EXIT=0, `RESULT: PASS` (all eight gates PASS; complexity and file-length report "no new item in your files")

No product file is changed by this session: the fix was already on the branch, so
this commit adds only this report. Everything above was run against the branch
head's product code (`src/core/Plugin.cpp:312`, `include/Plugin.h:316`).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it.** Reverted: `build-2-red.log` (EXIT=0) then `test-3-red.log` (EXIT=1,
  `'!library.expired()' returned FALSE`). Restored: `build-3-green.log`
  (EXIT=0) then `test-4-green.log` (EXIT=0). The revert patch is saved as
  `.../logs/red-revert.patch`; only `src/core/Plugin.cpp` was touched and
  `git apply -R` restored it (EXIT=0, clean tree).
- **The test is deterministic.** It uses no timing, sleeps or threads: it holds
  a `std::weak_ptr<QLibrary>` to the catalogue entry, calls the real
  `discoverPlugins()` twice, and observes reference-count ownership. The only
  environmental condition is the pre-existing Windows QSKIP (a Windows test host
  cannot load plugin modules); on this Linux host the case runs — the log shows
  three completed scans and `0 skipped`.
- **The fix is at the cause and complete.** The cause is "the factory is the only
  owner of the module reference". The retention lives in the single funnel every
  plug-in object is created through (`Plugin::instantiate`, reached from
  `instantiateWithKey` and `Effect::instantiate`), so instruments, effects and
  DnD/sub-plugin instances alike keep their module. Both catalogue replacement
  sites are covered (`PluginFactory.cpp:277`, `PluginFactoryBackgroundScan.cpp:393`);
  the cancelled-background-scan path never unloads a library a live instance
  holds either, because the staging infos never produced instances and the live
  catalogue is untouched by a cancel.
- **No collateral damage.** No product code changed in this session, so project
  loading, saving, undo and commands are untouched. The retention only adds a
  reference count; it takes no lock, allocates nothing per block and never runs
  on the audio thread. The module is released as soon as the last instance dies,
  as the test's second assertion shows, so a plugin is still unloadable at
  runtime (no permanent leak of the module).
- **Why ALREADY-FIXED.** `src/core/Plugin.cpp:312` (`inst->m_library = pi.library;`)
  plus the member at `include/Plugin.h:316`, introduced by commit
  `8dab701ac5`, are exactly the contract the finding asks for ("keep each
  library loaded for at least as long as its plug-in instances"), and removing
  them reproduces the reported failure.

## Other findings

- An open `PluginBrowser`/`EffectSelectDialog` holds `Plugin::Descriptor*`
  values only for the duration of one `descriptors()` call, and a `PluginInfo` /
  `PluginInfoAndKey` copy keeps its module loaded, so no dangling descriptor was
  found. A caller that cached a raw `Plugin::Descriptor*` across a rescan would
  still dangle for module-owned descriptors; nothing in the tree does that today.
  Not changed here — outside this finding.
- `PluginScanWorkerTest` had never been built in this worktree, which made the
  first family `ctest -R PluginScan` run report `Not Run` (EXIT=8). Built and
  re-run; it passes. Worth noting for anyone reading that exit code as a
  regression.
