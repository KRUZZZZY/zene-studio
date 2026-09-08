# RnnoiseDenoiser — licensing notice

Task: AI-KOS #559 (`lmms-ai-dsp-mission`). Status: runtime-verified (see `RUNTIME-TEST.md`, `WORKLOG.md`).

## This plugin

`RnnoiseDenoiser` is part of LMMS and is distributed under the same terms as LMMS
(GPL-2.0-or-later). It links the vendored RNNoise library below.

## Vendored RNNoise

- Location: `rnnoise/` (vendored source; canonical upstream <https://gitlab.xiph.org/xiph/rnnoise>).
- Licence: **BSD-3-Clause** — full text in `rnnoise/COPYING`.
  Copyright (c) 2007-2017, 2024 Jean-Marc Valin; (c) 2023 Amazon;
  (c) 2017 Mozilla; (c) 2005-2017 Xiph.Org Foundation; (c) 2003-2004 Mark Borgerding.
- GPL compatibility: BSD-3-Clause is a permissive, GPL-compatible licence — it adds no
  restriction that conflicts with GPL-2.0-or-later. Only the copyright notice, the
  condition list and the disclaimer must be retained, which `rnnoise/COPYING` does.
  (Matches `findings-ai-dsp.md` §4: the AI dependencies chosen for this program are
  MIT/BSD only, so the GPLv2-clean rule in `AGENTS.md` holds.)

## Model data / endianness

- `CMakeLists.txt` selects `rnnoise/rnnoise_data.c` (the big-endian fallback table).
- `rnnoise/rnnoise_data_little.c` is also vendored for a little-endian build.
- The endianness choice is **unverified on non-x86 targets** (WORKLOG item 4). A
  portability pass should either select the table by target endianness at configure
  time or add a compile-time assertion; x86_64 is unaffected.

## Not covered here

Third-party sample content, models or IRs supplied by users are their own licensing
problem; this plugin ships no data beyond the RNNoise model tables above.
