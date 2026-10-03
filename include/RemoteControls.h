/*
 * RemoteControls.h - eight MIDI controls driving the selected device's parameters, page by page
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
#ifndef LMMS_REMOTE_CONTROLS_H
#define LMMS_REMOTE_CONTROLS_H

#include <QJsonObject>
#include <QObject>

#include "MidiSurfaceInput.h"
#include "lmms_export.h"

namespace lmms
{

//! One knob turn, as the MIDI thread hands it over.
struct RemoteTurn
{
	int knob = 0;
	int value = 0;
};

/*! 0.5.0 L10 (MX-106): remote-control pages. A controller's eight knobs (CCs 'first_cc' ..
 *  'first_cc' + 7 on one MIDI channel, or any channel) drive the parameters of one device, eight at a
 *  time: page 0 is parameters 1-8, page 1 is 9-16, ... The device is set (remote.target) or, with
 *  'follow' on, follows the selection - the shell names the selected track's instrument or channel's
 *  first device. A knob moves its parameter's model directly, as any MIDI controller does (the MCU
 *  surface's faders are the precedent); when the page or the device changes, the values are sent
 *  back so motorised or LED-ring knobs show them. Session state, not project state.
 *
 *  THREADS: MidiSurfaceInput - decoded on the MIDI client's thread, applied on the UI thread. */
class LMMS_EXPORT RemoteControls : public QObject, public MidiSurfaceInput<RemoteTurn>
{
	Q_OBJECT
public:
	static constexpr int Knobs = 8;

	RemoteControls();
	~RemoteControls() override;

	static RemoteControls* instance();
	static RemoteControls* connect(const QString& input, const QString& output, int channel, int firstCc);
	static void disconnect();

	//! The device the knobs drive: a track or channel ('target') and its device ('plugin', fx-<n> or inst).
	void setTarget(const QString& target, const QString& plugin);
	//! What the selection names, taken only while following.
	void followSelection(const QString& target, const QString& plugin);
	void setFollow(bool follow) { m_follow = follow; }
	void setPage(int page);
	QJsonObject state() const;

	//! Apply knob @a knob (0..7) at @a value (0..127) - what a received CC does, for the UI thread.
	bool applyKnob(int knob, int value);

protected:
	bool decode(const MidiEvent& event, RemoteTurn* out) override;
	void apply(const RemoteTurn& turn) override { applyKnob(turn.knob, turn.value); }

private:
	void sendPage();
	int pageCount() const;

	int m_channel = -1;  //!< -1: any
	int m_firstCc = 16;
	QString m_target;
	QString m_plugin;
	bool m_follow = true;
	int m_page = 0;
};

} // namespace lmms

#endif // LMMS_REMOTE_CONTROLS_H
