/*
 * ClapEffectIntegrationTest.cpp - headless end-to-end test of the CLAP effect
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

#include <QtTest>

#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <QWidget>

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AutomationClip.h"
#include "BufferManager.h"
#include "ClapEffect.h"
#include "ClapEffectControls.h"
#include "ClapKnobGrid.h"
#include "ClapSubPluginFeatures.h"
#include "ConfigManager.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "EffectChain.h"
#include "EffectControlDialog.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "PluginWindowSystem.h"
#include "Song.h"
#include "Track.h"
#include "plugin_export.h" // PLUGIN_EXPORT of the clapeffect plug-in

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
#endif
#ifndef CLAP_TEST_BROKEN_NO_SYMBOL_PATH
#define CLAP_TEST_BROKEN_NO_SYMBOL_PATH ""
#endif

namespace lmms
{

class ClapEffectIntegrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testProcessesAudioThroughAudioBus();
	void testMultiChannelBusPassesThroughExtraChannels();
	void testStateRoundTripThroughMmp();
	void testAValueTheStateDoesNotCarryIsRestoredFromTheHost();
	void testRendersBeforeAfterWav();
	void testLegacyAudioBufferPathRoutesPlanarPorts();
	void testTheEffectsOwnEditorOpensInItsOwnWindow();
	void testTheControlDialogsButtonOpensTheEditor();
	void testTheButtonNamesThePlatformAPluginCannotEmbedOn();
	void testThePluginsLatencyReachesTheChain();
	void testThePluginsTailKeepsItRunning();
	void testParameterRescansRebuildTheModels();
	void testReadOnlyParametersGetNoKnob();
	void testReadOnlyParametersAreReportedReadOnly();
	void testAProcessingErrorFallsBackToDryAudio();
	void testScanFailuresAreReported();

private:
	auto makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key;
};

namespace
{

constexpr double kPi = 3.14159265358979323846;

//! qWarning() text collected while a WarningCapture is alive. File-scope so the
//! static handler Qt wants can reach the capture currently in progress.
QStringList g_warnings;
bool g_capturingWarnings = false;

void collectWarning(QtMsgType type, const QMessageLogContext&, const QString& message)
{
	if (g_capturingWarnings && type == QtWarningMsg) { g_warnings.push_back(message); }
}

//! Collects qWarning() text for the length of one scope, so a test can prove that a
//! scan SAID why a module did not appear instead of dropping the reason on the floor.
class WarningCapture
{
public:
	WarningCapture() { m_previous = qInstallMessageHandler(&collectWarning); g_capturingWarnings = true; }
	~WarningCapture()
	{
		g_capturingWarnings = false;
		qInstallMessageHandler(m_previous);
	}
	auto warnings() const -> const QStringList& { return g_warnings; }

private:
	QtMessageHandler m_previous = nullptr;
};

auto rms(const std::vector<float>& samples) -> double
{
	double sum = 0.0;
	for (const auto sample : samples)
	{
		sum += static_cast<double>(sample) * static_cast<double>(sample);
	}
	return std::sqrt(sum / static_cast<double>(samples.size()));
}

//! Where a rendered WAV for inspection goes, on THIS platform.
//!
//! Same defect and same fix as Vst3EffectIntegrationTest.cpp: the write sites
//! spelled "/tmp/clap_before.wav", which on Windows is "C:/tmp/...", a
//! directory that does not exist, so QFile::open() failed. This test is not
//! built on msvc-x64 today, so the assumption is latent there rather than red -
//! QDir::tempPath() removes it before it becomes a failure on the next box that
//! does build it. The resolved path is logged so the artefact can be found.
auto renderedWavPath(const QString& name) -> QString
{
	const auto path = QDir{QDir::tempPath()}.filePath(name);
	qInfo("rendered wav: %s", qPrintable(path));
	return path;
}

//! Minimal 16-bit stereo PCM WAV writer, so rendered audio can be inspected.
auto writeWav(const QString& path, const std::vector<float>& samples, int sampleRate) -> bool
{
	QFile file{path};
	if (!file.open(QIODevice::WriteOnly)) { return false; }

	QDataStream out{&file};
	out.setByteOrder(QDataStream::LittleEndian);

	const auto dataBytes = static_cast<quint32>(samples.size() * 2 * sizeof(qint16));
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVE", 4);
	out.writeRawData("fmt ", 4);
	out << quint32(16) << quint16(1) << quint16(2) << quint32(sampleRate)
	    << quint32(sampleRate * 2 * sizeof(qint16)) << quint16(2 * sizeof(qint16))
	    << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;

	for (const auto sample : samples)
	{
		const auto pcm = static_cast<qint16>(
			std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
		out << pcm << pcm;
	}

	return file.error() == QFileDevice::NoError;
}

} // namespace

extern "C" Plugin::Descriptor PLUGIN_EXPORT clapeffect_plugin_descriptor;

auto ClapEffectIntegrationTest::makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.desc = &clapeffect_plugin_descriptor;
	key.name = QStringLiteral("LMMS CLAP Test Gain");
	key.attributes["file"] = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	key.attributes["id"] = QStringLiteral("org.lmms.test.clap-gain");
	return key;
}

void ClapEffectIntegrationTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_PLUGIN_PATH)))
	{
		QSKIP("no CLAP test plug-in available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers)");
	}
}

void ClapEffectIntegrationTest::cleanupTestCase()
{
	Engine::destroy();
}

void ClapEffectIntegrationTest::testProcessesAudioThroughAudioBus()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	QVERIFY(controls->paramModels().size() >= 2);

	// id 1 is "Gain"; id 2 is the stepped bypass switch
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = 0.5f;
		storage[f][1] = -0.25f;
	}
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};

	QVERIFY(chain.processAudioBuffer(bus));

	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(storage[f][0] - 0.25f) < 1e-6f,
			qPrintable(QStringLiteral("left %1").arg(storage[f][0])));
		QVERIFY2(std::abs(storage[f][1] + 0.125f) < 1e-6f,
			qPrintable(QStringLiteral("right %1").arg(storage[f][1])));
	}
	qInfo("effect chain: input 0.5/-0.25, gain 0.5 -> measured out[0]=%.9f out[1]=%.9f",
		storage[0][0], storage[0][1]);
}

//! The Part B transport is multi-channel: the stereo plug-in must process its
//! own channel pair and leave the remaining track channels untouched.
void ClapEffectIntegrationTest::testMultiChannelBusPassesThroughExtraChannels()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame pairA[fpp];
	SampleFrame pairB[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		pairA[f][0] = 0.5f;
		pairA[f][1] = 0.5f;
		pairB[f][0] = 0.25f;
		pairB[f][1] = -0.5f;
	}
	SampleFrame* busData[2] = { pairA, pairB };
	AudioBus bus{busData, 2, fpp}; // four track channels

	QVERIFY(chain.processAudioBuffer(bus));

	QVERIFY2(std::abs(pairA[0][0] - 0.25f) < 1e-6f,
		qPrintable(QStringLiteral("processed %1").arg(pairA[0][0])));
	QVERIFY2(std::abs(pairB[0][0] - 0.25f) < 1e-6f,
		qPrintable(QStringLiteral("passthrough %1").arg(pairB[0][0])));
	QVERIFY2(std::abs(pairB[0][1] + 0.5f) < 1e-6f,
		qPrintable(QStringLiteral("passthrough %1").arg(pairB[0][1])));
	qInfo("multichannel bus: pair 0 in 0.5 -> measured %.9f, pair 1 untouched %.9f/%.9f",
		pairA[0][0], pairB[0][0], pairB[0][1]);
}

void ClapEffectIntegrationTest::testStateRoundTripThroughMmp()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);

	gain->setValue(0.3f);

	// Deliver the change to the plug-in before saving. Parameter changes reach
	// the processor through processAudioBuffer(), exactly as in a running host.
	SampleFrame storage[fpp];
	auto fillInput = [&storage, fpp] {
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			storage[f][0] = 0.5f;
			storage[f][1] = 0.5f;
		}
	};
	fillInput();
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("delivered %1").arg(storage[0][0])));

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);

	gain->setValue(0.9f);
	QVERIFY(std::abs(gain->value() - 0.9f) < 1e-6f);

	effect->loadSettings(element);
	QVERIFY2(std::abs(gain->value() - 0.3f) < 1e-6f,
		qPrintable(QStringLiteral("restored %1").arg(gain->value())));

	fillInput();
	QVERIFY(chain.processAudioBuffer(bus));

	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("processed %1").arg(storage[0][0])));
	qInfo("state round trip: input 0.5, restored gain 0.3 -> measured out[0]=%.9f",
		storage[0][0]);
}

/*! stress-4 (b): a SUCCESSFUL clap.state load that does not bring a writable parameter back must not lose it.
 *  The stress run lost Surge XT's parameters this way, and an effect that has not heard audio yet loses every
 *  edit: the host hands a change to the plug-in only in process(), so a save before the next block writes the
 *  plug-in's old value into its state. Here Gain is set to 0.3 and the effect saved with no block in between, so
 *  the plug-in's state holds its default and the host's saved <parameters> hold 0.3. Loading it back leaves Gain
 *  at 0.3, and the next block applies it to the audio. */
