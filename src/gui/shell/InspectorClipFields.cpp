/*
 * InspectorClipFields.cpp - the Inspector's numeric clip and note fields
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

/*! ED-015 / ED-016 (0.5.0 L04): the clip's Start, Length and Offset, and the selected notes' Key,
 *  Start, Length and Velocity, as fields you can type into or drag - each a registered command
 *  (clip.move / clip.resize / clip.slip; note.transpose / note.move / note.resize /
 *  note.velocity_offset), so the Inspector edits exactly as an agent does. */

#include <algorithm>
#include <cmath>
#include <functional>

#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InspectorPanel.h"
#include "MidiClip.h"
#include "NoteGrid.h"
#include "Song.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

bool invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args).ok;
}

QString barsText(int ticks)
{
	const double bars = static_cast<double>(ticks) / std::max(1, Engine::getSong()->ticksPerBar());
	return QObject::tr("%1 bars").arg(bars, 0, 'g', 4);
}

//! "2", "1.5 bars" -> ticks; false when it is not a positive number of bars.
bool ticksFromBars(const QString& typed, int* ticks)
{
	bool ok = false;
	const double bars = QString(typed).remove(QObject::tr("bars")).trimmed().toDouble(&ok);
	*ticks = static_cast<int>(std::lround(bars * Engine::getSong()->ticksPerBar()));
	return ok && *ticks > 0;
}

//! "C#4" or a key number -> the engine's key; false when it is neither.
bool keyFromText(const QString& typed, int* key)
{
	bool ok = false;
	*key = typed.trimmed().toInt(&ok);
	for (int k = 0; !ok && k < NumKeys; ++k)
	{
		if (NoteGrid::keyName(k).compare(typed.trimmed(), Qt::CaseInsensitive) == 0) { *key = k; ok = true; }
	}
	return ok && *key >= 0 && *key < NumKeys;
}

//! The selected notes of @a clip (the note editor's selection), in clip order.
QVector<Note*> selectedNotes(const MidiClip* clip)
{
	QVector<Note*> out;
	for (Note* note : clip->notes())
	{
		if (note->selected()) { out.append(note); }
	}
	return out;
}

//! The first selected note of @a clip (gone or none selected: nullptr).
const Note* firstSelected(const QPointer<MidiClip>& clip)
{
	const QVector<Note*> notes = clip ? selectedNotes(clip) : QVector<Note*>();
	return notes.isEmpty() ? nullptr : notes.first();
}

QString keyText(const Note* note) { return NoteGrid::keyName(note->key()); }
// The engine's note volume scale, 0..200 - the one note.velocity_* and the note editor use.
QString velocityText(const Note* note) { return QString::number(note->getVolume()); }
QString startText(const Note* note) { return barBeatText(note->pos().getTicks()); }
QString lengthText(const Note* note) { return InspectorPanel::tr("%1 ticks").arg(note->length().getTicks()); }

QJsonObject selectionOf(const QString& id)
{
	return {{QStringLiteral("clip"), id}, {QStringLiteral("scope"), QStringLiteral("selection")}};
}

//! The typed key for the selection: transposed so the first selected note lands on it.
bool setSelectedKey(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int wanted = 0;
	const Note* note = firstSelected(clip);
	if (note == nullptr || !keyFromText(typed, &wanted)) { return false; }
	QJsonObject args = selectionOf(id);
	args.insert(QStringLiteral("semitones"), wanted - note->key());
	return invoke(QStringLiteral("note.transpose"), args);
}

//! The typed velocity: the selection offset so the first selected note has it.
bool setSelectedVelocity(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	bool ok = false;
	const int wanted = typed.trimmed().toInt(&ok);
	const Note* note = firstSelected(clip);
	if (note == nullptr || !ok) { return false; }
	QJsonObject args = selectionOf(id);
	args.insert(QStringLiteral("delta"), std::clamp(wanted, 0, 200) - static_cast<int>(note->getVolume()));
	return invoke(QStringLiteral("note.velocity_offset"), args);
}

//! One note.edit (one undo step) setting @a field on every selected note to what @a valueOf gives it.
bool editSelected(const QPointer<MidiClip>& clip, const QString& id, const QString& field, const std::function<int(const Note*)>& valueOf)
{
	if (clip.isNull()) { return false; }
	QJsonArray edits;
	for (const Note* note : selectedNotes(clip))
	{
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {field, valueOf(note)}});
	}
	return !edits.isEmpty() && invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), id}, {QStringLiteral("edits"), edits}});
}

