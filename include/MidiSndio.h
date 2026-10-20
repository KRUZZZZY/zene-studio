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
#include <memory>

#include <QThread>

#include "MidiClient.h"


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


protected:
	void sendByte(const unsigned char c) override;
	void run() override;

private:
	void writeQueuedBytes();

	mio_hdl *m_hdl;
	std::atomic<bool> m_quit;
	//! Output queue between sendByte() and m_writer: [0] is read by the writer,
	//! [1] is written by sendByte() without blocking.
	int m_outPipe[2];
	std::unique_ptr<QThread> m_writer;
} ;


} // namespace lmms

#endif // LMMS_HAVE_SNDIO

#endif // LMMS_MIDI_SNDIO_H