void ClapEffectIntegrationTest::testAValueTheStateDoesNotCarryIsRestoredFromTheHost()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.3f);

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);
	QVERIFY2(!element.elementsByTagName(QStringLiteral("state")).isEmpty(),
		"the effect wrote no plug-in state: this case is about a SUCCESSFUL state load");

	gain->setValue(0.9f);
	effect->loadSettings(element);
	QVERIFY2(std::abs(gain->value() - 0.3f) < 1e-6f, qPrintable(QStringLiteral("restored %1").arg(gain->value())));

	SampleFrame storage[fpp];
	for (f_cnt_t frame = 0; frame < fpp; ++frame)
	{
		storage[frame][0] = 0.5f;
		storage[frame][1] = 0.5f;
	}
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f, qPrintable(QStringLiteral("processed %1").arg(storage[0][0])));
	chain.clear();
}

void ClapEffectIntegrationTest::testRendersBeforeAfterWav()
{
	constexpr f_cnt_t fpp = 48;
	constexpr int sampleRate = 44100;
	constexpr int totalFrames = sampleRate; // one second
	constexpr double frequency = 440.0;
	constexpr float amplitude = 0.5f;

	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);

	std::vector<float> dry(totalFrames, 0.0f);
	std::vector<float> wetUnity(totalFrames, 0.0f);
	std::vector<float> wetHalf(totalFrames, 0.0f);

	auto render = [&](std::vector<float>& wet, float gainValue) {
		gain->setValue(gainValue);
		for (int pos = 0; pos < totalFrames; pos += fpp)
		{
			// The final block is partial: only `blockFrames` frames belong to
			// the render. Writing past the end of dry/wet would be out of
			// bounds (and corrupts the heap in Debug builds), so clamp.
			const auto blockFrames = static_cast<f_cnt_t>(
				std::min(static_cast<int>(fpp), totalFrames - pos));
			SampleFrame storage[fpp]{};
			for (f_cnt_t f = 0; f < blockFrames; ++f)
			{
				const auto sample = amplitude * static_cast<float>(std::sin(
					2.0 * kPi * frequency * static_cast<double>(pos + f) / sampleRate));
				storage[f][0] = sample;
				storage[f][1] = sample;
				if (gainValue == 1.0f) { dry[static_cast<std::size_t>(pos) + f] = sample; }
			}

			SampleFrame* busData[1] = {storage};
			AudioBus bus{busData, 1, fpp};
			QVERIFY(chain.processAudioBuffer(bus));

			for (f_cnt_t f = 0; f < blockFrames; ++f)
			{
				wet[static_cast<std::size_t>(pos) + f] = storage[f][0];
			}
		}
	};

	render(wetUnity, 1.0f);
	render(wetHalf, 0.5f);

	QVERIFY(writeWav(renderedWavPath(QStringLiteral("clap_before.wav")), dry, sampleRate));
	QVERIFY(writeWav(renderedWavPath(QStringLiteral("clap_after.wav")), wetHalf, sampleRate));

	const auto dryRms = rms(dry);
	const auto unityRms = rms(wetUnity);
	const auto halfRms = rms(wetHalf);
	qInfo("wav render: 1 s @ %d Hz, 440 Hz sine amp 0.5 -> dry RMS=%.9f "
		  "unity gain RMS=%.9f ratio=%.6f, gain 0.5 RMS=%.9f ratio=%.6f",
		sampleRate, dryRms, unityRms, unityRms / dryRms, halfRms, halfRms / dryRms);
	QVERIFY2(std::abs(unityRms - dryRms) < 1e-6,
		qPrintable(QStringLiteral("unity %1 dry %2").arg(unityRms).arg(dryRms)));
	QVERIFY2(std::abs(halfRms - 0.5 * dryRms) < 1e-6,
		qPrintable(QStringLiteral("half %1 dry %2").arg(halfRms).arg(dryRms)));
}

