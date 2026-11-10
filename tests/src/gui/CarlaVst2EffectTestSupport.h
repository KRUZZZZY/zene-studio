/*
 * CarlaVst2EffectTestSupport.h - the VST2 test effect's fixtures and helpers that
 *                                CarlaVst2EffectHostingTest and CarlaVst2EffectExportTest
 *                                share (Q050-481.2).
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

// Included by tests/src/gui/CarlaVst2EffectHostingTest.cpp and CarlaVst2EffectExportTest.cpp (the helpers live in an
// unnamed namespace; [[maybe_unused]]: a helper only one of them uses).

#ifndef LMMS_TESTS_CARLA_VST2_EFFECT_TEST_SUPPORT_H
#define LMMS_TESTS_CARLA_VST2_EFFECT_TEST_SUPPORT_H

#include "CarlaVst2TestSupport.h"
#include "ControlDeviceSupport.h"
#include "Effect.h"
#include "EffectChain.h"

namespace
{

const QString EffectNativeFile = QStringLiteral("ZeneVst2TestEffect.so");
const QString EffectWineFile = QStringLiteral("ZeneVst2TestEffect.dll");
//! What the source instrument plays while a key is held, (left, right).
constexpr float SourceLeft = DefaultLevel;
constexpr float SourceRight = DefaultTone;

[[maybe_unused]] QString effectNativeFixture() { return QDir(QStringLiteral(CARLA_VST2_EFFECT_NATIVE_FIXTURE_DIR)).filePath(EffectNativeFile); }
QString effectWineFixture() { return QDir(QStringLiteral(CARLA_VST2_EFFECT_WINE_FIXTURE_DIR)).filePath(EffectWineFile); }

//! The VST2 effect on @a track's chain, the only effect the cases put there.
Effect* effectOn(InstrumentTrack* track)
{
	EffectChain* chain = track->audioBusHandle()->effects();
	return chain->effects().empty() ? nullptr : chain->effects().front();
}

QJsonObject effectAddress(InstrumentTrack* track)
{
	return {{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), control::effectIdOf(effectOn(track))}};
}

//! plugin.param_get by name on the effect: (index, value), or (-1, -1).
[[maybe_unused]] QPair<int, double> effectParam(InstrumentTrack* track, const QString& name)
{
	QJsonObject args = effectAddress(track);
	args.insert(QStringLiteral("name"), name);
	const QJsonObject got = run(QStringLiteral("plugin.param_get"), args).result.value(QStringLiteral("parameter")).toObject();
	return {got.value(QStringLiteral("index")).toInt(-1), got.value(QStringLiteral("value")).toDouble(-1.0)};
}

ControlResult setEffectParam(InstrumentTrack* track, const QString& name, double value)
{
	QJsonObject args = effectAddress(track);
	args.insert(QStringLiteral("name"), name);
	args.insert(QStringLiteral("value"), value);
	return run(QStringLiteral("plugin.param_set"), args);
}

//! The test effect's chunk in saved state @a state ("ZNE2 left=... heard=... editor=..."): the Carla <Chunk> (base64)
//! that is the effect's - a saved track holds the source instrument's ("ZNV2 ...") as well.
[[maybe_unused]] QString chunkIn(const QDomElement& state)
{
	const QDomNodeList chunks = state.elementsByTagName(QStringLiteral("Chunk"));
	for (int i = 0; i < chunks.size(); ++i)
	{
		const QByteArray decoded = QByteArray::fromBase64(chunks.item(i).toElement().text().simplified().toLatin1());
		const QString chunk = QString::fromLatin1(decoded).trimmed().remove(QChar(0));
		if (chunk.startsWith(QLatin1String("ZNE2"))) { return chunk; }
	}
	return {};
}

//! The test effect's chunk as Carla saves it in the effect's state now, or empty.
[[maybe_unused]] QString effectChunk(Effect* effect)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("fxchain"));
	return chunkIn(effect->saveState(doc, parent));
}

//! The integer @a chunk carries for @a field ("heard", "editor"), or -1.
[[maybe_unused]] int chunkField(const QString& chunk, const QString& field)
{
	const QRegularExpressionMatch match = QRegularExpression(field + QStringLiteral("=(\\d+)")).match(chunk);
	return match.hasMatch() ? match.captured(1).toInt() : -1;
}

[[maybe_unused]] int effectChunkField(Effect* effect, const QString& field) { return chunkField(effectChunk(effect), field); }

//! Whether the rendered frame is the source through the effect at (Left, Right).
[[maybe_unused]] bool processedAs(const Rendered& sound, float left, float right)
{
	return sound.sounded && std::abs(sound.left / sound.right - (SourceLeft * left) / (SourceRight * right)) < 1e-3f;
}

[[maybe_unused]] QString describe(const Rendered& sound)
{
	return QStringLiteral("sounded=%1 left=%2 right=%3").arg(sound.sounded).arg(sound.left).arg(sound.right);
}

QString missingEffectRuntime()
{
	if (QString why = missingCarlaRuntime(nativeFixture()); !why.isEmpty()) { return why; }
	return missingCarlaRuntime(effectNativeFixture());
}
bool effectWineAvailable() { return carlaWineAvailable(effectWineFixture()); }

//! A track playing the native VST2 test instrument into the VST2 effect @a fileName, loaded by plugin.load (with
//! the fixture stalling @a stallMs per process call when given). nullptr when either load failed.
InstrumentTrack* sourceWithEffect(const QString& fileName, qint64 stallMs = 0)
{
	const QJsonObject source = vst2Device(NativeFile);
	const QJsonObject device = vst2Device(fileName);
	if (source.isEmpty() || device.isEmpty()) { qWarning("the VST2 fixtures are not listed"); return nullptr; }
	InstrumentTrack* track = newTrack();
	const QString target = control::trackIdOf(track);
	if (!run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), source.value(QStringLiteral("id"))}}).ok)
	{
		qWarning("the source instrument did not load");
		delete track;
		return nullptr;
	}
	if (stallMs > 0) { qputenv("ZENE_VST2_FIXTURE_STALL_MS", QByteArray::number(stallMs)); }
	const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
		{QStringLiteral("device"), device.value(QStringLiteral("id"))}});
	qunsetenv("ZENE_VST2_FIXTURE_STALL_MS");
	if (!loaded.ok || loaded.result.value(QStringLiteral("kind")).toString() != QLatin1String("effect")
		|| effectOn(track) == nullptr
		|| QString::fromLatin1(effectOn(track)->descriptor()->name) != QLatin1String("carlavst2effect"))
	{
		qWarning("plugin.load of the effect failed: %s", qPrintable(loaded.errorMessage));
		delete track;
		return nullptr;
	}
	return track;
}

[[maybe_unused]] void removeAutomationTracks()
{
	// The automation lives on a track of its own: it goes with the effect, so no later case renders a clip whose
	// parameter is gone.
	const TrackContainer::TrackList tracks = Engine::getSong()->tracks();
	for (Track* other : tracks)
	{
		if (other->type() == Track::Type::Automation) { delete other; }
	}
}

} // namespace

#endif // LMMS_TESTS_CARLA_VST2_EFFECT_TEST_SUPPORT_H
