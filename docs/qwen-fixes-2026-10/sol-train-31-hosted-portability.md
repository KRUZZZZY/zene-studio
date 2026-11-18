# Train 43 hosted portability repair

Source: c20ef591b571e3c3dc7b108107d166497f1b34be; hosted run 43611324689.
Seven failed jobs expose five causes: macOS deployment-floor barrier availability,
Qt5 QStringList assignment, MinGW QThread::create availability, Windows ARM64
POSIX clock linkage, and Windows PCRE2 patch newline hashes. These are source or
source-controlled dependency defects; the MSVC missing Ninja/compiler messages
follow the overlay failure, rather than establishing a separate infrastructure outage.

All three FIFO rendezvous and live-thread comparisons remain. Warp workers retain
their starts and joins. Windows NAM timings use GetThreadTimes kernel plus user CPU
in microseconds; POSIX keeps CLOCK_THREAD_CPUTIME_ID. PCRE2 keeps every source and
patch pin, feature and negative control. No deployment target, gate or assertion changes.

## Changed paths and reasons

The modified paths and reasons are recorded under Train 43 in
`tests/upstream-modifications.txt`; new helper, compiler regression and this report
are registered together in `tests/fork-sources.txt`.

## Verification

Configure command (owner options, final WASM=ON):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Configure exited 0. Only `build` was used; free disk stayed above 67 GB.
Targeted build command:

```sh
cmake --build build --target zene RemotePluginShmFifoTest ImportTrackIoTest SampleClipWarpEditTest NeuralAmpHarnessBlockCount -j12
```

The delegated Qt worker attempted a build despite its scoped edit task and stopped
that build after noticing the parent build. The first parent link failed because
two object files were missing; the next build completed with exit 0. No compiler
error was suppressed. A final rebuild after negative controls is recorded below.

Compiler checks:

```sh
python3 tests/train-43-portability-check.py --source-root . --build-dir build --moc /usr/lib/qt6/moc
python3 tests/vcpkg-pcre2-gitlink-test.py
python3 /tmp/sol43-negative-controls.py
```

All exited 0 on the fixed sources. The temporary negative-control driver imported
this committed probe and temporarily restored each of the three original test
sources using `git show HEAD:<path>` (always restored in `finally`). Each was
rejected: missing barrier API, missing QThread::create API, and the same ambiguous
Qt5 QStringList assignment. It also changed the Windows tick divisor from 10 to
100 and confirmed the clock fixture failed. All four controls rejected the defect.
The original PCRE2 `file(WRITE)` call with the new Windows-text-write model failed
(exit 1); explicit Unix generation passed with all three unchanged upstream hashes.
The literal token control also preserves `@TOKEN@`, `${TOKEN}` and semicolons with
the named variable defined. Existing API, missing-file and tamper controls remain.

The first runtime run passed 16/16 tests, including all affected native suites and
both unchanged metric gates. The final 17-entry command adds Train43Portability:

```sh
cd build/tests
PATH=/tmp/sol39-lizard125/bin:$PATH PYTHONPATH=/tmp/sol39-lizard125 ctest -R '^(RemotePluginShmFifoTest|ImportTrackIoTest|SampleClipWarpEditTest|NeuralAmpHarnessBlockCount|NeuralAmpHarnessRateValidation|NamLoaderTest_.*|VcpkgPcre2Gitlink|FileLengthGate|NoteCanvasComplexityGate|Train43Portability)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Gate commands from the source root:

```sh
bash tests/no-upstream-regression-gate.sh
bash tests/fork-sources-gate.sh
bash tests/evidence-gate.sh
bash tests/unregistered-tests-gate.sh
git diff --check
```

Initial execution: all exit 0; no stale registrations, no undeclared tests or source
changes, and no evidence violations. FileLengthGate and NoteCanvasComplexityGate
also pass without baseline writes (Lizard 1.25.1). Full whitespace checking has no
exclusions.

Independent Luna review confirmed FIFO visibility/lifetime, worker joins and CPU
metric meaning. Its PCRE2 literal-token concern was resolved by the one-level
placeholder plus executed literal-token fixture. CMake compatibility is unchanged:
the overlay already needs string(JSON), introduced after file(CONFIGURE). Review
also requested a faithful 32-bit DWORD in the fake Windows API; this was corrected.
The final QThread probe shadows only that class's create API, avoiding a global
create macro. Moc is supplied by the configured Qt target rather than PATH.

Native Windows, macOS and full-train hosted verification remain required. Local
compiler contracts model the failing API surfaces; they do not claim to be native
cross-platform certification. No candidate branch was excluded.


Final results: the final targeted rebuild exited 0. The exact final CTest command
passed **17/17**, exit 0, in 37.05 seconds, with zero skips. All four registration,
evidence and provenance gates exited 0 again on the final sources. `git diff
--check` exited 0. No commits, pushes, queue writes, baseline changes or gate
exemptions were made. Hosted native and full-train validation remain outstanding.