//! The legacy single-buffer entry point must bridge ClapEffect's planar,
//! non-in-place port set through the audio ports router. Before task #607 the
//! bridge only compiled for in-place interleaved effects, so this path did not
//! exist for CLAP (and VST3) effects at all.
void ClapEffectIntegrationTest::testLegacyAudioBufferPathRoutesPlanarPorts()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	AudioBuffer buffer{fpp, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		buffer.interleavedBuffer()[f][0] = 0.5f;
		buffer.interleavedBuffer()[f][1] = -0.25f;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);

	// Non-virtual Effect::processAudioBuffer(AudioBuffer&) reaches the legacy
	// AudioPlugin::processImpl(SampleFrame*, f_cnt_t), which routes the buffer
	// through the ports router instead of constructing an interleaved view.
	// Call through the Effect interface: AudioPlugin's AudioBus override hides
	// the AudioBuffer overload on concrete plugin types.
	Effect& legacyEntry = *effect;
	QVERIFY(legacyEntry.processAudioBuffer(buffer));

	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(buffer.interleavedBuffer()[f][0] - 0.25f) < 1e-6f,
			qPrintable(QStringLiteral("legacy left %1").arg(buffer.interleavedBuffer()[f][0])));
		QVERIFY2(std::abs(buffer.interleavedBuffer()[f][1] + 0.125f) < 1e-6f,
			qPrintable(QStringLiteral("legacy right %1").arg(buffer.interleavedBuffer()[f][1])));
	}
	qInfo("legacy AudioBuffer path: input 0.5/-0.25, gain 0.5 -> measured out[0]=%.9f out[1]=%.9f",
		buffer.interleavedBuffer()[0][0], buffer.interleavedBuffer()[0][1]);
}

} // namespace lmms