//! The typed start (bar.beat): the selection moved so the first selected note starts there.
bool setSelectedStart(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	const Note* first = firstSelected(clip);
	if (first == nullptr || !ticksFromBarBeat(typed, &ticks)) { return false; }
	const int delta = ticks - first->pos().getTicks();
	return editSelected(clip, id, QStringLiteral("position"), [delta](const Note* n) { return std::max(0, n->pos().getTicks() + delta); });
}

//! The typed length in ticks, for every selected note.
bool setSelectedLength(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	bool ok = false;
	const int ticks = QString(typed).remove(InspectorPanel::tr("ticks")).trimmed().toInt(&ok);
	if (!ok || ticks <= 0) { return false; }
	return editSelected(clip, id, QStringLiteral("length"), [ticks](const Note*) { return ticks; });
}

} // namespace

void InspectorPanel::addClipFields(QGridLayout* grid, Clip* clip, const QString& id, QWidget* parent)
{
	const QPointer<Clip> guarded(clip);
	auto* start = new ReadoutField(tr("Start"), parent);
	start->bindText([guarded] { return guarded ? barBeatText(guarded->startPosition().getTicks()) : QString(); },
		[id](const QString& typed) {
			int ticks = 0;
			return ticksFromBarBeat(typed, &ticks) && invoke(QStringLiteral("clip.move"), {{QStringLiteral("clip"), id}, {QStringLiteral("position"), ticks}});
		});
	auto* length = new ReadoutField(tr("Length"), parent);
	length->bindText([guarded] { return guarded ? barsText(guarded->length().getTicks()) : QString(); },
		[id](const QString& typed) {
			int ticks = 0;
			return ticksFromBars(typed, &ticks) && invoke(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), ticks}});
		});
	auto* offset = new ReadoutField(tr("Offset"), parent);
	offset->bindText([guarded] { return guarded ? tr("%1 ticks").arg(guarded->startTimeOffset().getTicks()) : QString(); },
		[id](const QString& typed) {
			bool ok = false;
			const int ticks = QString(typed).remove(tr("ticks")).trimmed().toInt(&ok);
			return ok && invoke(QStringLiteral("clip.slip"), {{QStringLiteral("clip"), id}, {QStringLiteral("offset"), ticks}});
		});
	for (auto [label, field] : {std::pair{tr("Start"), start}, std::pair{tr("Length"), length}, std::pair{tr("Offset"), offset}})
	{
		addRow(grid, label, field);
		m_liveFields.append(field);
	}
}

QWidget* InspectorPanel::notesSection(MidiClip* clip, const QString& id, QWidget* parent)
{
	auto* box = new QWidget(parent);
	auto* grid = new QGridLayout(box);
	grid->setContentsMargins(0, 0, 0, 0);
	const QPointer<MidiClip> guarded(clip);
	const auto shown = [guarded](QString (*text)(const Note*)) {
		return [guarded, text] { const Note* note = firstSelected(guarded); return note != nullptr ? text(note) : tr("no notes selected"); };
	};
	auto* key = new ReadoutField(tr("Key"), box);
	key->bindText(shown(&keyText), [guarded, id](const QString& typed) { return setSelectedKey(guarded, id, typed); });
	auto* velocity = new ReadoutField(tr("Velocity"), box);
	velocity->bindText(shown(&velocityText), [guarded, id](const QString& typed) { return setSelectedVelocity(guarded, id, typed); });
	auto* start = new ReadoutField(tr("Start"), box);
	start->bindText(shown(&startText), [guarded, id](const QString& typed) { return setSelectedStart(guarded, id, typed); });
	auto* length = new ReadoutField(tr("Length"), box);
	length->bindText(shown(&lengthText), [guarded, id](const QString& typed) { return setSelectedLength(guarded, id, typed); });
	grid->addWidget(new QLabel(tr("Selected notes"), box), 0, 0, 1, 2);
	int row = 1;
	for (ReadoutField* field : {key, start, length, velocity})
	{
		grid->addWidget(field, row++, 0, 1, 2);  // a readout field carries its own caption
		m_liveFields.append(field);
	}
	box->setObjectName(QStringLiteral("inspectorNotes"));
	return box;
}

} // namespace lmms::gui
