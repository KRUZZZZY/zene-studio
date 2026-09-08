# Third-party notices for plugins/NeuralAmp

## RTNeural — vendored (`rtneural/`)

* Upstream: https://github.com/jatinchowdhury18/RTNeural
* Commit vendored: `95c3c0f987a6fe903e7eec71e797405dbed7caf7` (2026-08-20,
  "Update XSIMD submodule to version 14.3.0 (#169)")
* Licence: MIT — see `rtneural/LICENSE`
* Vendored content: `rtneural/RTNeural/**` (headers) plus the backend modules the
  upstream repository pins:
  * `rtneural/modules/Eigen` — Eigen 3.4.90, MPL2 (`Eigen/src/Core/util/Macros.h`)
  * `rtneural/modules/json` — nlohmann/json 3.11.1, MIT
* How it is used: this plugin uses the Eigen and nlohmann/json headers that RTNeural
  vendors for the in-plugin `.nam` engine (`nam/NamModel.*`, `nam/NamModelLoader.cpp`).
  RTNeural's own layer classes are not instantiated: as of the vendored commit
  RTNeural has no WaveNet / dilated-convolution layer and no `.nam` loader
  (`grep -ri dilat rtneural/RTNeural` returns nothing), so it cannot load NAM
  WaveNet captures directly. It remains the vendored fallback inference path for
  future non-WaveNet (dense/GRU/LSTM) model support.

## NeuralAmpModelerCore — reference only, NOT vendored

* Upstream: https://github.com/sdatkinson/NeuralAmpModelerCore
* Licence: MIT (Copyright (c) 2023 Steven Atkinson) — `LICENSE` inspected at
  commit `1b1b1b0`-era `main` (see WORKLOG for the exact clone date)
* How it is used: as the reference for the `.nam` WaveNet A1 weight ordering and DSP
  graph semantics (`NAM/wavenet/model.cpp`, `NAM/conv1d.cpp`, `NAM/dsp.cpp`,
  `NAM/activations.h`). The implementation in `nam/` is an independent, minimal
  Eigen implementation of those semantics; no NeuralAmpModelerCore source files
  are copied into this repository.

## neural-amp-modeler-lv2 — NOT used

* GPL-3.0. Consulted as documentation only; **no code was copied** from it.

## NeuralAudio — NOT vendored

* Upstream: https://github.com/mikeoliphant/NeuralAudio (MIT).
* Used as the source of public test captures; see `models/README.md`.

## Model files

No `.nam` model file is committed to this repository. `.nam` captures carry their own
licences (often CC BY-NC-ND) and are loaded at runtime from user-provided files, per
`specs/SPEC-neural-amp.md` §4.
