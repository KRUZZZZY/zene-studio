# Cluster new-p9-52: `note.rotate` grows auto-resizing clips

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsNoteTools.cpp:255` rotates note starts. At lines 276–286, a note crossing the wrap boundary is split: the original ends at the boundary and a copied continuation starts at the clip offset.
- `src/core/ControlCommandsNoteShared.h:64` calls `updateLength()` from `finish()` at line 67. `src/tracks/MidiClip.cpp:129` sizes auto-resizing clips from the furthest note end.
- `tests/src/core/NoteToolsTest.cpp:381` covers a full-bar note rotated forward 20 ticks. This tree rounds auto-resizing MIDI clips to bars, so the fixture uses its 192-tick bar as the span.
- Before the fix, the regression reported actual clip length 384 versus expected 192 at test line 395. After the fix, the note is represented by pieces at ticks 0 and 20 whose lengths sum to the original span.

## Build and test evidence

Build and test output was redirected to new numbered files in the supplied log folder. The command column gives the exact build or test invocation; all exits are the values printed by the shell wrapper.

| Phase | Command | Log | Exit |
|---|---|---|---:|
| Baseline build, before production fix | `cmake --build build --target zene NoteToolsTest` | `build-1.log` | 0 |
| RED on original code | `ctest -R NoteToolsTest --output-on-failure` | `test-1.log` | 8 |
| Build after fix | `cmake --build build --target zene NoteToolsTest` | `build-2.log` | 0 |
| First GREEN run | `ctest -R NoteToolsTest --output-on-failure` | `test-2.log` | 0 |
| Reverse-patch proof: `git diff -- src/core/ControlCommandsNoteTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-52.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-52.r1/logs/fix.patch` | — | — | 0 |
| Rebuild with production fix removed | `cmake --build build --target zene NoteToolsTest` | `build-3.log` | 0 |
| RED after reverse-patch | `ctest -R NoteToolsTest --output-on-failure` | `test-3.log` | 8 |
| Restore fix: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-52.r1/logs/fix.patch` | — | — | 0 |
| Final build | `cmake --build build --target zene NoteToolsTest` | `build-4.log` | 0 |
| Final GREEN run | `ctest -R NoteToolsTest --output-on-failure -V` | `test-4.log` | 0 |

The reverse-patch run failed at the same clip-length assertion, proving the regression catches the original behavior. The final run executed 23 Qt test cases successfully; CTest reported 1/1 test targets passed.

## Ratchet gates

| Command | Log | Exit |
|---|---|---:|
| `bash tests/complexity-gate.sh --check` | `gate-1.log` | 0 |
| `bash tests/file-length-gate.sh --check` | `gate-2.log` | 0 |

## Files committed

- `src/core/ControlCommandsNoteTools.cpp`
- `tests/src/core/NoteToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-52.md`

The two source paths are already registered in `tests/fork-sources.txt`.