/*! R4.3/R4.4 through the product's own path: a ClapEffect opens its plug-in's embedded
 *  clap.gui in a native window of its own (Plugin::openNativeEditor -> PluginNativeEditor ->
 *  the host's gui life cycle), the window takes the gui's size, and closing leaves nothing
 *  open. Offscreen: what is proved is the embed/size/close contract, not pixels. */
void lmms::ClapEffectIntegrationTest::testTheEffectsOwnEditorOpensInItsOwnWindow()
{
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->nativeEditorOpen());
	QString error;
	QVERIFY2(effect->openNativeEditor(&error), qPrintable(error));
	QVERIFY(effect->nativeEditorOpen());
	QWidget* window = nullptr;
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		if (top->isVisible() && top->windowTitle() == effect->displayName()) { window = top; }
	}
	QVERIFY2(window != nullptr, "no window titled after the effect is showing");
	// The plug-in's 400x300 view with the host header (0.5.0 MX-091) above it; the header's
	// buttons may make the window wider than the view, never narrower.
	const auto* header = window->findChild<QWidget*>(QStringLiteral("pluginHostHeader"));
	QVERIFY(header != nullptr);
	QCOMPARE(window->height(), 300 + header->sizeHint().height());
	QVERIFY(window->width() >= 400);
	effect->closeNativeEditor();
	QVERIFY(!effect->nativeEditorOpen());
}

/*! R4.4's surface for an effect: the generated control dialog carries a "Show plugin editor"
 *  button, and clicking it runs plugin.editor_open for the effect where it sits - here the
 *  master channel (ch-0) and its own fx-<n> - which opens the editor. */
void lmms::ClapEffectIntegrationTest::testTheControlDialogsButtonOpensTheEditor()
{
	EffectChain& master = Engine::mixer()->mixerChannel(0)->m_fxChain;
	const auto key = makeKey();
	auto* effect = new ClapEffect{&master, &key};
	master.appendEffect(effect);
	ControlRegistry::setReady(true);
	const bool headless = ControlRegistry::instance()->isHeadless();
	ControlRegistry::instance()->setHeadless(false);  // the offscreen platform is a display
	QWidget parent;
	gui::EffectControlDialog* dialog = effect->controls()->createView();
	dialog->setParent(&parent);
	QToolButton* show = nullptr;
	for (QToolButton* button : dialog->findChildren<QToolButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String("plugin.editor_open")) { show = button; }
	}
	QVERIFY2(show != nullptr, "the effect's control dialog has no plugin-editor button");
	QTest::mouseClick(show, Qt::LeftButton);
	const bool opened = effect->nativeEditorOpen();
	effect->closeNativeEditor();
	ControlRegistry::instance()->setHeadless(headless);
	ControlRegistry::setReady(false);
	master.removeEffect(effect);
	delete effect;
	QVERIFY2(opened, "the dialog's button did not open the editor");
}

