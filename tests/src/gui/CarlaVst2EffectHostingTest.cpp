/*
 * CarlaVst2EffectHostingTest.cpp - VST2 effects hosted through Carla, native
 *                                  and through Carla's Wine bridge (Q050-481.2).
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

/*
 * The subject is the product's own path, end to end: the build's
 * carlavst2effect module, a REAL Carla runtime (tools/build-carla-runtime.sh,
 * found through LD_LIBRARY_PATH from tests/CMakeLists.txt), and the in-tree VST2
 * test effect (tests/data/vst2-test-effect) built twice - a native .so Carla
 * loads in process and a Windows .dll Carla can only run through Wine.
 *
 * The signal it processes is the stage-1 VST2 test instrument's (hosted by
 * carlavst2): while a key is held it outputs the constant (0.5, 0.25). The test
 * effect multiplies the left channel by 2 * Left and the right one by
 * 2 * Right, its two parameters (0.5: unity), counts the blocks it heard sound
 * in its chunk only, and says in its chunk whether its editor is open.
 *
 * Everything goes through what a user or an agent uses - plugin.list,
 * plugin.load, plugin.param_*, plugin.preset_*, plugin.state_*,
 * plugin.editor_*, automation.add_point, control.undo, the Device panel's
 * "Add device" button and the effect dialog's editor button - and sound is read
 * from the engine's own render (renderNextPeriod). Without a Carla runtime (an
 * ordinary build: the weak stand-in hosts nothing) the cases report Skipped.
 */

#include <QtTest>

#include "CarlaVst2TestSupport.h"
#include "ControlDeviceSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderPeriodClock.h"

namespace
{

const QString EffectNativeFile = QStringLiteral("ZeneVst2TestEffect.so");
const QString EffectWineFile = QStringLiteral("ZeneVst2TestEffect.dll");
//! What the source instrument plays while a key is held, (left, right).
constexpr float SourceLeft = DefaultLevel;
constexpr float SourceRight = DefaultTone;

QString effectNativeFixture() { return QDir(QStringLiteral(CARLA_VST2_EFFECT_NATIVE_FIXTURE_DIR)).filePath(EffectNativeFile); }
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
QPair<int, double> effectParam(InstrumentTrack* track, const QString& name)
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
QString chunkIn(const QDomElement& state)
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
QString effectChunk(Effect* effect)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("fxchain"));
	return chunkIn(effect->saveState(doc, parent));
}

//! The integer @a chunk carries for @a field ("heard", "editor"), or -1.
int chunkField(const QString& chunk, const QString& field)
{
	const QRegularExpressionMatch match = QRegularExpression(field + QStringLiteral("=(\\d+)")).match(chunk);
	return match.hasMatch() ? match.captured(1).toInt() : -1;
}

int effectChunkField(Effect* effect, const QString& field) { return chunkField(effectChunk(effect), field); }

//! Whether the rendered frame is the source through the effect at (Left, Right).
bool processedAs(const Rendered& sound, float left, float right)
{
	return sound.sounded && std::abs(sound.left / sound.right - (SourceLeft * left) / (SourceRight * right)) < 1e-3f;
}

//! The interleaved float frames of a 32-bit float stereo WAV, or empty.
std::vector<SampleFrame> readFloatWav(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	const QByteArray bytes = file.readAll();
	int at = 12;  // past "RIFF" <size> "WAVE"
	while (at + 8 <= bytes.size())
	{
		const QByteArray id = bytes.mid(at, 4);
		quint32 size = 0;
		std::memcpy(&size, bytes.constData() + at + 4, 4);
		if (id == "data")
		{
			const auto count = std::min<qsizetype>(size, bytes.size() - at - 8) / qsizetype(2 * sizeof(float));
			std::vector<SampleFrame> frames(static_cast<std::size_t>(count));
			for (qsizetype f = 0; f < count; ++f)
			{
				float pair[2];
				std::memcpy(pair, bytes.constData() + at + 8 + f * 2 * sizeof(float), sizeof(pair));
				frames[static_cast<std::size_t>(f)] = SampleFrame(pair[0], pair[1]);
			}
			return frames;
		}
		at += 8 + static_cast<int>(size) + static_cast<int>(size & 1);
	}
	return {};
}

