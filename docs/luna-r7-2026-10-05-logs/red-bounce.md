# Unfixed bounce source: ctest ControlSuccessPaths from build/tests; exit 8. MIDI import/export fixtures were built before this run, so only the two bounce mute assertions fail.

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/build/tests
    Start 357: ControlSuccessPaths
1/1 Test #357: ControlSuccessPaths ..............***Failed    8.75 sec
arrangement record status: {'armed': True, 'capacity': 64, 'dropped': 0, 'landable_pairs': 1, 'open_starts': 0, 'pending': 2, 'recorded': 2}
land: {'armed': True, 'clips': 1, 'collapsed': 0, 'dropped': 0, 'events': 2, 'landed': [{'clip': 'clip-11', 'content': 'pattern_reference_not_copied', 'landed': True, 'length': 112, 'name': 'take', 'pattern': 1, 'position': 0, 'scene': 0, 'start_tick': 0, 'stop_tick': 112, 'track': 'trk-2', 'track_index': 0}], 'pairs': 1, 'recorded': 2, 'skipped_columns': 0, 'unmatched_stops': 0}
audible_diff(a, a): {'differ': False, 'exit_code': 0, 'report': "mmpz-git audible-diff: /tmp/zctl-run-5h2i8wc4/same.mmp vs /tmp/zctl-run-5h2i8wc4/same.mmp\n  renderer   : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/build/zene\n  tempo      : 140 BPM, 4/4 -> 1 bar = 1.7143 s (192 ticks)\n  compared   : 1 bar(s) per track, render 44100 Hz, 2 channel(s)\n  thresholds : 1.00 dB RMS, -60.0 dBFS peak, floor -60.0 dBFS\n  differences: none\n  summary    : the two versions render identically within the thresholds\n  limits     : per-bar loudness and peak difference. It says WHICH bars\n               and WHICH track differ, not which note; a change that\n               keeps a bar's energy identical is invisible; the bar\n               grid assumes a 4/4 bar.\n  artifacts  : /tmp/mmpz-audible-diff-6wsi7okg\n"}

==== checks ====
  a slot is built to perform                                 ok
  Arrangement Record arms                                    ok
  the land pass writes the one clip the launch/stop pair describes ok
  project.audible_diff succeeds                              ok
  a project compared with itself does not differ             ok
  project.new replaces the session with one that has no file yet ok
  transport.set_metronome switches the click on              ok
  project.save_as_template writes default.mpt                ok
  project.import adds the MIDI file's track                  ok
  project.export_midi writes a .mid                          ok
  project.save_version writes the next version beside the first ok
  project.subproject_add renders the file onto a new track   ok
  project.subproject_refresh renders it again into a new file ok
  track.flatten replaces the track with an audio track playing its render ok
  range.bounce renders the selection onto a new track        ok
  range.bounce mutes the bounced track                       ok
  range.bounce's undo takes the landed track away after its removal is undone ok
  bounce.land creates a track and mutes its source           ok
  window.sample_editor opens the bounced clip with the requested range ok
  undo of source removal restores the bounced, muted source  ok
  bounce undo unmutes a rebuilt source and removes the landed track FAILED
      track='trk-29' landed='trk-33'
  bounce redo mutes the rebuilt source again                 ok
  a second bounce undo restores the source's original mute   FAILED
      track='trk-29'
  window.screenshot renders the main window to a PNG         ok
  window.detach_all answers without holding the surface      ok
  window.attach_all answers without holding the surface      ok
  window.settings answers without holding the surface        ok
  app.about answers without holding the surface              ok
  window.command_palette answers without holding the surface ok
  window.screenshot renders the ACTIVE window too            ok
  window.fullscreen enters fullscreen                        ok

