# Train 30 hosted build follow-up (failure 41)

Base: `40b4322860e9c2133f412a4b921b63b057dd710c`, hosted build run
`43569206372` (2026-11-14). Seven failures represent five source portability
causes plus one dependency-service failure; this was not a resource kill.

## Changes and file registration

| Path | Reason |
| --- | --- |
| `tests/src/core/MountNamespaceTestSupport.h` | Check both pipe notifications, retry EINTR, reject failed/short writes, close the parent write end before awaiting the reply. |
| `tests/src/core/MountNamespaceHandshakeTest.cpp` | Deterministic EINTR, EBADF, short-write and exact-byte regression. |
| `plugins/Eq/EqSpectrumView.h` | Use QVector for the contiguous copied spectrum on both Qt5 and Qt6. |
| `tests/src/core/CrashReporterArmTest.cpp` | Derive the interposer exception specification from the platform declaration; allow Darwin's function-like sigemptyset macro. |
| `tests/src/core/RenderSourceGuardTest.cpp` | Compile the POSIX-only queue helper under its callers' existing platform guard. |
| `tests/src/core/NamModelLoaderTest.cpp` | Replace filesystem fixture plumbing with unique native temporary directories, stat and RAII cleanup; preserve all loader refusal and file-size assertions and the macOS deployment floor. |
| `tests/train-41-portability-check.py` | Strict compiler probes against the actual changed translation units and both sigaction declaration contracts. |
| `tests/Qt5QFileInfoDateCheck.py` | Retain the independently accepted pending QFileInfo/QDateTime compiler regression unchanged. |
| `docs/qwen-fixes-2026-10/cluster-ci-84e380615213284e.md` | Retain that repair's original report unchanged. |
| `tests/CMakeLists.txt` | Register the handshake executable, six compiler probes and retained date-accessor check. |
| `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | Declare new paths and inherited changes with reasons. |
| This report | Record reproduction, verification, review and remaining hosted blocker. |

The pending repair `ci-repair-ci-84e380615213284e-dc4ee443d591`
(commit `05bed25c6f60d3929a9624d91e80a1375b732c40`) cannot replay: its
Carla include is already present and its CMake/manifest context has moved.
This replacement retains its script and report byte for byte, and registers its
check in stable context. Supersede that pending ID; do not discard its regression.

## Reproduction and verification

Configured the one private build (exit 0):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Targeted build (exit 0):

```sh
cmake --build build --target zene RecordToolsTest CrashReporterArmTest RenderSourceGuardTest NamModelLoaderTest eq AudioPluginTest EqPeakHandoffTest AudioAnalysisTest MountNamespaceHandshakeTest -j12
```

Negative controls use HEAD sources copied into temporary directories; no other
worktree is edited. Strict namespace compilation uses code generation, since
`-fsyntax-only` does not reliably expose fortified unused-result diagnostics:

```sh
c++ -std=c++20 -O2 -D_FORTIFY_SOURCE=2 -Werror=unused-result -fPIC $(pkg-config --cflags Qt6Core) -I. -c /tmp/sol41-namespace-probe.cpp -o /tmp/sol41-namespace-probe.o
```

The probe includes the namespace helper and calls enterOwnMountNamespace.
Original helper: exit 1 at both ignored write calls. Repaired helper: exit 0.
Actual Qt5 compilation of EqSpectrumView.cpp with the HEAD header fails at the
QList sized constructor and data access; the QVector header passes:

```sh
c++ -std=c++20 -fPIC -fsyntax-only $(pkg-config --cflags Qt5Widgets Qt5Xml Qt5Network) -Iinclude -Ibuild -Ibuild/src -Iplugins/Eq -Ibuild/plugins/Eq -I/usr/include/cairo -DPLUGIN_NAME=eq plugins/Eq/EqSpectrumView.cpp
```

The four remaining negative controls invoke the registered probe functions with
original HEAD translation units substituted into the build compilation database.
Each fails for the intended cause: throwing sigaction declaration versus
unconditional noexcept, scope-qualified function macro, forbidden filesystem
include, and the unused queueRender helper with Windows slots excluded.
The render check generates its own Windows-mode moc and emits an object with
`-Werror=unused-function`; it does not reuse the incompatible Linux-mode moc.

```sh
python3 /tmp/sol41-negative-controls.py
```

Exit 0 means all four original-source compilations failed as required. Evidence:
`/tmp/sol41-negative-controls.log`; namespace and Qt5 red/green evidence:
`/tmp/sol41-namespace-{red,green}.log`, `/tmp/sol41-eq-{red,green}.log`.
These are local compiler-contract checks, not native Windows/macOS certification.

Final targeted CTest command (from build/tests; result recorded below):

```sh
PATH=/tmp/sol39-lizard125/bin:$PATH PYTHONPATH=/tmp/sol39-lizard125 ctest -R '^(RecordToolsTest|CrashReporterArmTest|RenderSourceGuardTest|NamModelLoaderTest|AudioPluginTest|EqPeakHandoffTest|AudioAnalysisTest|MountNamespaceHandshakeTest|Train41-(linux|eq|darwin-signature|darwin-tu|nam|render)|Qt5QFileInfoDateCheck|FileLengthGate|NoteCanvasComplexityGate|complexity-regression-check)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Lizard is 1.25.1, matching hosted CI. Gates and baselines are unchanged.

