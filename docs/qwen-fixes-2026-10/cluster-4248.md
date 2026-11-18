# Cluster 4248 — clearing a linked clip's notes does not update its linked clips

Verdict: **ALREADY-FIXED** (on this branch, `bugs/qwen-4248`).

## The finding

`MidiClip::clearNotes()` deleted the notes and emitted `dataChanged()` but, unlike
`addNote()` and both `removeNote()` overloads, never called
`ClipLinks::mirrorContent()` — so clearing one member of a link group left the
group diverged (source empty, peers holding the old notes).

## Evidence

- `src/tracks/MidiClip.cpp:290` — `clearNotes()` (lines 272–291) now ends with
  `if (linkId() > 0) { ClipLinks::mirrorContent(this); }`, matching
  `addNote()` (`src/tracks/MidiClip.cpp:203`) and `removeNote()`
  (`src/tracks/MidiClip.cpp:222` / `:243`).
- Introduced by commit `4218c1e30` — `fix(tracks): a bulk note clear left the
  clip's link group diverged` (2026-10-06, "Luna cluster 4248"), +6 lines in
  `src/tracks/MidiClip.cpp` and +58 lines in `tests/src/core/ClipLinkTest.cpp`.
- Re-entrancy: `ClipLinks::mirrorContent()` is guarded by
  `g_mirrorDepth`/`mirroringAllowed()` (`src/core/ClipLinks.cpp:58`/`:62`), which
  is what makes `writeContent()` (`src/core/ClipLinks.cpp:191`, it clears a
  member before copying onto it) safe to reach this entry point.
- Load path unchanged: `MidiClip::loadSettings()` holds a
  `ClipLinks::MirroringSuspension` (`src/tracks/MidiClip.cpp:509`) around its
  `clearNotes()` call (`src/tracks/MidiClip.cpp:529`), so mirroring stays
  suspended during load as designed.
- Regression test: `tests/src/core/ClipLinkTest.cpp:438`
  `aBulkClearIsSeenByTheOtherMember()` — links two MIDI clips, adds two notes to
  A, calls `source->clearNotes()`, and requires `noteCount(b) == 0` plus
  `divergentCount(a) == 0`.

## Commands run (logs in this run's log folder)

RED — the fix was already committed, so the RED was reproduced by temporarily
reverting the mirror call in `clearNotes()` (restored immediately after):

```
cmake --build build --target ClipLinkTest > LOGS/build-red.log 2>&1            EXIT=0
cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure > LOGS/test-red.log 2>&1   EXIT=8
```

`test-red.log`: `FAIL! : ClipLinkTest::aBulkClearIsSeenByTheOtherMember()
'noteCount(b) == 0' returned FALSE. (clearNotes wrote the source clip only…)`;
`Totals: 14 passed, 1 failed, 0 skipped, 0 blacklisted, 1361ms`;
`0% tests passed, 1 tests failed out of 1`.

GREEN — fix restored (`git diff --stat src/tracks/MidiClip.cpp` empty, file
matches the committed state):

```
cmake --build build --target ClipLinkTest > LOGS/build-green.log 2>&1          EXIT=0
cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure > LOGS/test-green.log 2>&1  EXIT=0
```

`test-green.log`: `1/1 Test #16: ClipLinkTest ..................... Passed`;
`100% tests passed, 0 tests failed out of 1` (1 ctest test, 15 cases).

Load path (mirroring suspension retained):

```
cmake --build build --target ClipLinkPersistenceTest > LOGS/build-persist.log 2>&1          EXIT=0
cd build/tests && ctest -R '^ClipLinkPersistenceTest$' --output-on-failure > LOGS/test-persist.log 2>&1  EXIT=0
```

Gates:

```
bash tests/complexity-gate.sh --check > LOGS/gate-complexity.log 2>&1          EXIT=0
bash tests/file-length-gate.sh --check > LOGS/gate-length.log 2>&1             EXIT=0
```

## Registration gap closed

Commit `4218c1e30` changed the inherited file `src/tracks/MidiClip.cpp` but did
not extend its `tests/upstream-modifications.txt` entry (the entry only covered
the #645 `addNote`/`removeNote` hooks; `grep clearNotes` found nothing). This
commit appends a clause to that entry registering the `clearNotes()` mirror hook,
so the branch's inherited-file bookkeeping matches its history.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-4248.md` (this report)
- `tests/upstream-modifications.txt` (registration clause for the existing
  `clearNotes()` change)
