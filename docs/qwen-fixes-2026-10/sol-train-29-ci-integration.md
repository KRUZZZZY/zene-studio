# Train 29 CI repair integration

The train applied the Windows ARM64 repair aa2040351948 and then failed replaying
6cc60cb31f: all three Qt 5 platform repairs change the same FollowActionEditor
signal connection, and both Linux repairs introduce a different scanner under the
same filename/CTest name. This replacement preserves the already applied Windows
repair as a prerequisite, retains the x86 scanner and its real Qt 5 compile probe,
and retains the ARM scanner under a distinct path and CTest name. The independent
MinGW repair 397694798e is integrated unchanged except shared registration context.
No gate, baseline, assertion, or resource limit is relaxed.

Supersedes the Linux x86, Linux ARM and MinGW pending records only. The Windows
record remains required because its changes precede this delta in the checkout.
The accepted source semantics remain QOverload<int>::of for the step signal;
no-gestures Qt gets the bool pinch-completion API and no pinch filter, while
Qt builds with gestures retain the typed overload and existing behavior.

Changed files: ArrangerZoom.h, ArrangerZoom.cpp, ArrangerAids.cpp and the existing
ArrangerTimelineTest retain the accepted MinGW changes; three accepted Python
regressions and their three reports are preserved; CMake and source ledgers
register the combined checks and this report. The ARM scanner is only renamed.

## Commands and results

Configure (owner options, one private build), exit 0:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

- `python3 tests/qt5-signal-overload-check.py --root . --cxx c++`: exit 0;
  self-test passes and Qt 5 rejects the bare signal and accepts the explicit overload.
- `python3 tests/qt5-signal-overload-arm-check.py --root .`: exit 0;
  red/green fixtures pass and the current source scan is clean.

- `cmake --build build --target zene FollowActionEditorTest ArrangerTimelineTest -j12`:
  exit 0 (1244 initial steps). Rebuilt after the layout correction with the same
  command, exit 0 (5 final steps).
- `python3 build/sol32-negative-control.py`: exit 0; this temporary verification
  driver restored the four pre-repair gesture files from HEAD, ran
  `ctest --output-on-failure -R '^ArrangerZoomNoGestures$'` from build/tests,
  observed exit 8 with seven failed compilation/API cases, then restored all four
  repaired files in a finally block. No source reversal remains.
- All three accepted signal scanners' `scan_text` routines were checked against
  `git show c233621e80:src/gui/FollowActionEditor.cpp` and the current source:
  each reports one finding on the original and zero on the repaired source.
- From `build/tests`:
  `ctest --output-on-failure -R '^(Qt5OverloadedSignalCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures|FollowActionEditorTest|ArrangerTimelineTest)$'`:
  final exit 0, 6/6 passed in 27.80 seconds. The gesture check compiles six real
  translation units under a shadow Qt feature configuration with gestures off,
  confirms the enum is absent, and checks both API forms where supported.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/fork-sources-gate.sh`: exit 0.
- `bash tests/unregistered-tests-gate.sh`: exit 0.
- `bash tests/evidence-gate.sh`: exit 0.
- `git diff --check`: exit 0.

## Additional failure found during verification

The first combined run passed 5/6 but exposed the existing
`ArrangerTimelineTest::theRulerHasAUsableLayoutHeight` failure: 28 pixels against
its unchanged scaled 32-pixel requirement. The theme's fixed em-based bounds
replace the constructor's minimum during stylesheet processing. Merely polishing
before setting the minimum was insufficient (the second combined run also failed
5/6). The final SongEditor.cpp change scopes the intended scaled 32-pixel height
to the arranger ruler's own stylesheet, retaining it through repolishing. The
unchanged assertion and the full timeline test then pass. No assertion or test
selection was removed.

Registration gates inspect tracked files locally; the runner must repeat them
after committing the explicit new paths. The known broader file-length debt was
not rebaselined or exempted by this repair.
Native hosted builds remain required; local Qt probes do not replace them.
