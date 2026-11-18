/*
 * LinkSync.h - the Link-style tempo/phase sync model: a session peer
 *              abstraction and the engine-side state a peer agrees on
 *              (D11 "Ableton Link sync").
 *
 * TWO BACKENDS behind one facade (docs/LINK-SYNC.md sections 1-3):
 *
 *  - "ableton-link" (the default wherever the build carries the vendored
 *    library, WANT_ABLETON_LINK): REAL Ableton Link 4.1 (src/3rdparty/ableton-link,
 *    GPL-2.0-or-later). Zene joins the same session as Ableton Live, Bitwig and
 *    every other Link application: tempo, beat phase and start/stop. The UI half
 *    is include/AbletonLinkBridge.h, the audio half include/LinkAudioFollower.h.
 *  - "zene-link-style" (the fallback: a build without the library, or
 *    ZENE_LINK_BACKEND=zene-link-style in the environment): this project's own
 *    model with Link's semantics - every instance announces a tempo, a beat
 *    phase on a shared timeline and a quantum over UDP multicast, and a peer
 *    adopts the newest announcement. Only other Zene instances speak it; it is
 *    what Zene 0.3/0.4 builds sync with. Everything below the facade's public
 *    methods (the packet, the peer table, the revision order) is that model.
 *
 * Real-time rule (AGENTS.md rule 4). The facade, both UI-side backends and the
 * zene-link-style transport run on the UI thread only. The one audio-thread
 * path is LinkAudioFollower (Ableton backend), which documents and measures
 * its own realtime safety.
 *
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

#ifndef LMMS_LINK_SYNC_H
#define LMMS_LINK_SYNC_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include "lmms_export.h"

class QTimer;

namespace lmms
{

class AbletonLinkBridge;
class LinkPeerTransport;

namespace link
{

//! The model name this build puts on the wire and reports in link.get_state.
//! A future transport that speaks the real Link protocol reports its own, so a
//! reader can always tell which one answered.
constexpr const char* SyncModelName = "zene-link-style";
//! Protocol version of the packet below. A peer that speaks another version is
//! ignored rather than misread.
constexpr int ProtoVersion = 1;

//! The engine's bar is DefaultTicksPerBar ticks and this model treats one bar
//! as four beats, which is what the tempo means in this engine: at 120 BPM a
//! 192-tick bar lasts 4 * 60 / 120 = 2 s, and that is the conversion Song
//! already performs. Phase is therefore measured in beats at this rate.
constexpr int BeatsPerBar = 4;

//! Quantums the model accepts, in beats. 4 is one 4/4 bar and the default.
constexpr int DefaultQuantum = 4;
constexpr int MinQuantum = 1;
constexpr int MaxQuantum = 64;

//! BPM bounds on what a PEER may announce. Deliberately named apart from the
//! engine's own `lmms::MinTempo`/`MaxTempo` (include/Song.h), which bound what
//! the Song's tempo model accepts: a session tempo is a double on the wire and
//! this is the range the model believes before it is applied to that model.
constexpr double MinSessionTempo = 20.0;
constexpr double MaxSessionTempo = 999.0;

/*! The peer table is FIXED-size: the model's memory does not grow with the
 *  number of instances on the network, and a full table replaces its stalest
 *  entry rather than allocating. 16 peers is far past the release's use case
 *  (two instances on one box) and bounded on purpose. */
constexpr int MaxPeers = 16;
//! A peer unheard for this long leaves the session.
constexpr int PeerTimeoutMs = 1500;
//! How often the model services itself and announces.
constexpr int DefaultPublishIntervalMs = 100;

//! The group and port Ableton Link itself uses for discovery, so a future
//! real-Link transport joins the same place rather than inventing a new one.
constexpr quint16 DefaultPort = 20808;
constexpr const char* DefaultGroup = "224.76.78.75";

//! One announcement, as it travels between instances (JSON over UDP).
struct LinkPeerPacket
{
	int proto = ProtoVersion;
	QString model;              //!< SyncModelName, or another producer's name
	QString id;                 //!< the peer's stable id for this process
	QString name;               //!< a human label (host name + instance)
	quint32 revision = 0;       //!< monotone per session-tempo declaration
	QString revisionOwner;      //!< the peer id that made that declaration
	double tempo = 0.0;         //!< the session tempo, in BPM
	double beat = 0.0;          //!< the session beat at `sentUs`
	quint64 sentUs = 0;         //!< the sender's monotone clock, microseconds
	int quantum = DefaultQuantum;//!< beats per quantum
	bool startStopSync = false; //!< whether transport start/stop is shared
	bool playing = false;       //!< the sender's transport state
};

//! One peer as this instance last heard it - what link.get_state lists.
struct LinkPeerView
{
	QString id;
	QString name;
	double tempo = 0.0;
	quint32 revision = 0;
	int quantum = DefaultQuantum;
	bool playing = false;
	qint64 ageMs = 0;           //!< since its last announcement
};

