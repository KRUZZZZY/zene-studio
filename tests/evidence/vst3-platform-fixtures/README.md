# The platform-specific host limitations: CI evidence, the fix, and what only CI can confirm

Lane: worktree `zene-fix-vst3plat`, branch `fix/vst3-macos-msvc-tests`, based on `ec07dd3a0` (= `main`,
tag `v0.2.1-alpha`). Box: Linux/gcc — MSVC and macOS cannot be run here, so every claim below is either CI
text or a Linux-side measurement, and nothing else.

CI evidence: run **34757467632** — `macos-arm64` **103724228405**, `macos-x86_64` **103724228370**,
`msvc-x64` **103724228360**. (`linux-x86_64` 103724228380 passed Vst3BusMapTest/Vst3HostTest/
Vst3EffectIntegrationTest, which is why the fixture is the suspect and not the tests.)

## Vst3HostTest + Vst3EffectIntegrationTest, both macOS jobs — the entry point, not the layout

```
FAIL! : lmms::vst3::Vst3HostTest::initTestCase() 'm_plugin.load(...)' returned FALSE.
        (Bundle does not export the required 'bundleEntry' function)
QWARN : Vst3EffectIntegrationTest::testProcessesAudioThroughAudioBus() VST3: could not load
        ".../vst3-test-effect.vst3" "AGain Sample Accurate" : "Bundle does not export the required
        'bundleEntry' function"
```

`module_mac.mm:92-104` asks CoreFoundation for the bundle and loads its executable, then
`:118-128` resolves `bundleEntry` **and** `bundleExit` by name, then `:134` `GetPluginFactory`. Getting
as far as the symbol lookup means the layout was already right (`Contents/MacOS/<stem>` + the
`Info.plist` naming it) — and 103724228405's build log shows the entry point compiled was still
`public.sdk/source/main/linuxmain.cpp.o`, i.e. `ModuleEntry`/`ModuleExit` and no `bundleEntry`.

Fix (fixture, `tests/data/vst3-test-effect/CMakeLists.txt`): the entry-point translation unit is
per-platform — `linuxmain.cpp` / `macmain.cpp` / `dllmain.cpp`, the same three the SDK's own
`smtg_target_add_library_main` selects (`cmake/modules/SMTG_AddSMTGLibrary.cmake:186-206`).

## Vst3HostTest + Vst3EffectIntegrationTest, msvc-x64 — the arch package

```
FAIL! : lmms::vst3::Vst3HostTest::initTestCase() ... returned FALSE.
        (LoadLibraryW failed for path ...\vst3-test-effect.vst3\Contents\x86_64-win\
         vst3-test-effect.vst3: The specified module could not be found.)
```

`module_win32.cpp:152-166` appends `Contents`/<architectureString>/<the bundle directory's own
filename>; `:89-107` fixes architectureString per host (`x86_64-win` on AMD64). The fixture laid the
module out as `Contents/<uname -m>-linux/<name>.so` on every platform.

Fix: the `WIN32` branch lays out `Contents/x86_64-win/vst3-test-effect.vst3` and compiles
`dllmain.cpp` (`InitDll`/`ExitDll`/`DllMain`; `InitDll` is optional at `:224-228`,
`GetPluginFactory` is not). `lmms_vst3_sdk` is STATIC (`cmake/modules/Vst3Sdk.cmake:162`), so the
module carries no shared-local dependency of its own.

## ZynSeparateProcessTest, msvc-x64 — a skip that crashed

```
SKIP   : ZynSeparateProcessTest::initTestCase() cannot load a plugin module from a Windows test host
A crash occurred in ...\ZynSeparateProcessTest.exe.    While testing cleanupTestCase
  # 8: QHash<unsigned int,lmms::JournallingObject *>::begin()
  # 9: lmms::ProjectJournal::stopAllJournalling()
  #10: lmms::Engine::destroy()
```

`Engine::destroy()` (`src/core/Engine.cpp:95-97`) dereferences `s_projectJournal`, which is `nullptr`
(`:48`) until `Engine::init()` sets it (`:69`). On Windows `initTestCase()` QSKIPs before that, so
`cleanupTestCase()` was destroying an engine that never existed.

Fix: `cleanupTestCase()` destroys the engine only if `initTestCase()` got past the skip
(`m_engineInitialised`). What can Windows observe? Nothing: every case reaches the instrument through
the module's own `lmms_plugin_main` (`initTestCase()` resolves it after `QLibrary::load`;
`instantiate()` calls it), so with no loadable module there is no subject — the suite's stated skip is
the whole of what that platform can produce, and the skip message now names the mechanism
(zene.exe import descriptor, `ERROR_MOD_NOT_FOUND` 126) in the same words AudioPluginTest.cpp uses.

