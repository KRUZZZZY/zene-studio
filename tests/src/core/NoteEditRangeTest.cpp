/*
 * NoteEditRangeTest.cpp - note.edit's range validation: a note may not end past MaxSongLength
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
 */

/*! note.edit checks 'position' and 'length' against MaxSongLength one at a time, so the note's
 *  END - the sum of the two - needs its own check: a note written at the very end of the
 *  supported range with a length would otherwise end past it. Both the position+length edit and
 *  the resize alone are covered, and the limit itself stays reachable. The commands themselves
 *  (scale, ratchet, add_many, the clipboard, slide and Mod X / Y) are NoteEditingCommandsTest. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class NoteEditRangeTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	QString clip() const { return control::clipIdOf(m_clip); }

	void freshClip()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		m_clip->rearrangeAllNotes();
	}

	//! The error kind one edit makes; Ok when the command succeeded.
	ControlErrorKind edit(const QString& note, const QJsonObject& fields)
	{
		QJsonObject entry{{QStringLiteral("note"), note}};
		for (auto it = fields.begin(); it != fields.end(); ++it) { entry.insert(it.key(), it.value()); }
		const ControlResult result = run(QStringLiteral("note.edit"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"), QJsonArray{entry}}});
		return result.ok ? ControlErrorKind::None : result.errorKind;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! Luna cluster 3914: position and length were each validated on their own, so a note
	//! could be written ending past the supported song range.
	void aNoteMayNotEndPastTheSongLimit()
	{
		freshClip();
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		// The limit the check is against, read from the engine's own constant.
		QCOMPARE(static_cast<int>(MaxSongLength), 9999 * static_cast<int>(DefaultTicksPerBar));

		// (a) position and length together: each passes on its own, the sum does not.
		QCOMPARE(edit(note, {{QStringLiteral("position"), MaxSongLength}, {QStringLiteral("length"), 48}}),
			ControlErrorKind::InvalidArgs);
		// ...and nothing was written: the batch validates every entry before the first one.
		QCOMPARE(m_clip->notes()[0]->pos().getTicks(), 0);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);

		// (b) a resize alone counts too: at the far end of the range, a longer note is refused.
		QCOMPARE(edit(note, {{QStringLiteral("position"), MaxSongLength - 48}}), ControlErrorKind::None);
		QCOMPARE(edit(note, {{QStringLiteral("length"), 96}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);

		// (c) the limit itself is still reachable: ending exactly at MaxSongLength passes.
		QCOMPARE(edit(note, {{QStringLiteral("length"), 48}}), ControlErrorKind::None);
		QCOMPARE(m_clip->notes()[0]->pos().getTicks() + m_clip->notes()[0]->length().getTicks(),
			static_cast<int>(MaxSongLength));
	}
};

QTEST_MAIN(NoteEditRangeTest)
#include "NoteEditRangeTest.moc"