# Vendored MTS-ESP client and master API (board card #712)

What these files are, exactly where they came from, and what licence they
carry — recorded beside them the way `thirdparty/` vendoring is recorded
elsewhere in this tree, with the upstream blob identities verified rather than
assumed.

## Provenance

- **Upstream:** ODDSound/MTS-ESP — <https://github.com/ODDSound/MTS-ESP>
  ("A simple but versatile C/C++ library for adding microtuning support to
  audio and MIDI plugins").
- **Exact revision:** branch `main`, commit
  `f214739b8832e7f297cb9970d0c0efbf783f1462` (2026-02-04, "Update README.md";
  that commit only touched `README.md`, so the four sources below are older
  than it and unchanged by it).
- **Upstream paths → local paths:**

  | upstream (at that commit)                              | local                      |
  |--------------------------------------------------------|----------------------------|
  | `Client/libMTSClient.h` (10529 bytes)                   | `libMTSClient.h`           |
  | `Client/libMTSClient.cpp` (38387 bytes)                 | `libMTSClient.cpp`         |
  | `Master/libMTSMaster.h` (9374 bytes)                    | `libMTSMaster.h`          |
  | `Master/libMTSMaster.cpp` (13438 bytes)                 | `libMTSMaster.cpp`         |
  | `LICENSE` (661 bytes)                                   | `LICENSE`                  |

- **Verification (2026-09-22):** each local file's git blob identity
  (`git hash-object <file>`) equals the upstream blob sha at that commit:

  | file              | blob sha                                                    |
  |-------------------|-------------------------------------------------------------|
  | `libMTSClient.h`  | `dee12c3b05345cfeef698d110aeab0b89a638a66`                  |
  | `libMTSClient.cpp`| `9bbfb1ee0525fe529a27d4d5d1da346f6cd73a30`                  |
  | `libMTSMaster.h`  | `aec42025d7d04b227ad6e830894241cf5e0832e6`                  |
  | `libMTSMaster.cpp`| `552aeeb5195d59d05779c9ca9bec707f9ba75490`                  |
  | `LICENSE`         | `5d77a0bfdbb69fb2bae0c984afadda0c8f7b3a41`                  |

  So the vendored files are byte-identical copies, not edits.

## Licence — verified, not assumed

- The **repository's own licence metadata** is SPDX **`0BSD`** (BSD Zero Clause
  License), per the GitHub API licence endpoint for ODDSound/MTS-ESP.
- The **files' own headers say what we claim**: each of the four source files
  begins with `Copyright (C) 2021 by ODDSound Ltd. info@oddsound.com` followed
  by the 0BSD grant, verbatim:

  > Permission to use, copy, modify, and/or distribute this software for any
  > purpose with or without fee is hereby granted.

  followed by the ISC-style disclaimer paragraph ("THE SOFTWARE IS PROVIDED
  "AS IS"..."). `LICENSE` holds the same text; its blob identity is verified
  above.
- **GPLv2-clean:** 0BSD is permissive and fully compatible with this tree's
  GPL-2.0-or-later licence. The upstream copyright and permission lines are
  retained unmodified (rule: retain upstream notices). No Steinberg VST2
  material is involved.

## How this tree uses them

- `src/core/SessionTuning.cpp` compiles **both** translation units (they are
  registered in `src/core/CMakeLists.txt` with the rest of the board card #712
  block) and calls the **master** side: when the session-wide tuning table is
  armed with `mts.master_set`, the table is published through
  `MTS_RegisterMaster()` / `MTS_SetNoteTunings()` / `MTS_SetScaleName()` so
  MTS-ESP **client** plugins loaded anywhere in this host follow Zene's
  session-wide tuning. The client half is vendored and compiled because it is
  the other half of the pair upstream ships (and the natural hook for a future
  "follow an external MTS-ESP master" mode); no code calls it yet — that
  absence is recorded in `docs/KNOWN-LIMITATIONS.md`.
- **These two files are shims, not the IPC core.** Upstream ships the actual
  shared-memory core as a separately-installed `libMTS.so` (Linux:
  `/usr/local/lib/libMTS.so`, macOS: `/Library/Application Support/MTS-ESP/
  libMTS.dylib`, Windows: `MTS-ESP\LIBMTS.dll` under Program Files\Common —
  see `load_lib()` in both files, and upstream's README "libMTS" section).
  At static initialisation each wrapper `dlopen`s that library if it is
  installed and forwards every `MTS_*` call to it; **with no installed
  `libMTS.so` every master call is an inert no-op** (`if (global.X) global.X()`).
  This tree does not ship or install `libMTS.so`: `SessionTuning` probes for it
  and `mts.master_set` returns a typed refusal naming the path when it is
  absent, so a build on a box without the MTS-ESP library reports its own state
  instead of pretending to publish.