## Independent review and external blocker

Read-only Luna review checked assertion retention, namespace error paths, NAM
fixture ownership, pending-repair equivalence and the hosted logs. Its sole
initial blocker was this missing report, now supplied. Final validation and
review results follow below.

MSVC issue `ci-b38398fa7833e0a4`: job `132369662829` fails during vcpkg
configuration, before source compilation. Its log lines 1036-1038 show HTTP 403
from GitHub's PCRE2 contents API for `deps/sljit?ref=pcre2-10.49`, classified
non-transient by vcpkg. The evidence does not identify quota or authentication
as the cause. CI infrastructure must inspect response/auth/proxy diagnostics and
restore dependency access, then rerun the native job. No speculative workflow,
secret, deployment-floor or warning-policy changes are included. The full train
and native hosted builds remain required.

## Final results

- Targeted build: exit 0 (`/tmp/sol41-build-verified.log`).
- Exact CTest command above: exit 0, **18/18 entries passed**, 162.96 seconds
  (`/tmp/sol41-ctest.log`). All eight runtime executables passed. RecordToolsTest
  contains one existing permission-based skip for an unprivileged mount namespace;
  the deterministic handshake executable ran successfully.
- Retained Qt5QFileInfoDateCheck: 47 translation units checked, seven skipped by
  its unchanged baseline policy because the Qt6-generated moc includes
  qtmochelpers.h unavailable in the local Qt5 headers. Carla passed on both
  inspected toolchain shapes. These skips do not certify the skipped Qt5 test TUs.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/fork-sources-gate.sh`: exit 0, zero stale entries.
- `bash tests/evidence-gate.sh`: exit 0, zero refused files.
- `bash tests/unregistered-tests-gate.sh`: exit 0.
- `git diff --check`: exit 0.
- `PATH=/tmp/sol39-lizard125/bin:$PATH PYTHONPATH=/tmp/sol39-lizard125 lizard -C 10 -w tests/train-41-portability-check.py tests/src/core/MountNamespaceHandshakeTest.cpp tests/src/core/MountNamespaceTestSupport.h tests/src/core/NamModelLoaderTest.cpp`: exit 0, no functions above CCN 10.
- Final independent read-only Luna review: approved, no blockers; confirmed all
  assertions retained and both pending files byte-identical to the accepted commit
  (`/tmp/sol41-final-review.log`). Native hosted and full-train verification remain
  outstanding. Free disk space remained above 60 GiB.
