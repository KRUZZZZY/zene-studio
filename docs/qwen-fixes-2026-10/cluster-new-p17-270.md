FIXED

# cluster new-p17-270: collection names are trimmed when saved but not when run or removed

## Verdict

FIXED. The finding is real on the current branch. Before this change
`src/core/ControlCommandsBrowserCollections.cpp` trimmed the name in `save()` (historical line 134) but
`remove()` (historical line 157) and `run()` (historical line 184) looked the raw argument up in the store,
so a collection saved as `"  drums  "` was stored as `"drums"` and could not be run or removed with the
name it was saved under.

## Fix

The same normalization now lives in one helper and every handler uses it:

- `src/core/ControlCommandsBrowserCollections.cpp:135` - `collectionName()` returns the trimmed `name`;
- `:142` - `save()` uses it (and still refuses an empty name after trimming);
- `:165` - `remove()` uses it, both for the lookup and for the `removed` reply;
- `:192` - `run()` uses it, both for the lookup and for the `collection` reply;
- `list()` (`:188`) is unchanged: it reports the stored keys, which are already trimmed by `save()`.

The GUI already supplied trimmed names (`src/gui/BrowserSearchView.cpp:552,559`) and was therefore not a
second broken path; the command API was the gap. Searching the store code shows no other caller that
builds or resolves a collection name (`src/core/ControlCommandsBrowserCollections.cpp` is the only reader
and writer of `browser-collections.json`).

## Regression test

`tests/src/core/BrowserCollectionStoreWriteTest.cpp:132`
`aCollectionNameIsTrimmedWhenRunAndRemoved()` covers:

- normal path: save `"  Trim Probe  "` stores `"Trim Probe"`, lists only that spelling, runs with either
  spelling, and reports the trimmed name in the reply;
- failure path: `"  Missing  "` is still `NotFound`;
- removal and undo: remove `"  Trim Probe  "` removes the stored collection, and `control.undo` restores
  it as one step;
- cleanup on a failed assertion via `qScopeGuard`, so the store the neighbouring slots count is not left
  holding the probe collection.

No exhaustion/reconfiguration limit exists for collection names, so that row is the existing blank-name
refusal (`save()` -> `InvalidArgs`), which is unchanged. The product change is control-thread command code
only; it adds nothing to the audio path. The added code and test are plain Qt and use a `QTemporaryDir`,
so they build on every supported platform.

## Commands and results (copied from this session's logs)

Initial RED, before the product fix (test initially placed in `LibraryProjectsTest.cpp`):

```
cmake --build build --target LibraryProjectsTest > logs/build-1-red.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure > logs/test-1-red.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : LibraryProjectsTest::aCollectionNameIsTrimmedWhenRunAndRemoved() 'ran.ok' returned FALSE. (no collection '  Trim Probe  ')
```

The test was then moved to `BrowserCollectionStoreWriteTest.cpp`: the first gate run (`logs/gates-1.log`,
EXIT=1) reported `FAIL file-length NEW: new file over 500 lines: tests/src/core/LibraryProjectsTest.cpp`
because that file was already at 481 gate-counted lines. The moved test is in an existing registered file
and keeps the same area together with the collection-store tests.

Step 4b with the final test and only product hunks reversed:

```
git diff -- src/core/ControlCommandsBrowserCollections.cpp > logs/fix.patch && git apply -R logs/fix.patch
APPLY_REVERSE_EXIT=0
cmake --build build --target BrowserCollectionStoreWriteTest > logs/build-7b-revert.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R '^BrowserCollectionStoreWriteTest$' --output-on-failure > logs/test-7b-revert.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=8
FAIL!  : BrowserCollectionStoreWriteTest::aCollectionNameIsTrimmedWhenRunAndRemoved() 'ran.ok' returned FALSE. (no collection '  Trim Probe  ')
```

Restored, rebuilt (including `zene`) and rerun:

```
git apply logs/fix.patch; echo APPLY_EXIT=$?
APPLY_EXIT=0
cmake --build build --target BrowserCollectionStoreWriteTest zene > logs/build-8-reapply.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R '^BrowserCollectionStoreWriteTest$' --output-on-failure > logs/test-8-reapply.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0
1/1 Test #280: BrowserCollectionStoreWriteTest ...   Passed
```

Family run (the collection, browser tempo/key and library/menu paths that exercise the same store):

```
cd build/tests && ctest -R '^(LibraryProjectsTest|BrowserCollectionStoreWriteTest|BrowserTempoKeyTest|LibraryMenusTest)$' --output-on-failure > logs/test-9-family.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed, 0 tests failed out of 4
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p17-270 > logs/gates-2.log 2>&1; echo EXIT=$?
EXIT=0
PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length
RESULT: PASS
```

Registration: both changed files are already fork-owned and listed in `tests/fork-sources.txt`
(`src/core/ControlCommandsBrowserCollections.cpp` line 1699,
`tests/src/core/BrowserCollectionStoreWriteTest.cpp` line 2832). No new file and no inherited file changed,
so `tests/upstream-modifications.txt` needs no entry.

## Self-check

- **Regression test fails reverted, passes restored:** see `test-7b-revert.log` EXIT=8 (only
  `aCollectionNameIsTrimmedWhenRunAndRemoved` failed) and `test-8-reapply.log` EXIT=0. The product patch
  reversed in step 4b contains only `src/core/ControlCommandsBrowserCollections.cpp`.
- **Deterministic:** the test uses a per-test `QTemporaryDir`, the `ZENE_BROWSER_COLLECTIONS` override set
  in `initTestCase`, a local probe file and synchronous command calls. There are no sleeps, timers,
  wall-clock comparisons, threads or races.
- **Cause and completeness:** all three name-taking handlers (`save`, `remove`, `run`) now use the one
  `collectionName()` helper; `list` returns stored keys unchanged. `BrowserSearchView` already trimmed
  before calling save/remove. A search of the product code found no other collection-name lookup.
- **No collateral damage:** the store format and existing keys are unchanged; every name written by
  `save()` was already trimmed, so existing saved collections resolve exactly as before. The change only
  makes padded spellings of a saved name resolve; a hand-edited key that itself has padding is still
  listed, but was never produced by `save()`. No new limit or check was added.
- **Sibling commands:** `browser.collection_save`, `browser.collection_remove`, `browser.collection_run`
  are the only ones handling a collection name and are all fixed; `browser.collections` lists stored keys
  unchanged. The other browser commands are unaffected because they do not read the collections store:
  `browser.browse`, `browser.categories`, `browser.category.clear`, `browser.category.set`,
  `browser.history`, `browser.history_go`, `browser.index_query`, `browser.index_status`, `browser.load`,
  `browser.peaks`, `browser.peaks_start`, `browser.preview`, `browser.preview_output`,
  `browser.preview_stop`, `browser.query`, `browser.reindex`, `browser.remember_source`, `browser.roots`,
  `browser.save_clip`, `browser.similar`, `browser.sources`, `browser.tag.add`, `browser.tag.remove`,
  `browser.tags`, `browser.thumbnail`.
- **ALREADY-FIXED / FALSE-POSITIVE question:** not applicable; the verdict is FIXED.

## Other findings

None noticed in the files touched by this job.
