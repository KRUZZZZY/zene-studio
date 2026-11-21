# Train 39: hosted portability and Linux link disk usage

Pinned source: `7798296550396e87861e7e98c43397dcf854f506`.
Hosted build run: `43537465762`. Windows ARM, both macOS jobs, MinGW and MSVC expose four source/build portability defects; Linux x86 and ARM fail linking tests with `No space left on device`. These are distinct causes, not a memory-limit failure.

## Changes and registration

- `plugins/CarlaBase/CarlaVst2Discovery.cpp`: explicitly include QDateTime for QFileInfo::lastModified on Qt5.
- `tests/data/vst2-test-effect/vst2-test-effect.c`: request POSIX 2001 declarations, including snprintf and nanosleep, on Darwin.
- `plugins/SfzPlayer/sfizz/external/st_audiofile/src/st_audiofile_sndfile.c`: cast the memory reader's void pointer to a const byte pointer before adding its byte offset. `plugins/SfzPlayer/LICENSE-NOTICE.md` records this local vendor patch; the existing vendor exclusion remains unchanged.
- `tests/CMakeLists.txt`: give the direct-source RNNoise effect test its library export definition and register host compiler probes. Probe registration requires the corresponding host tools/features; all five execute in this repair build.
- `.github/workflows/build.yml`: Linux x86/ARM opts into `ZENE_TEST_STRIP_DEBUG`; only links in the tests directory use `--strip-debug`. Production application debug information remains intact. Each test otherwise duplicates the static core's DWARF. Objects retain debug data and test artifacts retain ordinary symbols; test selection, jobs, assertions and resource limits stay intact.
- `tests/train-39-portability-check.py`: compiler probes against actual affected source, an offset/chunk/EOF memory callback harness, a generated actual-target export-definition check with a failing import control, and a linked ELF debug/symbol/execution check.
- This report and the probe are declared in `tests/fork-sources.txt`; inherited modifications and the vendor patch reason are recorded in `tests/upstream-modifications.txt`.

## Failing then passing evidence

Run from this private source root. Logs are external temporary artifacts, not committed evidence dumps.

1. Carla, `/tmp/sol39-carla-{red,green}.log`:
   `c++ -std=c++20 -fPIC -fsyntax-only $(pkg-config --cflags Qt5Widgets Qt5Xml Qt5Network) -Iinclude -Ibuild -Ibuild/src -Ibuild/plugins/CarlaBase -Iplugins/CarlaBase/carla/source/includes -Iplugins/CarlaBase/carla/source/utils -Iplugins/CarlaBase/carla/source/backend -Iplugins/CarlaBase/carla/source -DPLUGIN_NAME=carlabase plugins/CarlaBase/CarlaVst2Discovery.cpp`
   Original exits 1 with incomplete QDateTime, explicit include exits 0.
2. VST2, `/tmp/sol39-vst2-{red,green}.log`:
   `clang -std=gnu89 -Werror=implicit-function-declaration -fsyntax-only -Itests/data/vst2-test-instrument tests/data/vst2-test-effect/vst2-test-effect.c`
   Original exits 1 with undeclared snprintf; POSIX 2001 exits 0 (existing Clang C99-loop extension warning remains).
3. SFZ, `/tmp/sol39-sfz-{red,green}.log`:
   `cc -std=c11 -Werror=pointer-arith -DST_AUDIO_FILE_USE_SNDFILE -fsyntax-only $(pkg-config --cflags sndfile) plugins/SfzPlayer/sfizz/external/st_audiofile/src/st_audiofile_sndfile.c`
   Original exits 1 on void pointer arithmetic; byte-pointer cast exits 0. The new harness additionally checks offset reads, clipped EOF reads and seeks.
4. `python3 tests/train-39-portability-check.py all`: 5/5 groups pass. The RNNoise Windows-target compiler control rejects dllimport definitions, while the configured test target exports them. Linux control executables contain DWARF; repaired executables have no debug sections, retain the global symbol, and execute successfully.
5. `python3 -m lizard tests/train-39-portability-check.py -C 10`: no threshold violations. Rechecked with Lizard 1.25.1 from an isolated temporary install: maximum CCN 8.
6. `git show --format= --binary 99f8fcabb9ba > /tmp/sol39-pending.patch` and `git apply --check /tmp/sol39-pending.patch`: exit 0. The pending Gate 7/Link teardown repair applies over this delta; it is neither superseded nor duplicated.

## Verification

- Configured with the owner-supplied all-features command using `bash /tmp/sol26-configure-command.txt` (exit 0), retaining Qt6, WASM, WERROR and the supplied private linker flags. Then `cmake -S . -B build -DZENE_TEST_STRIP_DEBUG=ON` exits 0; generated test configuration confirms requested=1 and actual test link option=1.
- `cmake --build build --target zene carlabase rnnoisedenoiser sfzplayer RnnoiseDenoiserEffectTest SfzPlayerTest SfzPlayerRealtimeTest SfzFilePoolTest CarlaVst2HostingTest CarlaVst2EffectHostingTest CarlaVst2EffectExportTest vst2-test-effect -j12`: exit 0, repeated after final CMake configuration with 17 affected build steps, exit 0. Disk remained above 68 GB free.
- From `build/tests`: `ctest -R '^(Train39-(vst2|sfz|qt5|rnnoise|disk)|RnnoiseDenoiserEffectTest|SfzPlayerTest|SfzPlayerRealtimeTest|SfzFilePoolTest|CarlaVst2HostingTest|CarlaVst2EffectHostingTest|CarlaVst2EffectExportTest)$' -j1 --timeout 900 --no-tests=error --output-on-failure`: exit 0, 12/12 entries, 4.88 s. All five compiler/ELF probe groups execute and pass. Existing Carla runtime-dependent cases skip because this private build has no Carla runtime; these entries provide build coverage, not runtime-hosting verification.
- `bash tests/no-upstream-regression-gate.sh`, `bash tests/fork-sources-gate.sh`, `bash tests/evidence-gate.sh`, `bash tests/unregistered-tests-gate.sh`: each exits 0. `bash tests/complexity-gate.sh --check`: exit 0 with local Lizard 1.24.0. Repeated as `PATH=/tmp/sol39-lizard125/bin:$PATH PYTHONPATH=/tmp/sol39-lizard125 bash tests/complexity-gate.sh --check` with hosted-version Lizard 1.25.1: exit 0, no regression, baseline not written. No gate, baseline, exemption, assertion or resource limit was changed.
- Independent read-only Luna review of the final delta reports no blocker. Its earlier Clang-discovery concern was resolved by explicitly passing CMake's detected compiler to the RNNoise probe. The refined test-only disk option also received independent review.
- The pending `sol-repair-1794619548741537751` / `99f8fcabb9ba` remains required, unchanged and replay-compatible. No repair is superseded.

Native hosted Windows/macOS verification, sufficient disk space on a complete hosted Linux build, Carla runtime hosting and the full train remain required. Local strict compiler probes reproduce the reported language/header causes but do not claim native platform coverage.
