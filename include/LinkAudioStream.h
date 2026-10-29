/*
 * LinkAudioStream.h - a small PCM stream between two Zene instances
 *                     (Q050-024, docs/LINK-AUDIO.md).
 *
 * It is NOT Ableton's LinkAudio and it is NOT Link transport sync. The vendored
 * Link 4.1 tree has no LinkAudio (src/3rdparty/ableton-link/README.lmms), so this
 * is a clearly scoped stream of our own: float32 stereo in UDP datagrams between
 * two addresses the user names. It shares no state with LinkSyncEngine - tempo,
 * beat phase and start/stop keep working exactly as before, with the stream on or
 * off, and the stream works with Link on or off.
 *
 * Threads, and who owns what (no lock anywhere, no thread blocks another):
 *   audio thread  process(): pushes the master mix into the TX ring, pulls the
 *                 peer's audio from the RX ring and adds it to the master. It only
 *                 touches the two preallocated rings, the preallocated scratch
 *                 buffer and atomics: no allocation, no lock, no I/O.
 *   I/O thread    one std::thread that polls the socket (2 ms), reads datagrams
 *                 into the RX ring (sole producer) and sends the TX ring's frames
 *                 as datagrams (sole consumer).
 *   UI thread     configure() / report(): starts and joins the I/O thread.
 * Each ring therefore has exactly one producer and one consumer (SPSC).
 *
 * The receive side is a jitter buffer: playback starts once the target latency is
 * buffered, an empty ring is an UNDERRUN (silence is output, playback re-primes),
 * and a ring grown past the high-water mark (the peers' clocks differ) is trimmed
 * back to the target - so latency stays bounded without resampling.
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_LINK_AUDIO_STREAM_H
#define LMMS_LINK_AUDIO_STREAM_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleFrame;

namespace link
{

constexpr int AudioChannels = 2;
//! Frames per datagram: 16 + 128 * 2 * 4 = 1040 bytes, under a 1500-byte LAN MTU.
constexpr std::size_t AudioMaxPacketFrames = 128;
constexpr std::size_t AudioHeaderBytes = 16;
constexpr std::size_t AudioMaxPacketBytes = AudioHeaderBytes + AudioMaxPacketFrames * AudioChannels * 4;
//! Each ring's capacity, in frames (a power of two): about 1.4 s at 48 kHz.
constexpr std::size_t AudioRingFrames = 65536;
constexpr int DefaultAudioPort = 20810;
constexpr int MinAudioLatencyMs = 5;
constexpr int MaxAudioLatencyMs = 200;
constexpr int DefaultAudioLatencyMs = 20;
constexpr const char* DefaultAudioBindAddress = "127.0.0.1";

//! The datagram header (16 bytes, little endian): magic "ZLA1", sequence, rate, frames, channels, 0.
struct AudioPacketHeader
{
	std::uint32_t sequence = 0;
	std::uint32_t sampleRate = 0;
	std::uint16_t frames = 0;
};

//! Encode \a header + \a frames interleaved stereo floats into \a out; returns the byte count, 0 if it does not fit.
LMMS_EXPORT std::size_t encodeAudioPacket(const AudioPacketHeader& header, const float* interleaved,
	std::uint8_t* out, std::size_t capacity);
//! Decode a datagram; false for anything that is not a well-formed packet of ours (wrong magic/size/channels).
//! A non-finite sample (NaN, infinity) in an otherwise valid packet is decoded as 0.f: it never reaches the mix.
LMMS_EXPORT bool decodeAudioPacket(const std::uint8_t* data, std::size_t size, AudioPacketHeader& header,
	float* interleavedOut, std::size_t outCapacityFloats);
//! Dotted-quad IPv4 only (no name lookup, ever): true and \a address in host order on success.
LMMS_EXPORT bool parseIpv4(const std::string& text, std::uint32_t& address);

/*! A single-producer single-consumer ring of interleaved stereo frames. All storage
 *  is allocated by the constructor; write/read/skip never allocate or lock. */
class LMMS_EXPORT AudioRing
{
public:
	explicit AudioRing(std::size_t capacityFrames);
	AudioRing(const AudioRing&) = delete;
	AudioRing& operator=(const AudioRing&) = delete;

	std::size_t capacity() const { return m_capacity; }
	//! Frames readable now (either side may ask).
	std::size_t fill() const;
	//! Producer: write up to \a frames (all or what fits); returns the frames written.
	std::size_t write(const float* interleaved, std::size_t frames);
	//! Producer: write \a frames of silence (what fits).
	std::size_t writeSilence(std::size_t frames);
	//! Consumer: read up to \a frames; returns the frames read.
	std::size_t read(float* interleaved, std::size_t frames);
	//! Consumer: drop up to \a frames; returns the frames dropped.
	std::size_t skip(std::size_t frames);

private:
	const std::size_t m_capacity;
	std::vector<float> m_data;
	alignas(64) std::atomic<std::size_t> m_head{0};  //!< frames ever written (producer)
	alignas(64) std::atomic<std::size_t> m_tail{0};  //!< frames ever read (consumer)
};

