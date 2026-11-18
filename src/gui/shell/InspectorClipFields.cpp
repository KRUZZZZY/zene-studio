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
 *  from the clip's start, as the note editor's ruler does.
 *  Theme 13: a field the selected notes disagree on says "Multiple values" (a dash before it, not a colour) - never one
 *  note's value as if it were all of them; a key, velocity or length typed into it is given to every selected note
 *  (one note.edit), and a start moves the selection so its first note starts there, keeping the notes' spacing.
 *  Velocities read in % of the default (the engine's 0..200 note volume, 100 % the default). */

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>

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
#include "Song.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args);
}

ControlResult invalidEdit() { return ControlResult::failure(ControlErrorKind::InvalidArgs, QString()); }

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
// The engine's note volume scale, 0..200 - the one note.velocity_* and the note editor use - in % (100 % the default).
QString velocityText(const Note* note) { return QStringLiteral("%1%").arg(note->getVolume()); }

//! Theme 13: what a field shows when the selected notes differ - text and a dash, so it does not rest on colour.
QString mixedText() { return InspectorPanel::tr("\u2014 Multiple values"); }

//! Whether the selected notes of @a clip give different texts for one field.
bool differ(const QPointer<MidiClip>& clip, QString (*text)(const Note*))
{
	const QVector<Note*> notes = clip ? selectedNotes(clip) : QVector<Note*>();
	return std::any_of(notes.begin(), notes.end(), [&notes, text](const Note* n) { return text(n) != text(notes.first()); });
}
//! A note's start and length as exact ticks, for telling selected notes apart (differ()).
QString startKey(const Note* note) { return QString::number(note->pos().getTicks()); }
QString lengthKey(const Note* note) { return QString::number(note->length().getTicks()); }
//! Where @a clip starts (gone: 0) - a length, and a note's time, are measured from there.
int clipStart(const Clip* clip) { return clip != nullptr ? clip->startPosition().getTicks() : 0; }

qint64 noteTimeOrigin(const Clip* clip, const Note* note)
{
	return static_cast<qint64>(clipStart(clip)) + note->pos().getTicks();
}

bool tickInRange(qint64 tick)
{
	return tick >= std::numeric_limits<tick_t>::min() && tick <= std::numeric_limits<tick_t>::max();
}

//! Whether the typed length uses seconds, the only MusicalTime unit that needs its absolute start tick.
bool lengthNeedsTimeOrigin(const QString& typed)
{
	QString body = typed.trimmed();
	if (body.startsWith(QLatin1Char('-'))) { body.remove(0, 1); }
	const auto hasSuffix = [&body](const QStringList& words) {
		return std::any_of(words.begin(), words.end(), [&body](const QString& word) {
			return body.endsWith(word, Qt::CaseInsensitive);
		});
	};
	// Keep the same unit precedence as musicaltime::unitOf: tick/bar suffixes take priority over seconds.
	if (hasSuffix({QObject::tr("ticks"), QStringLiteral("ticks"), QStringLiteral("tick"), QStringLiteral("t")})
		|| hasSuffix({QObject::tr("bars"), QStringLiteral("bars"), QObject::tr("bar"), QStringLiteral("bar")}))
	{
		return false;
	}
	if (body.contains(QLatin1Char(':')) || hasSuffix({QStringLiteral("sec"), QStringLiteral("s")})) { return true; }
	if (body.count(QLatin1Char('.')) >= 2) { return false; }
	return musicaltime::currentFormat() == musicaltime::Format::Time;
}

//! The arguments that run a command on the notes selected in @a clip - the editor's selection, handed to the command
//! surface first, since that is the one a 'selection' scope reads.
QJsonObject selectionOf(const MidiClip* clip, const QString& id)
{
	handOverNoteSelection(clip);
	return {{QStringLiteral("clip"), id}, {QStringLiteral("scope"), QStringLiteral("selection")}};
}

ControlResult editSelected(const QPointer<MidiClip>& clip, const QString& id, const QString& field, const std::function<int(const Note*)>& valueOf);

//! The typed key for the selection: transposed so the first selected note lands on it - or, when the notes' keys
//! differ (the field said "Multiple values"), given to every selected note.
ControlResult setSelectedKey(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int wanted = 0;
	const Note* note = firstSelected(clip);
	if (note == nullptr || !keyFromText(typed, &wanted)) { return invalidEdit(); }
	if (differ(clip, &keyText)) { return editSelected(clip, id, QStringLiteral("key"), [wanted](const Note*) { return wanted; }); }
	QJsonObject args = selectionOf(clip, id);
	args.insert(QStringLiteral("semitones"), wanted - note->key());
	return invoke(QStringLiteral("note.transpose"), args);
}

