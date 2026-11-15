# Train 47 hosted compile repair

Base: dec3f1d45c62b26bce955875521780db173a0228. All seven hosted jobs failed in test compilation; no provisioning or disk failure was shown. Four Qt5 jobs rejected the two Qt6-only JSON reads in ScriptClockTest, two Windows jobs rejected unqualified timeout constants in the split shutdown fixture, and MSVC rejected the shared-memory compile-check target without Qt compiler usage requirements.

## Change registration

- `tests/src/core/ScriptClockTest.cpp`: preserve both UTF-8 byte-count assertions, the missing-value sentinel and cleanup, using a Qt5-compatible conversion for these bounded fixture values.
- `tests/src/core/ControlShutdownHookTestParts1.h`: qualify all six references to the existing public ControlServer timeout; no timeout or assertion changes.
- `tests/CMakeLists.txt`: link the object compile-check target to Qt as its sibling probes do, inheriting compiler definitions/options (including MSVC /Zc:__cplusplus); register the new compiler check in stable existing context. Reason recorded in `tests/upstream-modifications.txt`.
- `tests/train-47-portability-check.py`: source-derived compiler regressions for Qt5 JSON, Windows timeout name lookup and Qt target usage, with negative controls; registered with this report in `tests/fork-sources.txt`. Existing fork sources retain their registration. No gate executables, baselines, dependency pins, deployment floors or assertions change.

## Verification

The single private Qt6/WASM/WERROR build was configured with the owner options, exactly:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Configure exited 0. Targeted rebuild command:

```sh
cmake --build build --target zene ScriptClockTest ControlShutdownHookTest RemotePluginShmFifoTest -j12
```

Compiler controls:

```sh
python3 tests/train-47-portability-check.py --source-root . --build-dir build
python3 /tmp/sol47-original-controls.py
```

Both exited 0. The temporary original-source control loaded the two fixture files from HEAD and the saved pre-fix object-target compile command. Original Qt5 JSON reads failed compilation, original Windows timeout expressions failed name lookup, and the original compile command failed the Qt usage contract. Repaired versions passed. The committed checker also compiles both negative source controls and verifies the actual target declaration in a temporary CMake usage model, where the modeled MSVC option propagates with the Qt interface link and disappears without it. It selects the exact object target, not the similarly defined executable target.

Unchanged gates (all exited 0):

```sh
bash tests/no-upstream-regression-gate.sh
bash tests/fork-sources-gate.sh
bash tests/evidence-gate.sh
bash tests/unregistered-tests-gate.sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 bash tests/complexity-gate.sh --check
git diff HEAD --check
git diff --cached --check
```

Lizard is pinned to hosted version 1.25.1. No baseline writes or exemptions. Independent review confirmed the source changes preserve assertions and the Qt usage link repairs the MSVC compiler requirement; its initial coverage concern was addressed with the actual-declaration CMake model. Native hosted Windows/macOS compilation remains required; Linux compiler models do not claim native platform verification.


Targeted build exited 0. CTest ran from this checkout's `build/tests`:

```sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest -R '^(ScriptClockTest|ControlShutdownHookTest|RemotePluginShmFifoTest|Train47Portability|FileLengthGate|NoteCanvasComplexityGate)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Exited 0: **6/6 entries passed in 24.78 seconds**, with rebuilt test binaries. The existing Windows client-limit case skipped on Linux, and Windows-only named-pipe paths were not run; the source-derived compiler check covers their timeout lookup. Final independent Luna review found no blocker and confirmed the actual-target compile command selection and linked/unlinked CMake model, with maximum probe function CCN 7. Disk remained above 100 GB free. No commits, pushes, queue writes or other source worktree edits were made.