/*! Q050-490 through the product's own button: a plug-in whose editor embeds only in some OTHER
 *  platform's window is refused - the dialog's "Show plugin editor" runs plugin.editor_open, which
 *  refuses typed, and the refusal line under the button names this platform and its CLAP window API.
 *  No editor is open and no window titled after the effect is left on screen. */
void lmms::ClapEffectIntegrationTest::testTheButtonNamesThePlatformAPluginCannotEmbedOn()
{
	const PluginWindowSystem native = currentPluginWindowSystem();
	const char* nativeApi = native == PluginWindowSystem::Win32 ? "win32" : native == PluginWindowSystem::Cocoa ? "cocoa" : "x11";
	qputenv("CLAP_TEST_GAIN_GUI_API", native == PluginWindowSystem::Win32 ? "x11" : "win32");
	EffectChain& master = Engine::mixer()->mixerChannel(0)->m_fxChain;
	const auto key = makeKey();
	auto* effect = new ClapEffect{&master, &key};
	master.appendEffect(effect);
	ControlRegistry::setReady(true);
	const bool headless = ControlRegistry::instance()->isHeadless();
	ControlRegistry::instance()->setHeadless(false);  // the offscreen platform is a display
	QWidget parent;
	gui::EffectControlDialog* dialog = effect->controls()->createView();
	dialog->setParent(&parent);
	QToolButton* show = nullptr;
	for (QToolButton* button : dialog->findChildren<QToolButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String("plugin.editor_open")) { show = button; }
	}
	QVERIFY2(show != nullptr, "the effect's control dialog has no plugin-editor button");
	QTest::mouseClick(show, Qt::LeftButton);
	const bool opened = effect->nativeEditorOpen();
	QString reason;
	for (const QLabel* label : show->parentWidget()->findChildren<QLabel*>())
	{
		if (!label->isHidden()) { reason = label->text(); }
	}
	bool windowLeft = false;
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		if (top->isVisible() && top->windowTitle() == effect->displayName()) { windowLeft = true; }
	}
	effect->closeNativeEditor();
	ControlRegistry::instance()->setHeadless(headless);
	ControlRegistry::setReady(false);
	master.removeEffect(effect);
	delete effect;
	qunsetenv("CLAP_TEST_GAIN_GUI_API");
	QVERIFY2(!opened, "an editor that cannot embed here reported open");
	QVERIFY2(!windowLeft, "a refused editor left a window on screen");
	const QString expected = QStringLiteral("cannot embed on %1 (CLAP window API %2)").arg(pluginWindowSystemName(native),
		QLatin1String(nativeApi));
	QVERIFY2(reason.contains(expected), qPrintable(QStringLiteral("refusal line: '%1'").arg(reason)));
	std::printf("CLAP_EDITOR_BUTTON_REFUSAL_EVIDENCE %s\n", qPrintable(reason));
}

/*! BUGS 11.34: the latency a CLAP plug-in declares (clap.latency) is what the effect reports
 *  and what its chain adds up for delay compensation - before the fix every hosted plug-in
 *  read as zero-latency. */
void lmms::ClapEffectIntegrationTest::testThePluginsLatencyReachesTheChain()
{
	BufferManager::init(48);
	qputenv("CLAP_TEST_GAIN_LATENCY", "123");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	qunsetenv("CLAP_TEST_GAIN_LATENCY");
	QCOMPARE(effect->latencyFrames(), 123);
	QCOMPARE(chain.latencyFrames(), 123);
	chain.clear();
}

/*! Engine audit E6: a plug-in's declared tail lengthens the silence an effect waits through
 *  before it stops processing, so a reverb's tail is not cut by the Decay setting. */
void lmms::ClapEffectIntegrationTest::testThePluginsTailKeepsItRunning()
{
	BufferManager::init(48);
	qputenv("CLAP_TEST_GAIN_TAIL", "96000");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	qunsetenv("CLAP_TEST_GAIN_TAIL");
	QCOMPARE(effect->tailFrames(), 96000);
	const auto periods = static_cast<f_cnt_t>(96000 / Engine::audioEngine()->framesPerPeriod());
	QVERIFY2(effect->timeout() > periods, "the auto-quit wait covers the declared tail");
	chain.clear();
}

