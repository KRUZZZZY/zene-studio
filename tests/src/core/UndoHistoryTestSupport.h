/*
 * UndoHistoryTestSupport.h - the SHARED helpers of the undo-history tests:
 *                            one definition for the two test files that use
 *                            them (the format/loader half and the options
 *                            half) - a second copy of a helper is exactly the
 *                            drift the split exists to prevent.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LMMS_UNDO_HISTORY_TEST_SUPPORT_H
#define LMMS_UNDO_HISTORY_TEST_SUPPORT_H

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"

//! Shared helpers of the undo-history tests, ONE definition for the two test
//! files that use them: free inline functions in a namespace, not statics in
//! each test class, so both halves build the same fixture.
namespace undotest
{
using namespace lmms;

inline ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

inline QString compact(const QJsonObject& object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

inline QString textOf(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! Drops the save bookkeeping a revision stamp adds (rev / writer): it records
//! WHO saved, not what the project is.
inline void stripRevisionStamps(QDomElement element)
{
	element.removeAttribute(QStringLiteral("rev"));
	element.removeAttribute(QStringLiteral("writer"));
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		stripRevisionStamps(child);
	}
}

/*! The project's content as one canonical digest per section, over the file a
 *  save writes: <head> and every <song> section, each as DocumentIndex's
 *  process-independent digest. Left out: the append-only provenance log (an undo
 *  never rewinds it, by contract), the undo history itself and the document
 *  index. Returned as readable lines so a mismatch names the section. */
inline QString contentOf(const QString& path)
{
	QDomDocument document;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return QStringLiteral("<unreadable>"); }
	const QDomElement root = document.documentElement();
	QString out;
	const auto add = [&out](QDomElement element) {
		stripRevisionStamps(element);
		out += element.tagName() + QLatin1Char(' ') + sectionDigest(element) + QLatin1Char('\n');
	};
	add(root.firstChildElement(QStringLiteral("head")));
	for (QDomElement child = root.firstChildElement(QStringLiteral("song")).firstChildElement(); !child.isNull();
		child = child.nextSiblingElement())
	{
		const QString name = child.tagName();
		if (name == QLatin1String("z:provenance") || name == QLatin1String("z:undohistory")
			|| name == QLatin1String("z:index")) { continue; }
		add(child);
	}
	return out;
}

inline QJsonObject historyState()
{
	const ControlResult state = run(QStringLiteral("project.undo_history"));
	return state.ok ? state.result : QJsonObject{{QStringLiteral("error"), state.errorMessage}};
}

inline int journalDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

inline QJsonObject lastTransaction()
{
	return run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray().last().toObject();
}

//! Random text: incompressible enough that its compressed snapshot stays large.
inline QString noise(int bytes)
{
	QByteArray raw(bytes, Qt::Uninitialized);
	for (char& c : raw) { c = static_cast<char>(QRandomGenerator::global()->bounded(256)); }
	return QString::fromLatin1(raw.toBase64());
}

/*! The baseline: an instrument track with a real instrument, an automation clip linked to its
 *  first parameter, and a sample track with a clip on a real WAV. Built, then the journal is
 *  cleared, so none of it is a step: the steps are the edits each test makes. */
struct Baseline
{
	QString track;
	int trackId = -1;
	QString parameter;
	QString sampleClip;
	QString wav;
};

//! The parameter id automation.get_state names first for the track (the instrument's own).
inline QString firstParameter(const QString& track)
{
	const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}}).result
		.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString() : parameters.first().toObject().value(QStringLiteral("id")).toString();
}

inline double instrumentParameter(const QString& track)
{
	return run(QStringLiteral("plugin.param_get"),
		{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("index"), 0}})
		.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(-1);
}

//! The baseline, built in \a dir: the wav it needs lives beside the project.
inline Baseline buildBaseline(const QString& dir)
{
	Baseline base;
	Song* song = Engine::getSong();
	song->clearProject();
	auto* instrument = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
	if (instrument == nullptr) { return base; }
	instrument->loadInstrument(QStringLiteral("tripleoscillator"));
	base.trackId = instrument->id();
	base.track = QStringLiteral("trk-%1").arg(base.trackId);
	base.parameter = firstParameter(base.track);
	run(QStringLiteral("automation.add_point"), {{QStringLiteral("track"), base.track},
		{QStringLiteral("parameter"), base.parameter}, {QStringLiteral("ticks"), 0}, {QStringLiteral("value"), 0.4}});

	auto* samples = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
	base.wav = taketest::writeToneWav(dir + QStringLiteral("/tone.wav"), 440.0);
	if (samples != nullptr)
	{
		auto* clip = dynamic_cast<SampleClip*>(samples->createClip(TimePos(0)));
		if (clip != nullptr)
		{
			clip->setSampleFile(base.wav);
			base.sampleClip = QStringLiteral("clip-%1").arg(clip->id());
		}
	}
	Engine::projectJournal()->clearJournal();
	return base;
}

//! The session a test runs in: a throwaway config (project.open records recent
//! files, and the caps are set in it), the engine and the control surface.
inline void initSession(const QString& dir)
{
	qputenv("HOME", dir.toLocal8Bit());
	ConfigManager::inst()->loadConfigFile(dir + QStringLiteral("/zene.xml"));
	Engine::init(true);
	ControlRegistry::setReady(true);
}

inline void shutdownSession()
{
	ControlRegistry::setReady(false);
	Engine::destroy();
}

} // namespace undotest

#endif // LMMS_UNDO_HISTORY_TEST_SUPPORT_H
