/*
 * RenderManager - exporting logic common between the CLI and GUI.
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

#include <QDir>
#include <QRegularExpression>

#include <algorithm>

#include "RenderManager.h"

#include "Engine.h"
#include "PatternStore.h"
#include "Song.h"


namespace lmms
{

RenderManager::RenderManager(
	const OutputSettings& outputSettings, ProjectRenderer::ExportFileFormat fmt, QString outputPath)
	: m_outputSettings(outputSettings)
	, m_format(fmt)
	, m_outputPath(outputPath)
{
	Engine::audioEngine()->storeAudioDevice();
}

RenderManager::~RenderManager()
{
	Engine::audioEngine()->restoreAudioDevice();  // Also deletes audio dev.
}

void RenderManager::abortProcessing()
{
	if ( m_activeRenderer ) {
		disconnect( m_activeRenderer.get(), SIGNAL(finished()),
				this, SLOT(renderNextTrack()));
		m_activeRenderer->abortProcessing();
	}
	restoreMutedState();
	endStemExport();
}

// Called to render each new track when rendering tracks individually.
void RenderManager::renderNextTrack()
{
	m_activeRenderer.reset();

	if (m_tracksToRender.empty())
	{
		// nothing left to render
		restoreMutedState();
		endStemExport();
		emit finished();
	}
	else
	{
		// pop the next track from our rendering queue
		Track* renderTrack = m_tracksToRender.back();
		m_tracksToRender.pop_back();

		// mute everything but the track we are about to render
		for (auto track : m_unmuted)
		{
			track->setMuted(track != renderTrack);
		}

		QString path;
		if (m_exportingStems)
		{
			// Stems are queued in track order (see exportStems), so the counter
			// gives each file the number of the track it came from.
			path = pathForStem(renderTrack, ++m_stemsRendered, m_stemTotal);
		}
		else
		{
			// for multi-render, prefix each output file with a different number
			int trackNum = m_tracksToRender.size() + 1;
			path = pathForTrack(renderTrack, trackNum);
		}

		render( path );
	}
}

// Collect every unmuted Instrument/Sample track, from the song editor and from
// the beat/bassline containers, into the rendering queue. Used by both the
// legacy per-track export and the stem export.
void RenderManager::collectTracksToRender()
{
	m_unmuted.clear();

	const TrackContainer::TrackList* containers[] = {
		&Engine::getSong()->tracks(),
		&Engine::patternStore()->tracks()
	};

	for (const auto* tl : containers)
	{
		for (const auto& tk : *tl)
		{
			Track::Type type = tk->type();

			// Don't render automation tracks
			if ( tk->isMuted() == false &&
					( type == Track::Type::Instrument || type == Track::Type::Sample ) )
			{
				m_unmuted.push_back(tk);
			}
		}
	}
}

// Render the song into individual tracks
void RenderManager::renderTracks()
{
	collectTracksToRender();

	// copy the list of unmuted tracks into our rendering queue.
	// we need to remember which tracks were unmuted to restore state at the end.
	m_tracksToRender = m_unmuted;

	renderNextTrack();
}

// Render each unmuted track into its own file as a stem.
void RenderManager::exportStems(const StemExportOptions& options)
{
	m_stemOptions = options;
	collectTracksToRender();

	if (m_unmuted.empty())
	{
		// Nothing to export: behave like an empty render queue rather than
		// leaving the Song's render settings modified.
		emit finished();
		return;
	}

	// Nothing is muted yet, so this is the whole-project length -- the length the
	// mix renders to. Freeze it now: the per-stem mute pass below would otherwise
	// shrink `updateLength()` to the single track being rendered.
	Engine::getSong()->updateLength();
	m_stemLengthBars = Engine::getSong()->length();

	m_exportingStems = true;
	m_stemTotal = static_cast<int>(m_unmuted.size());
	m_stemsRendered = 0;

	// renderNextTrack() pops from the back, so reverse to render (and number) the
	// stems in track order.
	m_tracksToRender.assign(m_unmuted.rbegin(), m_unmuted.rend());

	auto* song = Engine::getSong();
	if (m_stemOptions.alignToProjectLength && m_stemLengthBars > 0)
	{
		song->setExportLengthOverrideBars(m_stemLengthBars);
	}
	song->setExportTailBars(std::max(m_stemOptions.tailBars, 0));

	renderNextTrack();
}

// Put the Song's render-length settings back to the whole-project defaults.
void RenderManager::endStemExport()
{
	if (!m_exportingStems) { return; }

	m_exportingStems = false;

	auto* song = Engine::getSong();
	song->setExportLengthOverrideBars(0);
	song->setExportTailBars(1);
}


// Render the song into a single track
void RenderManager::renderProject()
{
	render( m_outputPath );
}

void RenderManager::render(QString outputPath)
{
	m_activeRenderer = std::make_unique<ProjectRenderer>(m_outputSettings, m_format, outputPath);

	if( m_activeRenderer->isReady() )
	{
		// pass progress signals through
		connect( m_activeRenderer.get(), SIGNAL(progressChanged(int)),
				this, SIGNAL(progressChanged(int)));

		// when it is finished, render the next track.
		// if we have not queued any tracks, renderNextTrack will just clean up
		connect( m_activeRenderer.get(), SIGNAL(finished()),
				this, SLOT(renderNextTrack()));

		// Keep the loudness report alive past the renderer: the renderer is
		// destroyed on finish(), and the report has to outlive it to be shown
		// (the slot runs in this object's thread, i.e. after run() returned).
		connect( m_activeRenderer.get(), &ProjectRenderer::loudnessReportReady, this,
			[this](const QString& report)
			{
				m_loudnessReportText = report;
				m_loudnessReportPath = m_activeRenderer->loudnessReportPath();
				m_loudnessReportError = m_activeRenderer->loudnessReportError();
				emit loudnessReportReady( report );
			} );

		m_activeRenderer->startProcessing();
	}
	else
	{
		qDebug( "Renderer failed to acquire a file device!" );
		renderNextTrack();
	}
}

// Unmute all tracks that were muted while rendering tracks
void RenderManager::restoreMutedState()
{
	while (!m_unmuted.empty())
	{
		Track* restoreTrack = m_unmuted.back();
		m_unmuted.pop_back();
		restoreTrack->setMuted( false );
	}
}

// Determine the output path for a track when rendering tracks individually
QString RenderManager::pathForTrack(const Track *track, int num)
{
	QString extension = ProjectRenderer::getFileExtensionFromFormat( m_format );
	QString name = track->name();
	name = name.remove(QRegularExpression(FILENAME_FILTER));
	name = QString( "%1_%2%3" ).arg( num ).arg( name ).arg( extension );
	return QDir(m_outputPath).filePath(name);
}

// Stem file name: `<index>_<track name><extension>`, index zero-padded so that a
// directory listing sorts in track order. The index is what keeps two tracks that
// share a name (or an empty name) from overwriting each other.
QString RenderManager::stemFileName(const QString& trackName, int index, int total,
		const QString& extension)
{
	// Track::FILENAME_FILTER is a PCRE-incompatible pattern, so
	// QRegularExpression rejects it and QString::replace() with it is a silent
	// no-op (see docs/STEM-EXPORT.md, "Defects found"). Filter the characters
	// explicitly instead: it is also cheaper than a regex per stem.
	static const QString illegal = QStringLiteral("\"*/:<>?\\|");
	QString name;
	name.reserve(trackName.size());
	for (const QChar c : trackName)
	{
		if (c.unicode() < 0x20 || c.unicode() == 0x7f || illegal.contains(c)) { continue; }
		name.append(c);
	}
	name = name.trimmed();
	if (name.isEmpty()) { name = QStringLiteral("track"); }

	// At least two digits, wider when the project has more than 99 tracks.
	const int digits = static_cast<int>(QString::number(std::max(total, 1)).size());
	const int width = std::max(2, digits);

	return QStringLiteral("%1_%2%3")
		.arg(index, width, 10, QLatin1Char('0'))
		.arg(name)
		.arg(extension);
}

// Determine the output path for a stem
QString RenderManager::pathForStem(const Track *track, int index, int total) const
{
	return QDir(m_outputPath).filePath(stemFileName(track->name(), index, total,
			ProjectRenderer::getFileExtensionFromFormat(m_format)));
}


void RenderManager::updateConsoleProgress()
{
	if ( m_activeRenderer )
	{
		m_activeRenderer->updateConsoleProgress();

		int totalNum = m_unmuted.size();
		if ( totalNum > 0 )
		{
			// we are rendering multiple tracks, append a track counter to the output
			int trackNum = totalNum - m_tracksToRender.size();
			fprintf( stderr, "(%d/%d)", trackNum, totalNum );
		}
	}
}


} // namespace lmms