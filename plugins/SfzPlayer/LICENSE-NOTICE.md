# SFZ Player - third-party notice

The SFZ Player (`SfzPlayer*.cpp/.h`, `SfzSynth*`, `RtPublished.h`) is Zene Studio code, GPL-2.0-or-later like the rest
of the program. It plays SFZ instruments with **sfizz 1.2.3** (https://github.com/sfztools/sfizz, BSD-2-Clause),
vendored under `sfizz/` and built into the module as static libraries on every platform. The sfizz release tarball
(which carries its dependencies) is the source. **One change** was made to the files that were kept: in
`sfizz/src/sfizz/Synth.cpp`, `Synth::renderBlock` no longer waits for the background loaders when freewheeling and no
longer runs the file pool's garbage collection, when the module is built with `ZENE_SFIZZ_NO_RENDER_THREAD_WAITS` (the
module always is). Both took a lock or waited inside the render callback. Without the collection, a sample that was
streamed to its end stays in memory until the synth is rebuilt (a reload, an export, a project close).

| Path under `sfizz/` | What | Licence |
| --- | --- | --- |
| `src/sfizz/`, `src/sfizz.h`, `src/sfizz.hpp`, `src/sfizz_message.h` | the sfizz engine and its public API | BSD-2-Clause (`LICENSE`, `AUTHORS.md`) |
| `external/abseil-cpp/` | Abseil LTS 20230125 (strings, containers, span...), tests removed | Apache-2.0 |
| `external/atomic_queue/`, `external/threadpool/`, `external/jsl/` | lock-free queue, thread pool, utility headers | MIT / BSD-style, see each `LICENSE` |
| `external/cephes/` | Bessel function used by the windowed-sinc interpolator | public-domain-style, see `LICENSE` |
| `external/filesystem/` | ghc::filesystem | MIT |
| `external/simde/` | SIMD Everywhere (portable SIMD intrinsics) | MIT |
| `external/st_audiofile/` (`src/` only) | sfizz's audio-file reader, built on **libsndfile** (already a Zene dependency) | BSD-2-Clause |
| `src/external/{cpuid,hiir,kiss_fft,pugixml,spline,tunings}/` | CPU feature detection, half-band filters, FFT, XML, splines, Scala tunings | MIT / BSD-style / Boost, see each `LICENSE` |

Left out of the vendored tree (not needed to play SFZ files): sfizz's plug-in wrappers and UIs, the import filters
(`src/sfizz/import`, DecentSampler), the Faust `.dsp` sources (their generated `.hxx` output is kept), the render
and JACK clients, tests, benchmarks, fmidi, cxxopts, and the bundled WavPack / dr_libs / stb_vorbis / libaiff
decoders (`st_audiofile` uses libsndfile instead). `sfizz/src/sfizz/Config.h` is the file sfizz's CMake generates, with
512 MIDI controllers.

Abseil is Apache-2.0, which is compatible with GPL-3.0 and so with Zene's GPL-2.0-or-later licensing.
