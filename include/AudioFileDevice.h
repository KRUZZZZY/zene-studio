/*
 * AudioFileDevice.h - base-class for audio-device-classes which write
 *                     their output into a file
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_FILE_DEVICE_H
#define LMMS_AUDIO_FILE_DEVICE_H

#include <QFile>

#include "AudioDevice.h"
#include "OutputSettings.h"

namespace lmms
{

class AudioFileDevice : public AudioDevice
{
public:
	AudioFileDevice(OutputSettings const & outputSettings,
			const ch_cnt_t _channels, const QString & _file,
			AudioEngine* audioEngine );
	~AudioFileDevice() override;

	QString outputFile() const
	{
		return m_outputFile.fileName();
	}

	OutputSettings const & getOutputSettings() const { return m_outputSettings; }

	//! Write `size` sample frames from `buf` into the output file. Returns true on success.
	virtual bool writeBuffer(const SampleFrame* buf, const f_cnt_t frames) = 0;

	/*! 0.5.0 F7 (MX-060): write @a frames frames of @a samples, interleaved, channels() wide - the
	 *  surround export, whose master is wider than a SampleFrame. The samples may be altered in place
	 *  (the dither). False when this encoder cannot carry more than two channels (only WAV can). */
	virtual bool writeInterleaved(float* samples, const f_cnt_t frames)
	{
		(void)samples;
		(void)frames;
		return false;
	}

	/*! Finalise the encoder and report whether the output file was written completely: false when
	 *  a write or the close itself failed. A render calls this once when its loop ends (before
	 *  anything reads the result); the encoder's destructor calls it again, which must then be a
	 *  no-op. The base implementation finalises nothing and reports success. */
	virtual bool finishEncoding() { return true; }

	//! True when this file could not be written completely (a write or the close failed). False for
	//! encoders that do not report one, so a caller reads one question for every format.
	virtual bool hasWriteError() const { return m_writeError; }

protected:
	//! Record that a write (or a finalisation) failed; the export is then not a finished one.
	void recordWriteError() { m_writeError = true; }

	int writeData( const void* data, int len );

	/*! Pushes whatever writeData() buffered in the QFile down to the operating system and reports
	 *  whether that worked. writeData() answers with the bytes QFile ACCEPTED, and QFile buffers, so
	 *  a disk that is full can refuse the bytes only at the flush: without this call a short write
	 *  would look like a complete one. Every encoder's finishEncoding() ends with it. */
	bool flushOutputFile()
	{
		if (!m_outputFile.isOpen()) { return true; }
		if (m_outputFile.flush()) { return true; }
		recordWriteError();
		return false;
	}

	/*! What a file that could not be written completely leaves behind: nothing.
	 *  The handle is closed first, so the removal also works where a file that is still open
	 *  cannot be deleted. */
	void removeOutputFile()
	{
		if (m_outputFile.isOpen()) { m_outputFile.close(); }
		QFile::remove(m_outputFile.fileName());
	}
	//! A derived writer that has finished its samples may close the file before patching a header field.
	void closeOutputFile() { m_outputFile.close(); }

	inline bool outputFileOpened() const
	{
		return m_outputFile.isOpen();
	}

	inline int outputFileHandle() const
	{
		return m_outputFile.handle();
	}

	//! True once a write or a finalisation of this file failed (see recordWriteError()).
	bool m_writeError = false;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}

	QFile m_outputFile;
	OutputSettings m_outputSettings;
} ;

using AudioFileDeviceInstantiaton
	= AudioFileDevice* (*)(const QString&, const OutputSettings&, const ch_cnt_t, AudioEngine*, bool&);

} // namespace lmms

#endif // LMMS_AUDIO_FILE_DEVICE_H
