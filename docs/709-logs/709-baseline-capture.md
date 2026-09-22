instance: build/zene
socket:   /tmp/zctl-run-9t5thjnx/zene.sock
BASELINE-BEGIN
{
  "fixture": {
    "bus": "ch-9",
    "effect_where": "bus",
    "loaded": [
      "fx-11"
    ],
    "return": "ch-10"
  },
  "mixer": "{\"channels\":[{\"id\":\"ch-1\",\"index\":0,\"is_bus\":false,\"is_master\":true,\"muted\":false,\"name\":\"Master\",\"pan\":null,\"sends\":[],\"soloed\":false,\"volume\":1},{\"id\":\"ch-9\",\"index\":1,\"is_bus\":true,\"is_master\":false,\"muted\":false,\"name\":\"Bus 1\",\"pan\":null,\"sends\":[{\"amount\":1,\"pre_fader\":false,\"to\":\"ch-1\"},{\"amount\":1,\"pre_fader\":false,\"to\":\"ch-10\"}],\"soloed\":false,\"volume\":1},{\"id\":\"ch-10\",\"index\":2,\"is_bus\":false,\"is_master\":false,\"muted\":false,\"name\":\"Channel 2\",\"pan\":null,\"sends\":[{\"amount\":1,\"pre_fader\":false,\"to\":\"ch-1\"}],\"soloed\":false,\"volume\":1}],\"count\":3}",
  "pdc": "{\"channel_count\":3,\"channels\":[{\"chain_latency_frames\":0,\"id\":\"ch-1\",\"index\":0,\"input_latency_frames\":0,\"is_bus\":false,\"is_master\":true,\"muted\":false,\"name\":\"Master\",\"send_count\":0,\"sends\":[],\"sidechain_receive_count\":0,\"sidechain_send_count\":0,\"sidechain_sends\":[],\"volume\":1},{\"chain_latency_frames\":0,\"id\":\"ch-9\",\"index\":1,\"input_latency_frames\":0,\"is_bus\":true,\"is_master\":false,\"muted\":false,\"name\":\"Bus 1\",\"send_count\":2,\"sends\":[{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-9\",\"pre_fader\":false,\"to\":\"ch-1\"},{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-9\",\"pre_fader\":false,\"to\":\"ch-10\"}],\"sidechain_receive_count\":0,\"sidechain_send_count\":0,\"sidechain_sends\":[],\"volume\":1},{\"chain_latency_frames\":0,\"id\":\"ch-10\",\"index\":2,\"input_latency_frames\":0,\"is_bus\":false,\"is_master\":false,\"muted\":false,\"name\":\"Channel 2\",\"send_count\":1,\"sends\":[{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-10\",\"pre_fader\":false,\"to\":\"ch-1\"}],\"sidechain_receive_count\":0,\"sidechain_send_count\":0,\"sidechain_sends\":[],\"volume\":1}],\"delay_line_capacity_frames\":16384,\"delay_line_clamped\":false,\"note\":\"total_latency_frames is the delay from a source entering the mixer to the master output (Mixer::totalLatencyFrames); input_latency_frames is the alignment point the mixer publishes per channel (Mixer::channelInputLatency). Both are recomputed once per period by Mixer::updateLatencyCompensation - no command sets them, and this command writes nothing\",\"route_count\":3,\"routes\":[{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-9\",\"pre_fader\":false,\"to\":\"ch-1\"},{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-10\",\"pre_fader\":false,\"to\":\"ch-1\"},{\"amount\":1,\"compensation_frames\":0,\"from\":\"ch-9\",\"pre_fader\":false,\"to\":\"ch-10\"}],\"sidechain\":{\"count\":0,\"note\":\"sidechain sends ARE in this engine (Mixer::createSidechainSend, src/core/Mixer.cpp); create or adjust one with mixer.sidechain_to and read its tap and compensation here\",\"routes\":[],\"supported\":true,\"tap_points\":[\"post_fader\",\"pre_fx\",\"pre_fader\",\"post_fader_no_gain\"]},\"total_latency_frames\":0,\"track_input_count\":4,\"track_inputs\":[{\"channel\":\"ch-1\",\"latency_frames\":0,\"name\":\"Default preset\"},{\"channel\":\"ch-1\",\"latency_frames\":0,\"name\":\"TripleOscillator\"},{\"channel\":\"ch-1\",\"latency_frames\":0,\"name\":\"Sample track\"},{\"channel\":\"ch-1\",\"latency_frames\":0,\"name\":\"Kicker\"}]}",
  "save": {
    "file": "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz",
    "revision_kept": true,
    "revisions": {
      "count": 1,
      "file": "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz",
      "max_revision_bytes": 8388608,
      "max_total_bytes": 25165824,
      "policy": "keep-3",
      "retained_bytes": 2571,
      "revisions": [
        {
          "bytes": 2571,
          "path": "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz.rev0",
          "revision": 0,
          "sha256": "c2a76914c196bf5be712dd4bc6a22416104e6aa27564b7aad3dd88ea2ecdc3fd"
        }
      ]
    },
    "saved": true
  },
  "save_sha256": "551b7c956c1741703717b1dc153962defdb07de0b62d5715269ade3c1b3135f7"
}
BASELINE-END

