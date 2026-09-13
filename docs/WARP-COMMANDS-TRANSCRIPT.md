# Raw control-surface transcript — the `warp.*` command group (warp-marker editing)

**What this is.** The acceptance evidence that warp-marker editing is drivable through
`--control-socket` by an agent, produced by driving the REAL `zene` binary headless and printing every
request and every reply verbatim — no summarising layer, so a reader can see exactly what the engine
answered. It is the transcript half of the proof the 0.3.0 scope contract (`NEXT-0.3.0-AGENT-PROMPT.md`
§3.1 item 3) asks for; the registered-ctest half is `tests/src/core/ControlWarpCommandsTest.cpp`
(`ctest -R '^ControlWarpCommandsTest$'` → `100% tests passed, 0 tests failed out of 1`, and it is test #17
of the suite's 88).

**The engine half is not in this change.** The markers, the monotonic map, the `<warp>` element, the
tempo follower/leader modes and the measured render are #597's (`docs/WARP.md`). This group adds the
editing surface — `warp.list` / `warp.add` / `warp.move` / `warp.remove` / `warp.set` — and its proof. No
stretch code was touched.

**Reproduce (unpiped exit codes; the script exits 0 only when every check held):**

```
$ cd <worktree>
$ cmake --build build -j4 --target zene ControlWarpCommandsTest    # EXIT=0
$ cd build/tests
$ QT_QPA_PLATFORM=offscreen python3 ../../tests/control-warp-commands-transcript.py ../zene > transcript.log 2>&1; echo EXIT=$?
EXIT=0
```

The script starts the instance with the documented headless recipe through the shared harness
(`tests/control_socket_harness.py`: `QT_QPA_PLATFORM=offscreen`, a `--config` naming the dummy audio
device, `HOME`/`XDG_*` in a temp directory), so it adds no second launch path. It builds its own fixture —
`track.add` type=sample + `clip.add` — so it needs no project file, and the `warp.*` commands it drives are
the same ids an MCP client sees.

**What the transcript shows, in order:** an unwarped clip's empty map and its `follow`/0 defaults; two
markers added out of order and read back in the engine's own source-frame order; `warp.move`; `warp.set`
carrying the tempo mode (`source`) and the source tempo (140.5 BPM); `warp.set` with an empty list clearing
the map; `control.undo` taking the first marker back off (SPEC A16 — the checkpoint captured an unwarped
clip, i.e. a state with no `<warp>` element, which is the case `SampleClip::loadSettings` resets for); three
typed refusals; and the A16 records `control.transactions` reports for every mutating call
(`class=true_inverse`, `reversible=True`, the recorded inverse naming `warp.set` with the before-state's own
values).

**UI absence — one line:** warp marker editing is drivable through the socket, not from the interface.
`docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` carry the same sentence.

---

```
$ cd build/tests && QT_QPA_PLATFORM=offscreen python3 ../../tests/control-warp-commands-transcript.py ../zene
instance: ../zene
socket:   /tmp/zctl-run-gbvkws4e/zene.sock

---- the warp.* records control.transactions reports ----
warp.add     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [], "mode": "follow", "source_tempo": 0}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.add     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [{"index": 0, "offset_ticks": 96, "source_frame": 176400}], "mode": "follow", "source_tempo": 0}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.move    class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [{"index": 0, "offset_ticks": 24, "source_frame": 44100}, {"index": 1, "offset_ticks": 96, "source_frame": 176400}], "mode": "follow", "source_tempo": 0}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.set     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [{"index": 0, "offset_ticks": 24, "source_frame": 44100}, {"index": 1, "offset_ticks": 60, "source_frame": 176400}], "mode": "follow", "source_tempo": 0}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.set     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [{"index": 0, "offset_ticks": 24, "source_frame": 44100}, {"index": 1, "offset_ticks": 60, "source_frame": 176400}], "mode": "source", "source_tempo": 140.5}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.add     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [], "mode": "source", "source_tempo": 140.5}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)
warp.add     class=true_inverse reversible=True
             inverse={"args": {"clip": "clip-0", "markers": [], "mode": "source", "source_tempo": 140.5}, "op": "warp.set"}
             mechanism=ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)

==== checks ====
  an untouched clip reports an empty map               ok
  warp.add returns the new map                         ok
  the map reads back in source-frame order             ok
  warp.move reports the previous offset                ok
  warp.set sets the tempo mode and source tempo        ok
  an empty marker list clears the map                  ok
  control.undo takes the marker back off               ok
  a duplicate source frame is refused, typed           ok
  a malformed clip id is refused, typed                ok
  an unknown clip id is refused, typed                 ok
  every mutating warp.* call left an A16 record        ok
  a warp record's inverse names warp.set               ok
  control.quit stops the instance                      ok

---- raw request/response transcript ----
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":true,"pong":true,"proto":1,"version":"0.2.1-alpha"}}
-> {"id":1,"cmd":"control.version","args":{},"proto":1}
<- {"id":1,"ok":true,"result":{"proto":1,"version":"0.2.1-alpha"}}
-> {"id":2,"cmd":"track.add","args":{"type":"sample","name":"Warp Target"},"proto":1}
<- {"id":2,"ok":true,"result":{"clip_count":0,"id":"trk-5","index":4,"muted":false,"name":"Warp Target","soloed":false,"track":"trk-5","track_count":5,"type":"sample"}}
-> {"id":3,"cmd":"clip.add","args":{"track":"trk-5","position":0},"proto":1}
<- {"id":3,"ok":true,"result":{"auto_resize":true,"clip":"clip-0","id":"clip-0","index_in_track":0,"length":192,"muted":false,"name":"","note_count":null,"position":0,"selected":false,"track":"trk-5"}}
-> {"id":4,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":4,"ok":true,"result":{"clip":"clip-0","marker_count":0,"markers":[],"max_markers":128,"source_tempo":0,"tempo_mode":"follow","track":"trk-5","warped":false}}
-> {"id":5,"cmd":"warp.add","args":{"clip":"clip-0","source_frame":176400,"offset_ticks":96},"proto":1}
<- {"id":5,"ok":true,"result":{"added":{"index":0,"offset_ticks":96,"source_frame":176400},"clip":"clip-0","marker_count":1,"markers":[{"index":0,"offset_ticks":96,"source_frame":176400}],"max_markers":128,"source_tempo":0,"tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":6,"cmd":"warp.add","args":{"clip":"clip-0","source_frame":44100,"offset_ticks":24},"proto":1}
<- {"id":6,"ok":true,"result":{"added":{"index":0,"offset_ticks":24,"source_frame":44100},"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":96,"source_frame":176400}],"max_markers":128,"source_tempo":0,"tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":7,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":7,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":96,"source_frame":176400}],"max_markers":128,"source_tempo":0,"tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":8,"cmd":"warp.move","args":{"clip":"clip-0","source_frame":176400,"offset_ticks":60},"proto":1}
<- {"id":8,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"max_markers":128,"moved":{"offset_ticks":60,"previous_offset_ticks":96,"source_frame":176400},"source_tempo":0,"tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":9,"cmd":"warp.set","args":{"clip":"clip-0","mode":"source","source_tempo":140.5},"proto":1}
<- {"id":9,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"max_markers":128,"source_tempo":140.5,"tempo_mode":"source","track":"trk-5","warped":true}}
-> {"id":10,"cmd":"warp.set","args":{"clip":"clip-0","markers":[]},"proto":1}
<- {"id":10,"ok":true,"result":{"clip":"clip-0","marker_count":0,"markers":[],"max_markers":128,"source_tempo":140.5,"tempo_mode":"source","track":"trk-5","warped":false}}
-> {"id":11,"cmd":"warp.add","args":{"clip":"clip-0","source_frame":44100,"offset_ticks":24},"proto":1}
<- {"id":11,"ok":true,"result":{"added":{"index":0,"offset_ticks":24,"source_frame":44100},"clip":"clip-0","marker_count":1,"markers":[{"index":0,"offset_ticks":24,"source_frame":44100}],"max_markers":128,"source_tempo":140.5,"tempo_mode":"source","track":"trk-5","warped":true}}
-> {"id":12,"cmd":"control.undo","args":{},"proto":1}
<- {"id":12,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"warp.add"}}
-> {"id":13,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":13,"ok":true,"result":{"clip":"clip-0","marker_count":0,"markers":[],"max_markers":128,"source_tempo":140.5,"tempo_mode":"source","track":"trk-5","warped":false}}
-> {"id":14,"cmd":"warp.add","args":{"clip":"clip-0","source_frame":44100,"offset_ticks":96},"proto":1}
<- {"id":14,"ok":true,"result":{"added":{"index":0,"offset_ticks":96,"source_frame":44100},"clip":"clip-0","marker_count":1,"markers":[{"index":0,"offset_ticks":96,"source_frame":44100}],"max_markers":128,"source_tempo":140.5,"tempo_mode":"source","track":"trk-5","warped":true}}
-> {"id":15,"cmd":"warp.add","args":{"clip":"clip-0","source_frame":44100,"offset_ticks":48},"proto":1}
<- {"error":{"kind":"invalid_args","message":"a marker for source frame 44100 at offset 48 would reuse that source frame or put the set out of order: the engine refuses it and no marker was added"},"id":15,"ok":false}
-> {"id":16,"cmd":"warp.list","args":{"clip":"not-a-clip"},"proto":1}
<- {"error":{"kind":"invalid_args","message":"'not-a-clip' is not a clip id of the form clip-<n>"},"id":16,"ok":false}
-> {"id":17,"cmd":"warp.list","args":{"clip":"clip-9999"},"proto":1}
<- {"error":{"kind":"not_found","message":"no clip clip-9999 (the song has 1)"},"id":17,"ok":false}
-> {"id":18,"cmd":"control.transactions","args":{},"proto":1}
<- {"id":18,"ok":true,"result":{"cap_bytes":262144,"cap_records":100,"capped":false,"count":9,"evicted":0,"retained_bytes":4062,"transactions":[{"before":{"track_count":4,"tracks":["trk-0","trk-1","trk-2","trk-4"]},"bytes":425,"class":"true_inverse","command":"track.add","inverse":{"args":{},"op":"remove the created track"},"mechanism":"action checkpoint: the recorded undo step removes the created track through the product's own TrackContainerView::deleteTrackView path, and a fresh track carries only defaults, so removing it restores the container. LIMIT: the project's id counter is monotonic and is not rewound, so a re-add gets a fresh trk-<n>","reversible":true},{"before":{"clip_count":0,"track":"trk-5"},"bytes":252,"class":"true_inverse","command":"clip.add","inverse":{"args":{"clip":"clip-0"},"op":"clip.delete"},"mechanism":"ProjectJournal (Track checkpoint: Track::restoreState re-loads the track's serialized clips, which is how the GUI's own clip add/delete/split paths reverse themselves)","reversible":true},{"before":{"clip":"clip-0","markers":[],"source_tempo":0,"tempo_mode":"follow"},"bytes":378,"class":"true_inverse","command":"warp.add","inverse":{"args":{"clip":"clip-0","markers":[],"mode":"follow","source_tempo":0},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":96,"source_frame":176400}],"source_tempo":0,"tempo_mode":"follow"},"bytes":480,"class":"true_inverse","command":"warp.add","inverse":{"args":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":96,"source_frame":176400}],"mode":"follow","source_tempo":0},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":96,"source_frame":176400}],"source_tempo":0,"tempo_mode":"follow"},"bytes":583,"class":"true_inverse","command":"warp.move","inverse":{"args":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":96,"source_frame":176400}],"mode":"follow","source_tempo":0},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"source_tempo":0,"tempo_mode":"follow"},"bytes":582,"class":"true_inverse","command":"warp.set","inverse":{"args":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"mode":"follow","source_tempo":0},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"source_tempo":140.5,"tempo_mode":"source"},"bytes":590,"class":"true_inverse","command":"warp.set","inverse":{"args":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":24,"source_frame":44100},{"index":1,"offset_ticks":60,"source_frame":176400}],"mode":"source","source_tempo":140.5},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[],"source_tempo":140.5,"tempo_mode":"source"},"bytes":386,"class":"true_inverse","command":"warp.add","inverse":{"args":{"clip":"clip-0","markers":[],"mode":"source","source_tempo":140.5},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true},{"before":{"clip":"clip-0","markers":[],"source_tempo":140.5,"tempo_mode":"source"},"bytes":386,"class":"true_inverse","command":"warp.add","inverse":{"args":{"clip":"clip-0","markers":[],"mode":"source","source_tempo":140.5},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true}]}}
-> {"id":19,"cmd":"control.quit","args":{},"proto":1}
<- {"id":19,"ok":true,"result":{"project_modified":true,"quitting":true,"save_requested":false,"unsaved_changes":"discarded"}}
PASS: warp.* control-surface transcript (every check held)
```
