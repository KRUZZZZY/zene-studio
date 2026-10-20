Verdict: FALSE-POSITIVE

5684: FALSE-POSITIVE
new-p14-36: FIXED
new-p17-18: FIXED

# Cluster 5684 batch — articulation callback stall, lyrics input forms, lyrics count

Worktree `zene-qwen-5684` (branch `bugs/qwen-5684`). Three findings on
`src/core/ControlCommandsNotePlayback.cpp`, judged independently.

## 5684 — “Articulation updates can stall the audio callback”: FALSE-POSITIVE

The finding says the audio callback waits on the control thread's articulation-map
replacement. It does not: the audio-thread entry point takes the track lock with
`tryLock()`, never `lock()`.

- `src/tracks/InstrumentTrack.cpp:904` — `if( ! m_instrument || ! tryLock() )` then
  `return false;` (`:908`). A held track lock makes the callback skip the pass at
  once; it cannot wait for the writer.
- `include/Track.h:416-419` — `bool tryLock() { return m_processingLock.tryLock(); }`
  (non-blocking `QMutex::tryLock`).
- `src/tracks/InstrumentTrackNotes.cpp:245` — the map read
  `m_articulations.at(note.conditions().articulation)` is inside `triggerNote()`,
  which `InstrumentTrack::play` calls at `src/tracks/InstrumentTrack.cpp:1037` while
  it holds that `tryLock()`. The map is never read outside the `tryLock` section.
- `src/core/NotePlayHandle.cpp:291` — later note playback passes the note's own
  stored `ArticulationEvents` copy to `sendArticulationEvents`; it does not read the
  map at all.
- `src/core/ArticulationRemap.cpp:98-116` — the control thread's `track->lock()`
  can wait for the callback, not the reverse. The callback that meets the lock
  returns false and skips one pass, the same deliberate `tryLock` contract as every
  note/clip edit; the finding's “stall”/“callback waits” mechanism is absent.

The suggested lock-free map publication would not remove the skipped pass for a
`from` remap: the note indices are remapped under the same lock
(`ArticulationRemap.cpp:100-115`), and publishing the map outside that lock would
break the atomic map+note-index pair pinned by Q050-015
(`ArticulationPlaybackTest::theEditNeverAnnouncesANewMapWithOldNoteIndices`).
No product change was made for this finding; there is no failing symptom to pin.

## new-p14-36 — `note.lyrics` silently ignores `text` when `lyrics` is also supplied: FIXED

Before: `lyrics()` branched on `args.contains("lyrics")` first
(`src/core/ControlCommandsNotePlayback.cpp:114`), so a request carrying both fields
succeeded using only `lyrics`. After: the handler requires exactly one form,
before resolving the clip or adding a journal checkpoint
(`src/core/ControlCommandsNotePlayback.cpp:114-122`); both (or neither) returns
`InvalidArgs` with “give either 'text' or 'lyrics', not both”. The command
description now states the rule; the live snapshot was regenerated (833 ids /
76 groups, same `ids_sha256`), and `tests/control-commands-snapshot.py` passes.

Regression: `NotePlaybackTest::lyricsRejectBothInputFormsTogether` — sends
`text` + `lyrics`, asserts `InvalidArgs`, the original lyric is unchanged, and the
project journal depth is unchanged.

## new-p17-18 — `note.lyrics` can undercount notes it changes: FIXED

Before: the text path wrote every note in scope but replied
`set = min(notes.size(), syllables.size())`
(`src/core/ControlCommandsNotePlayback.cpp:136`). After: `set` is the number of
notes the command wrote, `notes.size()`; `syllables` still reports the syllable
count (`:140-142`). The per-note `lyrics` path is unchanged: it writes only the
entries it is given and still reports `writes.size()` (`:107`).

Regression: `NotePlaybackTest::lyricsReportEveryNoteTheyWrite` — three notes with
existing lyrics, text `solo` (one syllable): replies `set: 3`, `syllables: 1`, and
the notes end as `solo`, empty, empty.

## Commands and exit codes (all observed)

The assigned log folder was not writable by this session's file sandbox
(`touch /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5684.r1/logs/probe`
→ `Permission denied`, exit 1). All logs below are in `/tmp/zene-5684-logs` and
mirrored to `build/qwen-logs`; build commands used
`CCACHE_TEMPDIR=/tmp/ccache-tmp TMPDIR=/tmp` because `/run/user/1000/ccache-tmp`
is likewise read-only to the sandbox.

