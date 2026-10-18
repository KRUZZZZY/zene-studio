# Cluster new-p9-81-port — FIXED (port of bugs/qwen-new-p9-81)

Finding (BUG 5): `controlReadFileBytes` read a whole state or preset file with
`file.readAll()`, so a large file named by a state/preset load could exhaust memory.

## Verdict

**FIXED.** This is a port, not a merge: the three commits on `bugs/qwen-new-p9-81`
(`0b0d808fd`, `772d40642`, `058cd56f2`) were re-applied by hand onto
`050/m0-train-5` (`b02a0ff2b`). The newer line does **not** already fix this:
its reader is still unbounded.

## What changed (file:line on this branch)

- `src/core/ControlDeviceState.cpp:50-51` — two caps: 64 MiB for control data
  (state/preset), 256 MiB for project documents (matches `DataFile`'s recovery bound).
- `src/core/ControlDeviceState.cpp:60-98` — `controlReadBoundedFile`: refuses a file
  whose size is over the limit, and reads in 64 KiB chunks, refusing as soon as the
  total would pass the limit. Read errors are reported, never silently returned as empty.
- `src/core/ControlDeviceState.cpp:215-231` — `controlReadFileBytes` selects the limit
  from the purpose and calls the bounded reader (was `*bytes = file.readAll()`).
- `include/ControlDeviceSupport.h:244-252` — `ControlFileReadPurpose` enum; the new
  parameter defaults to `ControlData`, so existing callers keep the 64 MiB cap.
- `src/core/ControlCommandsCollectMedia.cpp:169,186` and
  `src/core/ControlCommandsProjectArchive.cpp:356` — `project.collect_media` and
  `project.relink` request `ProjectDocument` (256 MiB), because they legitimately read
  project documents above 64 MiB.
- `src/core/ControlCommandsScript.cpp:92-118` — `script.list` hashes each script in
  64 KiB reads (`scriptFileSha256`). This is required, not optional: once the 64 MiB
  cap applies, `script.list` would otherwise report an EMPTY sha256 for any script
  over 64 MiB. Unreadable scripts still get an empty hash, never the empty-bytes hash.
  The now-unused `ControlDeviceSupport.h` include was removed from this file.
- `docs/KNOWN-LIMITATIONS.md` — entry for the 64 / 256 MiB limits.

## Tests (regression, RED before / GREEN after)

- `tests/src/core/ControlChainPresetTest.cpp:229` `fileReaderRefusesOversizedFiles` —
  a 64 MiB+1 file must be refused and leave the output buffer unchanged.
- `tests/src/core/ControlProjectArchiveTest.cpp:772`
  `relinkAndCollectMediaAcceptProjectFilesAbove64MiB` (helper `writeOver64MiBProject`
  at line 164) — a 64 MiB+1 project must still relink and collect media.
- `tests/src/core/ControlAutomationScriptTest.cpp:556`
  `scriptListHashesScriptsLargerThanControlDataLimit` — `script.list` returns the true
  SHA-256 of a 64 MiB+1 script.

No new source files were created, so no `fork-sources.txt` registration was needed.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-81-port.r1/logs`.
All commands run from the worktree root unless marked `build/tests`. The build used
`CCACHE_TEMPDIR=/tmp/zene-ccache-tmp` because the default ccache temp dir under
`/run/user/1000` was not writable from this shell.

| Step | Exact command | EXIT |
|---|---|---:|
| Build test targets on original code (`build-2-red-tests.log`) | `cmake --build build --target zene ControlAutomationScriptTest ControlProjectArchiveTest ControlChainPresetTest -j20` | 0 |
| Build test dependencies (`build-3-plugins.log`) | `cmake --build build --target tripleoscillator amplifier bassbooster -j20` | 0 |
| **RED, original code** (`build/tests`; `test-2-red-original-deps.log`) | `ctest -R '^(ControlAutomationScriptTest\|ControlChainPresetTest)$' --output-on-failure` | 8 |
| ↳ RED assertion | `ControlChainPresetTest::fileReaderRefusesOversizedFiles()`: `'!controlReadFileBytes(path, &bytes, &error)' returned FALSE` | |
| **GREEN, fix applied** (`build/tests`; `test-3-green-fix.log`) | `ctest -R '^(ControlAutomationScriptTest\|ControlProjectArchiveTest\|ControlChainPresetTest)$' --output-on-failure` | 0 (3/3) |
| Full product diff saved (`fix.patch`) | `git diff -- include/ControlDeviceSupport.h src/core/ControlDeviceState.cpp src/core/ControlCommandsCollectMedia.cpp src/core/ControlCommandsProjectArchive.cpp src/core/ControlCommandsScript.cpp > fix.patch` | 0 |
| Mutation: reverse fix | `git apply -R fix.patch` | 0 |
| Rebuild with fix reversed (`build-5-reversed-fix.log`) | `cmake --build build --target zene <27 header-including targets> -j20` | 0 |
| **RED, fix reversed** (`build/tests`; `test-4-red-reversed.log`) | `ctest -R '^(ControlAutomationScriptTest\|ControlProjectArchiveTest\|ControlChainPresetTest)$' --output-on-failure` | 8 (1 failed: chain test, same assertion) |
| Re-apply fix | `git apply fix.patch` | 0 |
| Rebuild with fix re-applied (`build-6-reapplied-fix.log`) | same 27-target build | 0 |
| **GREEN, re-applied** (`test-5-green-reapplied.log`) | same family ctest | 0 (3/3) |
| Guard proof: revert only the three guard-covered changes | `git apply -R --include=src/core/ControlCommandsScript.cpp --include=src/core/ControlCommandsCollectMedia.cpp --include=src/core/ControlCommandsProjectArchive.cpp fix.patch` | 0 |
| Rebuild (`build-7-guard-reversed.log`) | `cmake --build build --target zene ControlAutomationScriptTest ControlProjectArchiveTest -j20` | 0 |
| **Guards RED** (`test-6-guard-reversed.log`) | `ctest -R '^(ControlAutomationScriptTest\|ControlProjectArchiveTest)$' --output-on-failure` | 8 (2/2 failed) |
| ↳ script guard | `scriptListHashesScriptsLargerThanControlDataLimit()`: `Compared values are not the same` | |
| ↳ project guard | `relinkAndCollectMediaAcceptProjectFilesAbove64MiB()`: `'relinked.ok' returned FALSE (... exceeds the 67108864-byte control file limit)` | |
| Restore guard files | `git apply --include=... fix.patch` (same three files) | 0 |
| Tree equals saved patch | `git diff -- <5 product files> \| cmp - fix.patch` | 0 |
| Rebuild (`build-8-restored.log`) | `cmake --build build --target zene ControlAutomationScriptTest ControlProjectArchiveTest -j20` | 0 |
| **Guards GREEN again** (`test-7-restored-green.log`) | same two-test ctest | 0 (2/2) |
| Header-dependent targets, with the fix (`test-8-header-dependents.log`) | ctest over the 23 other targets rebuilt against the changed header | 8 (20/23 pass; see below) |
| Patman plugin for `ControlDeviceCatalogueTest` (`build-9-patman.log`) | `cmake --build build --target patman -j20` | 0 |
| Recheck of two failing tests (`test-9-recheck.log`) | `ctest -R '^(ControlDeviceCatalogueTest\|ScriptDawRemovalUndoTest)$'` | 8 (ControlDeviceCatalogueTest passes; ScriptDawRemovalUndoTest fails again) |
| Complexity gate (`gate-1-complexity.log`) | `bash tests/complexity-gate.sh --check` | **1** |
| File-length gate (`gate-2-file-length.log`) | `bash tests/file-length-gate.sh --check` | **1** |
| Complexity gate on untouched HEAD export (`gate-3-head-complexity.log`) | same, run in `/tmp/zene-head-gate` from `git archive HEAD` | **1** |
| File-length gate on untouched HEAD export (`gate-4-head-file-length.log`) | same | **1** |

### Gate status (not a pass)

Both ratchet gates exit 1 **both on this branch and on an untouched export of HEAD**.
The regression lists are identical in both: 6 complexity entries and 14 file-length
entries, the same names. Those entries are in files this change does not touch
(for example `src/core/TakeLane.cpp`, `tests/src/core/ControlDeviceCatalogueTest.cpp`, `dotCentreAlong`
in `tests/src/gui/XyPadPairTest.cpp`). The file-length baseline does not list
`ControlAutomationScriptTest.cpp`, which was already over 500 code lines at HEAD (521);
this change grows it to 560 code lines inside the same flagged entry. No baseline or
limit was changed, as required. The gates were **not** made to pass; that needs the
baseline re-anchored by the owner.

### Other failures in the header-dependent run

The header changed, so every target including it was rebuilt. 20 of 23 pass.

- `ControlDeviceCatalogueTest` failed with `Patman plugin is unavailable`. Building the
  `patman` target fixed it (`build-9-patman.log`); it passes on recheck.
- `UndoHonestyTest::armingSeveralTracksIsOneStep` failed with
  `the take file /home/kruzzzzy/Documents/Zene Studio/zene-take-trk21.wav could not be opened for writing`.
  This is a take-recording write path, not the reader this change touches. I did not
  verify it against HEAD and did not write into that directory (outside the write-jail).
- `ScriptDawRemovalUndoTest::aRecordedDeviceIsBuiltWithoutTheAudioChangeLock` fails on
  two runs. `instantiateRecordedDevice` lives in `src/core/ControlStructuralSupport.cpp`,
  which this change does not touch, and it does not call `controlReadFileBytes`. I did not
  verify it against HEAD, so it is reported as unverified rather than pre-existing.

### Setup notes

Not changes to the code. The family needed the built test-dependency modules
`tripleoscillator`, `amplifier`, `bassbooster` (`build-3-plugins.log`); without them
`ControlAutomationScriptTest` showed four unrelated failures in `test-1-red-original.log`.

## Files committed

- `include/ControlDeviceSupport.h`
- `src/core/ControlDeviceState.cpp`
- `src/core/ControlCommandsCollectMedia.cpp`
- `src/core/ControlCommandsProjectArchive.cpp`
- `src/core/ControlCommandsScript.cpp`
- `tests/src/core/ControlChainPresetTest.cpp`
- `tests/src/core/ControlProjectArchiveTest.cpp`
- `tests/src/core/ControlAutomationScriptTest.cpp`
- `docs/KNOWN-LIMITATIONS.md`
- `docs/qwen-fixes-2026-10/cluster-new-p9-81-port.md`
