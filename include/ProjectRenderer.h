/*
 * ProjectRenderer.h - ProjectRenderer class for easily rendering projects
 *
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_PROJECT_RENDERER_H
#define LMMS_PROJECT_RENDERER_H

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "AudioFileDevice.h"
#include "AudioEngine.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"

#include "lmms_export.h"

#ifdef LMMS_TESTING
class SessionSchedulerRenderTest;
#endif

namespace lmms
{

class LoudnessReport;


class LMMS_EXPORT ProjectRenderer : public QThread
{
	Q_OBJECT
public:
	enum class ExportFileFormat : int
	{
		Wave,
		Flac,
		Ogg,
		MP3,
		Count
	} ;
	constexpr static auto NumFileFormats = static_cast<std::size_t>(ExportFileFormat::Count);

	struct FileEncodeDevice
	{
		bool isAvailable() const { return m_getDevInst != nullptr; }

		ExportFileFormat m_fileFormat;
		const char * m_description;
		const char * m_extension;
		AudioFileDeviceInstantiaton m_getDevInst;
	} ;

	ProjectRenderer(const OutputSettings& _os, ExportFileFormat _file_format, const QString& _out_file);
	//! Out of line: the unique_ptr<LoudnessReport> member needs the complete type
	//! when it is destroyed, and moc would instantiate the deleter from here.
	~ProjectRenderer() override;

	//! Projects rendered in this process across every instance: run() increments
	//! it once per render it starts. Instrumentation only - it changes nothing
	//! about what a render produces - and it is what lets a caller prove that N
	//! mastered outputs cost one project render instead of asserting it.
	static int renderCount();
	//! Puts the counter back to zero, so a caller can bracket one measurement.
	static void resetRenderCount();

	bool isReady() const
	{
		return m_fileDev != nullptr;
	}

	static ExportFileFormat getFileFormatFromExtension(
							const QString & _ext );

	static QString getFileExtensionFromFormat( ExportFileFormat fmt );

	static const std::array<FileEncodeDevice, 5> fileEncodeDevices;

	/**
	 * The loudness report of this render, when the OutputSettings asked for one
	 * (OutputSettings::loudnessReport()); null otherwise. Measure-only: the
	 * report taps the rendered blocks, it never alters them, so the file is
	 * byte-identical whether or not a report was requested.
	 */
	const LoudnessReport* loudnessReport() const { return m_loudnessReport.get(); }
	//! Where the report was written (sidecarPathFor(outputFile)); empty when none.
	QString loudnessReportPath() const { return m_loudnessReportPath; }
	//! Why the report could not be written, empty when it was (or none was asked).
	QString loudnessReportError() const { return m_loudnessReportError; }

	//! 0.5.0 F7: why these settings cannot be rendered (include/SurroundExport.h), empty when they can.
	//! A refused render has no file device (isReady() is false) and opens no file.
	QString refusal() const { return m_refusal; }
	//! The channels the file is written with: 2, or the surround master's for a surround export.
	int channels() const { return m_channels; }

	/*! True when this render's output could not be written completely - a write failed, or the
	 *  finalisation (the close, the header) did. Read after finished(); written on the render thread
	 *  from run(). A failed render also deletes its file, so a caller sees both by one question. */
	bool hasWriteError() const { return m_writeFailed.load(std::memory_order_relaxed); }

public slots:
	void startProcessing();
	void abortProcessing();

	void updateConsoleProgress();


signals:
	void progressChanged( int );

	//! The finished report, as text: emitted from the render thread once the
	//! render is complete and the sidecar has been written (never on abort).
	void loudnessReportReady( const QString& report );


private:
#ifdef LMMS_TESTING
	friend class ::SessionSchedulerRenderTest;
#endif

	void run() override;

	//! Writes the sidecar, tells the console and emits loudnessReportReady().
	void reportLoudness( const QString& renderedFile );
	//! 0.5.0 F7: the period's stereo pair and the export tap's side channels, interleaved, to the file.
	//! False when that write failed, so run() can stop producing bytes the file cannot take.
	bool writeSurround( const SampleFrame* pair, f_cnt_t frames );

	AudioFileDevice * m_fileDev;

	std::unique_ptr<LoudnessReport> m_loudnessReport;
	QString m_loudnessReportPath;
	QString m_loudnessReportError;

	std::atomic<int> m_progress;
	std::atomic<bool> m_abort;  //!< set by abortProcessing() on another thread; volatile is not a synchronisation
	//! Set in run() when a write or the finalisation failed; read via hasWriteError() after finished().
	std::atomic<bool> m_writeFailed{false};

	/*! The selection that was in force before this renderer published its own
	 *  (see ProjectRenderer.cpp): restored by ~ProjectRenderer(), so a render's
	 *  dither/SRC choice never leaks into the next one - including when the
	 *  render aborts or the file device could not be created. Declared after
	 *  m_progress/m_abort so the initialiser order in the constructor matches
	 *  the declaration order (-Werror=reorder).
	 */
	DitherMode m_previousDither;
	SrcQuality m_previousSrcQuality;

	//! 0.5.0 F7 (MX-060): the surround export. Default-initialised, so the initialiser list is unchanged.
	QString m_refusal;
	int m_channels = DEFAULT_CHANNELS;
	std::vector<float> m_interleaved;  //!< one period, m_channels wide; sized before the render starts

} ;


} // namespace lmms

#endif // LMMS_PROJECT_RENDERER_H