/*! W6: a parameter rescan rebuilds the effect's models and keeps what is connected to them. An info rescan renames
 *  the Gain model in place; a full rescan (a restart, then the list re-read deactivated) keeps the Gain model - the
 *  same object, so the automation clip on it still drives it - deletes the model of the removed Bypass switch, and
 *  adds models for Drive and Tone whose changes reach the plug-in. Before, the models stayed as they were loaded. */
void lmms::ClapEffectIntegrationTest::testParameterRescansRebuildTheModels()
{
	BufferManager::init(48);
	qputenv("CLAP_TEST_GAIN_RESCAN", "info");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	const QPointer<ClapParamModel> gain = controls->modelForParam(1);
	const QPointer<ClapParamModel> bypass = controls->modelForParam(2);
	QVERIFY(gain != nullptr && bypass != nullptr);
	QCOMPARE(gain->objectName(), QStringLiteral("clap-1"));  // the stable key its alias is kept by
	auto* clip = dynamic_cast<AutomationClip*>(Track::create(Track::Type::Automation, Engine::getSong())->createClip(TimePos(0)));
	QVERIFY(clip != nullptr);
	QVERIFY(clip->addObject(gain));
	QSignalSpy rebuilt(controls, &ClapEffectControls::parametersChanged);
	QVERIFY(QMetaObject::invokeMethod(controls, "poll"));  // the plug-in renames Gain: an info rescan
	QCOMPARE(rebuilt.count(), 1);
	QCOMPARE(controls->modelForParam(1), gain.data());
	QCOMPARE(gain->displayName(), QStringLiteral("Level"));

	qputenv("CLAP_TEST_GAIN_RESCAN", "all");
	effect->reprepare();  // activate() asks for the main-thread call in which the plug-in asks for a restart
	QVERIFY(QMetaObject::invokeMethod(controls, "poll"));  // the restart: deactivated, the list is re-read
	qunsetenv("CLAP_TEST_GAIN_RESCAN");
	QCOMPARE(rebuilt.count(), 2);
	QCOMPARE(controls->paramModels().size(), std::size_t{3});
	QCOMPARE(controls->modelForParam(1), gain.data());
	QVERIFY(bypass.isNull());
	QCOMPARE(clip->objects().size(), std::size_t{1});
	QCOMPARE(clip->objects().front().data(), static_cast<AutomatableModel*>(gain.data()));
	ClapParamModel* drive = controls->modelForParam(3);
	QVERIFY(drive != nullptr);
	QCOMPARE(drive->maxValue(), 10.f);
	QCOMPARE(drive->value(), 2.f);
	drive->setValue(4.f);
	QCOMPARE(effect->plugin()->paramPlain(3), 4.0);

	gain->setValue(0.5f);
	SampleFrame storage[48];
	for (auto& frame : storage) { frame = SampleFrame(0.5f, 0.5f); }
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, 48};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.25f) < 1e-6f, qPrintable(QString::number(storage[0][0])));
	chain.clear();
}

/*! stress-4 (a): a CLAP_PARAM_IS_READONLY parameter is the plug-in's to set, so the control surface's parameter
 *  object - what plugin.param_get and dsp.get_state report - says read_only, and plugin.param_set refuses it
 *  (the refusal is the shared one Vst3InstrumentIntegrationTest drives). The writable Gain says nothing new. */
void lmms::ClapEffectIntegrationTest::testReadOnlyParametersAreReportedReadOnly()
{
	BufferManager::init(48);
	qputenv("CLAP_TEST_GAIN_READONLY", "1");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	qunsetenv("CLAP_TEST_GAIN_READONLY");
	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* readOnly = controls->modelForParam(2);
	QVERIFY(readOnly != nullptr && readOnly->descriptor().readOnly);

	const QList<AutomatableModel*> models = controlEffectParameters(effect);
	const int readOnlyIndex = models.indexOf(readOnly);
	QVERIFY(readOnlyIndex >= 0);
	QVERIFY2(controlParameterJson(readOnly, readOnlyIndex).value(QStringLiteral("read_only")).toBool(),
		"the CLAP_PARAM_IS_READONLY parameter is not reported read_only");
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr && !gain->descriptor().readOnly);
	QVERIFY2(!controlParameterJson(gain, static_cast<int>(models.indexOf(gain))).contains(QStringLiteral("read_only")),
		"the writable Gain is reported read-only");
	chain.clear();
}

