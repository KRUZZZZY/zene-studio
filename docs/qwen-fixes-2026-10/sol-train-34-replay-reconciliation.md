# Train 49 repair replay reconciliation

This is failure case 49 on train 34. Private base f20d6ff8736b includes the accessibility candidate, sol-repair-1794749218500302546 and ci-repair-ci-77e0405b9f559208-b701aa293aff. Keep those two pending repairs required: this delta replaces only the remaining thirteen. The train aborted replaying 5c2d695b09 because its older ScriptClockTest JSON conversion overlaps the already promoted Qt5 repair; further accepted repairs share source and registration context.

## Replaced repairs

- `ci-repair-ci-e58078851126eac9-e38c40db77d1` (`5c2d695b09a0`)
- `ci-repair-ci-619dd453d973527c-d3eea4a3e3ff` (`ef1baff3719f`)
- `ci-repair-ci-9b3cdba8549c073f-0a7d9ced9980` (`bf74b7c952ac`)
- `ci-repair-ci-2fd63d3310176c0c-655d6b253348` (`ccb07d8093f8`)
- `ci-repair-ci-2fac5540e1a657f0-68b9426c1ebe` (`660d9cba2e50`)
- `ci-repair-ci-d2bfae2f2ab57bfb-73df13cfc211` (`7b5319ef5559`)
- `ci-repair-ci-a0493fd29d4e9b4b-d0b8b2742351` (`31dfb9b82d07`)
- `ci-repair-ci-f8d87e270a05ef5e-b6fb8e20fddc` (`6ab25975c894`)
- `ci-repair-ci-6422986f4db9676f-a0b124207c50` (`25aa2d7a562b`)
- `ci-repair-ci-9b6002ebebcf807a-0cba1a670472` (`9edd834aaf1d`)
- `ci-repair-ci-bbbeb2c8d6e707a8-02d4aa0a18e9` (`06a0bbe849cb`)
- `ci-repair-ci-e8063221c62bcc38-cc1ba72f567c` (`055e913243e3`)
- `ci-repair-ci-78cca41150908714-62b2bb468573` (`5777eafae785`)

## Preservation and reconciliation

All distinct accepted checkers, registrations and historical reports are retained. Registrations use stable pre-existing contexts, preserving the selected accessibility candidate and both already-applied repairs. No repair is removed just because it duplicates a product fix; its independent regression remains part of verification.

ScriptClockTest uses the existing jsonInteger compatibility adapter with the same sentinel, cleanup and UTF-8 assertions. Train47Portability and Qt5JsonValueIntegerCheck compile the actual source reads and compatibility header against Qt5 Core/Gui/Xml in C++20, retaining explicit original toInteger failure controls. The latter accepted checker originally assumed a standalone toDouble expression; its fixture now includes the actual adapter instead of substituting a fake implementation. The six Windows timeout references remain qualified, and both accepted source-level deadline checks remain present with distinct helper and slot names. SFZ uses the private vendored include root and accepted Abseil version assertion. Shared FIFO retains the Qt usage link and every accepted check.

The video fixture retains its no-FFmpeg failure API and gains the accepted forcing seam for VideoTestClipPortabilityTest. Both core and GUI callers retain their accepted compile-time guards. The accepted missing-helper compiler check force-includes the real fixture with the helper temporarily renamed, proves the original API absent, then compiles both real guarded TUs under the same model; removing either guard is still a regression. This deliberately strict model coexists with runtime tests of the public fallback API. The second no-codec check retains configured and shadowed source compilation. Its Windows-aware argument parser and driver switch selection are shared by the missing-helper probe, including cl/clang-cl syntax-only and forced-include options. Independent read-only design review confirmed the forced-include model and quoted-include precedence.

The existing PCRE2 materializer remains unchanged: its single placeholder expansion preserves literal template tokens and exact Unix patch bytes. Both the original literal-token/Windows controls and the additional accepted Windows text-mode regression are retained, along with all original SHA512 checks and dependency pins. Unix WAV helpers and POSIX render slots retain the verified guards, with both old and new compiler models present.

## Registration

New checker/test/report paths are registered in tests/fork-sources.txt; VideoTestClipPortabilityTest also belongs to the unchanged whole-tree recipe. Changed inherited metadata/build paths have reasons in tests/upstream-modifications.txt. This report records reconciliation of the existing fork sources and tests. No gate executable, threshold, baseline, exemption, deployment floor or dependency pin is changed.

## Verification

Exact commands run from this checkout (CTest from build/tests):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
cmake --build build --target zene ScriptClockTest ControlShutdownHookTest RemotePluginShmFifoTest SfzPlayerTest VideoClipsTest VideoClipsGuiTest VideoTestClipTest VideoTestClipPortabilityTest RenderSourceGuardTest ExportWavWriteErrorTest -j12
cd build/tests
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest -R '^(ScriptClockTest|ControlShutdownHookTest|RemotePluginShmFifoTest|SfzPlayerTest|VideoClipsTest|VideoClipsGuiTest|VideoTestClipTest|VideoTestClipPortabilityTest|RenderSourceGuardTest|ExportWavWriteErrorTest|Train47Portability|Train48Portability|Train41-render|Train41-wav-write-error|VcpkgPcre2Gitlink|FileLengthGate|NoteCanvasComplexityGate|Qt5QJsonToIntegerCheck|Qt5JsonIntegerCheck|Qt5QJsonValueIntegerCheck|Qt5JsonValueIntegerCheck|RemotePluginShmFifoQtUsageCheck|NoFfmpegVideoClipsCompileCheck|VideoClipsNoFfmpegCheck)$' -j1 --timeout 900 --no-tests=error --output-on-failure
cd ../..
bash tests/no-upstream-regression-gate.sh
bash tests/evidence-gate.sh
bash tests/fork-sources-gate.sh
bash tests/unregistered-tests-gate.sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 bash tests/complexity-gate.sh --check
git diff HEAD --check
git diff --cached --check
python3 /tmp/sol49-video-controls.py
python3 tests/vcpkg-pcre2-gitlink-test.py
python3 tests/Qt5JsonValueIntegerCheck.py --source-root .
```

Configure and final targeted build exit 0. The first build caught the duplicate Windows helper/slot; both accepted checks now use distinct names. The first 24-entry bar exposed the standalone Qt5 fixture assumption; compiling the actual adapter fixed it while retaining the negative control. Both original unguarded video caller TUs fail with missing makeClip under the identical forced-include model (control command exit 0 means both expected compiler failures were confirmed); repaired callers pass. PCRE2 and Qt5 direct checks exit 0, including their red controls.

Registration, upstream, evidence, test registration and unchanged complexity gates exit 0. Full diff and cached whitespace checks exit 0, including imported reports; no whitespace exemptions. Final CTest and independent review results follow below. Native hosted platform and full-train validation remain required.

Final combined bar: exit 0, 24/24 passed, no skipped tests, 87.00 seconds (`/tmp/sol49-ctest-final.log`). The rebuilt target command exits 0 (`/tmp/sol49-build-final.log`). Final registration, upstream and evidence reruns also exit 0. Independent Luna review confirmed the three true findings resolved; its remaining registration concern was checked against both distinct QJson checker names and withdrawn as a false positive. A final read-only review accepted the adapter fixture change. Synthetic cl and clang-cl argument models also preserve the source and require /I, /Zs and /FI while removing /c and /Fo. Keep both already-applied pending repairs required; supersede only the thirteen named above.
