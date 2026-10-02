/*
 * McuSurface.h - a Mackie Control surface driving the song's tracks and transport
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
 *
 */

#ifndef LMMS_MCU_SURFACE_H
#define LMMS_MCU_SURFACE_H

#include <array>
#include <atomic>
#include <memory>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "McuProtocol.h"
#include "MidiEventProcessor.h"
#include "lmms_export.h"

namespace lmms
{

class MidiPort;
class Track;

/*! WF-126: one Mackie Control surface (McuProtocol) bound to the song. Its eight strips are a
 *  bank of the song's instrument and sample tracks: a fader sets the track's volume, a V-pot its
 *  pan, mute / solo / rec / select run track.set_mute / track.set_solo / track.set_arm /
 *  window.select through the registry, and the transport buttons run transport.*. What the
 *  surface does is therefore what a click or an agent does - recorded and undone the same way -
 *  except the faders and pots, which move their models directly as a MIDI controller does.
 *
 *  THREADS. Events arrive on the MIDI client's thread; each is decoded there (no allocation
 *  beyond the queued call) and applied on the UI thread, where the registry lives. Feedback
 *  (motor faders, LEDs) is sent from a UI-thread timer when a strip's state changes, and never
 *  for a fader the user is touching. */
class LMMS_EXPORT McuSurface : public QObject, public MidiEventProcessor
{
	Q_OBJECT
public:
	McuSurface();
	~McuSurface() override;

	//! The one surface of this instance, or nullptr when none is connected.
	static McuSurface* instance();
	//! Connect a surface on the MIDI ports named @a input / @a output (a client's port names;
	//! empty = unsubscribed). Replaces a connected one.
	static McuSurface* connect(const QString& input, const QString& output);
	static void disconnect();

	//! The first strip's index among the bankable tracks.
	int bank() const { return m_bank; }
	void setBank(int first);
	//! The track strip @a strip (0..7) drives, or nullptr past the song's tracks.
	Track* stripTrack(int strip) const;

	//! What mcu_state reports: ports, bank, the strips' tracks and the event counts.
	QJsonObject state() const;

	//! Apply one decoded input (the UI thread's half; public so a test can drive it).
	void apply(const mcu::Input& input);

	void processInEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;
	void processOutEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;

private:
	void sendFeedback();
	void sendStrip(int strip);
	void sendLed(mcu::Kind kind, int strip, bool lit);
	void send(const MidiEvent& event);
	bool applyMix(const mcu::Input& input);
	bool applyBank(mcu::Kind kind);
	void applyStripButton(const mcu::Input& input, Track* track);
	void applyTransport(mcu::Kind kind);

	std::unique_ptr<MidiPort> m_port;
	QString m_input;
	QString m_output;
	int m_bank = 0;
	std::array<bool, mcu::MasterStrip + 1> m_touched{};
	std::array<int, mcu::MasterStrip + 1> m_sentFader{};
	QSet<QString> m_armed;     //!< tracks armed from the surface (track.set_arm has no getter)
	QHash<int, bool> m_sentLeds;  //!< note -> lit, the LEDs last sent
	QTimer m_feedback;
	std::atomic<int> m_eventsIn{0};  //!< counted on the MIDI thread
	int m_eventsOut = 0;
};

} // namespace lmms

#endif // LMMS_MCU_SURFACE_H
