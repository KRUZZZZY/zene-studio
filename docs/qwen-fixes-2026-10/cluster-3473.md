FALSE-POSITIVE

3473: FALSE-POSITIVE

Finding: "A negative pitch can index before the pitch table" (`src/core/ScoreExport.cpp:112`, reported as confidence low). The claim is that a note with key `-1` makes `steps[key % 12]` and `alters[key % 12]` index before their arrays. The current code makes that path unreachable in two independent ways.

## Evidence

**1. The indexing the finding describes is guarded.** The only table indexing by key is in `writePitch`: `steps[key % 12]` at [ScoreExport.cpp:144](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/ScoreExport.cpp:144) and `alters[key % 12]` at [ScoreExport.cpp:145](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/ScoreExport.cpp:145). `writePitch` has one caller, `writeNote`, and it is reached only for non-negative keys: [ScoreExport.cpp:176](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/ScoreExport.cpp:176) is `if (key < 0) { <rest/> } else { writePitch(xml, key); }`. A negative key is written as a rest and never indexes anything. The finding's line 112 is the call `chordsByVoice(startsOf(clip))` in `voicesOf` ([ScoreExport.cpp:110-115](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/ScoreExport.cpp:110)); it does no key indexing. The historical line number no longer maps to the indexing.

**2. A note's key cannot be negative.** The only key fed to the exporter is `note->key()` at [ScoreExport.cpp:70](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/ScoreExport.cpp:70). `Note::m_key` is written in exactly these places, all of them clamped to `[0, NumKeys - 1]` (`NumKeys` is 128, [include/Note.h:93](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/include/Note.h:93)):

| Writer | Location | Clamp |
| --- | --- | --- |
| Constructor | [src/core/Note.cpp:168](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/Note.cpp:168) | `m_key(std::clamp(key, 0, NumKeys - 1))` |
| Copy constructor | [src/core/Note.cpp:193](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/Note.cpp:193) | copies an already-clamped source |
| Copy assignment | [src/core/Note.cpp:225](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/Note.cpp:225) | copies an already-clamped source |
| `setKey` | [src/core/Note.cpp:351-354](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/Note.cpp:351) | `std::clamp(key, 0, NumKeys - 1)` |
| `loadSettings` | [src/core/Note.cpp:642](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/src/core/Note.cpp:642) | clamps the loaded `key` (and legacy key) to `[0, NumKeys - 1]` before `setKey` |

The script bindings reach the key only through these paths too (`ScriptBindings.cpp:486`, `:538`, the latter calling `setKey`). The accessor `Note::key()` returns `m_key` unchanged ([include/Note.h:365-367](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/include/Note.h:365)).

So the finding's scenario does not happen: a malformed project with key `-1` loads as key `0` (C-1), not `-1`, and key `% 12` is always in `[0, 11]`.

**Contract:** MIDI keys are 0..127 (`MidiMaxKey = 127`, [include/Midi.h:133](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473/include/Midi.h:133)). A negative key is not a valid note and has no defined pitch; the exporter writes it as a rest, which is the intended handling (`ScoreExport.cpp:176`).

## Decision

Not real. No product change is made. The defensive guard suggested by the finding (validate or use a non-negative modulo in `writePitch`) would be dead code for every input the model can hold, so it is not added. No regression test is added either: there is no defect whose fix it would prove. The rest-as-note handling is unchanged.

## Commands run (worktree `zene-qwen-3473`, branch `bugs/qwen-3473`, base `c6ee4fc7b`)

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3473.r1/logs/`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Gate, as specified | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3473` | 1 | `PermissionError` creating `zene-qwen-3473-feature-check` beside the worktree (`001-feature-check.log`). The write-jail blocks that path; no gate ran. |
| Gate, reproduced | `python3 logs/gate_driver.py <worktree> logs/gates-1` (driver imports `feature_check`'s own gate lists, commands and new-item comparison; only the log folder differs) | 0 | All eight rows PASS, `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length` (`002-feature-check-driver.log`, per-gate output in `gates-1/`). Zero changed product files, so the ratchet comparison has no item in a changed file. |
| Completion check, pre-report | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 3473` | 1 | FAIL only on the missing report, its Self-check, the missing commit and the feature gate (`003-submit-check-pre-report.log`). |

Not run: no build and no ctest. Nothing changed in product code, so there is no RED or GREEN run to record. The affected family (`ArrangeToolsTest`) was not executed.

## Self-check

- **RED/GREEN reversal:** not applicable. The finding is not real, so there is no product hunk to reverse. There is no test to show RED and GREEN.
- **Determinism:** not applicable. No test was added.
- **Cause and callers:** no product edit. I checked every writer of `Note::m_key` (table above) and every consumer of the key in `ScoreExport.cpp` (`:70`, `:144-145`, `:173-176`). The only key-indexing path is guarded at `:176`.
- **Collateral:** none. No product file changed, so saved project loading and rendering are unaffected.
- **Not-real evidence:** `ScoreExport.cpp:70`, `:144-145`, `:176`; `Note.cpp:168`, `:193`, `:225`, `:351-354`, `:642`; `Note.h:93`, `:365-367`; `Midi.h:133`. Scope of the claim: C++ source reading only. Nothing was built or run to exercise the path.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-3473.md` only.
