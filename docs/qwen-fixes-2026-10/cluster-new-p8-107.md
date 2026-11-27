FIXED

# Cluster new-p8-107: a compressed project load allocated from an unchecked size header

## Verdict: REAL

`DataFile::loadData` (`src/core/DataFile.cpp`, the `qUncompress( _data )` in the parse-failure branch) inflated any
payload whose plain parse failed, with no look at the 4-byte big-endian size a `qCompress` stream declares. Qt sizes its
output buffer from that header before it knows whether the stream is valid, so a few bytes claiming ~4 GB asked for ~4 GB.
Recovery already guarded the same header (`decompressesAsRecoverableDocument`, `src/core/DataFile.cpp:327`, using
`declaredUncompressedSize` :256 and `maxRecoveredDocumentBytes` :239); the normal load path did not.

All three `DataFile` constructors (path, `QByteArray`, container document) reach `loadData`, and `loadData` held the only
other `qUncompress` in the file, so there is one place to fix. Other `qUncompress` callers in `src/core` (the checkpoint,
`SongUndoSnapshot`, `RevisionTimelineCompare`, the control archive/assets code) are separate paths with their own bounds
or comments and are not changed here.

## Fix

`src/core/DataFile.cpp`: new helper `inflateProjectPayload()` (next to `decompressesAsRecoverableDocument`). When
`declaredUncompressedSize( data ) > maxRecoveredDocumentBytes()` (the 256 MiB recovery limit, same value and same test
seam) it sets `errorMsg`, sets `line = col = 0`, and returns an empty array without calling `qUncompress`. `loadData`
calls it instead of `qUncompress( _data )`. Because `line`/`col` are 0, the existing error branch (`qWarning`, error box
only with a GUI, `return`) runs, so the load never continues on an empty document. A compressed payload within the limit
takes exactly the old path; plain XML never reaches the helper.

Not touched: fsync, `.new` ownership, recovery.

## Regression test

`tests/src/core/DataFileFormatTest.cpp`
- `oversizedCompressedHeaderIsRefusedBeforeInflating`: 4-byte header `0xF0000000` (~3.75 GB) followed by 64 bytes of `Z`,
  into `DataFile( QByteArray )`. Asserts: empty `documentElement()`, type `Unknown`, no Qt message starting with
  `qUncompress` (Qt warns that on every stream it fails to inflate, so its absence shows qUncompress was never called),
  and on Linux the process `VmPeak` grew by less than 1 GiB.
- `validCompressedProjectStillLoads`: `qCompress` round trip of a valid project loads as `SongProject`.

## Commands (copied from the logs; folder `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-107.r1/logs`)

| Step | Command | EXIT |
|---|---|---|
| RED build (test only) | `cmake --build build --target zene DataFileFormatTest -j4` (`build-1-red.log`) | 0 |
| RED test | `cd build/tests && ctest -R '^DataFileFormatTest$' --output-on-failure` (`test-1-red.log`) | 8: `oversizedCompressedHeaderIsRefusedBeforeInflating` FAIL, `qUncompress: Input data is corrupted`; 18 passed, 1 failed |
| GREEN build | same target (`build-2-green.log`) | 0 |
| GREEN test | same ctest (`test-2-green.log`) | 0 |
| 4b revert | `git diff -- src/core/DataFile.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 |
| 4b red build | same target (`build-3-red.log`) | 0 |
| 4b red test | same ctest (`test-3-red.log`) | 8: same test fails at `DataFileFormatTest.cpp(295)`; 18 passed, 1 failed |
| 4b re-apply | `git apply .../fix.patch` | 0 |
| 4b green build | same target (`build-4-green.log`) | 0 |
| 4b green test | same ctest (`test-4-green.log`) | 0 (19 passed, 0 failed) |
| Family build | `cmake --build build --target DataFileSaveIntegrityTest ProjectOpenIntegrityTest DataFileContainerRecoveryTest ProjectVersionTest -j4` (`build-5-family.log`) | 0 |
| Family test | `ctest -R 'DataFile\|ProjectOpenIntegrity\|ProjectVersion'` (`test-5-family.log`) | 0, 5/5 passed |

The red and green builds were run by a Haiku assistant; I checked the `test-*.log` contents myself.

## Gates

`python3 .../feature_check.py <worktree>` did not run. Its baseline step runs `git -C .../gate-baselines/tree checkout -q
--detach 9913d36d53...`, and the lane guard refused it (`gates-1.log`: `bunny-guard: ... is not allowed in a Bunny lane`,
script EXIT=1, `CalledProcessError ... status 97`). Per the brief I did not look for a way around it. Checked by hand
instead: the new function has CCN 2; `loadData` gains no branch (the helper is a drop-in for the old call); the test file is
~400 lines; no new file was added. `tests/upstream-modifications.txt` has a new row for `src/core/DataFile.cpp` (the file
ends with a newline). The automatic gates are unverified by me.

## Realtime / undo / commands

Load-time code only. No audio-thread code, no undo state, no control command added.

## Self-check

- Red/green: with the product change reverted the test exits 8 (`test-3-red.log`); with it applied it exits 0
  (`test-4-green.log`); table above.
- Deterministic: no timing, sleeps or threads. The inputs are fixed bytes (`0xF0000000` header + 64 `Z`). The checks are
  the DOM state, a captured-message scan for `qUncompress`, and a 1 GiB bound on `VmPeak` growth against a ~3.75 GB
  request (skipped when `/proc` is unreadable). The message scan alone already fails red.
- Complete: `qUncompress` appears in `DataFile.cpp` only in `decompressesAsRecoverableDocument` (already guarded, :327)
  and `loadData` (now guarded); all three constructors go through `loadData`. The fix reuses the recovery limit and its
  seam, so the two paths stay consistent.
- No collateral damage: payloads at or under the limit take the unchanged `qUncompress` path (`validCompressedProjectStillLoads`
  and the 5-test family `DataFile*|ProjectOpenIntegrity|ProjectVersion` pass, `test-5-family.log`). The only newly refused
  input is a compressed project that declares more than 256 MiB uncompressed, the same bound recovery has always applied.
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE.
- Gates: BLOCKED in this lane. `feature_check.py` (also run by `submit_check.py`) fails at its own
  `git -C .../gate-baselines/tree checkout --detach` step, refused by bunny-guard (`gates-1.log`, `submit-1.log`). I did not
  work around it; the gate result is unverified.

## Other findings

- `DataFile::loadData` accepts plain XML of any size (no document limit on that path); this is outside this bug.
- A compressed project that legitimately expands past 256 MiB is now refused by `loadData` as recovery already refuses
  it; that matches the recovery limit and the checkpoint's instruction.
