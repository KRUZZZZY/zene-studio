# Pitch-preserving time stretch — control-surface transcript (`warp.stretch`, row 30)

Committed evidence for feature-list row 30: the stretch mode is **drivable over the control socket by an
agent**, produced by driving the REAL `zene` binary headless through the shared harness
(`tests/control_socket_harness.py`) — the same launch path every other transcript in this tree uses, no
second launch recipe. Driver: `tests/control-pitch-stretch-transcript.py`.

```
$ QT_QPA_PLATFORM=offscreen python3 control-pitch-stretch-transcript.py ../build/zene ; echo EXIT=$?
PASS an untouched clip reads the resampling default             stretch='resample' renders_linearly=True
PASS preserve_pitch on a clip with no rate change is REFUSED, typed kind='refused' message='this clip has no warp markers and follows the project tempo, so it renders linearly: there'
PASS a mode the engine does not know is invalid_args            kind='invalid_args'
PASS the 2x warp is authored                                    marker_count=2
PASS warp.stretch switches the mode and names the previous one  stretch='preserve_pitch' previous='resample' changed=True algorithm='wsola'
PASS warp.list reads the mode back                              stretch='preserve_pitch'
PASS the A16 record classifies warp.stretch as a true inverse   record={'before': {'clip': 'clip-0', 'markers': [{'index': 0, 'offset_ticks': 0, 'source_frame': 0}, {'index': 1, 'offset_ticks': 48, 'source_frame': 44100}], 'source_tempo': 0, 'stretch': 'resample', 'tempo_mode': 'follow'}, 'bytes': 489, 'class': 'true_inverse', 'command': 'warp.stretch', 'commands': 1, 'inverse': {'args': {'clip': 'clip-0', 'mode': 'resample'}, 'op': 'warp.stretch'}, 'mechanism': "ProjectJournal (Clip checkpoint: the stretch mode is the 'stretch' attribute of the clip's own <warp> element, which SampleClip::saveSettings writes and SampleClip::loadSettings re-reads, so one undo restores the previous mode)", 'reversible': True, 'step': 5}
PASS the recorded inverse names the command and the previous mode inverse={'args': {'clip': 'clip-0', 'mode': 'resample'}, 'op': 'warp.stretch'}
PASS control.undo restores the previous mode                    undone=True undone_command='warp.stretch' stretch='resample'
PASS: pitch-stretch control-surface transcript (every check held)
EXIT=0
```

What the checks cover: the default the engine starts from, the **typed refusal** for a clip with
no rate change (rather than a silent accept), `invalid_args` for a mode the engine does not know,
the switch itself, the read-back, the **A16 record** (class, mechanism, and the re-issuable
inverse), and `control.undo` restoring the previous mode.

## The raw transcript, verbatim

Every request and every reply, in order (22 requests):