/*! The whole answer behind link.get_state: the session as THIS instance sees
 *  it, plus the engine's own phase so a caller can see the two together. */
struct LinkSessionReport
{
	bool enabled = false;
	QString model;              //!< SyncModelName
	QString peerId;
	QString instanceName;
	int peerCount = 0;
	QVector<LinkPeerView> peers;
	int quantum = DefaultQuantum;
	bool startStopSync = false;

	//! The session tempo this instance believes, and who declared it.
	double sessionTempo = 0.0;
	quint32 revision = 0;
	QString revisionOwner;
	bool tempoIsLocal = true;

	//! The session timeline: beats since the declaration that owns it, the
	//! phase inside one quantum, and the same quantities for the engine's own
	//! play head.
	double beat = 0.0;
	double phaseBeats = 0.0;
	double enginePhaseBeats = 0.0;
	//! session phase minus engine phase, wrapped to (-quantum/2, quantum/2].
	double phaseErrorBeats = 0.0;
	int engineTempo = 0;

	//! Where the announcements travel, and whether they can travel at all.
	bool transportAvailable = false;
	QString transportReason;    //!< why not, when unavailable
	QString transportEndpoint;
	/*! The RECEIPT for `transportAvailable`. Availability is a MEASUREMENT, not
	 *  a configured socket: start() sends a probe to the group and requires a
	 *  SECOND socket on this host to receive it, and these four fields are that
	 *  measurement. `transportProbeAttempted == false` means this transport
	 *  cannot answer - never that the measurement passed - so a reader can tell
	 *  "we measured and it works" from "nobody measured" (docs/LINK-SYNC.md
	 *  section 3). */
	bool transportProbeAttempted = false;
	bool transportProbeDelivered = false;
	int transportProbeElapsedMs = -1;   //!< -1 when nothing arrived inside the bound
	int transportProbeBoundMs = 0;
	int publishIntervalMs = DefaultPublishIntervalMs;
	int peerTimeoutMs = PeerTimeoutMs;
	//! How many announcements THIS instance has sent since it was enabled, and
	//! how long ago the last one from a peer arrived. Both are evidence that a
	//! session is live rather than merely configured.
	quint64 publishedCount = 0;
	qint64 lastPacketAgeMs = -1;

	//! The Ableton Link backend's own facts (all false/0 for zene-link-style).
	bool abletonLink = false;
	bool sessionPlaying = false;     //!< the session's start/stop state
	QString libraryVersion;
	QString libraryCommit;
	//! The audio-thread follower (include/LinkAudioFollower.h).
	bool followerLocked = false;
	quint64 followerPeriods = 0;
	quint64 followerHolds = 0;
	quint64 followerJumps = 0;
	double followerErrorBeats = 0.0;
};

} // namespace link

/*! The engine-side session sync (one instance per process).
 *
 *  Every method runs on the UI thread. `service()` is the periodic tick: it
 *  expires peers that went quiet, declares a tempo change made locally, adopts
 *  the newest announcement it has heard, and publishes its own state. The
 *  engine's own tempo is the authority on "did something change locally" -
 *  which is why a caller that sets the tempo with `transport.set_tempo` needs
 *  no cooperation from this class to be followed.
 */
class LMMS_EXPORT LinkSyncEngine : public QObject
{
	Q_OBJECT
public:
	static LinkSyncEngine* instance();
	static void destroy();
	~LinkSyncEngine() override;

	//! The environment variable that selects the zene-link-style fallback in a
	//! build that carries Ableton Link (value: "zene-link-style").
	static constexpr const char* BackendEnvironment = "ZENE_LINK_BACKEND";
	//! True when this process speaks real Ableton Link (see the file header).
	bool abletonBackend() const { return m_ableton != nullptr; }

	bool enabled() const { return m_enabled; }
	//! Enabling starts the transport and the service tick; disabling stops
	//! both, forgets every peer and leaves the timeline where it stands.
	void setEnabled(bool enabled);

	int quantum() const { return m_quantum; }
	//! False (and unchanged) outside [MinQuantum, MaxQuantum].
	bool setQuantum(int quantum);

	bool startStopSync() const { return m_startStopSync; }
	void setStartStopSync(bool on);

	QString peerId() const { return m_peerId; }
	QString instanceName() const { return m_name; }
	void setInstanceName(const QString& name) { m_name = name; }

	double sessionTempo() const;
	/*! Declare a session tempo: the timeline is re-anchored so the beat count
	 *  does not jump, the revision advances and every peer follows. This is
	 *  also what a local `transport.set_tempo` becomes on the next service
	 *  tick - an agent needs no new call to drive the session. */
	void setSessionTempo(double bpm);

	//! One announcement from the wire. `receivedUs` is this process's own
	//! monotone clock at the moment the datagram was read.
	void observePacket(const link::LinkPeerPacket& packet, quint64 receivedUs);
	//! The periodic tick (see the class comment). Safe to call when disabled.
	void service();
	link::LinkSessionReport report() const;
	//! The announcement this instance would publish, stamped with `sentUs`.
	link::LinkPeerPacket buildPacket(quint64 sentUs) const;

