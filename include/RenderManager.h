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


/// Settings for a stem export (`RenderManager::exportStems`).
struct StemExportOptions
{
	/// Bars rendered past the project end so an effect tail (reverb, delay) is not
	/// truncated. Defaults to 1 — the whole-project render's own convention
	/// (`Song::startExport()` appends one bar to every non-loop render).
	int tailBars = 1;

	/// Render every stem to the same length — the length the whole-project render
	/// would have — so the stems line up with the mix and with each other. When
	/// false each stem is trimmed to its own track's length (the legacy
	/// `renderTracks()` behaviour) and the stems cannot be summed.
	bool alignToProjectLength = true;
};


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

	/// Export each track to its own file as a stem: post-fader, post-effects, and
	/// including the track's own sends (only that track's signal reaches them), with
	/// correct tail handling and predictable per-track naming. Selection is the same
	/// as renderTracks(): every unmuted Instrument/Sample track — and because soloing
	/// a track mutes the others, soloing is how a subset is chosen.
	/// `m_outputPath` is the destination directory. Runs on the calling (GUI/CLI)
	/// thread; the audio work happens on the ProjectRenderer threads it owns.
	void exportStems(const StemExportOptions& options = {});

	/// The length, in bars, every stem is rendered to (0 until exportStems runs).
	int stemLengthBars() const { return m_stemLengthBars; }

	/// File name for stem `index` (1-based) of `total`, from a track name.
	/// `index_name.ext`, index zero-padded to at least two digits, the name
	/// stripped of characters a filesystem will not take. Static so the naming
	/// contract in docs/STEM-EXPORT.md is directly testable.
	static QString stemFileName(const QString& trackName, int index, int total,
			const QString& extension);

	void abortProcessing();

signals:
	void progressChanged( int );
	void finished();

private slots:
	void renderNextTrack();
	void updateConsoleProgress();

private:
	QString pathForTrack( const Track *track, int num );
	QString pathForStem( const Track *track, int index, int total ) const;
	void restoreMutedState();
	void collectTracksToRender();
	void endStemExport();

	void render( QString outputPath );

	const OutputSettings m_outputSettings;
	ProjectRenderer::ExportFileFormat m_format;
	QString m_outputPath;

	std::unique_ptr<ProjectRenderer> m_activeRenderer;

	std::vector<Track*> m_tracksToRender;
	std::vector<Track*> m_unmuted;

	// Stem-export state. `m_exportingStems` selects the stem path in
	// renderNextTrack(); the rest is only meaningful while it is set.
	StemExportOptions m_stemOptions;
	bool m_exportingStems = false;
	int m_stemLengthBars = 0;
	int m_stemTotal = 0;
	int m_stemsRendered = 0;
} ;


} // namespace lmms


#endif // LMMS_RENDER_MANAGER_H
