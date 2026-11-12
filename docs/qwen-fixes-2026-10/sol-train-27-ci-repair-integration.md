# Train 27: integrate pending CI repairs

The train stopped while replaying `099cc85e2e`: its upstream-modifications ledger
append conflicted with newer train declarations. This replacement integrates the five
remaining independently reviewed repairs together, preserving the two repairs already
applied by the runner and the accepted punch-range integration.

- ARM64 Windows: retain all four atomic_queue member-call corrections and its compiler
  regression, vendor notice and provenance declaration.
- Both macOS repairs: combine their identical missing `<unistd.h>` fix once, remove
  the probe-side include that masked it, and retain the additional no-`__unix__`
  compile probe as a dependency of RemotePluginShmFifoTest.
- MinGW: retain the committed public keyring, signed-by provisioning and signature
  regression with authentic release and legacy-key fixtures; signature enforcement stays on.
- ARM64 Linux: all three test source files already equal `cdd767c8b8`, supplied by the
  applied x86_64 repair; retain its independently reviewed report without replaying edits.

All original reports and new-file registrations are retained. No gate executable,
limit, assertion or complexity baseline is changed. Native hosted platform builds
remain required; the local build uses the owner-prescribed Qt6 configuration.

## Verification

- Configure: the exact owner-provided `cmake -S . -B build -G Ninja ... -DWANT_WASM=ON`
  command, including all dependency switches and ccache flags: exit 0.
- `python3 tests/clang-template-keyword-check.py --root . --compiler clang++`:
  original HEAD atomic_queue header exit 1 (four offending calls), restored repair exit 0;
  both real queue templates instantiate successfully.
- `bash tests/mingw-ppa-key-check.sh`: exit 0, including signed release verification,
  legacy-key rejection and tampered-release rejection.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/evidence-gate.sh`: exit 0.

- `cmake --build build --target zene RemotePluginShmFifoTest SfzFilePoolTest ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j12`: exit 0, all 1,475 steps completed.
- With only `include/RemotePluginBase.h` temporarily restored to HEAD:
  `cmake --build build --target RemotePluginShmFifoNoSysIpcAppleMacroProbe -j12`
  exits 1 with undeclared `getpid` and `usleep`. The repaired header was restored in
  a `finally` block, then `cmake --build build --target zene RemotePluginShmFifoTest -j12`
  exits 0 (including both no-SysV compile probes).
- From `build/tests`, `ctest -R '^(RemotePluginShmFifoTest|SfzFilePoolTest|ControllerScriptTest|ExternalMidiDeviceTest|ExternalDeviceCardTest|ClangTemplateKeywordCheck|MingwPpaKeyGate)$' --output-on-failure`:
  exit 0, **7/7 passed**, 3.92 seconds, none skipped.
- `bash tests/fork-sources-gate.sh`: exit 0.
- `git diff --check`: exit 0.

Gate 6 and gate 11 were rerun after merging all provenance declarations: both exit 0.
They scan committed paths/history; the runner must rerun them after committing the
explicit repair paths to cover the new files as tracked content. No private-build
runtime test substitutes for native hosted CI. Final free disk: 116 GB.


## Replacement identities

Supersede only these five pending records (the earlier supersession and x86_64
Linux repairs remain active):

- ci-repair-ci-165e1914b308f189-8811f83254fa
- ci-repair-ci-4e020460347493c3-96d16dea4c4d
- ci-repair-ci-f0fa6219c5d4b443-ed52a98c744f
- ci-repair-ci-3eb9bda8aa6785a3-8f1eebeeaa53
- ci-repair-ci-7d1b70f4f0a0b12b-d8337eaf3271

Patch audit: unique files equal their accepted repair snapshots byte for byte.
The macOS header and probe equal the ARM64 repair; the x86_64 implementation has
the same include and removal with different comments. Its additional CMake compile
probe is preserved. The three ARM64 Linux test files already equal its accepted
snapshot, so no duplicate test delta is needed.

## Effective configure command

The repeated WANT_WASM argument resolves to ON; the generated cache records this
command with the same effective configuration (build type spelling is CMake-normalized).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CLAP:STRING=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3:STRING=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DWANT_WINMM:BOOL=OFF -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DLMMS_CLAP_FETCH:BOOL=ON -DONNXRUNTIME_ROOT:UNINITIALIZED=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER:UNINITIALIZED=ccache -DCMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=ccache '-DCMAKE_CXX_FLAGS:STRING=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS:STRING=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```
