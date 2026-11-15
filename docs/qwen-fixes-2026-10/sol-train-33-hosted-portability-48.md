# Train 48 hosted compile repair

Base cc29a63dde09cf28bb00498a2d2a5b124d4d614d. Seven jobs failed from four test-source/build causes: four Linux/macOS jobs cannot call makeClip without FFmpeg, Windows ARM64 lacks the direct Abseil include root on SfzPlayerTest, MinGW rejects the unused Unix WAV helper, and MSVC moc references three POSIX slots the compiler excludes. Wine fixture-generation warnings on Linux preceded the fatal source error; they were not the cause of these failed jobs. The MSVC queue ID ci-512849fa221097db points to an older accepted run, not this new moc failure; its old acceptance is not current verification.

## Changes and registration

- `tests/src/core/VideoTestClip.h`: retain the fixture API without FFmpeg with a false-returning makeClip overload and both palettes; it cannot generate a clip without an encoder. All enabled-codec paths and test assertions remain intact.
- `tests/src/core/RenderSourceGuardTest.cpp`: add the Qt OS macro alongside the compiler macro for the three existing POSIX slots so moc and the compiler select the same declarations; use the spelling recognized by the existing Windows exclusion parser.
- `tests/src/core/ExportWavWriteErrorTest.cpp`: guard the RIFF helper with the same Q_OS_UNIX condition as its callers, preserving every Unix test and assertion.
- `tests/CMakeLists.txt`: add the direct, private sfizz Abseil include root to SfzPlayerTest and register Train48Portability in stable existing context. The inherited-file reason is in `tests/upstream-modifications.txt`.
- `tests/train-48-portability-check.py`: compile the affected contracts, with optional-codec and Windows macro models; registered with this report in `tests/fork-sources.txt`. Existing fork files retain their registration. No baselines, gate executables, deployment floors or warning policies change.

## Verification

Configured one private build with the owner options, exactly:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Configure exited 0. The targeted build exited 0; it was repeated after final CTest arguments and guard compatibility were corrected, also exit 0:

```sh
cmake --build build --target zene SfzPlayerTest VideoClipsTest VideoClipsGuiTest VideoTestClipTest RenderSourceGuardTest ExportWavWriteErrorTest -j12
```

Compiler controls:

```sh
python3 tests/train-48-portability-check.py --source-root . --build-dir build --moc /usr/lib/qt6/moc --qt-major 6
python3 /tmp/sol48-original-controls.py
```

The original controls copied the three original core fixture files from HEAD at the pinned base into a temporary source tree. They failed with missing makeClip, missing successfulRenderKeepsSymbolicLinkDestination in moc, and unused-function for riffSizes, respectively. The corresponding repaired compiler models passed. The committed SFZ control additionally removes the private dependency root and compiles against an error header blocking the system Abseil fallback: this fails as expected, while the real target command passes. All controls completed with exit 0. An early checker invocation before target autogen correctly failed on missing SfzPlayerTest.moc; the final run is after the rebuilt targets.

The no-FFmpeg fixture uses a shadow configuration that undefines ZENE_HAVE_FFMPEG after every inclusion of the real configuration (which has no include guard). It compiles and runs the default and both explicit palette calls, asserting failure rather than creating a clip. Its mode_video function was also run with Qt major 5 in an isolated Python invocation and exited 0; the complete configured check uses major 6. Independent review caught the initial Qt6-only package assumption; CTest now passes the configured Qt major and matching moc executable. Follow-up independent review found no remaining blocker, and the unchanged Train41-render model remains covered with its original compiler-macro assumptions.

Unchanged gates:

```sh
bash tests/no-upstream-regression-gate.sh
bash tests/fork-sources-gate.sh
bash tests/evidence-gate.sh
bash tests/unregistered-tests-gate.sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 bash tests/complexity-gate.sh --check
git diff HEAD --check
git diff --cached --check
```

All exited 0; Lizard is pinned to hosted version 1.25.1. No baselines or exemptions were written. Native Windows/macOS compilation and full-train verification remain required; Linux macro/compiler models do not claim native execution.


Final CTest command, run from this private checkout's `build/tests`:

```sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest -R '^(SfzPlayerTest|VideoClipsTest|VideoClipsGuiTest|VideoTestClipTest|RenderSourceGuardTest|ExportWavWriteErrorTest|Train48Portability|Train41-render|FileLengthGate|NoteCanvasComplexityGate)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Exited 0: **10/10 entries passed in 54.99 seconds**. All six rebuilt runtime suites reported zero skipped cases; both state-saver regressions ran against the real sfzplayer module, and the enabled FFmpeg fixture paths ran as well as the separate no-codec model. Disk stayed above 180 GB free. No commits, pushes, queue changes or edits to other source worktrees were made.

Exact additional Qt5 fixture invocation (exit 0):

```sh
python3 - <<'PY'
import importlib.util
from pathlib import Path
r = Path.cwd()
spec = importlib.util.spec_from_file_location('probe', r / 'tests/train-48-portability-check.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
m.mode_video(r, r / 'build', '5')
PY
```