---- raw request/response transcript ----
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":true,"pong":true,"proto":1,"version":"0.4.0-alpha.115+bcac7c0"}}
-> {"id":1,"cmd":"track.add","args":{"type":"instrument","name":"Performed"},"proto":1}
<- {"id":1,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-9","index":4,"muted":false,"name":"Performed","soloed":false,"track":"trk-9","track_count":5,"type":"instrument","visible":true}}
-> {"id":2,"cmd":"session.set_grid","args":{"tracks":1,"scenes":1},"proto":1}
<- {"id":2,"ok":true,"result":{"grid":{"clips":0,"scenes":1,"tracks":1}}}
-> {"id":3,"cmd":"session.set_slot","args":{"track":0,"scene":0,"type":"midi","pattern":1,"name":"take","mode":"trigger","quantisation":"none"},"proto":1}
<- {"id":3,"ok":true,"result":{"slot":{"detune":0,"follow_actions":[],"gain_db":0,"legato":false,"loop_length":0,"loop_start":0,"mode":"trigger","name":"take","pattern":1,"quantisation":"none","ram":false,"scene":0,"source":"","track":0,"transpose":0,"type":"midi"}}}
-> {"id":4,"cmd":"session.set_quantisation","args":{"quantisation":"none"},"proto":1}
<- {"id":4,"ok":true,"result":{"quantisation":"none"}}
-> {"id":5,"cmd":"transport.play","args":{},"proto":1}
<- {"id":5,"ok":true,"result":{"playing":true}}
-> {"id":6,"cmd":"session.arrangement_record_arm","args":{"armed":true},"proto":1}
<- {"id":6,"ok":true,"result":{"armed":true}}
-> {"id":7,"cmd":"session.launch_slot","args":{"track":0,"scene":0,"quantisation":"none"},"proto":1}
<- {"id":7,"ok":true,"result":{"clip":{"detune":0,"follow_actions":[],"gain_db":0,"legato":false,"loop_length":0,"loop_start":0,"mode":"trigger","name":"take","pattern":1,"quantisation":"none","ram":false,"scene":0,"source":"","track":0,"transpose":0,"type":"midi"},"mode":"trigger","next_bar":0,"quantisation":"none","scene":0,"scheduled_tick":0,"track":0}}
-> {"id":8,"cmd":"session.stop_all","args":{},"proto":1}
<- {"id":8,"ok":true,"result":{"reset_requested":true}}
-> {"id":9,"cmd":"session.arrangement_record_status","args":{},"proto":1}
<- {"id":9,"ok":true,"result":{"armed":true,"capacity":64,"dropped":0,"landable_pairs":1,"open_starts":0,"pending":2,"recorded":2}}
-> {"id":10,"cmd":"session.arrangement_record_land","args":{},"proto":1}
<- {"id":10,"ok":true,"result":{"armed":true,"clips":1,"collapsed":0,"dropped":0,"events":2,"landed":[{"clip":"clip-11","content":"pattern_reference_not_copied","landed":true,"length":112,"name":"take","pattern":1,"position":0,"scene":0,"start_tick":0,"stop_tick":112,"track":"trk-2","track_index":0}],"pairs":1,"recorded":2,"skipped_columns":0,"unmatched_stops":0}}
-> {"id":11,"cmd":"session.arrangement_record_arm","args":{"armed":false},"proto":1}
<- {"id":11,"ok":true,"result":{"armed":false}}
-> {"id":12,"cmd":"transport.stop","args":{},"proto":1}
<- {"id":12,"ok":true,"result":{"playing":false}}
-> {"id":13,"cmd":"project.save","args":{"path":"/tmp/zctl-run-5h2i8wc4/same.mmp"},"proto":1}
<- {"id":13,"ok":true,"result":{"file":"/tmp/zctl-run-5h2i8wc4/same.mmp","revision_kept":false,"revision_skipped":"there was no previous revision to keep: this is the file's first save","revisions":{"count":0,"file":"/tmp/zctl-run-5h2i8wc4/same.mmp","max_revision_bytes":8388608,"max_total_bytes":25165824,"policy":"keep-3","retained_bytes":0,"revisions":[]},"saved":true}}
-> {"id":14,"cmd":"project.audible_diff","args":{"a":"/tmp/zctl-run-5h2i8wc4/same.mmp","b":"/tmp/zctl-run-5h2i8wc4/same.mmp","mix_only":true},"proto":1}
<- {"id":14,"ok":true,"result":{"differ":false,"exit_code":0,"report":"mmpz-git audible-diff: /tmp/zctl-run-5h2i8wc4/same.mmp vs /tmp/zctl-run-5h2i8wc4/same.mmp\n  renderer   : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/build/zene\n  tempo      : 140 BPM, 4/4 -> 1 bar = 1.7143 s (192 ticks)\n  compared   : 1 bar(s) per track, render 44100 Hz, 2 channel(s)\n  thresholds : 1.00 dB RMS, -60.0 dBFS peak, floor -60.0 dBFS\n  differences: none\n  summary    : the two versions render identically within the thresholds\n  limits     : per-bar loudness and peak difference. It says WHICH bars\n               and WHICH track differ, not which note; a change that\n               keeps a bar's energy identical is invisible; the bar\n               grid assumes a 4/4 bar.\n  artifacts  : /tmp/mmpz-audible-diff-6wsi7okg\n"}}
-> {"id":15,"cmd":"project.new","args":{},"proto":1}
<- {"id":15,"ok":true,"result":{"file":"","template":"","track_count":4}}
-> {"id":16,"cmd":"transport.set_metronome","args":{"enabled":true},"proto":1}
<- {"id":16,"ok":true,"result":{"enabled":true,"previous":false}}
-> {"id":17,"cmd":"transport.set_metronome","args":{"enabled":false},"proto":1}
<- {"id":17,"ok":true,"result":{"enabled":false,"previous":true}}
-> {"id":18,"cmd":"project.save_as_template","args":{},"proto":1}
<- {"id":18,"ok":true,"result":{"path":"/tmp/zctl-run-5h2i8wc4/workspace/templates/default.mpt","replaced":false}}
-> {"id":19,"cmd":"project.import","args":{"path":"/tmp/zctl-run-5h2i8wc4/one-note.mid"},"proto":1}
<- {"id":19,"ok":true,"result":{"path":"/tmp/zctl-run-5h2i8wc4/one-note.mid","track_count":8,"tracks_added":4}}
-> {"id":20,"cmd":"project.export_midi","args":{"path":"/tmp/zctl-run-5h2i8wc4/notes"},"proto":1}
<- {"id":20,"ok":true,"result":{"bytes":121,"path":"/tmp/zctl-run-5h2i8wc4/notes.mid"}}
-> {"id":21,"cmd":"project.save","args":{"path":"/tmp/zctl-run-5h2i8wc4/versioned.mmp"},"proto":1}
<- {"id":21,"ok":true,"result":{"file":"/tmp/zctl-run-5h2i8wc4/versioned.mmp","revision_kept":false,"revision_skipped":"there was no previous revision to keep: this is the file's first save","revisions":{"count":0,"file":"/tmp/zctl-run-5h2i8wc4/versioned.mmp","max_revision_bytes":8388608,"max_total_bytes":25165824,"policy":"keep-3","retained_bytes":0,"revisions":[]},"saved":true}}
-> {"id":22,"cmd":"project.save_version","args":{},"proto":1}
<- {"id":22,"ok":true,"result":{"file":"/tmp/zctl-run-5h2i8wc4/versioned-01.mmp","previous_file":"/tmp/zctl-run-5h2i8wc4/versioned.mmp"}}
-> {"id":23,"cmd":"project.new","args":{},"proto":1}
<- {"id":23,"ok":true,"result":{"file":"","template":"","track_count":4}}
-> {"id":24,"cmd":"track.add","args":{"type":"instrument","name":"Nested"},"proto":1}
<- {"id":24,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-9","index":4,"muted":false,"name":"Nested","soloed":false,"track":"trk-9","track_count":5,"type":"instrument","visible":true}}
-> {"id":25,"cmd":"clip.add","args":{"track":"trk-9","position":0},"proto":1}
<- {"id":25,"ok":true,"result":{"auto_resize":true,"clip":"clip-10","fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-10","index_in_track":0,"length":192,"muted":false,"name":"","note_count":0,"position":0,"selected":false,"track":"trk-9"}}
-> {"id":26,"cmd":"note.add","args":{"clip":"clip-10","key":60,"position":0,"length":96,"velocity":100},"proto":1}
<- {"id":26,"ok":true,"result":{"clip":"clip-10","id":"note-12","index":0,"key":60,"length":96,"midi_velocity":127,"note":"note-12","note_count":1,"pan":0,"position":0,"slide":false,"velocity":100}}
-> {"id":27,"cmd":"project.save","args":{"path":"/tmp/zctl-run-5h2i8wc4/sub.mmp"},"proto":1}
<- {"id":27,"ok":true,"result":{"file":"/tmp/zctl-run-5h2i8wc4/sub.mmp","revision_kept":false,"revision_skipped":"there was no previous revision to keep: this is the file's first save","revisions":{"count":0,"file":"/tmp/zctl-run-5h2i8wc4/sub.mmp","max_revision_bytes":8388608,"max_total_bytes":25165824,"policy":"keep-3","retained_bytes":0,"revisions":[]},"saved":true}}
-> {"id":28,"cmd":"project.save","args":{"path":"/tmp/zctl-run-5h2i8wc4/host.mmp"},"proto":1}
<- {"id":28,"ok":true,"result":{"file":"/tmp/zctl-run-5h2i8wc4/host.mmp","revision_kept":false,"revision_skipped":"there was no previous revision to keep: this is the file's first save","revisions":{"count":0,"file":"/tmp/zctl-run-5h2i8wc4/host.mmp","max_revision_bytes":8388608,"max_total_bytes":25165824,"policy":"keep-3","retained_bytes":0,"revisions":[]},"saved":true}}
-> {"id":29,"cmd":"project.subproject_add","args":{"path":"/tmp/zctl-run-5h2i8wc4/sub.mmp"},"proto":1}
<- {"id":29,"ok":true,"result":{"audio":"/tmp/zctl-run-5h2i8wc4/host-subprojects/sub-1791188978999.wav","path":"/tmp/zctl-run-5h2i8wc4/sub.mmp","track":"trk-13"}}
-> {"id":30,"cmd":"project.subproject_refresh","args":{"track":"trk-13"},"proto":1}
<- {"id":30,"ok":true,"result":{"audio":"/tmp/zctl-run-5h2i8wc4/host-subprojects/sub-1791188979423.wav","exists":true,"path":"/tmp/zctl-run-5h2i8wc4/sub.mmp","rendered_ms":1791188979834,"stale":false,"track":"trk-13"}}
-> {"id":31,"cmd":"track.add","args":{"type":"instrument","name":"Flattened"},"proto":1}
<- {"id":31,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-15","index":6,"muted":false,"name":"Flattened","soloed":false,"track":"trk-15","track_count":7,"type":"instrument","visible":true}}
-> {"id":32,"cmd":"clip.add","args":{"track":"trk-15","position":0},"proto":1}
<- {"id":32,"ok":true,"result":{"auto_resize":true,"clip":"clip-16","fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-16","index_in_track":0,"length":192,"muted":false,"name":"","note_count":0,"position":0,"selected":false,"track":"trk-15"}}
-> {"id":33,"cmd":"note.add","args":{"clip":"clip-16","key":62,"position":0,"length":96,"velocity":100},"proto":1}
<- {"id":33,"ok":true,"result":{"clip":"clip-16","id":"note-18","index":0,"key":62,"length":96,"midi_velocity":127,"note":"note-18","note_count":1,"pan":0,"position":0,"slide":false,"velocity":100}}
-> {"id":34,"cmd":"track.flatten","args":{"track":"trk-15"},"proto":1}
<- {"id":34,"ok":true,"result":{"bytes":604256,"end_ticks":384,"flattened":"trk-15","frames":151040,"path":"/tmp/zctl-run-5h2i8wc4/host-bounces/trk-15-flatten.wav","sample_rate":44100,"sha256":"c1964d6b744a076bc380d2e92ec517ea984342d2b48fc839eff68f686ec6623e","start_ticks":0,"track":"trk-19"}}
-> {"id":35,"cmd":"track.add","args":{"type":"instrument","name":"Selected"},"proto":1}
<- {"id":35,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-21","index":7,"muted":false,"name":"Selected","soloed":false,"track":"trk-21","track_count":8,"type":"instrument","visible":true}}
-> {"id":36,"cmd":"clip.add","args":{"track":"trk-21","position":0},"proto":1}
<- {"id":36,"ok":true,"result":{"auto_resize":true,"clip":"clip-22","fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-22","index_in_track":0,"length":192,"muted":false,"name":"","note_count":0,"position":0,"selected":false,"track":"trk-21"}}
-> {"id":37,"cmd":"note.add","args":{"clip":"clip-22","key":64,"position":0,"length":96,"velocity":100},"proto":1}
<- {"id":37,"ok":true,"result":{"clip":"clip-22","id":"note-24","index":0,"key":64,"length":96,"midi_velocity":127,"note":"note-24","note_count":1,"pan":0,"position":0,"slide":false,"velocity":100}}
-> {"id":38,"cmd":"range.set","args":{"start":0,"end":192,"tracks":["trk-21"]},"proto":1}
<- {"id":38,"ok":true,"result":{"active":true,"all_tracks":false,"clipboard":0,"end":192,"start":0,"tracks":["trk-21"]}}
-> {"id":39,"cmd":"range.bounce","args":{"out":"/tmp/zctl-run-5h2i8wc4/selection.wav"},"proto":1}
<- {"id":39,"ok":true,"result":{"bytes":302444,"end_ticks":192,"frames":75600,"landed_track":"trk-25","path":"/tmp/zctl-run-5h2i8wc4/selection.wav","sample_rate":44100,"sha256":"d61d6d6e53467a3edee1cad55f37da0d7afb2bb854921d803ae0069f91fd1726","start_ticks":0,"tracks":1}}
-> {"id":40,"cmd":"track.get_state","args":{"track":"trk-21"},"proto":1}
<- {"id":40,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-21","index":7,"monitor":"in","muted":true,"name":"Selected","soloed":false,"type":"instrument","visible":true}}
-> {"id":41,"cmd":"track.remove","args":{"track":"trk-25"},"proto":1}
<- {"id":41,"ok":true,"result":{"dry_run":false,"index":8,"removed":"trk-25","track_count":8}}
-> {"id":42,"cmd":"control.undo","args":{},"proto":1}
<- {"id":42,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"track.remove","undone_commands":1}}
-> {"id":43,"cmd":"control.undo","args":{},"proto":1}
<- {"id":43,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"track.remove","undone_commands":1}}
-> {"id":44,"cmd":"track.get_state","args":{"track":"trk-25"},"proto":1}
<- {"error":{"kind":"not_found","message":"no track trk-25 (the song has 8)"},"id":44,"ok":false}
-> {"id":45,"cmd":"track.get_state","args":{"track":"trk-21"},"proto":1}
<- {"id":45,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-21","index":7,"monitor":"in","muted":false,"name":"Selected","soloed":false,"type":"instrument","visible":true}}
-> {"id":46,"cmd":"track.add","args":{"type":"instrument","name":"Bounce source"},"proto":1}
<- {"id":46,"ok":true,"result":{"clip_count":0,"folder":"","id":"trk-29","index":8,"muted":false,"name":"Bounce source","soloed":false,"track":"trk-29","track_count":9,"type":"instrument","visible":true}}
-> {"id":47,"cmd":"clip.add","args":{"track":"trk-29","position":0},"proto":1}
<- {"id":47,"ok":true,"result":{"auto_resize":true,"clip":"clip-30","fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-30","index_in_track":0,"length":192,"muted":false,"name":"","note_count":0,"position":0,"selected":false,"track":"trk-29"}}
-> {"id":48,"cmd":"note.add","args":{"clip":"clip-30","key":65,"position":0,"length":96,"velocity":100},"proto":1}
<- {"id":48,"ok":true,"result":{"clip":"clip-30","id":"note-32","index":0,"key":65,"length":96,"midi_velocity":127,"note":"note-32","note_count":1,"pan":0,"position":0,"slide":false,"velocity":100}}
-> {"id":49,"cmd":"bounce.land","args":{"track":"trk-29","start":0,"end":192,"out":"/tmp/zctl-run-5h2i8wc4/bounce-source.wav"},"proto":1}
<- {"id":49,"ok":true,"result":{"bytes":302444,"end_ticks":192,"frames":75600,"landed_track":"trk-33","path":"/tmp/zctl-run-5h2i8wc4/bounce-source.wav","sample_rate":44100,"sha256":"d61d6d6e53467a3edee1cad55f37da0d7afb2bb854921d803ae0069f91fd1726","start_ticks":0,"track":"trk-29"}}
-> {"id":50,"cmd":"track.get_state","args":{"track":"trk-29"},"proto":1}
<- {"id":50,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-29","index":8,"monitor":"in","muted":true,"name":"Bounce source","soloed":false,"type":"instrument","visible":true}}
-> {"id":51,"cmd":"arrangement.get_state","args":{},"proto":1}
<- {"id":51,"ok":true,"result":{"clip_count":6,"clips":[{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-10","index_in_track":0,"length":192,"muted":false,"name":"","note_count":1,"position":0,"selected":false,"track":"trk-9"},{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-14","index_in_track":0,"length":383,"muted":false,"name":"","note_count":null,"position":0,"selected":false,"track":"trk-13"},{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-20","index_in_track":0,"length":383,"muted":false,"name":"","note_count":null,"position":0,"selected":false,"track":"trk-19"},{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-22","index_in_track":0,"length":192,"muted":false,"name":"","note_count":1,"position":0,"selected":false,"track":"trk-21"},{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-30","index_in_track":0,"length":192,"muted":false,"name":"","note_count":1,"position":0,"selected":false,"track":"trk-29"},{"auto_resize":true,"fade_in":0,"fade_in_shape":"linear","fade_out":0,"fade_out_shape":"linear","gain_db":0,"id":"clip-34","index_in_track":0,"length":192,"muted":false,"name":"","note_count":null,"position":0,"selected":false,"track":"trk-33"}],"selected_clip":"","track_count":10,"tracks":[{"clip_count":0,"clips":[],"folder":"","id":"trk-2","index":0,"muted":false,"name":"TripleOscillator","selected":false,"soloed":false,"type":"instrument","visible":true},{"clip_count":0,"clips":[],"folder":"","id":"trk-3","index":1,"muted":false,"name":"Sample track","selected":false,"soloed":false,"type":"sample","visible":true},{"clip_count":0,"clips":[],"folder":"","id":"trk-4","index":2,"muted":false,"name":"Pattern 0","selected":false,"soloed":false,"type":"pattern","visible":true},{"clip_count":0,"clips":[],"folder":"","id":"trk-8","index":3,"muted":false,"name":"Automation track","selected":false,"soloed":false,"type":"automation","visible":true},{"clip_count":1,"clips":["clip-10"],"folder":"","id":"trk-9","index":4,"muted":false,"name":"Nested","selected":false,"soloed":false,"type":"instrument","visible":true},{"clip_count":1,"clips":["clip-14"],"folder":"","id":"trk-13","index":5,"muted":false,"name":"sub","selected":false,"soloed":false,"type":"sample","visible":true},{"clip_count":1,"clips":["clip-20"],"folder":"","id":"trk-19","index":6,"muted":false,"name":"Flattened","selected":false,"soloed":false,"type":"sample","visible":true},{"clip_count":1,"clips":["clip-22"],"folder":"","id":"trk-21","index":7,"muted":false,"name":"Selected","selected":false,"soloed":false,"type":"instrument","visible":true},{"clip_count":1,"clips":["clip-30"],"folder":"","id":"trk-29","index":8,"muted":true,"name":"Bounce source","selected":false,"soloed":false,"type":"instrument","visible":true},{"clip_count":1,"clips":["clip-34"],"folder":"","id":"trk-33","index":9,"muted":false,"name":"Bounce source (bounce)","selected":false,"soloed":false,"type":"sample","visible":true}]}}
-> {"id":52,"cmd":"window.sample_editor","args":{"clip":"clip-34","from":441,"to":882,"unit":"samples","zoom":"selection"},"proto":1}
<- {"id":52,"ok":true,"result":{"clip":"clip-34","end_text":"882 smp","frames":75600,"from":441,"length":441,"length_text":"441 smp","sample_rate":44100,"start_text":"441 smp","status":"","to":882,"unit":"samples","view_from":419,"view_to":1014}}
-> {"id":53,"cmd":"track.remove","args":{"track":"trk-29"},"proto":1}
<- {"id":53,"ok":true,"result":{"dry_run":false,"index":8,"removed":"trk-29","track_count":9}}
-> {"id":54,"cmd":"control.undo","args":{},"proto":1}
<- {"id":54,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"track.remove","undone_commands":1}}
-> {"id":55,"cmd":"track.get_state","args":{"track":"trk-29"},"proto":1}
<- {"id":55,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-29","index":8,"monitor":"in","muted":true,"name":"Bounce source","soloed":false,"type":"instrument","visible":true}}
-> {"id":56,"cmd":"control.undo","args":{},"proto":1}
<- {"id":56,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"track.remove","undone_commands":1}}
-> {"id":57,"cmd":"track.get_state","args":{"track":"trk-29"},"proto":1}
<- {"id":57,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-29","index":8,"monitor":"in","muted":true,"name":"Bounce source","soloed":false,"type":"instrument","visible":true}}
-> {"id":58,"cmd":"control.redo","args":{},"proto":1}
<- {"id":58,"ok":true,"result":{"can_redo":true,"can_undo":true,"redone":true}}
-> {"id":59,"cmd":"track.get_state","args":{"track":"trk-29"},"proto":1}
<- {"id":59,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-29","index":8,"monitor":"in","muted":true,"name":"Bounce source","soloed":false,"type":"instrument","visible":true}}
-> {"id":60,"cmd":"control.undo","args":{},"proto":1}
<- {"id":60,"ok":true,"result":{"can_redo":true,"can_undo":true,"mechanism":"lmms::ProjectJournal","undone":true,"undone_command":"track.remove","undone_commands":1}}
-> {"id":61,"cmd":"track.get_state","args":{"track":"trk-29"},"proto":1}
<- {"id":61,"ok":true,"result":{"folder":"","frozen":false,"frozen_audio":"","frozen_audio_ready":false,"frozen_end_ticks":0,"frozen_muted_clips":0,"frozen_start_ticks":0,"id":"trk-29","index":8,"monitor":"in","muted":true,"name":"Bounce source","soloed":false,"type":"instrument","visible":true}}
-> {"id":62,"cmd":"mixer.add_channel","args":{},"proto":1}
<- {"id":62,"ok":true,"result":{"channel":"ch-40","index":1}}
-> {"id":63,"cmd":"mixer.add_channel","args":{},"proto":1}
<- {"id":63,"ok":true,"result":{"channel":"ch-41","index":2}}
-> {"id":64,"cmd":"vca.create","args":{"name":"Shell"},"proto":1}
<- {"id":64,"ok":true,"result":{"gain":1,"group":"vca-0","id":"vca-0","member_count":0,"members":[],"missing_count":0,"missing_tracks":[],"muted":false,"name":"Shell","phase_locked":true,"soloed":false,"track_count":0,"tracks":[],"volume":1}}
-> {"id":65,"cmd":"vca.assign","args":{"group":"vca-0","channel":"ch-40"},"proto":1}
<- {"id":65,"ok":true,"result":{"assigned":"ch-40","gain":1,"id":"vca-0","member_count":1,"members":[{"channel":"ch-40","gain_for_member":1}],"missing_count":0,"missing_tracks":[],"muted":false,"name":"Shell","phase_locked":true,"soloed":false,"track_count":0,"tracks":[],"volume":1}}
-> {"id":66,"cmd":"vca.assign","args":{"group":"vca-0","channel":"ch-41"},"proto":1}
<- {"id":66,"ok":true,"result":{"assigned":"ch-41","gain":1,"id":"vca-0","member_count":2,"members":[{"channel":"ch-40","gain_for_member":1},{"channel":"ch-41","gain_for_member":1}],"missing_count":0,"missing_tracks":[],"muted":false,"name":"Shell","phase_locked":true,"soloed":false,"track_count":0,"tracks":[],"volume":1}}
-> {"id":67,"cmd":"window.toggle","args":{"editor":"mixer"},"proto":1}
<- {"id":67,"ok":true,"result":{"editor":"mixer","panel":"mixer","view":"arrange","visible":true}}
-> {"id":68,"cmd":"window.screenshot","args":{"path":"/tmp/zctl-run-5h2i8wc4/mixer.png"},"proto":1}
<- {"id":68,"ok":true,"result":{"height":1000,"path":"/tmp/zctl-run-5h2i8wc4/mixer.png","width":1600}}
-> {"id":69,"cmd":"window.detach_all","args":{},"proto":1}
<- {"id":69,"ok":true,"result":{"detached":true}}
-> {"id":70,"cmd":"window.attach_all","args":{},"proto":1}
<- {"id":70,"ok":true,"result":{"detached":true}}
-> {"id":71,"cmd":"window.settings","args":{},"proto":1}
<- {"id":71,"ok":true,"result":{"shown":true}}
-> {"id":72,"cmd":"app.about","args":{},"proto":1}
<- {"id":72,"ok":true,"result":{"shown":true}}
-> {"id":73,"cmd":"window.command_palette","args":{},"proto":1}
<- {"id":73,"ok":true,"result":{"entries":902,"shown":true}}
-> {"id":74,"cmd":"window.screenshot","args":{"path":"/tmp/zctl-run-5h2i8wc4/active.png","window":"active"},"proto":1}
<- {"id":74,"ok":true,"result":{"height":460,"path":"/tmp/zctl-run-5h2i8wc4/active.png","width":720}}
-> {"id":75,"cmd":"window.fullscreen","args":{"enabled":true},"proto":1}
<- {"id":75,"ok":true,"result":{"changed":true,"fullscreen":true}}
-> {"id":76,"cmd":"window.fullscreen","args":{"enabled":false},"proto":1}
<- {"id":76,"ok":true,"result":{"changed":true,"fullscreen":false}}
-> {"id":77,"cmd":"control.quit","args":{},"proto":1}
<- {"id":77,"ok":true,"result":{"project_modified":true,"quitting":true,"save_requested":false,"unsaved_changes":"discarded"}}
FAIL: socket-side success paths
  - bounce undo unmutes a rebuilt source and removes the landed track (track='trk-29' landed='trk-33')
  - a second bounce undo restores the source's original mute (track='trk-29')
  - bounce undo unmutes a rebuilt source and removes the landed track (track='trk-29' landed='trk-33')
  - a second bounce undo restores the source's original mute (track='trk-29')


0% tests passed, 1 tests failed out of 1

Total Test time (real) =   8.76 sec

The following tests FAILED:
	357 - ControlSuccessPaths (Failed)
Errors while running CTest
```