---- raw request/response transcript ----
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"","requested":"","sound_output":false,"start_failed":false,"state":"engine_missing"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":false,"pong":true,"proto":1,"reason":{"code":"engine_starting","message":"the engine is still starting up; poll control.ping until engine_ready is true before issuing engine commands"},"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":0,"cmd":"control.ping","args":{},"proto":1}
<- {"id":0,"ok":true,"result":{"audio":{"device":"Dummy (no sound output)","requested":"Dummy (no sound output)","sound_output":true,"start_failed":false,"state":"ok"},"engine_ready":true,"pong":true,"proto":1,"version":"0.3.0-alpha.52+4ef3065"}}
-> {"id":1,"cmd":"bus.create","args":{},"proto":1}
<- {"id":1,"ok":true,"result":{"channel":"ch-9","count":2,"index":1,"is_bus":true,"name":"Bus 1"}}
-> {"id":2,"cmd":"mixer.add_channel","args":{},"proto":1}
<- {"id":2,"ok":true,"result":{"channel":"ch-10","index":2}}
-> {"id":3,"cmd":"plugin.list","args":{},"proto":1}
<- {"id":3,"ok":true,"result":{"count":1,"counts_by_format":{"builtin":1},"counts_by_kind":{"effect":1},"devices":[{"display_name":"Amplifier","format":"builtin","id":"dev-0","kind":"effect","loadable":true,"name":"amplifier"}],"loadable_count":1,"total":1}}
-> {"id":4,"cmd":"plugin.load","args":{"target":"ch-9","device":"dev-0"},"proto":1}
<- {"id":4,"ok":true,"result":{"device":"dev-0","display_name":"Amplifier","id":"fx-11","index":0,"kind":"effect","plugin":"amplifier","target":"ch-9"}}
-> {"id":5,"cmd":"mixer.send_to","args":{"channel":"ch-9","to":"ch-10","amount":1.0},"proto":1}
<- {"id":5,"ok":true,"result":{"amount":1,"created":true,"from":"ch-9","pre_fader":false,"route":{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-10"},"to":"ch-10"}}
-> {"id":6,"cmd":"mixer.send_to","args":{"channel":"ch-10","to":"ch-9","amount":0.7},"proto":1}
<- {"error":{"kind":"refused","message":"routing ch-10 to ch-9 would close a feedback path (the mixer refuses it)"},"id":6,"ok":false}
-> {"id":7,"cmd":"pdc.report","args":{},"proto":1}
<- {"id":7,"ok":true,"result":{"channel_count":3,"channels":[{"chain_latency_frames":0,"id":"ch-1","index":0,"input_latency_frames":0,"is_bus":false,"is_master":true,"muted":false,"name":"Master","send_count":0,"sends":[],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1},{"chain_latency_frames":0,"id":"ch-9","index":1,"input_latency_frames":0,"is_bus":true,"is_master":false,"muted":false,"name":"Bus 1","send_count":2,"sends":[{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-10"}],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1},{"chain_latency_frames":0,"id":"ch-10","index":2,"input_latency_frames":0,"is_bus":false,"is_master":false,"muted":false,"name":"Channel 2","send_count":1,"sends":[{"amount":1,"compensation_frames":0,"from":"ch-10","pre_fader":false,"to":"ch-1"}],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1}],"delay_line_capacity_frames":16384,"delay_line_clamped":false,"note":"total_latency_frames is the delay from a source entering the mixer to the master output (Mixer::totalLatencyFrames); input_latency_frames is the alignment point the mixer publishes per channel (Mixer::channelInputLatency). Both are recomputed once per period by Mixer::updateLatencyCompensation - no command sets them, and this command writes nothing","route_count":3,"routes":[{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-10","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-10"}],"sidechain":{"count":0,"note":"sidechain sends ARE in this engine (Mixer::createSidechainSend, src/core/Mixer.cpp); create or adjust one with mixer.sidechain_to and read its tap and compensation here","routes":[],"supported":true,"tap_points":["post_fader","pre_fx","pre_fader","post_fader_no_gain"]},"total_latency_frames":0,"track_input_count":4,"track_inputs":[{"channel":"ch-1","latency_frames":0,"name":"Default preset"},{"channel":"ch-1","latency_frames":0,"name":"TripleOscillator"},{"channel":"ch-1","latency_frames":0,"name":"Sample track"},{"channel":"ch-1","latency_frames":0,"name":"Kicker"}]}}
-> {"id":8,"cmd":"mixer.get_state","args":{},"proto":1}
<- {"id":8,"ok":true,"result":{"channels":[{"id":"ch-1","index":0,"is_bus":false,"is_master":true,"muted":false,"name":"Master","pan":null,"sends":[],"soloed":false,"volume":1},{"id":"ch-9","index":1,"is_bus":true,"is_master":false,"muted":false,"name":"Bus 1","pan":null,"sends":[{"amount":1,"pre_fader":false,"to":"ch-1"},{"amount":1,"pre_fader":false,"to":"ch-10"}],"soloed":false,"volume":1},{"id":"ch-10","index":2,"is_bus":false,"is_master":false,"muted":false,"name":"Channel 2","pan":null,"sends":[{"amount":1,"pre_fader":false,"to":"ch-1"}],"soloed":false,"volume":1}],"count":3}}
-> {"id":9,"cmd":"project.save","args":{"path":"/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz"},"proto":1}
<- {"id":9,"ok":true,"result":{"file":"/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz","revision_kept":true,"revisions":{"count":1,"file":"/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz","max_revision_bytes":8388608,"max_total_bytes":25165824,"policy":"keep-3","retained_bytes":2571,"revisions":[{"bytes":2571,"path":"/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests/709-feedback-fixture.mmpz.rev0","revision":0,"sha256":"c2a76914c196bf5be712dd4bc6a22416104e6aa27564b7aad3dd88ea2ecdc3fd"}]},"saved":true}}
-> {"id":10,"cmd":"control.quit","args":{},"proto":1}
<- {"id":10,"ok":true,"result":{"project_modified":true,"quitting":true,"save_requested":false,"unsaved_changes":"discarded"}}
