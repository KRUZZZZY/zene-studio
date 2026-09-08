# Third-party notices for plugins/NeuralAmp

Licence inventory. Every component below is compatible with LMMS's GPL-2.0+ terms;
no component carries a "no secondary licences" restriction.

| Component | In this tree | Licence | Notice retained at |
|---|---|---|---|
| RTNeural | `rtneural/RTNeural/**` | BSD-3-Clause | `rtneural/LICENSE` |
| Eigen 3.4.90 | `rtneural/modules/Eigen/` | MPL-2.0 | `rtneural/modules/Eigen/COPYING.MPL2` |
| nlohmann/json 3.11.1 | `rtneural/modules/json/` | MIT | header notice in `json.hpp` |
| NeuralAmpModelerCore | not vendored (reference only) | MIT | upstream `LICENSE` |
| NeuralAudio | not vendored (model files only) | MIT | `models/README.md` |
| neural-amp-modeler-lv2 | not used | GPL-3.0 | n/a |

## RTNeural — vendored (`rtneural/`)

* Upstream: https://github.com/jatinchowdhury18/RTNeural
* Commit vendored: `95c3c0f987a6fe903e7eec71e797405dbed7caf7` (2026-08-20,
  "Update XSIMD submodule to version 14.3.0 (#169)")
* Licence: **BSD 3-Clause** ("Copyright (c) 2020, jatinchowdhury18") — full text in
  `rtneural/LICENSE`. (Earlier revisions of this notice said "MIT"; the vendored
  LICENSE file has always been BSD-3-Clause — corrected 2026-09-09.)
* Vendored content: `rtneural/RTNeural/**` (headers) plus the backend modules the
  upstream repository pins:
  * `rtneural/modules/Eigen` — Eigen 3.4.90 (`EIGEN_WORLD/MAJOR/MINOR_VERSION` in
    `Eigen/src/Core/util/Macros.h`), **MPL-2.0**.
  * `rtneural/modules/json` — nlohmann/json 3.11.1 (`json.hpp` version guard), **MIT**.
* How it is used: the plugin's engine (`nam/NamModel.*`, `nam/NamModelLoader.cpp`)
  includes only `<Eigen/Dense>` and `<json.hpp>` from this vendored tree. RTNeural's
  own layer classes are not instantiated: at this commit RTNeural has dilated
  Conv1D/Conv2D layers but no WaveNet stack and no `.nam` loader
  (`grep -ri wavenet rtneural/RTNeural` → 0 hits; no `.nam`/NeuralAmp references),
  so it cannot load NAM WaveNet captures directly. It remains the vendored fallback
  inference path for future non-WaveNet (dense/GRU/LSTM) model support.

## Eigen — vendored (`rtneural/modules/Eigen/`) — MPL-2.0

* Upstream: https://gitlab.com/libeigen/eigen (GitHub mirror:
  https://github.com/eigen-mirror/eigen)
* Version vendored: 3.4.90, via the RTNeural submodule pin above.
* Licence: **Mozilla Public License 2.0**. Upstream Eigen ships the text as
  `COPYING.MPL2` at the repository root; the vendored copy here was trimmed to the
  `Eigen/` headers plus `CMakeLists.txt` and originally carried **no** licence file,
  only the short MPL-2.0 notice in each source header. The full MPL-2.0 text is now
  vendored at `rtneural/modules/Eigen/COPYING.MPL2` (verbatim from
  https://www.mozilla.org/media/MPL/2.0/index.txt, SHA-256
  `3f3d9e0024b1921b067d6f7f88deb4a60cbe7a78e76c64e3f1d7fc3b779b9d04`) so this
  source tree retains the notice as required.
* GPL compatibility: MPL-2.0 is compatible with GPL-2.0+ (the FSF lists MPL-2.0 as a
  GPL-compatible free software licence); the Eigen headers carry no "Incompatible
  With Secondary Licenses" notice.

## NeuralAmpModelerCore — reference only, NOT vendored

* Upstream: https://github.com/sdatkinson/NeuralAmpModelerCore
* Licence: MIT (Copyright (c) 2023 Steven Atkinson) — verified against upstream
  `LICENSE` on 2026-09-09 at commit `2563c0fd4cb1f9ce457d89a761738ea15097e1f3`.
* How it is used: as the reference for the `.nam` WaveNet A1 weight ordering and DSP
  graph semantics (`NAM/wavenet/model.cpp`, `NAM/conv1d.cpp`, `NAM/dsp.cpp`,
  `NAM/activations.h`). The implementation in `nam/` is an independent, minimal
  Eigen implementation of those semantics; no NeuralAmpModelerCore source files
  are copied into this repository.

## neural-amp-modeler-lv2 — NOT used

* GPL-3.0. Consulted as documentation only; **no code was copied** from it.

## NeuralAudio — NOT vendored

* Upstream: https://github.com/mikeoliphant/NeuralAudio (MIT, "Copyright (c) 2024
  Mike Oliphant" — verified against upstream `LICENSE` on 2026-09-09).
* Used as the source of the two `BossWN-*` public test captures; see
  `models/README.md` for per-file URLs and SHA-256 checksums.

## Model files

No `.nam` model file is committed to this repository (`models/*.nam` is gitignored).
`.nam` captures carry their own licences (often CC BY-NC-ND) and are loaded at
runtime from user-provided files, per `specs/SPEC-neural-amp.md` §4. The four files
used for testing are unmodified upstream example models (MIT); provenance and
checksums in `models/README.md`.