/*! BUGS 1902: a read-only CLAP parameter (CLAP_PARAM_IS_READONLY) ignores every write, so the knob the
 *  dialog used to build for it moved and snapped back on the next poll. The grid now skips it. */
void lmms::ClapEffectIntegrationTest::testReadOnlyParametersGetNoKnob()
{
	BufferManager::init(48);
	qputenv("CLAP_TEST_GAIN_READONLY", "1");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	qunsetenv("CLAP_TEST_GAIN_READONLY");
	auto* bypass = controls->modelForParam(2);
	QVERIFY(bypass != nullptr);
	QVERIFY(bypass->descriptor().readOnly);

	QWidget parent;
	QGridLayout grid;
	const auto rows = gui::fillClapKnobGrid(&grid, controls->paramModels(), &parent,
		QStringLiteral("no parameters"));
	QCOMPARE(rows, 1);  // one row: only Gain is left
	auto knobCount = 0;
	auto* first = static_cast<gui::Knob*>(nullptr);
	for (int i = 0; i < grid.count(); ++i)
	{
		auto* knob = dynamic_cast<gui::Knob*>(grid.itemAt(i)->widget());
		if (knob == nullptr) { continue; }
		++knobCount;
		if (first == nullptr) { first = knob; }
	}
	QCOMPARE(knobCount, 1);
	QVERIFY(first != nullptr);
	QCOMPARE(first->getLabel(), QStringLiteral("Gain"));
	chain.clear();
}

/*! BUGS 5514: a plug-in that reports CLAP_PROCESS_ERROR leaves the rest of the output block as the host
 *  handed it over, so the effect must hand the chain the dry signal instead of mixing stale wet audio. */
void lmms::ClapEffectIntegrationTest::testAProcessingErrorFallsBackToDryAudio()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);
	qputenv("CLAP_TEST_GAIN_PROCESS_ERROR", "1");
	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	controls->modelForParam(1)->setValue(0.5f);  // a successful block would come out at 0.25

	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = 0.5f;
		storage[f][1] = -0.25f;
	}
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	qunsetenv("CLAP_TEST_GAIN_PROCESS_ERROR");

	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(storage[f][0] - 0.5f) < 1e-6f,
			qPrintable(QStringLiteral("left %1 is not the dry signal").arg(storage[f][0])));
		QVERIFY2(std::abs(storage[f][1] + 0.25f) < 1e-6f,
			qPrintable(QStringLiteral("right %1 is not the dry signal").arg(storage[f][1])));
	}
	chain.clear();
}

namespace lmms
{

void ClapEffectIntegrationTest::testScanFailuresAreReported()
{
	// A module that is not a usable CLAP module has to be SAYED to be one: the scan
	// used to fill the error QString that listClasses writes and then throw it away,
	// so a broken .clap simply vanished from the browser with no reason anywhere.
	const auto modulePath = QString::fromLatin1(CLAP_TEST_BROKEN_NO_SYMBOL_PATH);
	if (!QFileInfo::exists(modulePath))
	{
		QSKIP("no broken CLAP test module available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers)");
	}

	auto* config = ConfigManager::inst();
	const auto previousVstDir = config->vstDir();
	config->setVSTDir(QFileInfo{modulePath}.absolutePath());

	QStringList reported;
	{
		WarningCapture capture;
		ClapSubPluginFeatures features{Plugin::Type::Effect};
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		features.listSubPluginKeys(nullptr, keys);
		reported = capture.warnings();
	}
	config->setVSTDir(previousVstDir);

	// The reason the typed ladder gives for this fixture, and the module it is about:
	// a scan that says neither cannot be told apart from a scan that never looked.
	const auto moduleName = QFileInfo{modulePath}.fileName();
	auto named = false;
	for (const auto& line : reported)
	{
		if (line.contains(moduleName) && line.contains(QStringLiteral("symbol-missing"))) { named = true; }
	}
	QVERIFY2(named,
		qPrintable(QStringLiteral("the scan reported %1 warning(s), none naming %2 and its typed reason: %3")
			.arg(reported.size()).arg(moduleName, reported.join(QStringLiteral(" | ")))));
}

} // namespace lmms

QTEST_MAIN(lmms::ClapEffectIntegrationTest)

#include "ClapEffectIntegrationTest.moc"