```
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":true,"pong":true,"proto":1,"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":1,"cmd":"control.version","args":{},"proto":1}
<- {"id":1,"ok":true,"result":{"proto":1,"version":"0.2.1-alpha.196+f611c88"}}
-> {"id":2,"cmd":"track.add","args":{"type":"sample","name":"Stretch Target"},"proto":1}
<- {"id":2,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-5","index":4,"muted":false,"name":"Stretch Target","soloed":false,"track":"trk-5","track_count":5,"type":"sample","visible":true}}
-> {"id":3,"cmd":"clip.add","args":{"track":"trk-5","position":0},"proto":1}
<- {"id":3,"ok":true,"result":{"auto_resize":true,"clip":"clip-0","fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-0","index_in_track":0,"length":192,"muted":false,"name":"","note_count":null,"position":0,"selected":false,"track":"trk-5"}}
-> {"id":4,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":4,"ok":true,"result":{"clip":"clip-0","marker_count":0,"markers":[],"max_markers":128,"renders_linearly":true,"source_tempo":0,"stretch":"resample","stretch_algorithm":"wsola","tempo_mode":"follow","track":"trk-5","warped":false}}
-> {"id":5,"cmd":"warp.stretch","args":{"clip":"clip-0","mode":"preserve_pitch"},"proto":1}
<- {"error":{"kind":"refused","message":"this clip has no warp markers and follows the project tempo, so it renders linearly: there is no rate change for a pitch-preserving stretch to render. Author the warp first (warp.add / warp.set), or declare the clip's own source tempo, and then set the stretch mode."},"id":5,"ok":false}
-> {"id":6,"cmd":"warp.stretch","args":{"clip":"clip-0","mode":"vocoder"},"proto":1}
<- {"error":{"kind":"invalid_args","message":"mode: value not in the allowed set"},"id":6,"ok":false}
-> {"id":7,"cmd":"warp.set","args":{"clip":"clip-0","markers":[{"source_frame":0,"offset_ticks":0},{"source_frame":44100,"offset_ticks":48}]},"proto":1}
<- {"id":7,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":0,"source_frame":0},{"index":1,"offset_ticks":48,"source_frame":44100}],"max_markers":128,"renders_linearly":false,"source_tempo":0,"stretch":"resample","stretch_algorithm":"wsola","tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":8,"cmd":"warp.stretch","args":{"clip":"clip-0","mode":"preserve_pitch"},"proto":1}
<- {"id":8,"ok":true,"result":{"changed":true,"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":0,"source_frame":0},{"index":1,"offset_ticks":48,"source_frame":44100}],"max_markers":128,"previous_mode":"resample","renders_linearly":false,"source_tempo":0,"stretch":"preserve_pitch","stretch_algorithm":"wsola","tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":9,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":9,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":0,"source_frame":0},{"index":1,"offset_ticks":48,"source_frame":44100}],"max_markers":128,"renders_linearly":false,"source_tempo":0,"stretch":"preserve_pitch","stretch_algorithm":"wsola","tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":10,"cmd":"control.transactions","args":{},"proto":1}
<- {"id":10,"ok":true,"result":{"cap_bytes":262144,"cap_records":100,"capped":false,"count":4,"evicted":0,"retained_bytes":1565,"transactions":[{"before":{"track_count":4,"tracks":["trk-0","trk-1","trk-2","trk-4"]},"bytes":425,"class":"true_inverse","command":"track.add","commands":1,"inverse":{"args":{},"op":"remove the created track"},"mechanism":"action checkpoint: the recorded undo step removes the created track through the product's own TrackContainerView::deleteTrackView path, and a fresh track carries only defaults, so removing it restores the container. LIMIT: the project's id counter is monotonic and is not rewound, so a re-add gets a fresh trk-<n>","reversible":true,"step":2},{"before":{"clip_count":0,"track":"trk-5"},"bytes":252,"class":"true_inverse","command":"clip.add","commands":1,"inverse":{"args":{"clip":"clip-0"},"op":"clip.delete"},"mechanism":"ProjectJournal (Track checkpoint: Track::restoreState re-loads the track's serialized clips, which is how the GUI's own clip add/delete/split paths reverse themselves)","reversible":true,"step":3},{"before":{"clip":"clip-0","markers":[],"source_tempo":0,"stretch":"resample","tempo_mode":"follow"},"bytes":399,"class":"true_inverse","command":"warp.set","commands":1,"inverse":{"args":{"clip":"clip-0","markers":[],"mode":"follow","source_tempo":0},"op":"warp.set"},"mechanism":"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the <warp> child element - markers, tempo mode and source tempo - and SampleClip::loadSettings re-reads it, so one undo restores the whole warp map)","reversible":true,"step":4},{"before":{"clip":"clip-0","markers":[{"index":0,"offset_ticks":0,"source_frame":0},{"index":1,"offset_ticks":48,"source_frame":44100}],"source_tempo":0,"stretch":"resample","tempo_mode":"follow"},"bytes":489,"class":"true_inverse","command":"warp.stretch","commands":1,"inverse":{"args":{"clip":"clip-0","mode":"resample"},"op":"warp.stretch"},"mechanism":"ProjectJournal (Clip checkpoint: the stretch mode is the 'stretch' attribute of the clip's own <warp> element, which SampleClip::saveSettings writes and SampleClip::loadSettings re-reads, so one undo restores the previous mode)","reversible":true,"step":5}]}}
-> {"id":11,"cmd":"control.undo","args":{},"proto":1}
<- {"id":11,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"warp.stretch"}}
-> {"id":12,"cmd":"warp.list","args":{"clip":"clip-0"},"proto":1}
<- {"id":12,"ok":true,"result":{"clip":"clip-0","marker_count":2,"markers":[{"index":0,"offset_ticks":0,"source_frame":0},{"index":1,"offset_ticks":48,"source_frame":44100}],"max_markers":128,"renders_linearly":false,"source_tempo":0,"stretch":"resample","stretch_algorithm":"wsola","tempo_mode":"follow","track":"trk-5","warped":true}}
-> {"id":13,"cmd":"control.quit","args":{},"proto":1}
<- {"id":13,"ok":true,"result":{"project_modified":true,"quitting":true,"save_requested":false,"unsaved_changes":"discarded"}}
```
