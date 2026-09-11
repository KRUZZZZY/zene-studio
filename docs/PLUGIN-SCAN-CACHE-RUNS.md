# Plugin scan cache — raw test runs (red and green)

Companion to `docs/PLUGIN-SCAN-CACHE.md`. Both runs use the *same* test binary
(`build/tests/PluginScanCacheTest`, 13 slots) and the same command; only the implementation
differs. Exit codes were measured unpiped (`cmd > log 2>&1; echo EXIT=$?`).

## 1. RED — the new behaviour disabled

The implementation is committed; the working tree was patched back to "feature not implemented" so
the test binary still compiles and runs against the real API. Exactly five changes make the red
state:

* `src/core/PluginScanCache.cpp` — an early `return` at the top of `load()` (`return false;`),
  `lookup()` (`return nullptr;`), `store()` (`return;`) and `isQuarantined()` (`return false;`),
  each marked `// RED RUN: the feature under test is disabled here`;
* `src/gui/PluginBrowser.cpp` — the constructor calls `addPlugins()` again (marked
  `// RED RUN: plugins are added right here, in the constructor.`).

So: no persistence, no fingerprint hits, no quarantine, and the browser scans at construction —
the tree as it was before this lane, with the new API present but inert.

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan
cmake --build build -j 6 --target PluginScanCacheTest > red-build.log 2>&1; echo BUILD_EXIT=$?
cd build/tests
QT_QPA_PLATFORM=offscreen ./PluginScanCacheTest > red.log 2>&1; echo TEST_EXIT=$?
```

`BUILD_EXIT=0`, and the suite:

```
QStandardPaths: XDG_RUNTIME_DIR not set, defaulting to '/tmp/runtime-kruzzzzy'
********* Start testing of PluginScanCacheTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QINFO  : PluginScanCacheTest::initTestCase() fixture plugin dir: "/tmp/zene-scan-plugins-onshdw" module copied: true
PASS   : PluginScanCacheTest::initTestCase()
FAIL!  : PluginScanCacheTest::testCacheRoundTrip() 'reloaded.load()' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(218)]
FAIL!  : PluginScanCacheTest::testLookupMissesOnChangedFingerprint() 'cache.lookup(file) != nullptr' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(258)]
FAIL!  : PluginScanCacheTest::testQuarantineSurvivesSaveAndLoad() Compared values are not the same
   Actual   (cache.quarantineCount()): 2
   Expected (1)                      : 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(283)]
QINFO  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() 'rewritten.load()' returned FALSE. (the scan must replace a corrupt cache with a valid one)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(331)]
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() 'QFileInfo::exists(cacheFile)' returned FALSE. (the first scan must write the cache)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(361)]
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testChangedPluginFileIsRescanned() Compared values are not the same
   Actual   (second.scanStats().servedFromCache): 0
   Expected (1)                                 : 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(401)]
QINFO  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() "LMMS plugin /tmp/zene-scan-plugins-onshdw/liblazyprobe.so does not have a plugin descriptor named lazyprobe_plugin_descriptor!"
QINFO  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() plugin-scan: 2 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 2 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() Compared values are not the same
   Actual   (factory.scanStats().scanned): 2
   Expected (1)                          : 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(466)]
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() Compared values are not the same
   Actual   (quarantined.scanStats().quarantined): 0
   Expected (1)                                  : 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(503)]
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() Cannot load library /tmp/zene-scan-plugins-onshdw/libnotaplugin.so: (/tmp/zene-scan-plugins-onshdw/libnotaplugin.so: file too short)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 2 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 2 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() Cannot load library /tmp/zene-scan-plugins-onshdw/libnotaplugin.so: (/tmp/zene-scan-plugins-onshdw/libnotaplugin.so: file too short)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 2 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 2 scanned, 1 plugin(s)
FAIL!  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() Compared values are not the same
   Actual   (second.scanStats().scanned): 2
   Expected (0)                         : 0
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(554)]
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache()
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "plugins": "File not found"
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "close": "File not found"
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "zoom": "File not found"
QINFO  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "tripleoscillator/logo": "File not found"
FAIL!  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() '!PluginFactory::instanceExists()' returned FALSE. (constructing the plugin browser must not trigger plugin discovery)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan/tests/src/core/PluginScanCacheTest.cpp(616)]
PASS   : PluginScanCacheTest::cleanupTestCase()
Totals: 3 passed, 10 failed, 0 skipped, 0 blacklisted, 59ms
********* Finished testing of PluginScanCacheTest *********
TEST_EXIT=10
```

**TEST_EXIT=10** (QtTest returns the number of failed slots). 10 of 13 slots fail; the three that
pass are `initTestCase`, `cleanupTestCase` and `testCacheDisabledBehavesLikeAColdCache` — the last
one asserts *behaviour preservation*, which must hold with or without the cache, so it is the one
slot that is expected to pass in both states.

## 2. GREEN — the implementation restored

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan
git checkout -- src/core/PluginScanCache.cpp src/gui/PluginBrowser.cpp
cmake --build build -j 6 --target PluginScanCacheTest > green-build.log 2>&1; echo BUILD_EXIT=$?
cd build/tests
QT_QPA_PLATFORM=offscreen ./PluginScanCacheTest > green.log 2>&1; echo TEST_EXIT=$?
```

