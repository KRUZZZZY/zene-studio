FALSE-POSITIVE

new-p21-46: note.add dereferences addNote() result without a null check

Evidence: `MidiClip::addNote()` cannot return null. It clones the argument with `_new_note.clone()` at [MidiClip.cpp:192](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/tracks/MidiClip.cpp:192), inserts that heap note into `m_notes` at [MidiClip.cpp:200-202](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/tracks/MidiClip.cpp:197), and its only return statement is `return new_note;` at [MidiClip.cpp:209](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/tracks/MidiClip.cpp:209). The declaration at [include/MidiClip.h:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/include/MidiClip.h:65) carries no nullable contract. `Note::clone()` is `new Note(*this)` at [Note.cpp:296](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/Note.cpp:296), returned at [Note.cpp:301](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/Note.cpp:301); a failed allocation throws `std::bad_alloc` rather than returning null. Every other caller in the tree also uses the result without a null check, for example [ControlCommandsNoteClipboard.cpp:158](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/ControlCommandsNoteClipboard.cpp:158) and [ControlCommandsNoteShape.cpp:237](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/ControlCommandsNoteShape.cpp:237). The contract is therefore non-null, and the flagged line [ControlCommandsNotes.cpp:134-135](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/ControlCommandsNotes.cpp:134) (`Note* added = clip->addNote(fresh, false); added->setSelected(false);`) is safe. The handler's real defensive check is the `indexOfNote(clip, added) < 0` branch at [ControlCommandsNotes.cpp:138-143](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-46/src/core/ControlCommandsNotes.cpp:138), which already returns a failure.

No product code was changed, so no regression test was written. A test that asserts "addNote returns non-null" would pass on the unmodified tree and would prove nothing about the finding.

Branch diff against the base is empty (`git diff --stat 7641e9493 HEAD` prints nothing). Only this report is committed.

Commands run (from the worktree unless noted; EXIT is the unpiped exit code):

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/feature-check-1.log` (`PermissionError`: the script tries to create `zene-qwen-new-p21-46-feature-check` beside the worktree, outside the write-jail) |
| `python3 -I logs/run_feature_check_logdir.py <worktree>`, the same `feature_check.py` with only its log-dir line redirected into the LOG FOLDER | 1 | `logs/feature-check-2.log` (the script's `bunny-guard` refused the baseline `git checkout` in `gate-baselines/tree`, exit 97, so the script stopped before printing its table) |
| `bash tests/fork-sources-gate.sh` | 0 | `logs/gate-fork-sources-1.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `logs/gate-all-sources-1.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `logs/gate-no-upstream-1.log` |
| `tests/scripted/check-namespace` | 0 | `logs/gate-check-namespace-1.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `logs/gate-no-tautology-1.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `logs/gate-doc-figures-1.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `logs/gate-complexity-1.log` |
| `bash tests/file-length-gate.sh --check` | 1 | `logs/gate-file-length-1.log` |

The two ratchet gates fail on items in files this branch does not touch. Complexity reports `WasmSandbox::loadModuleBytes`, `XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerStrip`, and others. File-length reports `Vst3Host.cpp`, `WasmSandbox.cpp`, `TakeLane.cpp`, and others. None of these is in the empty branch diff, so per the gate rule they are not new items in this lane's files. The gate's own baseline comparison was not run, because the bunny-guard blocked it.

## Self-check

- Verdict and evidence: FALSE-POSITIVE, with `file:line` citations above (MidiClip.cpp:190-209, Note.cpp:294-301, ControlCommandsNotes.cpp:134-143).
- Regression test: not written, because nothing changed in product code. The procedure's step 2 routes not-real findings straight to the report.
- Gates: six of eight gate commands exit 0. Complexity and file-length exit 1 on items outside this lane's files. The official `feature_check.py` could not finish in this jail (see the two rows above).
- Git state: this report is the only file committed. The worktree is clean after commit.