//! The typed velocity ("80" or "80%"): the selection offset so the first selected note has it - or, when the notes'
//! velocities differ, given to every selected note.
ControlResult setSelectedVelocity(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	bool ok = false;
	const int wanted = std::clamp(QString(typed).remove(QLatin1Char('%')).trimmed().toInt(&ok), 0, 200);
	const Note* note = firstSelected(clip);
	if (note == nullptr || !ok) { return invalidEdit(); }
	if (differ(clip, &velocityText)) { return editSelected(clip, id, QStringLiteral("velocity"), [wanted](const Note*) { return wanted; }); }
	QJsonObject args = selectionOf(clip, id);
	args.insert(QStringLiteral("delta"), wanted - static_cast<int>(note->getVolume()));
	return invoke(QStringLiteral("note.velocity_offset"), args);
}

//! One note.edit (one undo step) setting @a field on every selected note to what @a valueOf gives it.
ControlResult editSelected(const QPointer<MidiClip>& clip, const QString& id, const QString& field, const std::function<int(const Note*)>& valueOf)
{
	if (clip.isNull()) { return invalidEdit(); }
	QJsonArray edits;
	for (const Note* note : selectedNotes(clip))
	{
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {field, valueOf(note)}});
	}
	return edits.isEmpty() ? invalidEdit()
		: invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), id}, {QStringLiteral("edits"), edits}});
}

//! The typed start (a position from the clip's start): the selection moved so the first selected note starts there.
ControlResult setSelectedStart(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	const Note* first = firstSelected(clip);
	if (first == nullptr || !musicaltime::positionFromText(typed, &ticks, clipStart(clip.data()))) { return invalidEdit(); }
	const qint64 delta = static_cast<qint64>(ticks) - first->pos().getTicks();
	// note.edit accepts positions only through MaxSongLength; validate wide sums before narrowing them to int.
	for (const Note* note : selectedNotes(clip))
	{
		if (static_cast<qint64>(note->pos().getTicks()) + delta > MaxSongLength) { return invalidEdit(); }
	}
	return editSelected(clip, id, QStringLiteral("position"), [delta](const Note* n) {
		return static_cast<int>(std::max<qint64>(0, static_cast<qint64>(n->pos().getTicks()) + delta));
	});
}

//! The typed length (any time format), for every selected note.
ControlResult setSelectedLength(const QPointer<MidiClip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	const Note* first = firstSelected(clip);
	if (first == nullptr) { return invalidEdit(); }
	const qint64 origin = noteTimeOrigin(clip.data(), first);
	if (!tickInRange(origin))
	{
		if (lengthNeedsTimeOrigin(typed) || !positiveLength(typed, 0, &ticks)) { return invalidEdit(); }
	}
	else if (!positiveLength(typed, static_cast<tick_t>(origin), &ticks)) { return invalidEdit(); }
	return editSelected(clip, id, QStringLiteral("length"), [ticks](const Note*) { return ticks; });
}

//! The typed length (any time format) for the clip: clip.resize.
ControlResult resizeClip(const QPointer<Clip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	return clip && positiveLength(typed, clipStart(clip.data()), &ticks)
		? invoke(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), id}, {QStringLiteral("length"), ticks}}) : invalidEdit();
}

//! The typed offset (a signed length): clip.slip - the content shifts inside the clip, which does not move.
ControlResult slipClip(const QPointer<Clip>& clip, const QString& id, const QString& typed)
{
	int ticks = 0;
	return clip && musicaltime::lengthFromText(typed, &ticks, clipStart(clip.data()))
		? invoke(QStringLiteral("clip.slip"), {{QStringLiteral("clip"), id}, {QStringLiteral("offset"), ticks}}) : invalidEdit();
}

//! A field that shows a musical position or length: right-click picks the time format, and a new one shows at once.
void timeField(ReadoutField* field)
{
	addTimeFormatChoices(field);
	QObject::connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, field, &ReadoutField::refresh);
}

