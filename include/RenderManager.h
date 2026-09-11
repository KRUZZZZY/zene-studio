/*
 * RenderManager.h - Provides a uniform interface for rendering the project or
 *                   individual tracks for the CLI and GUI.
 *
 * Copyright (c) 2015 Ryan Roden-Corrent <ryan/at/rcorre.net>
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

#ifndef LMMS_RENDER_MANAGER_H
#define LMMS_RENDER_MANAGER_H

#include <memory>

#include "ProjectRenderer.h"
#include "OutputSettings.h"


namespace lmms
{


class RenderManager : public QObject
{
	Q_OBJECT
public:
	RenderManager(const OutputSettings& outputSettings, ProjectRenderer::ExportFileFormat fmt, QString outputPath);

	~RenderManager() override;

	/// Export all unmuted tracks into a single file
	void renderProject();

	/// Export all unmuted tracks into individual file
	void renderTracks();

	void abortProcessing();

	/// The loudness report of the last finished render, as text; empty when the
	/// render asked for none (OutputSettings::loudnessReport()). Same content as
	/// the ".loudness.txt" sidecar written beside the render.
	QString loudnessReportText() const { return m_loudnessReportText; }
	/// Path of the sidecar the report was written to; empty when there is none.
	QString loudnessReportPath() const { return m_loudnessReportPath; }
	/// Why the report could not be written; empty when it was written, or when
	/// none was requested.
	QString loudnessReportError() const { return m_loudnessReportError; }

signals:
	void progressChanged( int );
	void finished();
	/// Forwarded from the active renderer: the report, as text.
	void loudnessReportReady( const QString& report );

private slots:
	void renderNextTrack();
	void updateConsoleProgress();

private:
	QString pathForTrack( const Track *track, int num );
	void restoreMutedState();

	void render( QString outputPath );

	const OutputSettings m_outputSettings;
	ProjectRenderer::ExportFileFormat m_format;
	QString m_outputPath;

	QString m_loudnessReportText;
	QString m_loudnessReportPath;
	QString m_loudnessReportError;

	std::unique_ptr<ProjectRenderer> m_activeRenderer;

	std::vector<Track*> m_tracksToRender;
	std::vector<Track*> m_unmuted;
} ;


} // namespace lmms

#endif // LMMS_RENDER_MANAGER_H
