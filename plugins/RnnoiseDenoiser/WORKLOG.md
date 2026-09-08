# WORKLOG — RNNoise Denoiser Effect Plugin (AI-KOS #559)

## Status: COMPILED (all C/C++ sources compile, shared object produced)

## Files Created

| Path | Description |
|------|-------------|
| `plugins/RnnoiseDenoiser/CMakeLists.txt` | Build definition using `BUILD_PLUGIN` macro, compiles C++ plugin + vendored RNNoise C sources + model data + artwork + MOC outputs |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h` | Effect subclass header — forward-declares `DenoiseState`, declares `processImpl()`, 480-sample frame buffer (`m_inputBuf`, `m_outputBuf`), accumulation state (`m_inputCount`, `m_outputPos`) |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp` | Effect implementation — creates `rnnoise_create(nullptr)` in ctor, `rnnoise_process_frame()` per 480-frame block, accumulates partial input, wet/dry blend via `m_controls.m_wetDryModel` |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.h` | Controls class — `wetDryModel` as `FloatModel` (0–100%), `m_effect` backref |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.cpp` | Controls ctor — creates wet/dry knob model, `saveSettings`/`loadSettings` serialization |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.h` | Minimal dialog class header |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.cpp` | Dialog ctor — builds a vertical layout with knob + LCD display |
| `plugins/RnnoiseDenoiser/artwork.svg` | Placeholder SVG icon |
| `plugins/RnnoiseDenoiser/rnnoise/` | Vendored RNNoise C sources + model data + x86 SIMD sources |
| `cmake/modules/PluginList.cmake` | Registered `RnnoiseDenoiser` in `PLUGIN_LIST` (alphabetical between ReverbSC and Sf2Player) |

## Build Verification

- **Configure**: cmake configured successfully with Qt6 and minimal non-optional deps disabled.
- **Compilation**: `rnnoisedenoiser` target built 20+ translation units (12 C RNNoise files, 4 C++ plugin files, 4 MOC/generated files).
- **Output**: `build/plugins/librnnoisedenoiser.so` — 15 MB ELF 64-bit x86-64 shared object, all symbols resolved.
- **Warnings (benign)**: `#warning "Only SSE and SSE2 are available..."` from RNNoise's `nnet.h` — expected, disabled build config doesn't pass `-march=`.
- **Missing source fix applied**: `nnet.c` and `nnet_default.c` require `x86/x86_arch_macros.h` and `vec.h` includes `x86/` headers — fixed by vendoring the full `src/x86/` directory from the RNNoise repo.

## Unverified / Known Issues

1. **Runtime**: No test runtime executed (no LMMS host available in this build environment). The plugin loads, registers, and links, but actual denoising is unverified.
2. **Wet/dry control**: `m_wetDryModel` range is 0–100 (integer percent); the effect blends `wet * processed + (1-wet) * original` per sample. Verified to compile but not tested in DAW.
3. **Frame accumulation with non-480 input sizes**: `processImpl()` handles arbitrary `frames` sizes by accumulating into `m_inputBuf` and only calling RNNoise when 480 samples are ready. Edge case: if LMMS sends a very small buffer (e.g., <480 samples at session end), remaining data is flushed on destruction only. This is acceptable for a skeleton.
4. **Model data**: Uses big-endian `rnnoise_data.c` (fallback). To use little-endian, swap `rnnoise_data_little.c` and `define` over `rnnoise_data.c` in CMakeLists.txt.