//! What the user configures (link.audio_set_enabled).
struct AudioStreamConfig
{
	bool enabled = false;
	bool send = false;        //!< stream this instance's master mix to the peer
	bool receive = true;      //!< add the peer's stream to this instance's master mix
	std::string bindAddress = DefaultAudioBindAddress;  //!< IPv4 literal; loopback unless the user opts into a LAN address
	int listenPort = DefaultAudioPort;                   //!< 0 = the OS picks (reported back)
	std::string peerAddress;  //!< IPv4 literal of the peer; empty = none
	int peerPort = DefaultAudioPort;
	int latencyMs = DefaultAudioLatencyMs;               //!< jitter-buffer target
};

//! A consistent-enough snapshot of the counters (each one atomic; the set is not a transaction).
struct AudioStreamReport
{
	bool running = false;
	std::string error;        //!< why it is not running / the last start failure
	std::string boundAddress;
	int boundPort = 0;
	std::uint64_t packetsSent = 0;
	std::uint64_t packetsReceived = 0;
	std::uint64_t framesSent = 0;
	std::uint64_t framesReceived = 0;
	std::uint64_t underruns = 0;       //!< periods the RX ring could not fill (silence output)
	std::uint64_t trims = 0;           //!< times the RX ring was cut back to the target (clock drift / burst)
	std::uint64_t txOverruns = 0;      //!< periods the TX ring had no room for (I/O thread behind)
	std::uint64_t rxOverruns = 0;      //!< frames dropped because the RX ring was full
	std::uint64_t lostPackets = 0;     //!< sequence gaps (replaced with silence, up to 8 packets)
	std::uint64_t reorderedPackets = 0;//!< late or duplicate packets dropped
	std::uint64_t malformedPackets = 0;
	std::uint64_t rateMismatches = 0;  //!< packets at another sample rate than this engine's
	std::uint64_t sendErrors = 0;
	std::uint64_t periods = 0;         //!< audio periods processed
	double latencyMs = 0.0;            //!< RX ring fill after the last period
	double latencyMaxMs = 0.0;
	bool primed = false;
	std::uint32_t sampleRate = 0;
};

/*! A test seam for the stream's I/O thread: it can be held at two points, so that a test
 *  can pin the window between the validation of a packet and its publication, and keep the
 *  frames of the TX ring queued across a sample rate change. Both holds are off in
 *  production: the I/O thread only reads a relaxed flag when they are, and it never takes
 *  the seam's lock then. */
class LMMS_EXPORT LinkAudioStreamSeam
{
public:
	//! Hold the I/O thread before it sends the frames of the TX ring.
	static void setSendHold(bool on);
	//! Hold the I/O thread after it validated a packet, before it publishes it.
	static void setPublishHold(bool on);
	//! How many times the I/O thread waited at the sending hold.
	static std::uint64_t sendHolds();
	//! How many times the I/O thread waited at the publishing hold.
	static std::uint64_t publishHolds();

	//! The I/O thread waits here while the sending hold is on.
	static void waitAtSendHold();
	//! The I/O thread waits here while the publishing hold is on.
	static void waitAtPublishHold();

private:
	static void waitAt(std::atomic<bool>& hold, std::atomic<std::uint64_t>& counter);
	static std::atomic<bool> s_sendHold;
	static std::atomic<bool> s_publishHold;
	static std::atomic<std::uint64_t> s_sendHolds;
	static std::atomic<std::uint64_t> s_publishHolds;
	static std::mutex s_mutex;
	static std::condition_variable s_cond;
};

class LMMS_EXPORT LinkAudioStream
{
public:
	LinkAudioStream();
	~LinkAudioStream();
	LinkAudioStream(const LinkAudioStream&) = delete;
	LinkAudioStream& operator=(const LinkAudioStream&) = delete;

	//! The process-wide stream the engine's master mix is tapped into.
	static LinkAudioStream* instance();

	/*! Validate \a requested and apply it (stop, then start when enabled). UI thread only.
	 *  On failure nothing changes and \a error says why. */
	bool configure(const AudioStreamConfig& requested, std::string* error);
	AudioStreamConfig config() const;
	AudioStreamReport report() const;

	/*! Audio thread: tap the master mix (send) and add the peer's audio (receive).
	 *  Realtime-safe: no allocation, no lock, no I/O. */
	void process(SampleFrame* frames, f_cnt_t count, sample_rate_t sampleRate) noexcept;

	//! The engine's hook: one acquire load, and only then process(). Realtime-safe.
	static void tap(SampleFrame* frames, f_cnt_t count, sample_rate_t sampleRate) noexcept
	{
		if (LinkAudioStream* active = s_active.load(std::memory_order_acquire))
		{
			active->process(frames, count, sampleRate);
		}
	}

private:
	friend struct LinkAudioStreamTestAccess;
	struct JitterBounds
	{
		std::size_t minimum;
		std::size_t maximum;
	};

