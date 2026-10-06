# cmds1 batch-14 — six reported defects

Six findings, three fixed with a regression test each, three closed without a
product change. Two of the three closed findings were argued false against the
current code; one is out of this lane's scope.

### Cluster 5652 — Note end tick can overflow

Verdict: FIXED

Evidence: `src/core/ControlCommandsLibraryClips.cpp:103` computed the note-off
tick as `start + std::max<tick_t>(1, note->length().getTicks())` with
`tick_t = std::int32_t` (`include/LmmsTypes.h:35`). A note starting within
`length` ticks of `INT32_MAX` produced a negative end tick, which
`std::stable_sort` put *before* its own note-on, and the serialized delta
sequence was then corrupt. The end is now computed in `qint64` and clamped to
the `tick_t` range before it becomes an event tick.

Commit: ae7536117

Test: `LibraryBrowserTest::aNoteNearTheTopOfTheRangeIsWrittenSane`

Red/green:
`cmake --build build --target LibraryBrowserTest > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=8`
— `Actual 2147483685` vs `Expected 2147483637`: the wrapped note-off sorted
ahead of the note-on.
`cmake --build build --target LibraryBrowserTest zene > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=0`

Note: the first draft of the regression used `Note(TimePos(start), TimePos(length), key)`;
`Note`'s constructor is `Note(length, pos, key, ...)` (`include/Note.h:105`), so the
arguments are swapped in the test with a comment.

### Cluster 5649 — Undoing an overwritten library clip deletes the previous file

Verdict: FIXED

Evidence: `src/core/ControlCommandsLibraryClips.cpp:216` (was `:209`) recorded
an undo step that removed `path` and the tags this call added, and nothing ever
read the file an `overwrite: true` save was about to replace
(`:174-176` only refused when `overwrite` was absent). Undoing such a save
therefore destroyed the previous file and left nothing behind. The file's
previous bytes are now read before the write and put back on undo; when there
was no previous file the old behaviour (remove) stands.

Commit: 4ad394cc5

Test: `LibraryBrowserTest::anOverwrittenClipComesBackOnUndo`

Red/green:
`cmake --build build --target LibraryBrowserTest > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=8`
— `'QFileInfo::exists(path)' returned FALSE` at the undo.
`cmake --build build --target LibraryBrowserTest zene > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=0`

### Cluster 5654 — A refused quantum change can still add an undo step

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsLink.cpp:313-318` refuses any quantum
outside `link::MinQuantum..link::MaxQuantum` *before* `addUndoStep` at `:320`,
and `LinkSyncEngine::setQuantum` (`src/core/LinkSync.cpp:170-178`) returns false
on exactly and only that same range check. Having passed the handler's check,
`setQuantum` at `:323` cannot return false, so the failure branch below the
recorded step is unreachable and no refused call records a step.

Commit: none

Test: none

Red/green: none

### Cluster 4843 — Relative livecode paths are accepted despite the absolute-path contract

Verdict: FIXED

Evidence: `src/core/ControlCommandsLivecode.cpp:127` checked only
`QFileInfo(path).exists()`, so `path:"scripts/beat.lua"` resolved against the
process working directory, while the refusal message at `:131` and the command
description both promise an absolute path. A non-absolute path is now an
`invalid_args` refusal, typed before the existence check.

Commit: 82adf3454

Test: `ControlLivecodeCommands` — `check_refusals_are_typed`'s case
"a relative path is invalid_args"

Red/green:
`cmake --build build --target zene > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^ControlLivecodeCommands$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=8`
— `kind='not_found'`, the relative path was looked up.
`cmake --build build --target zene ScriptClockTest > build/bunny-b.log 2>&1; EXIT=0` /
`cd build/tests && ctest -R '^(ControlLivecodeCommands|ScriptClockTest)$' --output-on-failure > build/bunny-t.log 2>&1; EXIT=0`

### Cluster 4841 — Livecode schedule changes have no recorded undo step

Verdict: FALSE-POSITIVE

Evidence: `livecode.schedule` and `livecode.unschedule` are deliberate
`Snapshot` rows in `src/core/ControlReversibilityTableLivecode.cpp`, and each
handler returns `__transaction` with an inverse that is itself a command
(`scheduleInverse`/`unscheduleInverse`,
`src/core/ControlCommandsLivecode.cpp:193-200` and `:230-234`). `control.undo`
dispatches that inverse through `registry.invoke(op, args)` in
`undoByCommand` (`src/core/ControlCommandsControl.cpp:330-346`), so undo works
without a journal step; `tests/control-livecode-commands.py` asserts both
directions ("one control.undo takes a new schedule off the clock", "one
control.undo rearms a schedule that was taken off").

Commit: none

Test: none

Red/green: none

### Cluster 5650 — `source_bytes` reports UTF-16 units rather than source bytes

Verdict: SKIPPED-SCOPE

Evidence: `QString::size()` is indeed UTF-16 code units, and the value is
produced in three places, not one — `src/core/ControlCommandsLivecode.cpp:190`,
`src/core/ScriptClock.cpp:291` and `:397` — while the `SourceByteCap` check and
its message (`src/core/ScriptClock.cpp:230-233`) also use `source.size()`. A
coherent fix is an engine change in `ScriptClock.cpp`, which is outside this
lane's `ControlCommands*` scope, and changing only the command layer would make
`livecode.schedule` disagree with `livecode.get_state`.

Commit: none

Test: none

Red/green: none

Leftovers: Cluster 5650 is genuinely wrong but belongs to `src/core/ScriptClock.cpp`;
it needs `source_bytes` (all three sites) and the `SourceByteCap` check to move
to `toUtf8().size()` together, with the transcript's
`source_bytes == len("return 'kept'")` check kept honest.