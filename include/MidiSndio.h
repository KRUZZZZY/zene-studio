/*
 * MidiSndio.h - base-class that implements sndio midi support
 *
 * Copyright (c) 2010-2016 jackmsr@openbsd.net
 * Copyright (c) 2016-2017 David Carlier <devnexen@gmail.com>
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

#ifndef LMMS_MIDI_SNDIO_H
#define LMMS_MIDI_SNDIO_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_SNDIO

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>

#include <QThread>

#include "MidiClient.h"
#include "MpscQueue.h"


struct mio_hdl;

namespace lmms
{


class MidiSndio : public QThread, public MidiClientRaw
{
	Q_OBJECT
public:
	MidiSndio();
	~MidiSndio() override;

	static QString probeDevice();

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP("MidiSetupWidget", "sndio MIDI");
	}

	inline static QString configSection()
	{
		return "MidiSndio";
	}

	/*! Whole output events refused because the bounded queue was full, or still
	 *  undelivered when a stalled device was abandoned. A refused event is never
	 *  partly written, so the MIDI stream cannot be cut in the middle of a message. */
	std::uint64_t outputQueueOverflowCount() const noexcept override;


protected:
	void sendByte(const unsigned char c) override;
	void processOutEvent(const MidiEvent& event, const TimePos& time, const MidiPort* port) override;
	void run() override;

private:
	struct OutputWriter;
	//! Body of m_writer. It holds its own share of the state, so it stays valid
	//! after MidiSndio is gone if teardown has to abandon a stalled write.
	static void runWriter(std::shared_ptr<OutputWriter> out);

	//! Whole outgoing events that may wait for the writer. The audio path refuses
	//! (and counts) an event when this is full instead of growing or waiting.
	static constexpr std::size_t OutputQueueCapacity = 1024;

	mio_hdl *m_hdl;
	std::atomic<bool> m_quit;
	std::shared_ptr<OutputWriter> m_output;
	std::thread m_writer;
	std::future<void> m_writerDone;
} ;


} // namespace lmms

#endif // LMMS_HAVE_SNDIO

#endif // LMMS_MIDI_SNDIO_H