	void stopLocked();
	void resetCounters();
	void publish(const AudioStreamConfig& cfg);
	static JitterBounds jitterBounds(std::size_t period, std::size_t capacity) noexcept;
	void receivePeriod(SampleFrame* frames, std::size_t period, sample_rate_t sampleRate) noexcept;
	void addFromRing(SampleFrame* frames, std::size_t period) noexcept;
	void sendFrames(SampleFrame* frames, f_cnt_t count) noexcept;
	bool openSocket(const AudioStreamConfig& cfg, std::string* error);
	void ioLoop();
	void receiveDatagrams();
	void sendPending();
	void acceptPacket(const AudioPacketHeader& header, const float* samples);
	bool acceptSequence(const AudioPacketHeader& header) noexcept;
	void insertGapSilence(std::size_t frames) noexcept;
	void onRateChanged(std::uint32_t rate) noexcept;
	std::uint32_t txRateForNextFrames() noexcept;
	void acknowledgeRxRate() noexcept;

	static std::atomic<LinkAudioStream*> s_active;   //!< the instance() while it runs
	static LinkAudioStream* s_engineTapped;          //!< set by instance(), UI thread
	std::atomic<bool> m_running{false};

	AudioStreamConfig m_config;      //!< UI thread only
	std::string m_error;             //!< UI thread only
	bool m_restoring = false;        //!< UI thread only: configure() is putting the previous stream back
	std::string m_boundAddress;
	int m_boundPort = 0;

	AudioRing m_tx;                  //!< producer: audio thread; consumer: I/O thread
	AudioRing m_rx;                  //!< producer: I/O thread; consumer: audio thread
	std::vector<float> m_scratch;    //!< audio thread's read buffer (preallocated)
	std::vector<float> m_txBuf;      //!< audio thread's interleaved copy of the master's frames (preallocated)
	std::vector<std::uint8_t> m_packet;   //!< I/O thread buffers (preallocated)
	std::vector<float> m_samples;

	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::uintptr_t m_socket = ~std::uintptr_t{0};   //!< the datagram socket (a SOCKET on Windows; an fd on POSIX), all-ones = none
	std::uint32_t m_peerAddress = 0;     //!< host order, 0 = none
	int m_peerPort = 0;

	// audio-thread private state
	std::uint64_t m_seenEpoch = 0;
	bool m_primed = false;
	std::uint64_t m_txRateEpoch = 0;         //!< bumped when the engine's sample rate changes
	std::uint64_t m_rxRateEpoch = 0;         //!< bumped when the engine's sample rate changes
	std::uint64_t m_rxDrainedEpoch = 0;      //!< the RX epoch whose frames were dropped after the drain
	// shared control (UI writes before s_active is published)
	std::atomic<bool> m_sendOn{false};
	std::atomic<bool> m_receiveOn{false};
	std::atomic<int> m_latencyMs{DefaultAudioLatencyMs};
	std::atomic<std::uint64_t> m_epoch{0};
	std::atomic<std::uint32_t> m_rate{0};
	//! (epoch << 32) | rate: the rate the queued TX frames carry, and the epoch they belong to.
	//! Both travel together, so a packet is never stamped with a rate that is not its own.
	std::atomic<std::uint64_t> m_txStamp{0};
	//! The TX epoch the I/O thread has already flushed (the audio thread waits for it).
	std::atomic<std::uint64_t> m_txFlushedEpoch{0};
	//! (epoch << 32) | rate: the rate the frames in the RX ring carry, and the epoch they
	//! belong to. The I/O thread validates a packet against this pair, so the rate it checks
	//! and the epoch it publishes always belong together.
	std::atomic<std::uint64_t> m_rxStamp{0};
	//! The RX epoch the I/O thread has drained (the audio thread waits for it): the frames it
	//! published before that belong to the old rate and must not be played at the new one.
	std::atomic<std::uint64_t> m_rxFlushedEpoch{0};
	// I/O-thread private
	std::uint32_t m_nextSequence = 0;    //!< next sequence to send
	bool m_haveRx = false;
	std::uint32_t m_expectedRx = 0;
	std::uint64_t m_seenTxEpoch = 0;     //!< the TX epoch the I/O thread has already flushed
	std::uint64_t m_seenRxEpoch = 0;     //!< the RX epoch the I/O thread has already drained

	// counters
	std::atomic<std::uint64_t> m_packetsSent{0}, m_packetsReceived{0}, m_framesSent{0}, m_framesReceived{0};
	std::atomic<std::uint64_t> m_underruns{0}, m_trims{0}, m_txOverruns{0}, m_rxOverruns{0};
	std::atomic<std::uint64_t> m_lost{0}, m_reordered{0}, m_malformed{0}, m_rateMismatch{0}, m_sendErrors{0};
	std::atomic<std::uint64_t> m_periods{0};
	std::atomic<std::size_t> m_latencyFrames{0}, m_latencyMaxFrames{0};
	std::atomic<bool> m_primedReport{false};
};

} // namespace link
} // namespace lmms

#endif // LMMS_LINK_AUDIO_STREAM_H
