# NeuralAmp model files — provenance and licences

All four `.nam` files in this directory are **unmodified upstream example/test models**.
None were generated, trained, or altered by this project. Every file was re-downloaded
from the exact URL below on 2026-09-08 and its SHA-256 compared byte-for-byte against the
copy in this directory (all four match).

| File | Upstream repo | Path on `main` | SHA-256 | Licence |
|---|---|---|---|---|
| `A2.nam` | [sdatkinson/NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | `example_models/A2.nam` | `2d2d744516dc0197737a2c5001010429692d4cb20d72b08264781de626fcf4ca` | MIT |
| `wavenet_a1_standard.nam` | [sdatkinson/NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | `example_models/wavenet_a1_standard.nam` | `ceb53469a19ce278e2235da982ae676cb8d5451a8de22a7ecc7a2617d07224d1` | MIT |
| `BossWN-nano.nam` | [mikeoliphant/NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) | `Utils/Models/BossWN-nano.nam` | `747bd1d2afd1efe3aa84a851112984dca3f4082e36af4db9f74ffe1e94d57f11` | MIT |
| `BossWN-standard.nam` | [mikeoliphant/NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) | `Utils/Models/BossWN-standard.nam` | `0474d8e1593f9063b268c4d1636ce84ff62186ad1dfcc66533eceba39f952d65` | MIT |

Exact download URLs (verified reachable, HTTP 200, checksum match on 2026-09-08):

- `https://raw.githubusercontent.com/sdatkinson/NeuralAmpModelerCore/main/example_models/A2.nam`
- `https://raw.githubusercontent.com/sdatkinson/NeuralAmpModelerCore/main/example_models/wavenet_a1_standard.nam`
- `https://raw.githubusercontent.com/mikeoliphant/NeuralAudio/main/Utils/Models/BossWN-nano.nam`
- `https://raw.githubusercontent.com/mikeoliphant/NeuralAudio/main/Utils/Models/BossWN-standard.nam`

Repos cloned for verification at these commits:

- `NeuralAmpModelerCore`: `2563c0fd4cb1f9ce457d89a761738ea15097e1f3` (branch `main`, licence file `LICENSE` = MIT)
- `NeuralAudio`: `c5275bdb39db5fa7f547f50535ac75cc45aa1185` (branch `main`, licence file `LICENSE` = MIT, "Copyright (c) 2024 Mike Oliphant")

## Notes

- **A2.nam is a `SlimmableContainer` (A2) model.** This plugin's engine implements
  WaveNet A1 only; `NamModelLoader` rejects A2 files with
  `unsupported architecture 'SlimmableContainer' (this build supports WaveNet A1 only)`.
  The file is kept as a real-world negative test and as the reference for a future A2 port.
- **No synthetic models.** The previous lane's hand-off note said "real models downloaded
  (`A2.nam`, `BossWN-nano.nam`)"; the checksum table above confirms all four files are
  byte-identical to upstream, so nothing here was synthesised locally.
- `BossWN-nano.nam` (842 weights) and `BossWN-standard.nam` (13 802 weights) are the
  small and standard WaveNet variants used for the CPU measurements in
  `../../../../NEURAL-AMP.md`.
