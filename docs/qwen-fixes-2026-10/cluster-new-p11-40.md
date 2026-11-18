# Cluster new-p11-40: `note.add` and linked clips

Verdict: **ALREADY-FIXED**

## Evidence

`src/core/ControlCommandsNotes.cpp:133` adds the note through `MidiClip::addNote`.
That method already calls `ClipLinks::mirrorContent(this)` for clips with a
positive link ID at `src/tracks/MidiClip.cpp:203`. The propagation therefore
happens on the existing `note.add` path.

The existing `ClipLinkTest::anEditToOneMemberIsSeenByAll` covers this control
surface path at `tests/src/core/ClipLinkTest.cpp:222-247`: it links two clips,
adds a note through `note.add`, then verifies the other member has the note and
its key. It also checks propagation in the other direction. No product or test
change was needed.

## Verification

| Check | Command | Exit | Result |
|---|---|---:|---|
| RED regression | N/A | N/A | The finding is already fixed on this branch; per procedure step 2, no new regression was added or run against an unfixed implementation. |
| Build affected test | `cmake --build build --target ClipLinkTest` | 0 | Built successfully. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-40.r1/logs/build-1.log` |
| Existing GREEN coverage | `cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` | 0 | 1 CTest case run, 1 passed. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-40.r1/logs/test-1.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed on the final report. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-40.r1/logs/gate-7.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed on the final report. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-40.r1/logs/gate-8.log` |

No product fix was applied, so the reverse-patch RED/GREEN proof is not
applicable.

The requested `fff` grep tool/command was unavailable in this session, so I
searched the logs with targeted `rg` patterns instead of opening full logs.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p11-40.md`
