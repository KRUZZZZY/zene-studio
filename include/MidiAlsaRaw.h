/*
 * MidiAlsaRaw.h - MIDI client for RawMIDI via ALSA
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_MIDI_ALSA_RAW_H
#define LMMS_MIDI_ALSA_RAW_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_ALSA

#include <alsa/asoundlib.h>

#include <atomic>

#include <QThread>

#include "MidiClient.h"


struct pollfd;


namespace lmms
{

class MidiAlsaRaw : public QThread, public MidiClientRaw
{
	Q_OBJECT
public:
	MidiAlsaRaw();
	~MidiAlsaRaw() override;

	static QString probeDevice();


	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "MidiSetupWidget",
			"ALSA Raw-MIDI (Advanced Linux Sound Architecture)" );
	}

	inline static QString configSection()
	{
		return "MidiAlsaRaw";
	}


protected:
	void sendByte( const unsigned char c ) override;
	void run() override;

	//! Asks the worker to stop, wakes it out of its poll and joins it. Idempotent;
	//! never terminates the thread. Called by the destructor before any resource
	//! the worker uses is released (Q050-488).
	void stopWorker();


private:
	void closeDevice();
	void closeWakePipe();

	snd_rawmidi_t * m_input, * * m_inputp;
	snd_rawmidi_t * m_output, * * m_outputp;
	//! Number of ALSA descriptors in m_pfds; m_pfds holds one more, the wake pipe.
	int m_npfds;
	pollfd * m_pfds;
	//! Self-pipe: stopWorker() writes a byte so the worker's poll returns at once.
	int m_wakePipe[2];

	std::atomic_bool m_quit;

} ;


} // namespace lmms

#endif // LMMS_HAVE_ALSA

#endif // LMMS_MIDI_ALSA_RAW_H