	//! Whether announcements can travel, and why not when they cannot. Both
	//! answer without an enabled session: a caller that has never enabled sync
	//! still needs to know what it would get.
	bool transportAvailable() const;
	QString transportReason() const;
	QString transportEndpoint() const;

	/*! This process's monotone clock in microseconds. ONE definition: the
	 *  transport stamps `sentUs` with it and the model compares a received
	 *  stamp against it, so the two cannot drift. On Linux and macOS this is
	 *  CLOCK_MONOTONIC and on Windows QueryPerformanceCounter, both of which
	 *  are system-wide - which is the assumption the phase arithmetic in
	 *  docs/LINK-SYNC.md section 4 states plainly. */
	static quint64 monotonicMicros();

	//! Serialise / parse one announcement. False when the payload is not a
	//! packet this model can read (a foreign model, a future proto, garbage).
	static QByteArray encodePacket(const link::LinkPeerPacket& packet);
	static bool decodePacket(const QByteArray& payload, link::LinkPeerPacket* packet);

private:
	friend struct LinkSyncEngineTestAccess;

	/*! One peer in the fixed table: its last announcement verbatim, plus when
	 *  this process read it. Keeping the packet rather than unpacking it is
	 *  what lets the retry path below re-adopt a timeline with exactly the
	 *  arithmetic the live path used - one implementation, not two. */
	struct Peer
	{
		link::LinkPeerPacket packet;
		quint64 lastSeenUs = 0;
	};

	explicit LinkSyncEngine(QObject* parent = nullptr);

	void onTick();
	//! The zene-link-style model's own report (report() overlays the Ableton backend's).
	link::LinkSessionReport modelReport() const;
	void publish(quint64 nowUs);
	void expirePeers(quint64 nowUs);
	void pollLocalTempo(quint64 nowUs);
	void followNewestRevision(quint64 nowUs);
	bool adoptionIsNewer(const link::LinkPeerPacket& packet) const;
	void adopt(const link::LinkPeerPacket& packet, quint64 receivedUs);
	void declareTempo(double bpm, quint64 nowUs);
	void handleDatagram(const QByteArray& payload, quint64 receivedUs);
	void rememberPeer(const link::LinkPeerPacket& packet, quint64 receivedUs);
	const Peer* newestPeer() const;
	//! The highest revision this instance knows of (its own, or a peer's).
	quint32 highestSeenRevision() const;
	//! Re-anchor the timeline at `nowUs` so a tempo change cannot move the beat
	//! count: called before every tempo write.
	void rebase(quint64 nowUs);
	double beatsAt(quint64 us) const;
	double enginePhaseBeats() const;
	//! The phase of \a beats inside this instance's quantum, in [0, quantum).
	//! ONE definition: report() (LinkSyncWire.cpp) and the phase tests both go
	//! through it, so "phase" cannot mean two things.
	double phaseOf(double beats) const;
	//! \a a minus \a b wrapped into (-period/2, period/2] - so a report cannot
	//! say "0.9 beats ahead" when it means "0.1 beats behind".
	static double signedPhaseDifference(double a, double b, double period);
	void applyTempoToEngine(double bpm);
	void ensureTransport();
	void dropPeers();

	//! The Ableton Link backend, or null for zene-link-style (file header).
	AbletonLinkBridge* m_ableton = nullptr;
	QVector<Peer> m_peers;          //!< bounded by link::MaxPeers
	LinkPeerTransport* m_transport = nullptr;
	QTimer* m_timer = nullptr;
	static LinkSyncEngine* s_instance;

	bool m_enabled = false;
	bool m_startStopSync = false;
	int m_quantum = link::DefaultQuantum;
	QString m_peerId;
	QString m_name;

	//! The session timeline: `beat = m_anchorBeat + (t - m_anchorUs) * bpm / 60`.
	double m_tempo = 0.0;
	double m_anchorBeat = 0.0;
	quint64 m_anchorUs = 0;
	//! The tempo this class last wrote to (or read as unchanged from) the Song.
	//! A local change is "the Song's tempo is not this" - which is what makes a
	//! peer-adopted tempo not look like a local edit.
	double m_appliedTempo = 0.0;
	quint32 m_revision = 0;
	QString m_revisionOwner;
	bool m_tempoIsLocal = true;
	quint64 m_lastPacketUs = 0;
	quint64 m_publishedCount = 0;
};

//! Create the UDP multicast transport this build ships. Never null: a platform
//! without the POSIX socket API gets a stub that reports itself unavailable and
//! says why, so the model above still answers and is still testable there.
LMMS_EXPORT LinkPeerTransport* createLinkUdpTransport(QObject* parent);

} // namespace lmms

#endif // LMMS_LINK_SYNC_H
