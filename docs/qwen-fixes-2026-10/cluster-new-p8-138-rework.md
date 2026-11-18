# Rework of cluster new-p8-138: the clip-list test crashed in teardown

Verdict: **FIXED (test-side)**. The product fix in commit `89e9cb1b1` is correct and unchanged. The crash was a
use-after-free in `ItemListPanelTest::theClipListDeletesEverySelectedClip`'s own cleanup.

Starting HEAD: `89e9cb1b1`. Branch: `bugs/qwen-new-p8-138-rework`.

## Root cause

The test body ran to completion (every assertion passed, marker `DBG27` reached in every run). The abort happened
afterwards, in the fixture's `cleanup()` at `delete m_clip`, which came after `delete m_editor` had succeeded.

Why `m_clip` was stale:

1. `init()` creates the fixture clip `m_clip` at position 0.
2. The test deletes two other clips through `clip.delete` (`ClipsSource::remove`), then calls `undo()` twice.
3. The undo restores the track from its checkpoint. `Track::restoreState` re-loads the serialized clips as new
   objects (`src/core/ControlCommandsClip.cpp:46`, the comment there says so). The clip at position 0 is now a new
   object.
4. Instrumented run at body end: `m_clip=0x...1120`, while the track holds `0x...10f0` at position 0. The fixture
   pointer was no longer on the track, so it was freed.
5. `cleanup()` calls `delete m_clip` on freed memory. The virtual deleting-destructor slot of the dead object resolves
   to `__cxa_pure_virtual`. This is why the abort is "pure virtual method called", and why it is timing-dependent
   (heap reuse decides whether the old vtable slot still looks valid), which explains the 5 passes out of 40.

The product is not at fault. Nothing the panel keeps is dangling: `ClipsSource` holds `QPointer<Clip>`, and
`NoteEditor::setClip` connects `QObject::destroyed` to `onClipDestroyed`. The product's own design replaces clip
objects on track undo, and the test had not been written for that.

## Fix (test only, `tests/src/gui/ItemListPanelTest.cpp`)

- After the undo loop, the fixture's clip is re-bound to the restored object now at position 0
  (`m_clip = dynamic_cast<MidiClip*>(clipAt(0))`, only if one exists). `cleanup()` then deletes the live object, so
  the track is left clean for the next test, as before.
- The `discardAt` helper became a `clipAt(position)` lookup plus a removal. It deletes the restored
  first and second clips the same way.
- No assertion was removed or weakened. The checks still prove: both selected clips are removed, the clips table is
  in extended-selection mode, and the undo steps restore what was removed.

No product file changed in this commit.

## Evidence

Command for the solo runs, from `build/tests`:
`QT_QPA_PLATFORM=offscreen ./ItemListPanelTest theClipListDeletesEverySelectedClip`
(`LMMS_PLUGIN_DIR` unset.)

| Run | Binary | Passes (exit 0) | Aborts (exit 134) |
| --- | --- | ---: | ---: |
| Before, the commit as it stood (`89e9cb1b1` test file) | rebuilt from HEAD test file | 5 of 40 | 35 of 40 |
| After, the fixed test file | rebuilt | 40 of 40 | 0 of 40 |
| After, final binary re-check | rebuilt from the committed state | 40 of 40 | 0 of 40 |

Whole-binary runs, from `build/tests`: `ctest -R '^ItemListPanelTest$' --output-on-failure` run 10 times in a row,
exit 0 each time, "100% tests passed, 0 tests failed out of 1".

Gates, from the worktree root, after the test edit:
- `bash tests/complexity-gate.sh --check`: exit 0
- `bash tests/file-length-gate.sh --check`: exit 0

## Not done

- The RED-before-fix proof in the brief's form (reversing a product change) does not apply here. The product change
  is not the defect; the crash is reproducible on HEAD and disappears with the test-only change.
- A full `ctest` run was not run. Only the single affected test binary was executed.
