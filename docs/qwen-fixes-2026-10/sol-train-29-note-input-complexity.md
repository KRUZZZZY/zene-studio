# Train 29 NoteCanvas input complexity

Hosted quality-gates at aa20403519482cec35723ac029c61ddbb52e7e75 fail Gate 4
on NoteCanvas::mousePressEvent (CCN 11). This is a product-source metric failure,
not the previously repaired tools merge function or an infrastructure outage.
The newly nested parent lookup for empty-editor activation is extracted unchanged
into containingNoteEditor in the same anonymous namespace. Lookup order, left-click
guard, clip acquisition, editor activation, null-state refusal, focus and button
dispatch remain unchanged. There is no new public API or allocation path.

The existing complexity-regression-check.py now measures this ninth repaired
function. Its original fixtures and all eight prior targets remain enforced.
Both changed source paths and this report are explained in fork-sources.txt.
CMake registers the unchanged Gate 4 executable with --check as
NoteCanvasComplexityGate, with its reason in upstream-modifications.txt; this
lets the runner verify the actual failed gate without rewriting its baseline.
No gate, threshold, baseline or behavioral assertion changes.

## Verification

- `python3 tests/complexity-regression-check.py .` after adding the target but
  before the product refactor: exit 1, mousePressEvent CCN 11 > 10.
- `PYTHONPATH="$PWD/build/ci-python" bash tests/complexity-gate.sh --check`
  with private Lizard 1.25.1 and original product source: exit 1, same hosted
  regression. The existing local Lizard 1.24.0 also reproduces the metric failure.
- `PYTHONPATH="$PWD/build/ci-python" python3 tests/complexity-regression-check.py .`
  after extraction: exit 0, all nine functions at or under CCN 10.
- `git apply --check` of the complete binary diff a21e15e372e2^..a21e15e372e2:
  exit 0 against this delta. The pending signal/gesture/layout repair remains
  separate and required; no pending repair is superseded.

Configure (owner options, one private build):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

- `PYTHONPATH="$PWD/build/ci-python" bash tests/complexity-gate.sh --check`
  after extraction: exit 0, no regressions, baseline not written.
- The unchanged `code_lines()` function from tests/file-length-gate.sh measures
  NoteCanvasInput.cpp at exactly 500 code lines, within its unchanged 500 limit.
  An initial local assertion incorrectly required strictly fewer than 500; the
  corrected check uses the actual gate rule (at most 500) and passes.

- Configure above: exit 0. `cmake --build build --target zene NoteEditorTest NoteEditorFlTest -j12`:
  exit 0, 1244 initial build steps. Repeating the same command after CTest
  registration regenerated CMake and relinked the affected binaries, exit 0.
- From `build/tests`:
  `PYTHONPATH="$PWD/../ci-python" ctest --output-on-failure -R '^(NoteEditorTest|NoteEditorFlTest|complexity-regression-check|NoteCanvasComplexityGate)$'`:
  exit 0, 4/4 tests passed in 29.39 seconds, using Lizard 1.25.1.
- `bash tests/no-upstream-regression-gate.sh`: exit 0 on the final source delta.
- `bash tests/fork-sources-gate.sh`: exit 0, no stale declarations.
- `bash tests/evidence-gate.sh`: exit 0.
- `git diff --check`: exit 0.

The declaration and evidence gates read tracked files locally; the runner must
repeat them after committing the explicit new report path. The metric regression
fails on the original dispatcher and passes after the extraction; the behavior
suites verify the refactor against existing note input contracts.
Native hosted CI and the full train remain required. Existing unrelated
file-length debt is neither reanchored nor exempted here.
