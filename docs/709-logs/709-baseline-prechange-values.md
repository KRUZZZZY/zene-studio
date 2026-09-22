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