QString noteTimeText(const QPointer<MidiClip>& clip, const Note* note, bool start)
{
	const int origin = clipStart(clip.data());
	if (musicaltime::currentFormat() == musicaltime::Format::Time)
	{
		const qint64 absoluteStart = noteTimeOrigin(clip.data(), note);
		if (!tickInRange(absoluteStart)) { return InspectorPanel::tr("out of range"); }
		if (start) { return musicaltime::positionText(note->pos().getTicks(), origin); }
		const qint64 absoluteEnd = absoluteStart + std::abs(static_cast<qint64>(note->length().getTicks()));
		if (!tickInRange(absoluteEnd)) { return InspectorPanel::tr("out of range"); }
		return musicaltime::lengthText(note->length().getTicks(), static_cast<tick_t>(absoluteStart));
	}
	return start ? musicaltime::positionText(note->pos().getTicks(), origin)
		: musicaltime::lengthText(note->length().getTicks());
}

} // namespace

void InspectorPanel::addClipFields(QGridLayout* grid, Clip* clip, const QString& id, QWidget* parent)
{
	const QPointer<Clip> guarded(clip);
	auto* start = new ReadoutField(tr("Start"), parent);
	start->bindTextResult([guarded] { return guarded ? musicaltime::positionText(guarded->startPosition().getTicks()) : QString(); },
		[id](const QString& typed) {
			int ticks = 0;
			return musicaltime::positionFromText(typed, &ticks)
				? invoke(QStringLiteral("clip.move"), {{QStringLiteral("clip"), id}, {QStringLiteral("position"), ticks}})
				: invalidEdit();
		});
	auto* length = new ReadoutField(tr("Length"), parent);
	length->bindTextResult([guarded] { return guarded ? musicaltime::lengthText(guarded->length().getTicks(), clipStart(guarded)) : QString(); },
		[guarded, id](const QString& typed) { return resizeClip(guarded, id, typed); });
	auto* offset = new ReadoutField(tr("Offset"), parent);
	offset->bindTextResult([guarded] { return guarded ? musicaltime::lengthText(guarded->startTimeOffset().getTicks(), clipStart(guarded)) : QString(); },
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
	// Theme 13: the value the selected notes share, else "Multiple values" - never the first note's alone.
	const auto shown = [guarded](QString (*text)(const Note*)) {
		return [guarded, text] {
			const Note* note = firstSelected(guarded);
			if (note == nullptr) { return tr("no notes selected"); }
			return differ(guarded, text) ? mixedText() : text(note);
		};
	};
	// A note's start and length in the time format: a time is measured from where the clip starts. Differing starts
	// name the first one, where a typed start puts it.
	const auto timed = [guarded](bool start) {
		return [guarded, start] {
			const Note* note = firstSelected(guarded);
			if (note == nullptr) { return tr("no notes selected"); }
			const QString text = noteTimeText(guarded, note, start);
			if (!differ(guarded, start ? &startKey : &lengthKey)) { return text; }
			return start ? tr("%1, first at %2").arg(mixedText(), text) : mixedText();
		};
	};
	auto* key = new ReadoutField(tr("Key"), box);
	key->bindTextResult(shown(&keyText), [guarded, id](const QString& typed) { return setSelectedKey(guarded, id, typed); });
	auto* velocity = new ReadoutField(tr("Velocity"), box);
	velocity->bindTextResult(shown(&velocityText), [guarded, id](const QString& typed) { return setSelectedVelocity(guarded, id, typed); });
	auto* start = new ReadoutField(tr("Start"), box);
	start->bindTextResult(timed(true), [guarded, id](const QString& typed) { return setSelectedStart(guarded, id, typed); });
	auto* length = new ReadoutField(tr("Length"), box);
	length->bindTextResult(timed(false), [guarded, id](const QString& typed) { return setSelectedLength(guarded, id, typed); });
	timeField(start);
	timeField(length);
	// How many notes the fields speak for, live (a selection changed in the note editor shows at once).
	auto* count = new ReadoutField(tr("Selected notes"), box);
	count->bindText([guarded] { return guarded ? tr("%n note(s)", "", static_cast<int>(selectedNotes(guarded).size())) : QString(); });
	int row = 0;
	for (ReadoutField* field : {count, key, start, length, velocity})
	{
		grid->addWidget(field, row++, 0, 1, 2);  // a readout field carries its own caption
		m_liveFields.append(field);
	}
	box->setObjectName(QStringLiteral("inspectorNotes"));
	return box;
}

} // namespace lmms::gui
