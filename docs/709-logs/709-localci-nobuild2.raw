=== local-ci: reproducing .github/workflows/build.yml :: linux-x86_64 ===
machine     : Linux x86_64, Ubuntu 24.04.4 LTS
compiler    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake       : cmake version 3.28.3
ccache      : /home/kruzzzzy/.local/bin/ccache
build dir   : build (jobs=4, ctest -j2)
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_VST3_TEST_INSTRUMENT=ON
qt flags    : -DWANT_QT6=ON

--- [0/3] provision the pinned VST3 SDK and CLAP headers (the CI step) ---
  both checkouts already present in build; verifying they are the pins
provision EXIT=0   (log: build/provision.log)
  VST3 SDK: already at v3.8.1_build_84 (3cdf9ca5d) in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/vst3sdk — nothing to fetch
  CLAP: already at 1.2.10 (195b42a00) in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/clap — nothing to fetch
    VST3 SDK v3.8.1_build_84 (3cdf9ca5d, MIT)  -> /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/vst3sdk
    CLAP     1.2.10 (195b42a00, MIT)  -> /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/clap
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build skipped (--no-build) ---
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=8   (log: build/ctest.log)
ctest totals: 27% tests passed, 166 tests failed out of 228
--- ctest failed; last 30 lines: ---
	146 - TwoTrackRecordingHarness (Not Run)
	147 - WarpMarkersTest (Not Run)
	148 - FocusDeskPaneTest (Not Run)
	149 - FocusDeskTest (Not Run)
	150 - MidiLearnGuiTest (Not Run)
	151 - AutomationTrackTest (Not Run)
	152 - SampleClipStretchTest (Not Run)
	153 - SampleClipWarpTest (Not Run)
	154 - SampleClipWindowTest (Not Run)
	155 - SessionModelTest (Not Run)
	156 - SessionSchedulerTest (Not Run)
	157 - SessionSchedulerRenderTest (Not Run)
	158 - SessionFollowTest (Not Run)
	159 - SessionArrangementRecordTest (Not Run)
	160 - PluginPortsMigrationTest (Not Run)
	161 - ClapBusMapTest (Not Run)
	162 - ClapHostTest (Not Run)
	163 - ClapLoaderErrorTest (Not Run)
	164 - ClapEffectIntegrationTest (Not Run)
	165 - Vst3BusMapTest (Not Run)
	166 - Vst3HostTest (Not Run)
	167 - Vst3ChunkProbeTest (Not Run)
	168 - Vst3EffectIntegrationTest (Not Run)
	169 - ZynSeparateProcessTest (Not Run)
	170 - AudioPluginTest (Not Run)
	171 - Vst3InstrumentFixtureProbe (Not Run)
	172 - Vst3InstrumentTest (Not Run)
	173 - Vst3InstrumentIntegrationTest (Not Run)
	191 - ControlFeedbackCommands (Failed)
Errors while running CTest

=== job coverage on this machine ===
linux-x86_64: REPRODUCED
  run: configure=0, build skipped, ctest=8
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
linux-arm64: NOT-REPRODUCIBLE-HERE (x86_64 host; needs ubuntu-24.04-arm runner or an aarch64 toolchain)
mingw64: NOT-REPRODUCIBLE-HERE (no x86_64-w64-mingw32 cross toolchain; job uses vcpkg + cmake/toolchains/x64-mingw-vcpkg.cmake)
macos-x86_64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; Darwin-only libc++/std::filesystem floors cannot be emulated)
macos-arm64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; also brew ld search-path defaults)
msvc-x64: NOT-REPRODUCIBLE-HERE (MSVC toolchain required: /WX, no libm, import-library semantics)
windows-arm64: NOT-REPRODUCIBLE-HERE (Windows 11 ARM64 + msys2 CLANGARM64 required; CPack/NSIS path handling is Windows-specific)

local-ci: overall exit=1 (0 = every executed step passed)
