# Train 51 hosted test portability repairs

Pinned case: `31f559ab` (`31f559ab479dcd1b822545aae477222f58f32c0d`). The audit at
`/tmp/sol51-ci-audit.md` records four source compilation causes across seven hosted
jobs. The queue entry mentioned by the audit was not used as evidence of a repair.

## Repairs and diagnosis

- `tests/src/gui/RowTableViewTest.cpp:54,57`: Qt's `itemDelegateForIndex` API is
  unavailable in the Qt 5 job. Both checks now call the effective
  `QAbstractItemView::itemDelegate(const QModelIndex&)`, retaining the two
  inserted-row delegate comparisons; this does not substitute row-only behavior.
- `tests/src/core/MidiOutTimingTest.cpp:69-260`: `measure` and
  `measureAgainstRequested` are only used by the ALSA-backed cases. Their
  constants, helpers, and those cases now share the `LMMS_HAVE_ALSA` guard, so
  MinGW's no-ALSA build no longer diagnoses unused functions. Runtime cases and
  assertions are retained.
- `tests/src/plugins/SfzDistoBuffersTest.cpp:56-61,82-86`: the Windows branch
  now uses `_aligned_malloc` / `_aligned_free` for every Windows compiler;
  non-Windows retains `posix_memalign` / `free`. Allocation tracking and
  allocation-failure checks remain enabled.
- `tests/src/gui/TouchLayoutTest.cpp:300`: the view pointer is explicitly
  `lmms::gui::ClipView`, resolving the collision with `lmms::ClipView`.

Affected audit jobs and targets: `linux-arm64`, `macos-arm64`,
`linux-x86_64`, and `macos-x86_64` (`RowTableViewTest`); `mingw64`
(`MidiOutTimingTest`); `windows-arm64` (`SfzDistoBuffersTest`); and
`msvc-x64` (`TouchLayoutTest`). The Windows-arm64 log also has an earlier
package-install stdin message, but the fatal stop is the later undeclared
`posix_memalign` compile error.

## Regression and verification

Added `tests/train-51-portability-check.py`, registered as CTest
`Train51Portability` on Linux and in `tests/fork-sources.txt`. It compiles
source-derived snippets: fixed Qt assertions under installed Qt 5 and Qt 6,
with the original Qt 5 spelling as a failing control; guarded MIDI helpers with
and without the guard under `-Werror=unused-function`; Windows aligned
allocation/free with the original POSIX-only Windows branch as a failing
control; and the qualified ClipView declaration with the original ambiguous
declaration as a failing control. `tests/all-sources.txt` is the C/C++ source
inventory, so a Python checker does not belong in that manifest.

Command:

```sh
/home/kruzzzzy/.local/share/zene-bunny-fix/duet/quiet-run train51-probe -- python3 tests/train-51-portability-check.py --source-root "$PWD"
```

Result: exit 0 after 22 seconds; the checker produced no output. Quiet-run log:
`/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-234807-sol51-implement/logs/001-train51-probe.log`.
At this first implementation step, no configure, build, CTest, or gate command
was run because the RowTableView build was assigned to a separate worker. The
later independent build and gate results are recorded below.

### Independent bounded verification (2026-11-15)

All commands below were run through `/home/kruzzzzy/.local/share/zene-bunny-fix/duet/quiet-run`.
Build ran with 176G free beforehand (stop threshold: 12G).

- `cmake --build build --target zene RowTableViewTest MidiOutTimingTest SfzDistoBuffersTest TouchLayoutTest -j12`: exit 0, 74 s. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/001-sol51-build-expanded.log`.
- `env PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest --test-dir build/tests -R '^(RowTableViewTest|MidiOutTimingTest|SfzDistoBuffersTest|TouchLayoutTest|Train51Portability|FileLengthGate|NoteCanvasComplexityGate)$' -j1 --timeout 900 --no-tests=error --output-on-failure`: exit 0, 7/7 passed. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/002-sol51-ctest.log`.
- `tests/evidence-gate.sh`: exit 0. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/003-sol51-evidence-gate.log`.
- `tests/no-upstream-regression-gate.sh`: exit 0. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/004-sol51-no-upstream-regression-gate.log`.
- `tests/fork-sources-gate.sh`: exit 0. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/005-sol51-fork-sources-gate.log`.
- Direct `tests/unregistered-tests-gate.sh` invocation was refused by quiet-run (exit 127, permission denied; log `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/006-sol51-unregistered-tests-gate.log`). Running unchanged script as `bash tests/unregistered-tests-gate.sh` then exited 0. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/009-sol51-unregistered-tests-gate-bash.log`.
- `git diff HEAD --check`: exit 0 (rechecked after this report update). Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/010-sol51-final-diff-head-check.log`.
- `git diff --cached --check`: exit 0 (rechecked after this report update). Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-235622-sol51-verify/logs/011-sol51-final-diff-cached-check.log`.

### Final checker correction (2026-11-16)

The original MIDI helper compiler control now requires all three expected
diagnostics: `measure(`, `measureAgainstRequested(`, and `unused-function`.
This ensures the source-derived anonymous-namespace helpers fail actual C++
compilation specifically with the unused-function warning promoted to an
error, rather than passing merely because compilation failed for another
reason. The checker continues to cover natural missing POSIX API failure and
optional Qt 6 when installed.

From `build/tests`, ran:

```sh
env PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest -R '^(RowTableViewTest|MidiOutTimingTest|SfzDistoBuffersTest|TouchLayoutTest|Train51Portability|FileLengthGate|NoteCanvasComplexityGate)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Result: exit 0, all 7 matched tests passed. `Train51Portability` executes the
corrected checker. Quiet-run log:
`/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261116-000246-sol51-final-verify/logs/001-sol51-final-ctest.log`.
No rebuild was needed because C++ sources were unchanged. The MIDI backend
runtime test passed here without a skip; native hosted verification and the
full train remain required.

### Configure and initial build record

The exact configure/build commands and results from `/tmp/sol51-build.md` follow.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Build command:
```sh
cmake --build build --target zene RowTableViewTest -j12
```

Results (both invoked via `/home/kruzzzzy/.local/share/zene-bunny-fix/duet/quiet-run`):
- Configure: exit 0, 54 s. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-234722-sol51-build/logs/001-sol51-configure.log`
- Build: exit 0, 355 s. Log: `/home/kruzzzzy/.cache/zene-tmp/sol/luna/20261115-234722-sol51-build/logs/002-sol51-build.log`
- Disk free before configure: 184G; lowest observed during build: 176G.
- CTest was not run.

### Final independent review

The first scoped review accepted all four C++ repairs and found two compiler-probe issues. Both were corrected: MIDI retains anonymous linkage, removes both guard directives in its original control, compiles an object and requires both helper names plus the unused-function diagnostic; SFZ retains the real posix_memalign call under minimal Windows-model headers. The extra Qt6 probe is optional and explicitly reported when unavailable, while the real Qt5 fixed/original checks remain required. The final independent review found no remaining blocker: `/home/kruzzzzy/.cache/zene-tmp/sol/020-sol51-review-corrections.log`. Its conclusion does not replace the separately executed build, 7/7 CTest and unchanged gate results recorded above. The MIDI runtime output independently confirms 3 passed, 0 failed, 0 skipped (`quiet-run sol51-midi-result`, exit 0, `/home/kruzzzzy/.cache/zene-tmp/sol/021-sol51-midi-result.log`). No tests, assertions, gate executables, baselines, warnings, dependency pins or resource limits were weakened. Native hosted and full-train validation remain required.
