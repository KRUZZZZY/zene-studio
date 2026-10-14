# Cluster new-p11-34 — note.across clip limit vs duplicate clip IDs

Verdict: **FIXED**

## Finding

`note.across` applied its 64-clip limit to the RAW `clips` array length before dropping
duplicate clip names, so naming the same MIDI clip 65 times was refused even though the
command edits only one unique clip.

- File: `src/core/ControlCommandsNoteAcross.cpp`, `prepare()` (finding line 65; the check
  `if (clips.isEmpty() || clips.size() > MaxClips)` ran before the dedup loop
  `if (seen.contains(value.toString())) { continue; }`).

## Fix

Deduplicate first, then enforce the limit on unique clips. In `prepare()`, the raw array is
now filtered through the `seen` set into a `unique` array before the
`unique.isEmpty() || unique.size() > MaxClips` check, and the resolve/validate loop iterates
`unique`. The error message and the limit value (64) are unchanged; 65 distinct clips are
still refused.

- `src/core/ControlCommandsNoteAcross.cpp:65` — limit check now on `unique.size()` after dedup.
- `tests/src/core/NoteCurvesAcrossTest.cpp:196` — new case `theClipLimitCountsUniqueClips()`:
  one clip named 65 times must succeed (`edited == 1`), and 65 distinct clips must still be
  refused with `ControlErrorKind::InvalidArgs`.

Both files were already registered in `tests/fork-sources.txt` (lines 1196 and 1973); no
new files, so no tracking-file changes.

## Regression proof (logs in ~/.local/share/zene-bunny-fix/qwen/runs/new-p11-34.r1/logs)

RED on the ORIGINAL code (fix not yet applied):

```
cmake --build build --target NoteCurvesAcrossTest > LOGS/build-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R NoteCurvesAcrossTest --output-on-failure > LOGS/test-1.log 2>&1   # EXIT=8
```

test-1.log: `FAIL! : NoteCurvesAcrossTest::theClipLimitCountsUniqueClips() 'done.ok'
returned FALSE. ('clips' names 1..64 MIDI clips)` — 1 test run, 1 failed (10 passed, 1 failed
QTest cases).

GREEN after the fix:

```
cmake --build build --target NoteCurvesAcrossTest > LOGS/build-2.log 2>&1   # EXIT=0
cd build/tests && ctest -R NoteCurvesAcrossTest --output-on-failure > LOGS/test-2.log 2>&1   # EXIT=0
```

test-2.log: `100% tests passed, 0 tests failed out of 1`.

Test-catches-bug proof (step 4b):

```
git diff -- src/core/ControlCommandsNoteAcross.cpp > LOGS/fix.patch
git apply -R LOGS/fix.patch   # APPLY_R_EXIT=0
cmake --build build --target NoteCurvesAcrossTest > LOGS/build-6.log 2>&1   # BUILD6_EXIT=0
cd build/tests && ctest -R NoteCurvesAcrossTest --output-on-failure > LOGS/test-6.log 2>&1   # TEST6_EXIT=8 (FAIL)
git apply LOGS/fix.patch     # APPLY_EXIT=0
cmake --build build --target NoteCurvesAcrossTest > LOGS/build-7.log 2>&1   # BUILD7_EXIT=0
cd build/tests && ctest -R NoteCurvesAcrossTest --output-on-failure > LOGS/test-7.log 2>&1   # TEST7_EXIT=0 (PASS)
```

The ctest family `-R NoteCurvesAcrossTest` selects exactly 1 test; it ran in every cycle
(never 0 tests).

## Gates

```
bash tests/complexity-gate.sh --check   # EXIT=0 (LOGS/gate-1.log)
bash tests/file-length-gate.sh --check  # EXIT=0 (LOGS/gate-2.log)
```

`zene` rebuilt with the fix: `cmake --build build --target zene > LOGS/build-zene.log 2>&1` —
log ends `[100%] Built target zene`, 0 lines matching "error".

## Files committed

- `src/core/ControlCommandsNoteAcross.cpp`
- `tests/src/core/NoteCurvesAcrossTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-34.md`