QString describe(const Rendered& sound)
{
	return QStringLiteral("sounded=%1 left=%2 right=%3").arg(sound.sounded).arg(sound.left).arg(sound.right);
}

} // namespace

class CarlaVst2EffectHostingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_plugins.isValid());
		QVERIFY(m_files.isValid());
		// The source instrument (native) and the two effect fixtures, nothing the machine has installed.
		initCarlaHostingProcess(m_home, m_plugins, {QStringLiteral(CARLA_VST2_NATIVE_FIXTURE_DIR),
			QStringLiteral(CARLA_VST2_EFFECT_NATIVE_FIXTURE_DIR), QStringLiteral(CARLA_VST2_EFFECT_WINE_FIXTURE_DIR)},
			effectWineFixture());
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! AC1: plugin.list offers the native fixture as a loadable VST2 effect, with its vendor and unique id, and the
	//! bridged one as a Windows binary Carla runs through Wine; VST2 effects come after VST2 instruments, last.
	void vst2EffectsAreListed()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const QJsonObject effect = vst2Device(EffectNativeFile);
		QCOMPARE(effect.value(QStringLiteral("format")).toString(), QStringLiteral("vst2"));
		QCOMPARE(effect.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		QVERIFY(effect.value(QStringLiteral("loadable")).toBool());
		QCOMPARE(effect.value(QStringLiteral("display_name")).toString(), QStringLiteral("Zene VST2 Test Effect"));
		QCOMPARE(effect.value(QStringLiteral("vendor")).toString(), QStringLiteral("Zene Studio Tests"));
		QCOMPARE(effect.value(QStringLiteral("unique_id")).toString(), QStringLiteral("1517176114"));  // 'ZnE2'
		QCOMPARE(effect.value(QStringLiteral("binary")).toString(), QStringLiteral("native"));
		QCOMPARE(effect.value(QStringLiteral("bridged")).toBool(), false);
		// The instrument is still an instrument, and listed before the effect.
		const QJsonObject instrument = vst2Device(NativeFile);
		QCOMPARE(instrument.value(QStringLiteral("kind")).toString(), QStringLiteral("instrument"));
		QVERIFY(instrument.value(QStringLiteral("id")).toString().mid(4).toInt() < effect.value(QStringLiteral("id")).toString().mid(4).toInt());
		if (wineAvailable())
		{
			const QJsonObject bridged = vst2Device(EffectWineFile);
			QCOMPARE(bridged.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
			QCOMPARE(bridged.value(QStringLiteral("binary")).toString(), QStringLiteral("win64"));
			QCOMPARE(bridged.value(QStringLiteral("bridged")).toBool(), true);
		}
		const ControlResult effects = run(QStringLiteral("plugin.list"),
			{{QStringLiteral("format"), QStringLiteral("vst2")}, {QStringLiteral("kind"), QStringLiteral("effect")}});
		QVERIFY(effects.ok);
		QVERIFY(!effects.result.value(QStringLiteral("devices")).toArray().isEmpty());
		const QJsonArray all = run(QStringLiteral("plugin.list")).result.value(QStringLiteral("devices")).toArray();
		QCOMPARE(all.last().toObject().value(QStringLiteral("format")).toString(), QStringLiteral("vst2"));
		QCOMPARE(all.last().toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
	}

	//! AC1, the GUI: the Device panel's "Add device" button opens the effect picker, which shows the VST2 effect row
	//! (format VST2); accepting it loads the effect through plugin.load onto the selected track.
	void theDevicePanelAddsAVst2Effect()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = newTrack();
		track->loadInstrument(QStringLiteral("tripleoscillator"));
		ShellSelection::instance()->setTrack(track);
		DevicePanel panel;
		panel.rebuild();
		QCoreApplication::processEvents();
		auto* add = panel.findChild<QToolButton*>(QStringLiteral("deviceAdd"));
		QVERIFY2(add != nullptr, "the Device panel has no Add device button");
		QString row;
		QString picked;
		onceModal([&row, &picked](QWidget* modal) {
			auto* picker = qobject_cast<DevicePicker*>(modal);
			if (picker == nullptr) { if (modal != nullptr) { modal->close(); } return; }
			picker->setQuery(QStringLiteral("Zene VST2 Test Effect VST2"));
			row = picker->shownRowText(0);
			picked = picker->selectedDevice();
			if (picked.isEmpty()) { picker->reject(); return; }
			static_cast<QDialog*>(picker)->accept();
		});
		QTest::mouseClick(add, Qt::LeftButton);
		QVERIFY2(row.contains(QStringLiteral("VST2")), qPrintable(row));
		QCOMPARE(picked, vst2Device(EffectNativeFile).value(QStringLiteral("id")).toString());
		QCoreApplication::processEvents();
		Effect* effect = effectOn(track);
		QVERIFY2(effect != nullptr, "no effect was added to the track");
		QCOMPARE(QString::fromLatin1(effect->descriptor()->name), QStringLiteral("carlavst2effect"));
		QCOMPARE(effect->displayName(), QStringLiteral("Zene VST2 Test Effect"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! AC1/AC2 native: plugin.load hosts the native fixture through Carla on a track's chain; the engine renders the
	//! track's sound through it with the parameters plugin.param_* reads and writes; plugin.param_set is one undo
	//! step, and the next undo removes the effect plugin.load added.
	void pluginLoadHostsTheNativeEffectAndItProcesses()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = sourceWithEffect(EffectNativeFile);
		QVERIFY(track != nullptr);
		Effect* effect = effectOn(track);
		QCOMPARE(effectParam(track, QStringLiteral("Left")).second, 0.5);
		QCOMPARE(effectParam(track, QStringLiteral("Right")).second, 0.5);
		QVERIFY2(effect->latencyFrames() > 0, "the effect reports no latency for the chain to compensate");

		Rendered sound = renderNote(track, NativeRenderMs);
		QVERIFY2(processedAs(sound, 0.5f, 0.5f), qPrintable(describe(sound)));
		QVERIFY2(std::abs(sound.left - 2.f * sound.right) < 1e-3f, qPrintable(describe(sound)));  // unity on both sides

		const ControlResult written = setEffectParam(track, QStringLiteral("Left"), 0.75);
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		sound = renderNote(track, NativeRenderMs);
		QVERIFY2(processedAs(sound, 0.75f, 0.5f), qPrintable(describe(sound)));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(effectParam(track, QStringLiteral("Left")).second, 0.5);
		sound = renderNote(track, NativeRenderMs);
		QVERIFY2(processedAs(sound, 0.5f, 0.5f), qPrintable(describe(sound)));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(effectOn(track) == nullptr, "undoing plugin.load did not remove the effect");
		delete track;
	}

	void automationDrivesAVst2EffectParameter_data()
	{
		QTest::addColumn<QString>("file");
		QTest::newRow("native") << EffectNativeFile;
		QTest::newRow("wine-bridged") << EffectWineFile;
	}

	//! AC2 + realtime: automation written on a VST2 effect parameter (automation.add_point) drives the plug-in during
	//! playback, natively and through the Wine bridge, and the render thread never emits the parameter's change
	//! signal (the path into Carla's UI calls); the change is emitted afterwards on this (the GUI) thread.
	void automationDrivesAVst2EffectParameter()
	{
		QFETCH(QString, file);
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const bool bridged = file == EffectWineFile;
		if (bridged && !wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		InstrumentTrack* track = sourceWithEffect(file);
		QVERIFY(track != nullptr);
		const int right = effectParam(track, QStringLiteral("Right")).first;
		QVERIFY(right >= 0);
		const QString parameter = control::effectIdOf(effectOn(track)) + QLatin1Char('/') + QString::number(right);
		const ControlResult added = run(QStringLiteral("automation.add_point"), {{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("parameter"), parameter}, {QStringLiteral("ticks"), 0}, {QStringLiteral("value"), 0.125}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		AutomatableModel* model = controlEffectParameters(effectOn(track)).value(right);
		QVERIFY(model != nullptr);
		int onRenderThread = 0;
		int elsewhere = 0;
		const QMetaObject::Connection probe = connect(model, &Model::dataChanged, this, [&onRenderThread, &elsewhere] {
			++(Engine::audioEngine()->isRenderThread() ? onRenderThread : elsewhere);
		}, Qt::DirectConnection);
		const Rendered sound = renderNote(track, bridged ? BridgedRenderMs : NativeRenderMs);
		QVERIFY2(processedAs(sound, 0.5f, 0.125f), qPrintable(describe(sound)));
		QCOMPARE(onRenderThread, 0);
		QTRY_VERIFY_WITH_TIMEOUT(elsewhere > 0, 2000);  // the deferred change, emitted by the effect's GUI timer
		disconnect(probe);
		removeAutomationTracks();
		delete track;
	}

	void theAudioThreadNeverWaitsForTheEffect_data() { automationDrivesAVst2EffectParameter_data(); }

	//! Realtime: the engine's render period never waits for Carla - not for a native effect, nor for the Wine bridge,
	//! whose process call waits on the bridge's reply. The effect fixture stalls 800 ms in every process call
	//! (ZENE_VST2_FIXTURE_STALL_MS); while sound goes through it, every engine period still renders in a fraction of
	//! that.
	void theAudioThreadNeverWaitsForTheEffect()
	{
		QFETCH(QString, file);
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (file == EffectWineFile && !wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		InstrumentTrack* track = sourceWithEffect(file, StallMs);
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(16 * DefaultTicksPerBar), TimePos(0), 60, 100), false);
		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		qint64 worst = 0;
		float loudest = 0.f;
		QElapsedTimer loop;
		loop.start();
		for (int period = 0; period < 12; ++period)
		{
			QElapsedTimer timer;
			timer.start();
			const std::span<const SampleFrame> frames = Engine::audioEngine()->renderNextPeriod();
			worst = std::max(worst, timer.elapsed());
			for (const SampleFrame& frame : frames) { loudest = std::max(loudest, std::abs(frame.left())); }
			QTest::qWait(5);
		}
		const qint64 looped = loop.elapsed();
		song->stop();
		for (int period = 0; period < 8; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		delete clip;
		delete track;
		qInfo("worst render period with the effect stalling %lld ms per process call: %lld ms (loop %lld ms, peak %f)",
			StallMs, worst, looped, static_cast<double>(loudest));
		// No assertion on the sound: Carla's rack passes its input through until a plug-in it has just added is
		// active, so the first periods may carry the (unity) input. The peak is logged for the record.
		QVERIFY2(worst < StallMs / 2, qPrintable(QStringLiteral("a render period took %1 ms").arg(worst)));
	}

	void anExportWaitsBetweenPeriodsAndKeepsTheEffectsOutput_data() { automationDrivesAVst2EffectParameter_data(); }

	//! Realtime (review r1): a project export through the real exporter (ProjectRenderer) with the effect stalling
	//! 150 ms in every process call. The render period never waits for it - no period the exporter renders lasts
	//! anywhere near the stall (sampled from the render period clock while the export runs) - and the file still
	//! holds the effect's output without a gap: the exporter waits for the worker BETWEEN periods instead.
	void anExportWaitsBetweenPeriodsAndKeepsTheEffectsOutput()
	{
		QFETCH(QString, file);
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (file == EffectWineFile && !wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		constexpr qint64 ExportStallMs = 150;
		InstrumentTrack* track = sourceWithEffect(file, ExportStallMs);
		QVERIFY(track != nullptr);
		QVERIFY(setEffectParam(track, QStringLiteral("Right"), 0.25).ok);  // processed: R = 0.5 * input, L = input
		Song* song = Engine::getSong();
		const int tempo = song->getTempo();
		song->setTempo(600);  // one bar is 0.4 s: a short file, so the stalled export stays short
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(DefaultTicksPerBar), TimePos(0), 60, 100), false);
		song->updateLength();
		// Played once first, as any session is before an export: a Wine bridge applies a parameter change one or
		// two blocks late (measured, with or without the stall: the first sounding blocks at the old value), and
		// that is not what this case is about. Each period is given the time the stalled worker needs.
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		for (int period = 0; period < 8; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			QTest::qWait(static_cast<int>(ExportStallMs) + 20);
		}
		song->stop();
		for (int period = 0; period < 4; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			QTest::qWait(static_cast<int>(ExportStallMs) + 20);
		}
		const QString path = QDir(m_files.path()).filePath(QStringLiteral("vst2-effect-export-%1.wav").arg(QTest::currentDataTag()));
		const OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		qint64 longestPeriodMs = 0;
		int periodsSeen = 0;
		{
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QVERIFY(renderer.isReady());
			renderer.startProcessing();
			bool inPeriod = false;
			while (!renderer.wait(0))
			{
				if (RenderPeriodClock::inPeriod())
				{
					const qint64 ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - RenderPeriodClock::periodStart()).count();
					longestPeriodMs = std::max(longestPeriodMs, ms);
					if (!inPeriod) { ++periodsSeen; }
					inPeriod = true;
				}
				else { inPeriod = false; }
				std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
		}
		song->setTempo(tempo);
		delete clip;
		delete track;
		const std::vector<SampleFrame> frames = readFloatWav(path);
		QVERIFY2(!frames.empty(), "the export wrote no audio");
		auto sounding = [](const SampleFrame& frame) { return std::abs(frame.left()) > Tolerance; };
		const auto first = std::find_if(frames.begin(), frames.end(), sounding);
		const auto last = std::find_if(frames.rbegin(), frames.rend(), sounding);
		QVERIFY2(first != frames.end(), "the exported file is silent: the effect's output was lost");
		const auto span = std::distance(first, last.base());
		int gaps = 0;
		int wrongRatio = 0;
		for (auto frame = first; frame != last.base(); ++frame)
		{
			if (!sounding(*frame)) { ++gaps; continue; }
			// The source's (0.5, 0.25) through Left 0.5 / Right 0.25: right = left * (0.25 / 0.5) * (0.25 / 0.5).
			if (std::abs(frame->right() - frame->left() * 0.25f) > 1e-3f)
			{
				if (wrongRatio++ == 0)
				{
					qInfo("first unprocessed frame at %td of the span: (%f, %f)", std::distance(first, frame),
						static_cast<double>(frame->left()), static_cast<double>(frame->right()));
				}
			}
		}
		qInfo("export: %d periods seen, longest %lld ms; %zu frames, %td sounding in one span, %d silent inside it, "
			"%d not processed", periodsSeen, longestPeriodMs, frames.size(), span, gaps, wrongRatio);
		QVERIFY2(periodsSeen > 0, "no render period was observed: nothing was measured");
		QVERIFY2(longestPeriodMs < ExportStallMs / 2, qPrintable(QStringLiteral("a render period took %1 ms").arg(longestPeriodMs)));
		// The note sounds for one bar (17640 frames at 600 BPM, 44.1 kHz): most of it must be in the file, unbroken.
		QVERIFY2(span > 17640 / 2, qPrintable(QString::number(span)));
		QCOMPARE(gaps, 0);
		QCOMPARE(wrongRatio, 0);
	}

	//! AC2: the plug-in's own editor, from the agent's plugin.editor_open/close and from the effect dialog's button:
	//! Carla embeds it (the fixture's effEditOpen, seen in its chunk) in a window of its reported size.
	void theEffectsOwnEditorOpensThroughCarla()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = sourceWithEffect(EffectNativeFile);
		QVERIFY(track != nullptr);
		Effect* effect = effectOn(track);
		const bool headless = ControlRegistry::instance()->isHeadless();
		ControlRegistry::instance()->setHeadless(false);
		const ControlResult opened = run(QStringLiteral("plugin.editor_open"), effectAddress(track));
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("open")).toBool());
		QCOMPARE(effectChunkField(effect, QStringLiteral("editor")), 1);
		QWidget* window = nullptr;
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			if (top->isVisible() && top->windowTitle() == effect->displayName()) { window = top; }
		}
		QVERIFY2(window != nullptr, "no window titled after the effect is showing");
		QVERIFY(window->width() >= 240);
		QVERIFY(window->height() >= 120);
		const ControlResult closed = run(QStringLiteral("plugin.editor_close"), effectAddress(track));
		QVERIFY(closed.ok);
		QVERIFY(closed.result.value(QStringLiteral("was_open")).toBool());
		QCOMPARE(effectChunkField(effect, QStringLiteral("editor")), 0);

		// The GUI: the effect dialog's "Show plugin editor" button sends the same command.
		QVERIFY(effect->controls()->controlCount() > 0);  // the effect's view offers the dialog
		gui::EffectControlDialog* dialog = effect->controls()->createView();
		QVERIFY(dialog != nullptr);
		QToolButton* show = nullptr;
		for (QToolButton* button : dialog->findChildren<QToolButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("plugin.editor_open")) { show = button; }
		}
		QVERIFY2(show != nullptr, "the VST2 effect's dialog has no plugin-editor button");
		QTest::mouseClick(show, Qt::LeftButton);
		QVERIFY2(effect->nativeEditorOpen(), "the dialog's button did not open the editor");
		QCOMPARE(effectChunkField(effect, QStringLiteral("editor")), 1);
		effect->closeNativeEditor();
		QCOMPARE(effectChunkField(effect, QStringLiteral("editor")), 0);
		delete dialog;
		ControlRegistry::instance()->setHeadless(headless);
		delete track;
	}

	//! AC2: presets - plugin.preset_save, a change, plugin.preset_load: the saved value comes back, and the sound.
	void presetsSaveAndLoad()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = sourceWithEffect(EffectNativeFile);
		QVERIFY(track != nullptr);
		QVERIFY(setEffectParam(track, QStringLiteral("Right"), 0.625).ok);
		QJsonObject save = effectAddress(track);
		save.insert(QStringLiteral("name"), QStringLiteral("vst2-effect-right"));
		const ControlResult saved = run(QStringLiteral("plugin.preset_save"), save);
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QVERIFY(setEffectParam(track, QStringLiteral("Right"), 0.0625).ok);
		QVERIFY(std::abs(effectParam(track, QStringLiteral("Right")).second - 0.0625) < Tolerance);
		const ControlResult loaded = run(QStringLiteral("plugin.preset_load"), save);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY(std::abs(effectParam(track, QStringLiteral("Right")).second - 0.625) < Tolerance);
		const Rendered sound = renderNote(track, NativeRenderMs);
		QVERIFY2(processedAs(sound, 0.5f, 0.625f), qPrintable(describe(sound)));
		delete track;
	}

	//! AC2: device state - plugin.state_save writes the effect's state (parameters and the plug-in's chunk) to a file,
	//! plugin.state_load brings both back after a change.
	void deviceStateSaveAndLoad()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = sourceWithEffect(EffectNativeFile);
		QVERIFY(track != nullptr);
		QVERIFY(setEffectParam(track, QStringLiteral("Left"), 0.875).ok);
		QVERIFY(renderNote(track, NativeRenderMs).sounded);
		QJsonObject args = effectAddress(track);
		const QString path = QDir(m_files.path()).filePath(QStringLiteral("vst2-effect.state"));
		args.insert(QStringLiteral("path"), path);
		const ControlResult saved = run(QStringLiteral("plugin.state_save"), args);
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		// The count the file holds (the worker may still have been processing the tail when the case read it).
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QDomDocument stateDoc;
		QVERIFY(stateDoc.setContent(file.readAll()));
		const int heard = chunkField(chunkIn(stateDoc.documentElement()), QStringLiteral("heard"));
		QVERIFY2(heard >= 1, qPrintable(chunkIn(stateDoc.documentElement())));
		QVERIFY(setEffectParam(track, QStringLiteral("Left"), 0.25).ok);
		QVERIFY(renderNote(track, NativeRenderMs).sounded);  // the plug-in hears more blocks after the save
		QVERIFY(effectChunkField(effectOn(track), QStringLiteral("heard")) > heard);
		const ControlResult loaded = run(QStringLiteral("plugin.state_load"), args);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY(std::abs(effectParam(track, QStringLiteral("Left")).second - 0.875) < Tolerance);
		QCOMPARE(effectChunkField(effectOn(track), QStringLiteral("heard")), heard);
		delete track;
	}

	//! AC2: project state - the track saved and loaded into a new track brings back the same effect with its
	//! parameters AND its chunk (the heard-block count no parameter carries), and it sounds the same.
	void projectStateRoundTripsThroughCarla()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		roundTrip(EffectNativeFile, NativeRenderMs);
	}

	//! AC3 bridged: the Windows fixture loads through Carla's Wine bridge, processes with its parameters, and its state
	//! (parameters and chunk) round-trips through a project like the native one.
	void theWindowsEffectRunsThroughTheWineBridge()
	{
		if (const QString why = missingRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (!wineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime: the bridged path cannot run here"); }
		InstrumentTrack* track = sourceWithEffect(EffectWineFile);
		QVERIFY(track != nullptr);
		QVERIFY(effectParam(track, QStringLiteral("Left")).first >= 0);
		QVERIFY(setEffectParam(track, QStringLiteral("Right"), 0.375).ok);
		const Rendered sound = renderNote(track, BridgedRenderMs);
		QVERIFY2(processedAs(sound, 0.5f, 0.375f), qPrintable(describe(sound)));
		delete track;
		roundTrip(EffectWineFile, BridgedRenderMs);
	}

private:
	static QString missingRuntime()
	{
		if (QString why = missingCarlaRuntime(nativeFixture()); !why.isEmpty()) { return why; }
		return missingCarlaRuntime(effectNativeFixture());
	}
	static bool wineAvailable() { return carlaWineAvailable(effectWineFixture()); }

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

	static void removeAutomationTracks()
	{
		// The automation lives on a track of its own: it goes with the effect, so no later case renders a clip whose
		// parameter is gone.
		const TrackContainer::TrackList tracks = Engine::getSong()->tracks();
		for (Track* other : tracks)
		{
			if (other->type() == Track::Type::Automation) { delete other; }
		}
	}

	void roundTrip(const QString& fileName, int renderMs)
	{
		InstrumentTrack* track = sourceWithEffect(fileName);
		QVERIFY(track != nullptr);
		QVERIFY(setEffectParam(track, QStringLiteral("Left"), 0.3125).ok);
		const Rendered before = renderNote(track, renderMs);
		QVERIFY2(processedAs(before, 0.3125f, 0.5f), qPrintable(describe(before)));

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("project"));
		doc.appendChild(root);
		track->saveState(doc, root);
		const QDomElement saved = root.firstChildElement();
		QVERIFY(!saved.isNull());
		// The count the project holds (the worker may still have been processing the tail a moment before).
		const int heard = chunkField(chunkIn(saved), QStringLiteral("heard"));
		QVERIFY2(heard >= 1, qPrintable(chunkIn(saved)));
		delete track;

		InstrumentTrack* restored = newTrack();
		restored->restoreState(saved);
		QCoreApplication::processEvents();
		Effect* effect = effectOn(restored);
		QVERIFY2(effect != nullptr, "the saved effect did not come back");
		QCOMPARE(QString::fromLatin1(effect->descriptor()->name), QStringLiteral("carlavst2effect"));
		QVERIFY2(effect->unavailableReason().isEmpty(), qPrintable(effect->unavailableReason()));
		QCOMPARE(QFileInfo(effect->key().attributes.value(QStringLiteral("file"))).fileName(), fileName);
		// A bridged plug-in restores its chunk in its own process: wait for its reply.
		QTRY_VERIFY_WITH_TIMEOUT(std::abs(effectParam(restored, QStringLiteral("Left")).second - 0.3125) < Tolerance, 20000);
		if (fileName == EffectWineFile && !carlaTempDirWritable())
		{
			// Carla hands a chunk to its bridge through a file in /var/tmp (water's tempDirectory, hard-coded); where
			// that is not writable (a sandbox) the bridge never receives it. Everything else is still checked.
			qWarning("bridged chunk restore not checked: Carla's temp directory (/var/tmp) is not writable here");
		}
		else
		{
			QTRY_COMPARE_WITH_TIMEOUT(effectChunkField(effectOn(restored), QStringLiteral("heard")), heard, 20000);
		}
		const Rendered after = renderNote(restored, renderMs);
		QVERIFY2(after.sounded, "the restored track rendered nothing");
		QVERIFY2(std::abs(after.left - before.left) < Tolerance && std::abs(after.right - before.right) < Tolerance,
			qPrintable(QStringLiteral("before %1 after %2").arg(describe(before), describe(after))));
		delete restored;
	}

	QTemporaryDir m_home;
	QTemporaryDir m_plugins;
	QTemporaryDir m_files;
};

QTEST_MAIN(CarlaVst2EffectHostingTest)
#include "CarlaVst2EffectHostingTest.moc"