## PluginScanCacheTest, msvc-x64 — the module name the scan filter wants

```
QINFO  : initTestCase() fixture plugin dir: "..." module copied: false
FAIL!  : testLookupMissesOnChangedFingerprint() 'file.exists()' returned FALSE.
FAIL!  : testCorruptCacheDegradesToFullScan() Actual (factory.scanStats().candidateFiles): 0
                                               Expected (1)                                : 1
FAIL!  : testPluginBrowserDefersDiscoveryToFirstShow() 'candidateFiles >= 1' returned FALSE.
SKIP   : testRealPluginIsScannedThenServedFromCache() no built tripleoscillator module to scan   (x6)
```

`PluginFactory.cpp:51-54` filters candidates by `*.dll` where `LMMS_BUILD_WIN32` is set (from the
generated `lmmsconfig.h`, never a compile definition) and `lib*.so` otherwise. The test's
`moduleFileName()` read `LMMS_BUILD_WIN32` without including that header, so the fixture looked for
`libtripleoscillator.so` on Windows — where the job links `plugins\tripleoscillator.dll`.

Fix: include `lmmsconfig.h` (as the code under test does, `PluginFactory.cpp:35`) and gate the
descriptor-dependent halves on the host being able to load a module at all. The counts that need no
load are asserted on every platform; the `else` branches say out loud what is UNEXERCISED and why.

## PhaseDSidechainTest, msvc-x64 — the fixture link name, and the module it needs

```
QINFO  : demoProjectBusWithNativeSidechainCompressor() plugin-scan: 0 file(s), ...
QWARN  : ... "The plugin \"compressor\" wasn't found or could not be loaded!"
FAIL!  : demoProjectBusWithNativeSidechainCompressor() 'compressor != nullptr' returned FALSE.
```

The fixture linked the built Compressor in under a hardcoded `libcompressor.so`, a name Windows'
`*.dll` filter never looks at; the job links `plugins\compressor.dll`. The fixture now names the link
after the built module's own file name. The case itself needs the module LOADED, which a Windows test
host cannot do (the same limitation as AudioPluginTest.cpp), so it QSKIPs there with that reason while
the other six cases in the file run on every platform.

## Local verification (this box)

```
cmake --build build --target ... (incremental, 5 targets + the fixture)      BUILD_EXIT=0
ctest -R "Vst3HostTest|Vst3EffectIntegrationTest|ZynSeparateProcessTest|
          PhaseDSidechainTest|PluginScanCacheTest"                            AFFECTED_EXIT=0 (5/5)
ctest --output-on-failure   (from build/tests)                                CTEST_EXIT=0 (92/92)
bash tests/run-all-gates.sh                                                   GATES_EXIT=3 (9 PASS, gate 2 SKIP)
```

The Linux half of the fixture is unchanged and proven unchanged: the build compiles only
`linuxmain.cpp.o` and the laid-out module still exports `GetPluginFactory`, `ModuleEntry`, `ModuleExit`.

The fixture's two configure-time guards were exercised for real: with the entry-point path sabotaged,
the configure fails with the stated message (`CMake Error at
tests/data/vst3-test-effect/CMakeLists.txt:158 (message): The VST3 test fixture needs the SDK's Linux
entry point ...`), exit 2; restored, configure+build exit 0.

## unverified — only CI can confirm

- macOS: that `macmain.cpp` is the symbol set the CoreFoundation loader resolves (`bundleEntry`/
  `bundleExit`) and that the MODULE links CoreFoundation through `lmms_vst3_sdk`'s PUBLIC frameworks —
  no macOS toolchain here.
- Windows: that the arch directory this SDK's loader computes on the runners is `x86_64-win` (from
  `uname -m` = `x86_64`, or the `CMAKE_SYSTEM_PROCESSOR` = `AMD64` fallback), that `dllmain.cpp`
  compiles and links as the PE module, and that LoadLibraryW then finds no missing dependency.
- Windows: the three skip/else paths themselves — the Windows test-host limitation and the
  LoadFailed-record branch that `second.scanStats().scanned == 0` rests on (its rule is read out of
  PluginFactory.cpp:556-561 and :223-239, and is unexercised on Linux, where the module loads).
- The whole-tree gate scope (gates 4/7/8 `--scope all`) was not run by this lane.