```
QStandardPaths: XDG_RUNTIME_DIR not set, defaulting to '/tmp/runtime-kruzzzzy'
********* Start testing of PluginScanCacheTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QINFO  : PluginScanCacheTest::initTestCase() fixture plugin dir: "/tmp/zene-scan-plugins-pvFcAC" module copied: true
PASS   : PluginScanCacheTest::initTestCase()
PASS   : PluginScanCacheTest::testCacheRoundTrip()
PASS   : PluginScanCacheTest::testLookupMissesOnChangedFingerprint()
PASS   : PluginScanCacheTest::testQuarantineSurvivesSaveAndLoad()
QWARN  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() plugin-scan-cache: ignoring corrupt cache "/tmp/zene-scan-cache-PTdOBq/corrupt.json" - "unterminated object" - scanning all plugins
QWARN  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() plugin-scan-cache: ignoring corrupt cache "/tmp/zene-scan-cache-PTdOBq/corrupt.json" - "unterminated object" - scanning all plugins
QINFO  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testCorruptCacheDegradesToFullScan() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testCorruptCacheDegradesToFullScan()
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testRealPluginIsScannedThenServedFromCache()
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testChangedPluginFileIsRescanned() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testChangedPluginFileIsRescanned()
QINFO  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary() plugin-scan: 2 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 1 scanned, 2 plugin(s)
PASS   : PluginScanCacheTest::testCacheHitDoesNotLoadTheLibrary()
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 1 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s); skipped /tmp/zene-scan-plugins-pvFcAC/libtripleoscillator.so (hung the host on load)
QINFO  : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery() plugin-scan: 1 file(s), 1 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s); skipped /tmp/zene-scan-plugins-pvFcAC/libtripleoscillator.so (hung the host on load)
PASS   : PluginScanCacheTest::testQuarantinedPluginIsHiddenFromDiscovery()
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() Cannot load library /tmp/zene-scan-plugins-pvFcAC/libnotaplugin.so: (/tmp/zene-scan-plugins-pvFcAC/libnotaplugin.so: file too short)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 2 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 2 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 2 file(s), 0 quarantined, 1 served from cache, 1 known-bad skipped, 0 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() Cannot load library /tmp/zene-scan-plugins-pvFcAC/libnotaplugin.so: (/tmp/zene-scan-plugins-pvFcAC/libnotaplugin.so: file too short)
QINFO  : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain() plugin-scan: 2 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testKnownBadFileIsNotLoadedAgain()
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QINFO  : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache() plugin-scan: 1 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 1 scanned, 1 plugin(s)
PASS   : PluginScanCacheTest::testCacheDisabledBehavesLikeAColdCache()
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "plugins": "File not found"
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "close": "File not found"
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "zoom": "File not found"
QINFO  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() plugin-scan: 1 file(s), 0 quarantined, 1 served from cache, 0 known-bad skipped, 0 scanned, 1 plugin(s)
QWARN  : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow() Error loading icon pixmap "tripleoscillator/logo": "File not found"
PASS   : PluginScanCacheTest::testPluginBrowserDefersDiscoveryToFirstShow()
PASS   : PluginScanCacheTest::cleanupTestCase()
Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted, 73ms
********* Finished testing of PluginScanCacheTest *********
TEST_EXIT=0
```

**TEST_EXIT=0** — 13 passed, 0 failed.

## 3. Full local CI run (the lane's official build + suite)

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-scan
JOBS=6 bash tools/local-ci.sh --build-dir build --jobs 6 > ci.log 2>&1; echo EXIT=$?
```

(`tools/local-ci.sh` is committed mode 100644, so it must be invoked through `bash`.)

```
=== local-ci: reproducing .github/workflows/build.yml :: linux-x86_64 ===
machine     : Linux x86_64, Ubuntu 24.04.4 LTS
compiler    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake       : cmake version 3.28.3
ccache      : /home/kruzzzzy/.local/bin/ccache
build dir   : build (jobs=6, ctest -j2)
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON

--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j6) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26

=== job coverage on this machine ===
linux-x86_64: REPRODUCED
  run: configure OK, build OK, ctest OK (100% tests passed, 0 tests failed out of 26)
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
linux-arm64: NOT-REPRODUCIBLE-HERE (x86_64 host; needs ubuntu-24.04-arm runner or an aarch64 toolchain)
mingw64: NOT-REPRODUCIBLE-HERE (no x86_64-w64-mingw32 cross toolchain; job uses vcpkg + cmake/toolchains/x64-mingw-vcpkg.cmake)
macos-x86_64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; Darwin-only libc++/std::filesystem floors cannot be emulated)
macos-arm64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; also brew ld search-path defaults)
msvc-x64: NOT-REPRODUCIBLE-HERE (MSVC toolchain required: /WX, no libm, import-library semantics)
windows-arm64: NOT-REPRODUCIBLE-HERE (Windows 11 ARM64 + msys2 CLANGARM64 required; CPack/NSIS path handling is Windows-specific)

local-ci: overall exit=0 (0 = every executed step passed)
EXIT=0
```

**EXIT=0**: configure 0, build 0, ctest 0, `100% tests passed, 0 tests failed out of 26`
(`PluginScanCacheTest` is test #11 and passed: `build/ctest.log`).

The CI's `CMAKE_OPTS` are used byte for byte; the script adds `-DWANT_QT6=ON` on this box (no Qt5
development files) and prints that as a deviation, per its own contract.

## 4. The cheap gates (run by hand; not part of `tools/local-ci.sh`)

```
bash tests/no-upstream-regression-gate.sh   # PASS (37 files in the ledger)
bash tests/complexity-gate.sh               # PASS (ratchet clean)
bash tests/no-tautology-gate.sh             # PASS
bash tests/file-length-gate.sh              # PASS (ratchet clean)
bash tests/duplication-gate.sh              # PASS (0.53% duplicated lines)
```

`tests/complexity-gate.sh` rewrites `tests/complexity-baseline.tsv` into a new key format on every
run (function@path, no line spans) even when nothing regressed; that rewrite was reverted, not
committed — it is unrelated to this lane and belongs to the gate-debt work.
