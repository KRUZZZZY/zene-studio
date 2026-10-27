# Link audio stream (`link.audio_*`) - PCM between Zene instances

**This is audio streaming. It is not Link transport sync.** Tempo, beat phase and
start/stop sharing are `link.*` (docs/LINK-SYNC.md) and are not touched by anything
here: the stream shares no state with `LinkSyncEngine`, works with Link on or off,
and Link works the same with the stream on or off.

**It is not Ableton's LinkAudio either.** The vendored Link 4.1 tree has no
LinkAudio (`src/3rdparty/ableton-link/README.lmms`: "Not taken: LinkAudio"), and
this lane did not vendor it (no network access to read its licence and API at the
pinned commit; unverified, so not adopted). What exists is a small stream of our own,
so it only connects Zene instances, never Live or Bitwig.

| Part | Where |
|---|---|
| Stream, ring, codec | `include/LinkAudioStream.h`, `src/core/LinkAudioStream.cpp`, `src/core/LinkAudioCodec.cpp` (codec, ring, parser) |
| Engine tap (after the master gain) | `AudioEngine::renderStageMix()` -> `LinkAudioStream::tap` |
| Surface | `link.audio_get_state`, `link.audio_set_enabled` (`src/core/ControlCommandsLinkAudio.cpp`) |
| Menu | Sync > Link Audio Stream / Link Audio Peer... / Link Audio Status |
| Proof | `ctest -R '^LinkAudioStreamTest$'` (from `<build>/tests`) |

## What it does

* **Send:** the master mix of this instance (post master gain) goes to one peer.
* **Receive:** the peer's stream is *added* to this instance's master mix, after the
  send tap, so a pair of instances sending to each other never feeds back.
* Format: float32, stereo, little endian, at most 128 frames per UDP datagram
  (1040 bytes, one LAN frame). A 16-byte header carries a sequence number and the
  sample rate; a packet at another rate than the receiving engine's is dropped and
  counted (`rate_mismatches`) - there is no resampling.
* A non-finite sample (NaN or an infinity) in a received packet is decoded as silence,
  so a peer cannot poison the master mix.

## Threads and the realtime rule

* **Audio thread** (`process()`): copies the master mix into a preallocated TX ring and
  reads the RX ring into the master. It touches only two preallocated rings, a
  preallocated scratch buffer and atomics: no allocation, no lock, no I/O. A disabled
  stream costs one acquire load per period. `LinkAudioStreamTest::audioThreadAllocatesNothing`
  counts `operator new` on the calling thread (0 over 200 periods).
* **I/O thread** (one `std::thread`): polls the socket every 2 ms, reads datagrams into
  the RX ring and sends the TX ring's frames.
* Each ring has exactly one producer and one consumer (single-producer
  single-consumer, acquire/release counters). TSan does not run on this box
  (runtime crash at thread creation), so the race freedom is by that design, not measured.

## The jitter buffer, and what "bounded" means

* The RX side starts playing once `latency_ms` (default 20, 5..200) is buffered.
* An empty buffer is an **underrun**: that period outputs nothing from the peer, and playback
  re-primes. `underruns` counts those events, not silent periods.
* The two machines' audio clocks differ, so the buffer drifts. When it grows past
  `2 x target + one period` it is cut back to the target (`trims`). Latency is therefore
  bounded at that high-water mark, and the cost of drift is an occasional trim or
  underrun, not growing delay.
* A sequence gap (lost packet) is replaced with silence, up to 8 packets, so timing holds
  (`lost_packets`). Late or duplicate packets are dropped (`reordered_packets`).
* The stress test (`realtimeStressReportsBoundedUnderrunsAndLatency`, two 1 ms-cadence
  threads, 3000 periods, loopback) asserts: samples in order, underruns <= 1% of periods,
  buffered latency under the high-water mark, no ring overruns, 80% of the stream heard.

## Network limits (LAN-first)

* **No discovery.** Link's discovery gives a peer *count*, never addresses, so the peer is
  named by the user: `peer` is an **IPv4 literal** (no DNS, ever).
* **Default bind is `127.0.0.1`.** Reaching another machine needs an explicit
  `bind_address` (e.g. the LAN address or `0.0.0.0`) and the peer's address; the peer
  must reach your `listen_port` (default 20810) over UDP.
* **LAN only.** No NAT traversal, no encryption, no authentication: anyone who can send UDP
  to the port can inject audio (datagrams from an address other than `peer` are ignored
  when a peer is set). Plan for wired or good Wi-Fi: Wi-Fi jitter shows up as underruns;
  raise `latency_ms`.
* One peer per direction. Sockets are POSIX on Linux and macOS and winsock2 on Windows
  (`WSAStartup`, non-blocking `ioctlsocket`, `select`, link `ws2_32`). **Unverified on Windows:** the
  Windows path was written and reviewed, not built or run here; the POSIX path is what the tests ran.
* Starting the stream resets its counters. The send/receive path is not recorded in saved
  projects (session state, like `link.set_enabled`).

## Commands

```
link.audio_set_enabled {enabled, peer?, peer_port?, send?, receive?, bind_address?, listen_port?, latency_ms?}
link.audio_get_state {}
```

Arguments not given keep their value. `send` defaults to "a peer is set"; `receive` to true.
`listen_port: 0` lets the OS choose (reported as `bound_port`). `set_enabled` is one undo
step (the previous configuration is restored; reversibility class true_inverse);
`get_state` is read-only.

## Testing with Link active, without touching the LAN

Link's own discovery multicasts on every interface. To run Link beside the stream in a test with no
packet leaving the host, `AbletonLinkSession::setLoopbackOnly(true)` (or `ZENE_LINK_LOOPBACK_ONLY=1`)
confines Link's interface scan to `lo`: the vendored scanner (`ableton/platforms/posix/ScanIpIfAddrs.hpp`)
calls `getifaddrs`, and `include/LinkVendored.h` routes that one call to `lmms::linkseam::scan`, one
non-inline function defined in `src/core/AbletonLinkSession.cpp`. Every translation unit that includes the
vendored Link does so through `LinkVendored.h` (the session wrapper and `AbletonLinkInteropTest`), so all
of them see the same tokens - no ODR split - and the vendored headers stay byte-identical.
`LinkAudioStreamTest::pcmStreamsWhileLinkSyncIsActive` uses it and, on Linux, reads `/proc/net/igmp` to show
the group is joined on `lo` alone (skipped with a message elsewhere). The seam is POSIX only; on Windows the
test skips.
