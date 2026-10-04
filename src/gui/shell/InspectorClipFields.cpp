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
 *  note.velocity_offset), so the Inspector edits exactly as an agent does.
 *  Theme 7: every position and length reads in the time format (bars|beats by default, ticks or time by
 *  choice - a right-click on the field, MusicalTime.h) and is typed in any of them. A note's start counts
 *  from the clip's start, as the note editor's ruler does. */

#include <algorithm>
#include <functional>

#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "InspectorPanel.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "NoteGrid.h"
#include "NoteTransformActions.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

bool invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args).ok;
}

//! A typed length that starts at @a at, in any time format: "2" (bars, as shown), "1.2.0", "1.5 bars", "96 ticks";
//! false when it is not a positive length. The text shown is exact, so Enter on it unchanged is the same length.
bool positiveLength(const QString& typed, int at, int* ticks)
{
	return musicaltime::lengthFromText(typed, ticks, at) && *ticks > 0;
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
//! Where @a clip starts (gone: 0) - a length, and a note's time, are measured from there.
int clipStart(const Clip* clip) { return clip != nullptr ? clip->startPosition().getTicks() : 0; }

//! The arguments that run a command on the notes selected in @a clip - the editor's selection, handed to the command
//! surface first, since that is the one a 'selection' scope reads.
QJsonObject selectionOf(const MidiClip* clip, const QString& id)
{
	handOverNoteSelection(clip);
	return {{QStringLiteral("clip"), id}, {QStringLiteral("scope"), QStringLiteral("selection")}};
}

//! The typed key for the selection: transposed so the first selected note lands on it.
bool setSelectedKey(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int wanted = 0;
	const Note* note = firstSelected(clip);
	if (note == nullptr || !keyFromText(typed, &wanted)) { return false; }
	QJsonObject args = selectionOf(clip, id);
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
	QJsonObject args = selectionOf(clip, id);
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

//! The typed start (a position from the clip's start): the selection moved so the first selected note starts there.
bool setSelectedStart(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	const Note* first = firstSelected(clip);
	if (first == nullptr || !musicaltime::positionFromText(typed, &ticks, clipStart(clip.data()))) { return false; }
	const int delta = ticks - first->pos().getTicks();
	return editSelected(clip, id, QStringLiteral("position"), [delta](const Note* n) { return std::max(0, n->pos().getTicks() + delta); });
}

//! The typed length (any time format), for every selected note.
bool setSelectedLength(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	const Note* first = firstSelected(clip);
	if (first == nullptr || !positiveLength(typed, clipStart(clip.data()) + first->pos().getTicks(), &ticks)) { return false; }
	return editSelected(clip, id, QStringLiteral("length"), [ticks](const Note*) { return ticks; });
}

//! The typed length (any time format) for the clip: clip.resize.
bool resizeClip(const QPointer<Clip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	return clip && positiveLength(typed, clipStart(clip.data()), &ticks)
		&& invoke(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), ticks}});
}

//! The typed offset (a signed length): clip.slip - the content shifts inside the clip, which does not move.
bool slipClip(const QPointer<Clip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	return clip && musicaltime::lengthFromText(typed, &ticks, clipStart(clip.data()))
		&& invoke(QStringLiteral("clip.slip"), {{QStringLiteral("clip"), id}, {QStringLiteral("offset"), ticks}});
}

//! A field that shows a musical position or length: right-click picks the time format, and a new one shows at once.
void timeField(ReadoutField* field)
{
	addTimeFormatChoices(field);
	QObject::connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, field, &ReadoutField::refresh);
}

} // namespace

void InspectorPanel::addClipFields(QGridLayout* grid, Clip* clip, const QString& id, QWidget* parent)
{
	const QPointer<Clip> guarded(clip);
	auto* start = new ReadoutField(tr("Start"), parent);
	start->bindText([guarded] { return guarded ? musicaltime::positionText(guarded->startPosition().getTicks()) : QString(); },
		[id](const QString& typed) {
			int ticks = 0;
			return musicaltime::positionFromText(typed, &ticks) && invoke(QStringLiteral("clip.move"), {{QStringLiteral("clip"), id}, {QStringLiteral("position"), ticks}});
		});
	auto* length = new ReadoutField(tr("Length"), parent);
	length->bindText([guarded] { return guarded ? musicaltime::lengthText(guarded->length().getTicks(), clipStart(guarded)) : QString(); },
		[guarded, id](const QString& typed) { return resizeClip(guarded, id, typed); });
	auto* offset = new ReadoutField(tr("Offset"), parent);
	offset->bindText([guarded] { return guarded ? musicaltime::lengthText(guarded->startTimeOffset().getTicks(), clipStart(guarded)) : QString(); },
		[guarded, id](const QString& typed) { return slipClip(guarded, id, typed); });
	for (ReadoutField* field : {start, length, offset})
	{
		grid->addWidget(field, grid->rowCount(), 0, 1, 2);  // a readout field carries its own caption
		timeField(field);
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
	// A note's start and length in the time format: a time is measured from where the clip starts.
	const auto timed = [guarded](bool start) {
		return [guarded, start] {
			const Note* note = firstSelected(guarded);
			if (note == nullptr) { return tr("no notes selected"); }
			const int origin = clipStart(guarded.data());
			return start ? musicaltime::positionText(note->pos().getTicks(), origin)
				: musicaltime::lengthText(note->length().getTicks(), origin + note->pos().getTicks());
		};
	};
	auto* key = new ReadoutField(tr("Key"), box);
	key->bindText(shown(&keyText), [guarded, id](const QString& typed) { return setSelectedKey(guarded, id, typed); });
	auto* velocity = new ReadoutField(tr("Velocity"), box);
	velocity->bindText(shown(&velocityText), [guarded, id](const QString& typed) { return setSelectedVelocity(guarded, id, typed); });
	auto* start = new ReadoutField(tr("Start"), box);
	start->bindText(timed(true), [guarded, id](const QString& typed) { return setSelectedStart(guarded, id, typed); });
	auto* length = new ReadoutField(tr("Length"), box);
	length->bindText(timed(false), [guarded, id](const QString& typed) { return setSelectedLength(guarded, id, typed); });
	timeField(start);
	timeField(length);
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
