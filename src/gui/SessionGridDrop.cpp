/*
 * SessionGridDrop.cpp - an audio file dropped on a clip launcher cell, previewed and undoable
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

/*! 0.5.0 L02 (WF-053) and theme 14: SessionGridView's drop half. A drag over a cell is previewed - over a filled
 *  cell the cell reads "Replace <clip>" inside a dashed outline and the status line says what the drop replaces -
 *  and the drop is one session.set_slot, so one undo step. A drop that replaced a clip says what it replaced and
 *  offers Undo Replace, which runs control.undo only while nothing else has been edited since. */

#include <algorithm>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QToolButton>
#include <QUrl>

#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "SampleDecoder.h"
#include "SessionGridCells.h"
#include "SessionGridView.h"
#include "StringPairDrag.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const QString& command, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(command, args);
}

//! The audio file a drag carries: the browser's sample file, or a local file dropped from outside.
QString droppedFile(QDropEvent* event)
{
	if (StringPairDrag::decodeKey(event) == QLatin1String("samplefile")) { return StringPairDrag::decodeValue(event); }
	const QList<QUrl> urls = event->mimeData()->urls();
	return !urls.isEmpty() && urls.first().isLocalFile() ? urls.first().toLocalFile() : QString();
}

//! The existing audio file a drag carries, or empty: a text file or a folder is not a clip (review L10-L03-L02-1 #12).
//! An audio file is one the engine reads - SampleDecoder's list, which the Open Audio File dialog offers too, and the
//! compressed kinds it decodes beyond libsndfile's simple formats.
QString droppedAudio(QDropEvent* event)
{
	const QFileInfo info(droppedFile(event));
	const QString suffix = info.suffix().toLower();
	static const QStringList decoded{QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("flac"), QStringLiteral("mp3"),
		QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("opus")};
	const auto& types = SampleDecoder::supportedAudioTypes();
	const bool audio = decoded.contains(suffix) || std::any_of(types.begin(), types.end(),
		[&suffix](const SampleDecoder::AudioType& type) { return suffix == QString::fromStdString(type.extension); });
	return audio && info.isFile() ? info.filePath() : QString();
}

//! "Track 2 (Bass), scene 3": where a cell is, by its column's header as well as its number.
QString cellPlace(int track, int scene)
{
	return QObject::tr("track %1 (%2), scene %3").arg(QString::number(track + 1), sessiongrid::columnTitle(track),
		QString::number(scene + 1));
}

} // namespace


bool SessionGridView::dropOnCell(QToolButton* cell, QEvent* event)
{
	const auto at = std::find(m_cells.begin(), m_cells.end(), cell);
	if (at == m_cells.end() || m_scenes <= 0) { return false; }
	const int index = static_cast<int>(at - m_cells.begin());
	if (event->type() == QEvent::DragLeave)
	{
		previewDrop(-1, QString());
		return true;
	}
	if (event->type() == QEvent::DragEnter)
	{
		// Only a drag a cell can take is accepted, so anything else shows the no-drop cursor from the start.
		auto* enter = static_cast<QDragEnterEvent*>(event);
		const QString file = droppedAudio(enter);
		if (!file.isEmpty()) { enter->acceptProposedAction(); }
		previewDrop(file.isEmpty() ? -1 : index, file);
		return true;
	}
	if (event->type() != QEvent::Drop) { return false; }
	auto* drop = static_cast<QDropEvent*>(event);
	const QString file = droppedAudio(drop);
	previewDrop(-1, QString());
	if (file.isEmpty()) { return true; }
	drop->acceptProposedAction();  // the drag source is told the drop was taken (review L17 #11)
	finishDrop(index, file);
	return true;
}


void SessionGridView::previewDrop(int cell, const QString& file)
{
	const int before = m_dropCell;
	m_dropCell = cell;
	m_dropFile = file;
	// Each cell is painted whole, so the one the drag left gets its own text back as well as its outline.
	for (const int index : {before, cell})
	{
		if (index >= 0 && index < m_tracks * m_scenes) { paintCell(index / m_scenes, index % m_scenes); }
	}
	m_status->setText(statusText(m_tracks, m_scenes));
}


QString SessionGridView::dropPreviewText() const
{
	const int track = m_dropCell / m_scenes;
	const int scene = m_dropCell % m_scenes;
	const QString& held = m_names[static_cast<std::size_t>(m_dropCell)];
	const QString file = QFileInfo(m_dropFile).fileName();
	return held.isEmpty() ? tr("Drop to put %1 in %2.").arg(file, cellPlace(track, scene))
		: tr("Drop to replace %1 in %2 with %3 (undoable).").arg(held, cellPlace(track, scene), file);
}


void SessionGridView::paintDropPreview(QToolButton* button, int index) const
{
	const bool target = index == m_dropCell && index >= 0;
	if (!target)
	{
		// WF-064: a selected cell's dotted outline (SessionGridSelect.cpp), else no style of its own.
		const bool selected = m_scenes > 0 && isCellSelected(index / m_scenes, index % m_scenes);
		const QString wanted = selected ? QStringLiteral("QToolButton { border: 2px dotted %1; }").arg(tokens::palette().accent.name()) : QString();
		if (button->styleSheet() != wanted) { button->setStyleSheet(wanted); }
		return;
	}
	// The replacement shown before it happens: the words and a dashed outline, so not by colour alone.
	const QString& held = m_names[static_cast<std::size_t>(index)];
	const tokens::Palette& pal = tokens::palette();
	button->setText(held.isEmpty() ? QStringLiteral("+ ") + QFileInfo(m_dropFile).fileName() : tr("Replace %1").arg(held));
	button->setStyleSheet(QStringLiteral("QToolButton { border: 2px dashed %1; }")
		.arg((held.isEmpty() ? pal.accent : pal.warning).name()));
}


void SessionGridView::finishDrop(int cell, const QString& file)
{
	const int track = cell / m_scenes;
	const int scene = cell % m_scenes;
	const QString replaced = m_names[static_cast<std::size_t>(cell)];
	// The cell takes the file's name too: a replaced clip's name must not stay on the clip that replaced it.
	report(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene},
		{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), file},
		{QStringLiteral("name"), QFileInfo(file).completeBaseName()}}));
	if (!m_notice.isEmpty() || replaced.isEmpty()) { return; }  // refused (and said so), or nothing was replaced
	m_notice = tr("Replaced %1 in %2 with %3. Undo Replace or Edit > Undo puts it back.")
		.arg(replaced, cellPlace(track, scene), QFileInfo(file).fileName());
	m_undoAt = undoMark();
	m_undoReplace->show();
	m_status->setText(m_notice);
	a11y::announceStatus(m_status, m_notice);
}


QJsonObject SessionGridView::undoMark() const
{
	const QJsonObject depth = run(QStringLiteral("control.undo_depth")).result;
	return {{QStringLiteral("depth"), depth.value(QStringLiteral("depth"))}, {QStringLiteral("evicted"), depth.value(QStringLiteral("evicted"))}};
}


void SessionGridView::undoReplace()
{
	// The newest undo step is the drop's only while nothing else has been done since: else Undo would take back
	// something the button does not name.
	if (undoMark() != m_undoAt)
	{
		m_undoReplace->hide();
		m_notice = tr("Something else changed after the drop: Edit > Undo steps back through it.");
		a11y::announceStatus(m_status, m_notice, a11y::Politeness::Assertive);
		refresh();
		return;
	}
	report(run(QStringLiteral("control.undo")));
}

} // namespace lmms::gui
