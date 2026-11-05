/*
 * CarlaVst2TestSupport.h - the fixtures, helpers and renders CarlaVst2HostingTest
 *                          uses (Q050-481.1).
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

// Included by tests/src/gui/CarlaVst2HostingTest.cpp only (its helpers live in an unnamed namespace).

#ifndef LMMS_TESTS_CARLA_VST2_TEST_SUPPORT_H
#define LMMS_TESTS_CARLA_VST2_TEST_SUPPORT_H

#include <QtTest>

#include <QApplication>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "SampleFrame.h"
#include "ShellSelection.h"
#include "Song.h"

#ifndef CARLA_VST2_NATIVE_FIXTURE_DIR
#define CARLA_VST2_NATIVE_FIXTURE_DIR ""
#endif
#ifndef CARLA_VST2_WINE_FIXTURE_DIR
#define CARLA_VST2_WINE_FIXTURE_DIR ""
#endif
#ifndef ZENE_CARLA_RUNTIME_DIR
#define ZENE_CARLA_RUNTIME_DIR ""
#endif
#ifndef ZENE_TEST_WINEPREFIX
#define ZENE_TEST_WINEPREFIX ""
#endif

using namespace lmms;
using namespace lmms::gui;

namespace
{

const QString NativeFile = QStringLiteral("ZeneVst2TestInstrument.so");
const QString WineFile = QStringLiteral("ZeneVst2TestInstrument.dll");
constexpr float DefaultLevel = 0.5f;
constexpr float DefaultTone = 0.25f;
constexpr float Tolerance = 1e-4f;
//! How long a render may wait for sound: an in-process plug-in sounds in its first period; a bridged one only once
//! its Wine process is up.
constexpr int NativeRenderMs = 2000;
constexpr int BridgedRenderMs = 20000;
//! How long the stalling fixture sleeps in each process call.
constexpr qint64 StallMs = 800;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString nativeFixture() { return QDir(QStringLiteral(CARLA_VST2_NATIVE_FIXTURE_DIR)).filePath(NativeFile); }
QString wineFixture() { return QDir(QStringLiteral(CARLA_VST2_WINE_FIXTURE_DIR)).filePath(WineFile); }

//! plugin.list's vst2 entry for the binary named @a fileName, or an empty object.
QJsonObject vst2Device(const QString& fileName)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("vst2")}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		if (QFileInfo(value.toObject().value(QStringLiteral("file")).toString()).fileName() == fileName) { return value.toObject(); }
	}
	return {};
}

InstrumentTrack* newTrack()
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	QCoreApplication::processEvents();
	return track;
}

QJsonObject address(InstrumentTrack* track)
{
	return {{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")}};
}

//! The fixture's chunk as Carla saved it in the instrument's state ("ZNV2 level=... editor=..."), or empty.
QString chunkOf(Instrument* instrument)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("instrument"));
	const QDomElement state = instrument->saveState(doc, parent);
	const QDomNodeList chunks = state.elementsByTagName(QStringLiteral("Chunk"));
	if (chunks.isEmpty()) { return {}; }
	const QByteArray decoded = QByteArray::fromBase64(chunks.item(0).toElement().text().simplified().toLatin1());
	return QString::fromLatin1(decoded).trimmed().remove(QChar(0));
}

//! The integer the chunk carries for @a field ("noteons", "editor"), or -1.
int chunkField(Instrument* instrument, const QString& field)
{
	const QRegularExpression pattern(field + QStringLiteral("=(\\d+)"));
	const QRegularExpressionMatch match = pattern.match(chunkOf(instrument));
	return match.hasMatch() ? match.captured(1).toInt() : -1;
}

double paramValue(InstrumentTrack* track, int index)
{
	QJsonObject args = address(track);
	args.insert(QStringLiteral("index"), index);
	const ControlResult got = run(QStringLiteral("plugin.param_get"), args);
	return got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(-1.0);
}

//! What the engine rendered while the song played one long note on @a track: the last sounding
//! frame's (left, right). A bridged plug-in may be silent for its first periods while Wine starts.
struct Rendered
{
	bool sounded = false;
	float left = 0.f;
	float right = 0.f;
};

Rendered renderNote(InstrumentTrack* track, int timeoutMs)
{
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(64 * DefaultTicksPerBar), TimePos(0), 60, 100), false);
	Song* song = Engine::getSong();
	song->setPlayPos(0, Song::PlayMode::Song);
	MidiPlayback::instance().stopped();
	song->playSong();
	Rendered out;
	int soundingPeriods = 0;
	QElapsedTimer elapsed;
	elapsed.start();
	QElapsedTimer sinceStart;
	sinceStart.start();
	while (elapsed.elapsed() < timeoutMs && soundingPeriods < 4)
	{
		// A Wine bridge that has only just started can drop the MIDI of its first blocks: while nothing sounds, the
		// note is played again from the start (one note-on per start; steady-state delivery is what is checked).
		if (soundingPeriods == 0 && sinceStart.elapsed() > 1500)
		{
			song->stop();
			for (int period = 0; period < 4; ++period) { Engine::audioEngine()->renderNextPeriod(); }
			song->setPlayPos(0, Song::PlayMode::Song);
			song->playSong();
			sinceStart.restart();
		}
		const std::span<const SampleFrame> frames = Engine::audioEngine()->renderNextPeriod();
		const SampleFrame& last = frames.back();
		if (std::abs(last.left()) > Tolerance)
		{
			out = Rendered{true, last.left(), last.right()};
			++soundingPeriods;
		}
		QCoreApplication::processEvents();
		QTest::qWait(2);
	}
	song->stop();
	for (int period = 0; period < 8; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	delete clip;
	return out;
}

//! Runs @a handle on the modal dialog the next action opens, once it is up (it is shown inside exec()).
void onceModal(const std::function<void(QWidget*)>& handle, int triesLeft = 100)
{
	QTimer::singleShot(10, [handle, triesLeft] {
		QWidget* modal = QApplication::activeModalWidget();
		if (modal == nullptr && triesLeft > 0) { onceModal(handle, triesLeft - 1); return; }
		handle(modal);
	});
}

//! The instrument's device card's action menu (Device panel, the track selected), filled as it opens.
QMenu* cardMenu(DevicePanel* panel, InstrumentTrack* track)
{
	ShellSelection::instance()->setTrack(track);
	panel->rebuild();
	QCoreApplication::processEvents();
	for (DeviceCard* card : panel->findChildren<DeviceCard*>())
	{
		auto* actions = card->findChild<QToolButton*>(QStringLiteral("deviceActions"));
		if (card->spec().instrumentTrack != track || actions == nullptr || actions->menu() == nullptr) { continue; }
		emit actions->menu()->aboutToShow();
		return actions->menu();
	}
	return nullptr;
}

} // namespace

#endif // LMMS_TESTS_CARLA_VST2_TEST_SUPPORT_H