| Step | Command | EXIT |
|---|---|---|
| Build tests (RED) | `cmake --build build --target NotePlaybackTest` → `build-1-red.log` | 0 |
| Run family (RED) | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` → `test-1-red.log` | 8 |
| RED assertions | `lyricsRejectBothInputFormsTogether` expected `InvalidArgs`, got 0 (`NotePlaybackTest.cpp:650`); `lyricsReportEveryNoteTheyWrite` expected 3, got 1 (`:666`) | — |
| new-p14-36 reverse patch | `git apply -R /tmp/zene-5684-logs/fix-new-p14-36.patch` | 0 |
| Build reversed | `cmake --build build --target NotePlaybackTest` → `build-4-red-p14.log` | 0 |
| Test reversed | `./NotePlaybackTest lyricsRejectBothInputFormsTogether` → `test-4-red-p14.log` | 1 |
| Restore patch | `git apply /tmp/zene-5684-logs/fix-new-p14-36.patch` | 0 |
| Build restored | `cmake --build build --target NotePlaybackTest` → `build-5-green-p14.log` | 0 |
| Test restored | `./NotePlaybackTest lyricsRejectBothInputFormsTogether` → `test-5-green-p14.log` | 0 |
| new-p17-18 reverse patch | `git apply -R /tmp/zene-5684-logs/fix-new-p17-18.patch` | 0 |
| Build reversed | `cmake --build build --target NotePlaybackTest` → `build-6-red-p17.log` | 0 |
| Test reversed | `./NotePlaybackTest lyricsReportEveryNoteTheyWrite` → `test-6-red-p17.log` | 1 |
| Restore patch | `git apply /tmp/zene-5684-logs/fix-new-p17-18.patch` | 0 |
| Build restored | `cmake --build build --target NotePlaybackTest` → `build-7-green-p17.log` | 0 |
| Test restored | `./NotePlaybackTest lyricsReportEveryNoteTheyWrite` → `test-7-green-p17.log` | 0 |
| Build family | `cmake --build build --target NotePlaybackTest ArticulationPlaybackTest` → `build-8-family.log` | 0 |
| Run family | `cd build/tests && ctest -R '^(NotePlaybackTest\|ArticulationPlaybackTest)$' --output-on-failure` → `test-8-family.log` | 0 (2/2 ctest tests) |
| Direct totals | `./NotePlaybackTest` → `test-9-direct-noteplayback.log`; `./ArticulationPlaybackTest` → `test-10-direct-articulation.log` | 0, 0 (25 and 17 slots passed) |
| Rebuild zene | `cmake --build build --target zene` → `build-9-zene.log` | 0 |
| Regenerate snapshot | `python3 /tmp/capture_5684_snapshot.py` → `snapshot-1.log` | 0 (833 commands, 76 groups, same `ids_sha256`) |
| Snapshot drift check | `QT_QPA_PLATFORM=offscreen python3 tests/control-commands-snapshot.py build/zene` → `snapshot-check-1.log` | 0 |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 — script cannot create `<worktree>-feature-check` outside the sandbox |
| Gates (same script, log dir in /tmp) | `python3 /tmp/feature_check_5684.py <worktree>` → `gate-2.log`, `gate-3-final.log` | 0 — all 8 rows PASS |
| Submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5684` → `submit-2.log` | 1 — only the feature_check child's log dir fails; report/tests/commits PASS |
| Submit check (same check, feature-check log dir in /tmp) | `python3 /tmp/submit_check_5684.py <worktree> 5684` → `submit-3-patched.log` | 0 — RESULT: PASS - submit |

The gate script copy changes only `logs = Path("/tmp/zene-5684-feature-check")`;
`ROOT` stays the real harness directory and no gate, limit or baseline changed.

## Self-check

- **Regression fails with the product change reverted, passes restored.** See the
  four reverse/restore rows above: new-p14-36 EXIT 1 → 0, new-p17-18 EXIT 1 → 0.
- **Deterministic.** Both tests drive the real command handler directly with fixed
  `QJsonObject` arguments; no threads, sleeps, clocks or timing.
- **Cause and complete.** `lyrics()` now validates the two forms once, before any
  clip resolution/checkpoint; the per-note branch (`lyricsByNote`, `:80-108`) and
  the text branch still write exactly the notes they did before (text branch:
  every scoped note, clearing past the last syllable). The GUI caller
  `NoteEditorMenus.cpp:110` invokes the same command and needs no change. Siblings
  in the file (`note.group`, `note.ungroup`, `track.articulations_set`,
  `track.articulations`, `transport.groove`, `transport.midi_options`) do not
  share the lyrics handler or its result fields; `note.edit` does not write lyrics.
- **No collateral damage.** Saved note lyrics are interned/saved by
  `NoteConditions` exactly as before; no serializer changed. The only input now
  rejected is the previously-ambiguous both-forms request. `set` for the text form
  now counts the notes it wrote; `syllables` and the per-note `set` are unchanged.
  No new limits.
- **Realtime.** No audio-thread code changed. The product changes run only in the
  control command handler; 5684 is the audio path and is documented as
  false-positive, not changed.
- **ALREADY-FIXED/FALSE-POSITIVE evidence for 5684.** `InstrumentTrack.cpp:904`
  `tryLock()`, `Track.h:416-419` non-blocking lock, `InstrumentTrackNotes.cpp:245`
  map read inside that `tryLock`, `NotePlayHandle.cpp:291` stored events path. The
  callback never waits; the control thread may wait on the audio thread instead.
- **Sibling commands in the changed command file.** `note.lyrics` is the only
  changed command. `note.group`/`note.ungroup` use their own `group()` result
  (`notes`, `group`); `track.articulations*` use `articulations`,
  `notes_remapped`; `transport.groove`/`transport.midi_options` use their own
  result objects. None reads `note.lyrics`' `set`/`syllables` or its input forms.
  The submit check also lists the rest of the `transport.*` group because this
  file registers two of them: `transport.audition_start/stop`, `get_state`,
  `low_latency`, `metronome_options`, `panic`, `play`, `punch_*`, `record`,
  `record_options` live in other command files and take no lyric argument, so
  they are not affected by either handler change.
- **Log folder deviation.** The mandated `runs/5684.r1/logs` path is EACCES under
  this session's file sandbox, even after `chmod 777`; the same numbered logs are
  kept in `/tmp/zene-5684-logs` and `build/qwen-logs`.

## Files changed

- `src/core/ControlCommandsNotePlayback.cpp` — exactly-one input form; `set` count.
- `tests/src/core/NotePlaybackTest.cpp` — two regression slots.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` — regenerated live
  for the changed `note.lyrics` description (id/count/surface unchanged).
