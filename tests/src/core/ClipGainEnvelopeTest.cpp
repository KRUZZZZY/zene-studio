/*
 * ClipGainEnvelopeTest.cpp - a sample clip's gain envelope: the curve, the file, the commands
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

/*! ED-042 (0.5.0 L04): envelopeGainAt between and outside the points; the "tick:dB" text the clip
 *  element carries, bounded and sorted; a clip without an envelope writes no attribute and one
 *  with an envelope round-trips; clip.gain_envelope_set refuses a point outside the clip or the
 *  dB range and a MIDI clip, and one control.undo takes a set back. (The render is measured in
 *  ClipFadesRenderTest.) */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject point(int tick, double db)
{
	return {{QStringLiteral("tick"), tick}, {QStringLiteral("gain_db"), db}};
}

} // namespace

class ClipGainEnvelopeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theCurveIsLinearInDbAndHoldsOutside()
	{
		ClipEdits edits;
		QCOMPARE(envelopeGainAt(edits, 10.0), 1.0f);
		edits.envelope[0] = {0, 0.f};
		edits.envelope[1] = {96, -6.f};
		edits.envelopeCount = 2;
		QVERIFY(qFuzzyCompare(envelopeGainAt(edits, 48.0), gainDbToLinear(-3.f)));
		QVERIFY(qFuzzyCompare(envelopeGainAt(edits, -5.0), 1.0f));
		QVERIFY(qFuzzyCompare(envelopeGainAt(edits, 500.0), gainDbToLinear(-6.f)));
		QVERIFY(!edits.isNeutral());
	}

	void theTextIsBoundedAndSorted()
	{
		ClipEdits edits;
		envelopeFromText(QStringLiteral("96:-6;0:0;48:-3"), &edits);
		QCOMPARE(edits.envelopeCount, 3);
		QCOMPARE(edits.envelope[0].tick, 0);
		QCOMPARE(edits.envelope[2].tick, 96);
		ClipEdits again;
		envelopeFromText(envelopeToText(edits), &again);
		QVERIFY(again == edits);
		QStringList many;
		for (int i = 0; i < MaxGainPoints + 8; ++i) { many.append(QStringLiteral("%1:0").arg(i)); }
		envelopeFromText(many.join(QLatin1Char(';')), &again);
		QCOMPARE(again.envelopeCount, MaxGainPoints);
	}

	void theClipElementCarriesItOnlyWhenThereIsOne()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = new SampleTrack(song);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		clip->changeLength(TimePos(192));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement plain = clip->saveState(doc, root);
		QVERIFY2(!plain.hasAttribute(QStringLiteral("gainenv")), "a clip with no envelope wrote one");
		ClipEdits edits;
		edits.envelope[0] = {0, -12.f};
		edits.envelope[1] = {96, 0.f};
		edits.envelopeCount = 2;
		clip->setClipEdits(edits);
		const QDomElement shaped = clip->saveState(doc, root);
		auto* loaded = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		loaded->restoreState(shaped);
		QVERIFY(loaded->clipEdits() == edits);
	}

	void theCommandValidatesAndUndoes()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = new SampleTrack(song);
		Clip* clip = track->createClip(TimePos(0));
		clip->changeLength(TimePos(192));
		const QString id = control::clipIdOf(clip);
		QCOMPARE(run(QStringLiteral("clip.gain_envelope_set"), {{QStringLiteral("clip"), id},
			{QStringLiteral("points"), QJsonArray{point(500, 0)}}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("clip.gain_envelope_set"), {{QStringLiteral("clip"), id},
			{QStringLiteral("points"), QJsonArray{point(0, 40)}}}).errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult set = run(QStringLiteral("clip.gain_envelope_set"), {{QStringLiteral("clip"), id},
			{QStringLiteral("points"), QJsonArray{point(96, -6), point(0, 0)}}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const QJsonArray got = run(QStringLiteral("clip.gain_envelope_get"), {{QStringLiteral("clip"), id}}).result
			.value(QStringLiteral("points")).toArray();
		QCOMPARE(got.size(), 2);
		QCOMPARE(got.at(0).toObject().value(QStringLiteral("tick")).toInt(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!clip->clipEdits().hasEnvelope());

		auto* instrument = Track::create(Track::Type::Instrument, song);
		Clip* midi = instrument->createClip(TimePos(0));
		QCOMPARE(run(QStringLiteral("clip.gain_envelope_set"), {{QStringLiteral("clip"), control::clipIdOf(midi)},
			{QStringLiteral("points"), QJsonArray()}}).errorKind, ControlErrorKind::Refused);
	}
};

QTEST_MAIN(ClipGainEnvelopeTest)
#include "ClipGainEnvelopeTest.moc"
