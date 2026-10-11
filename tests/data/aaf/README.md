# aaf — an AAF file with embedded audio, for AafImportTest (WF-038)

`PR_WAV_Internal.aaf` is an AAF exported by Adobe Premiere Pro 23.5: one mono
audio track ("Audio 1") with one clip, a 29.97 fps (30000/1001) edit rate, a
48 kHz 16-bit WAV essence EMBEDDED in the file, and one empty video track.
`tests/src/core/AafImportTest.cpp` reads it (`interchange.aaf_read`) and
imports it (`interchange.aaf_import`) when the build has LibAAF, and skips
those two cases when it does not.

## Contents

| File | What it is |
|------|------------|
| `PR_WAV_Internal.aaf` | 344 KiB, sha256 `c4f3c232d255642940702a0a220e598b995e0ec8991d124d861598a61dd6211b` |
| `PR_WAV_Internal_TrackGain_250pct.aaf` | Derived fixture with a 250% top-level track gain, for the dry-run clamp regression |

`PR_WAV_Internal_TrackGain_250pct.aaf` is derived from the Premiere fixture
above. Its mono audio track has a top-level Mono Audio Gain parameter set to
250% (250000000/100000000), with the embedded essence retained. This fixture
drives `interchange.aaf_read` through the command path and checks the serialized
`loss.track_gain_clamps` field; the unmodified fixture keeps the in-range check.

## Provenance and licence

Copied unmodified from LibAAF's test suite, `test/aaf/PR_WAV_Internal.aaf`, at
LibAAF commit `7925dff4026be06b4edfacdfe458887c2d6503ca`
(https://github.com/agfline/LibAAF). LibAAF, its test files included, is
distributed under the GNU General Public License version 2 or (at your option)
any later version - the repository's `LICENSE` and every source header say so,
and the test files carry no separate licence. GPL-2.0-or-later is compatible
with this project's GPL-2.0-or-later.

## What the import should find

Read with `aaftool --aaf-clips --pos-format raw`: the clip starts at edit unit
107892, is 30 edit units long, and starts 1 edit unit into its source. At the
composition's 48 kHz that is a 48048-frame clip with a 1602-frame source offset
(1601.6, rounded). The essence is 51252 frames long.
