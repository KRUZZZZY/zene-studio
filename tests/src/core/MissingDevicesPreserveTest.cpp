/*
 * MissingDevicesPreserveTest.cpp - Q050-018: a project with unavailable instruments and effects keeps their
 * XML through open -> save -> reopen, and names each placeholder (name, format, vendor)
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

/*! The real in-tree modules (kicker, amplifier, and the VST3/CLAP/LV2 hosts) are copied into test-local plug-in
 *  directories: one that has them and one that does not. No LMMS_PLUGIN_DIR is exported; the factory's search
 *  path is pinned per phase with QDir::setSearchPaths, exactly as PluginScanCacheTest does.
 *    - module missing: a project saved with kicker/amplifier is opened with a directory that lacks them;
 *    - format binary missing: the VST3/CLAP/LV2 host module is present but the plug-in it names is not.
 *  The saved XML must equal the original; the plug-in directory restored, the instrument comes back with its
 *  original state. */

#include <QtTest>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QMessageBox>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "DummyEffect.h"
#include "DummyInstrument.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "AudioBusHandle.h"
#include "InstrumentTrack.h"
#include "MissingDevices.h"
#include "MissingDevicesNotice.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

QString readAll(const QString& path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString canon(const QDomNode& node)
{
	QString out;
	QTextStream stream(&out);
	node.save(stream, 0);
	stream.flush();
	return out;
}

QString stripIds(QString text)
{
	static const QRegularExpression idPattern(" id=\"[0-9]+\"");
	return text.remove(idPattern);
}

//! The first <instrument> element of the first track in the project file.
QDomElement instrumentOf(const QDomDocument& doc)
{
	const QDomNodeList list = doc.elementsByTagName("instrument");
	return list.isEmpty() ? QDomElement() : list.item(0).toElement();
}

QDomElement effectOf(const QDomDocument& doc)
{
	const QDomNodeList list = doc.elementsByTagName("effect");
	return list.isEmpty() ? QDomElement() : list.item(0).toElement();
}

QDomDocument parse(const QString& path)
{
	QDomDocument doc;
	doc.setContent(readAll(path));
	return doc;
}

InstrumentTrack* firstInstrumentTrack()
{
	for (Track* t : Engine::getSong()->tracks())
	{
		if (auto* it = dynamic_cast<InstrumentTrack*>(t)) { return it; }
	}
	return nullptr;
}

//! A device whose plug-in binary is missing: the wrapper a hosted plug-in
//! leaves behind, with the file's own XML kept beside it.
class UnavailableTestEffect : public Effect
{
public:
	UnavailableTestEffect(Model* parent) :
		Effect{&s_descriptor, parent, nullptr},
		m_controls(this)
	{
	}

	EffectControls* controls() override { return &m_controls; }

	QString unavailableReason() const override
	{
		return QStringLiteral("the plug-in binary is not available");
	}

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Sleep; }

private:
	DummyEffectControls m_controls;

	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor UnavailableTestEffect::s_descriptor
{
	"unavailabletesteffect",
	"Unavailable test effect",
	"A device whose plug-in binary is missing",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

//! The XML a file held for a device whose plug-in is missing.
QDomElement preservedDeviceXml(QDomDocument& file)
{
	if (!file.setContent(QStringLiteral(
			"<effect name=\"ghost\" wet=\"1\" on=\"1\"><ghost><state>QUJD</state></ghost></effect>")))
	{
		return QDomElement();
	}
	return file.documentElement();
}

} // namespace

class MissingDevicesPreserveTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QTemporaryDir m_full;    //!< plug-in directory holding kicker, amplifier and the format hosts
	QTemporaryDir m_without; //!< plug-in directory with none of them
	QString m_original;      //!< the project saved with the real devices
	QString m_kickerXml;     //!< its <instrument> element, canonical
	QString m_ampXml;        //!< its <effect> element, canonical

	void pin(const QTemporaryDir& dir)
	{
		QDir::setSearchPaths("plugins", QStringList{dir.path()});
		getPluginFactory()->discoverPlugins();
	}

	static QStringList moduleNames()
	{
		QStringList names{QStringLiteral(LMMS_TEST_MODULE_KICKER), QStringLiteral(LMMS_TEST_MODULE_AMPLIFIER)};
#ifdef LMMS_TEST_MODULE_VST3INSTRUMENT
		names << QStringLiteral(LMMS_TEST_MODULE_VST3INSTRUMENT);
#endif
#ifdef LMMS_TEST_MODULE_CLAPINSTRUMENT
		names << QStringLiteral(LMMS_TEST_MODULE_CLAPINSTRUMENT);
#endif
#ifdef LMMS_TEST_MODULE_LV2INSTRUMENT
		names << QStringLiteral(LMMS_TEST_MODULE_LV2INSTRUMENT);
#endif
#ifdef LMMS_TEST_MODULE_VST3EFFECT
		names << QStringLiteral(LMMS_TEST_MODULE_VST3EFFECT);
#endif
#ifdef LMMS_TEST_MODULE_CLAPEFFECT
		names << QStringLiteral(LMMS_TEST_MODULE_CLAPEFFECT);
#endif
		return names;
	}

	//! Replaces the project's first <instrument> element with one built from @a inner and writes it to @a path.
	void writeVariant(const QString& path, const QString& instrumentXml)
	{
		QDomDocument doc = parse(m_original);
		QDomElement old = instrumentOf(doc);
		QDomDocument frag;
		QVERIFY(frag.setContent(instrumentXml));
		old.parentNode().replaceChild(doc.importNode(frag.documentElement(), true), old);
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(doc.toString().toUtf8());
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid() && m_full.isValid() && m_without.isValid());
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_dir.filePath(QStringLiteral("plugin-scan-cache.json")).toLocal8Bit());
		Engine::init(true);
		ControlRegistry::setReady(true);
		const QString src = QStringLiteral(LMMS_TEST_PLUGIN_DIR);
		int copied = 0;
		for (const QString& name : moduleNames())
		{
			if (QFile::exists(src + "/" + name) && QFile::copy(src + "/" + name, m_full.filePath(name))) { ++copied; }
		}
		QVERIFY2(QFile::exists(m_full.filePath(QStringLiteral(LMMS_TEST_MODULE_KICKER)))
			&& QFile::exists(m_full.filePath(QStringLiteral(LMMS_TEST_MODULE_AMPLIFIER))),
			"the real kicker/amplifier modules must be built and copied");
		qInfo() << "modules copied into the full fixture:" << copied;

		// Phase A: a project saved with the real devices.
		pin(m_full);
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Instrument* kicker = track->loadInstrument("kicker");
		QVERIFY(kicker != nullptr);
		track->setName("Kick Track");  // after loadInstrument, which names the track after the instrument
		QVERIFY(dynamic_cast<DummyInstrument*>(kicker) == nullptr);
		Effect* amp = Effect::instantiate("amplifier", track->audioBusHandle()->effects(), nullptr);
		QVERIFY(amp != nullptr);
		track->audioBusHandle()->effects()->appendEffect(amp);

		m_original = m_dir.filePath("original.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(m_original));
		const QDomDocument doc = parse(m_original);
		QVERIFY(!instrumentOf(doc).isNull());
		QVERIFY(!effectOf(doc).isNull());
		m_kickerXml = canon(instrumentOf(doc));
		m_ampXml = canon(effectOf(doc));
		QVERIFY(m_kickerXml.contains("name=\"kicker\""));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		QDir::setSearchPaths("plugins", QStringList{});
		Engine::destroy();
	}

	//! Criterion 1, native modules: hidden module -> placeholder -> save -> the original XML is still there.
	void missingModuleKeepsTheOriginalXmlThroughSaveAndReopen()
	{
		pin(m_without);
		Engine::getSong()->loadProject(m_original);
		InstrumentTrack* track = firstInstrumentTrack();
		QVERIFY(track != nullptr);
		QVERIFY2(dynamic_cast<DummyInstrument*>(track->instrument()) != nullptr, "kicker must be a placeholder");
		QVERIFY(!track->preservedInstrument().isNull());

		const QString second = m_dir.filePath("second.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(second));
		const QDomDocument doc2 = parse(second);
		QCOMPARE(canon(instrumentOf(doc2)), m_kickerXml);
		QCOMPARE(canon(effectOf(doc2)), m_ampXml);

		// and again through a second open + save: stable, not lossless once
		Engine::getSong()->loadProject(second);
		const QString third = m_dir.filePath("third.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(third));
		const QDomDocument doc3 = parse(third);
		QCOMPARE(canon(instrumentOf(doc3)), m_kickerXml);
		QCOMPARE(canon(effectOf(doc3)), m_ampXml);

		// The module comes back: the instrument and effect are real again, with their original state.
		pin(m_full);
		Engine::getSong()->loadProject(third);
		track = firstInstrumentTrack();
		QVERIFY(track != nullptr);
		QVERIFY(dynamic_cast<DummyInstrument*>(track->instrument()) == nullptr);
		QVERIFY(track->preservedInstrument().isNull());
		QCOMPARE(QString::fromUtf8(track->instrument()->descriptor()->name), QString("kicker"));
		QVERIFY(missingdevices::collect(Engine::getSong()).isEmpty());
		const QString fourth = m_dir.filePath("fourth.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(fourth));
		const QDomDocument doc4 = parse(fourth);
		QCOMPARE(stripIds(canon(instrumentOf(doc4))), stripIds(m_kickerXml));
		QCOMPARE(stripIds(canon(effectOf(doc4))), stripIds(m_ampXml));
	}

	//! Criterion 2: the same list in project.open's reply and project.get_state, with name, format and vendor.
	void projectOpenAndGetStateListEachPlaceholder()
	{
		pin(m_without);
		const ControlResult opened = ControlRegistry::instance()->invoke("project.open", {{"path", m_original}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		const QJsonObject r = opened.result;
		QCOMPARE(r.value("missing_device_count").toInt(), 2);
		const QJsonArray list = r.value("missing_devices").toArray();
		QCOMPARE(list.size(), 2);
		const QJsonObject inst = list.at(0).toObject();
		QCOMPARE(inst.value("kind").toString(), QString("instrument"));
		QCOMPARE(inst.value("name").toString(), QString("kicker"));
		QCOMPARE(inst.value("format").toString(), QString("native"));
		QCOMPARE(inst.value("vendor").toString(), QString("LMMS"));
		QCOMPARE(inst.value("track").toString(), QString("Kick Track"));
		const QJsonObject fx = list.at(1).toObject();
		QCOMPARE(fx.value("kind").toString(), QString("effect"));
		QCOMPARE(fx.value("name").toString(), QString("amplifier"));
		// a placeholder is not a load error: the open is not marked errored
		QVERIFY(!r.value("loaded_with_errors").toBool());

		const ControlResult state = ControlRegistry::instance()->invoke("project.get_state", {});
		QVERIFY(state.ok);
		QCOMPARE(state.result.value("missing_device_count").toInt(), 2);
		QCOMPARE(state.result.value("missing_devices").toArray(), list);

		// the user replaces the placeholder with a real instrument: the list shrinks
		pin(m_full);
		InstrumentTrack* track = firstInstrumentTrack();
		QVERIFY(track != nullptr);
		track->loadInstrument("kicker");
		QCOMPARE(missingdevices::collect(Engine::getSong()).size(), 1);
	}

	//! A project with every device available reports nothing and saves exactly what it did before.
	void presentDevicesAreUntouched()
	{
		pin(m_full);
		const ControlResult opened = ControlRegistry::instance()->invoke("project.open", {{"path", m_original}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(opened.result.value("missing_device_count").toInt(), 0);
		QVERIFY(opened.result.value("missing_devices").toArray().isEmpty());
		const QString again = m_dir.filePath("again.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(again));
		const QDomDocument doc = parse(again);
		QCOMPARE(stripIds(canon(instrumentOf(doc))), stripIds(m_kickerXml));
	}

	//! Each format whose host module is present but whose plug-in binary is not.
	void hostedFormatsWithAMissingBinaryKeepTheirXml_data()
	{
		QTest::addColumn<QString>("xml");
		QTest::addColumn<QString>("format");
		QTest::addColumn<QString>("name");
		QTest::addColumn<QString>("vendor");
		QTest::newRow("vst3") << QString(
			"<instrument name=\"vst3instrument\" vendor=\"Ghost Audio\"><vst3instrument><componentstate>QUJD</componentstate>"
			"<key><attribute name=\"file\" value=\"/nonexistent/Ghost.vst3\"/><attribute name=\"class\" value=\"GhostSynth\"/></key>"
			"</vst3instrument></instrument>") << "vst3" << "GhostSynth" << "Ghost Audio";
		QTest::newRow("clap") << QString(
			"<instrument name=\"clapinstrument\"><clapinstrument><state>QUJD</state>"
			"<key><attribute name=\"file\" value=\"/nonexistent/ghost.clap\"/><attribute name=\"id\" value=\"com.ghost.synth\"/>"
			"<attribute name=\"name\" value=\"Ghost CLAP\"/></key></clapinstrument></instrument>")
			<< "clap" << "Ghost CLAP" << "unknown";
		QTest::newRow("lv2") << QString(
			"<instrument name=\"lv2instrument\"><lv2instrument><key><attribute name=\"uri\" value=\"http://ghost.example.org/plugins/ghostsynth\"/></key>"
			"</lv2instrument></instrument>") << "lv2" << "ghostsynth" << "ghost.example.org";
	}

	void hostedFormatsWithAMissingBinaryKeepTheirXml()
	{
		QFETCH(QString, xml);
		QFETCH(QString, format);
		QFETCH(QString, name);
		QFETCH(QString, vendor);
		pin(m_full);
		const QString variant = m_dir.filePath("variant-" + format + ".mmp");
		writeVariant(variant, xml);
		QDomDocument frag;
		QVERIFY(frag.setContent(xml));
		const QString original = canon(frag.documentElement());

		const ControlResult opened = ControlRegistry::instance()->invoke("project.open", {{"path", variant}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		InstrumentTrack* track = firstInstrumentTrack();
		QVERIFY(track != nullptr);
		QVERIFY2(!track->preservedInstrument().isNull(), "a missing hosted binary must leave a placeholder");
		QVERIFY(!track->instrument()->unavailableReason().isEmpty());
		if (format != QLatin1String("lv2"))
		{
			QVERIFY2(dynamic_cast<DummyInstrument*>(track->instrument()) == nullptr,
				"the host module itself must be present for a missing-binary test row");
		}

		const QJsonArray list = opened.result.value("missing_devices").toArray();
		QVERIFY2(!list.isEmpty(), "the open reply lists the placeholder");
		const QJsonObject d = list.at(0).toObject();
		QCOMPARE(d.value("format").toString(), format);
		QCOMPARE(d.value("name").toString(), name);
		QCOMPARE(d.value("vendor").toString(), vendor);

		const QString out = m_dir.filePath("out-" + format + ".mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(out));
		QCOMPARE(canon(instrumentOf(parse(out))), original);
	}

	//! A hosted effect (VST3 / CLAP) whose host module is present but whose plug-in binary is not.
	void hostedEffectWithAMissingBinaryKeepsItsXml_data()
	{
		QTest::addColumn<QString>("xml");
		QTest::addColumn<QString>("format");
		QTest::addColumn<QString>("name");
		QTest::newRow("vst3") << QString(
			"<effect name=\"vst3effect\" vendor=\"Ghost Audio\" wet=\"1\" gate=\"0\" autoquit=\"1000\" enabled=\"1\">"
			"<vst3effect><componentstate>QUJD</componentstate>"
			"<key><attribute name=\"file\" value=\"/nonexistent/GhostFx.vst3\"/><attribute name=\"class\" value=\"GhostFx\"/></key>"
			"</vst3effect></effect>") << "vst3" << "GhostFx";
		QTest::newRow("clap") << QString(
			"<effect name=\"clapeffect\" wet=\"1\" gate=\"0\" autoquit=\"1000\" enabled=\"1\"><clapeffect><state>QUJD</state>"
			"<key><attribute name=\"file\" value=\"/nonexistent/ghostfx.clap\"/><attribute name=\"id\" value=\"com.ghost.fx\"/>"
			"<attribute name=\"name\" value=\"Ghost FX\"/></key></clapeffect></effect>") << "clap" << "Ghost FX";
	}

	void hostedEffectWithAMissingBinaryKeepsItsXml()
	{
		QFETCH(QString, xml);
		QFETCH(QString, format);
		QFETCH(QString, name);
		pin(m_full);
		QDomDocument doc = parse(m_original);
		QDomElement old = effectOf(doc);
		QDomDocument frag;
		QVERIFY(frag.setContent(xml));
		const QString original = canon(frag.documentElement());
		old.parentNode().replaceChild(doc.importNode(frag.documentElement(), true), old);
		const QString variant = m_dir.filePath("fx-" + format + ".mmp");
		QFile f(variant);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(doc.toString().toUtf8());
		f.close();

		const ControlResult opened = ControlRegistry::instance()->invoke("project.open", {{"path", variant}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QJsonObject effect;
		for (const QJsonValue& v : opened.result.value("missing_devices").toArray())
		{
			if (v.toObject().value("kind").toString() == "effect") { effect = v.toObject(); }
		}
		QVERIFY2(!effect.isEmpty(), "the missing hosted effect is listed");
		QCOMPARE(effect.value("format").toString(), format);
		QCOMPARE(effect.value("name").toString(), name);

		const QString out = m_dir.filePath("fx-out-" + format + ".mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(out));
		QCOMPARE(canon(effectOf(parse(out))), original);
	}

	//! The old compat format (no <instrument> wrapper, the module's own element) also survives.
	void compatFormatPlaceholderIsKept()
	{
		pin(m_without);
		QDomDocument doc = parse(m_original);
		QDomElement old = instrumentOf(doc);
		QDomDocument frag;
		QVERIFY(frag.setContent(QString("<ghostsynth vol=\"55\"><param a=\"1\"/></ghostsynth>")));
		old.parentNode().replaceChild(doc.importNode(frag.documentElement(), true), old);
		const QString path = m_dir.filePath("compat.mmp");
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(doc.toString().toUtf8());
		f.close();
		Engine::getSong()->loadProject(path);
		const auto devices = missingdevices::collect(Engine::getSong());
		QVERIFY(!devices.isEmpty());
		QCOMPARE(devices.at(0).name, QString("ghostsynth"));
		const QString out = m_dir.filePath("compat-out.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(out));
		const QDomNodeList kept = parse(out).elementsByTagName("ghostsynth");
		QCOMPARE(kept.size(), 1);
		QCOMPARE(kept.item(0).toElement().attribute("vol"), QString("55"));
	}

	//! The GUI half: one box, non-modal, with every placeholder named; nothing for an empty list.
	void noticeIsOneNonModalBox()
	{
		pin(m_without);
		Engine::getSong()->loadProject(m_original);
		const auto devices = missingdevices::collect(Engine::getSong());
		QCOMPARE(devices.size(), 2);
		QVERIFY(gui::showMissingDevicesNotice(nullptr, {}) == nullptr);
		QMessageBox* box = gui::showMissingDevicesNotice(nullptr, devices);
		QVERIFY(box != nullptr);
		QVERIFY(!box->isModal());
		QVERIFY(box->windowModality() == Qt::NonModal);
		const QString text = box->text();
		QVERIFY(text.contains("kicker"));
		QVERIFY(text.contains("amplifier"));
		QVERIFY(text.contains("native"));
		QVERIFY(text.contains("LMMS"));
		QCOMPARE(text.count("could not be loaded"), 1);
		box->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

	//! new-p14-97: the bypass the user set on a device whose plug-in binary is
	//! missing is saved with the file's own XML, not dropped by it.
	void theGenericSettingsOfAnUnavailableDeviceAreSaved()
	{
		EffectChain chain{nullptr};
		auto* device = new UnavailableTestEffect(&chain);
		chain.appendEffect(device);

		QDomDocument file;
		device->setPreservedPluginData(preservedDeviceXml(file));
		// the user bypasses the placeholder while the plug-in is missing
		device->setEnabled(false);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = chainElement.firstChildElement(QStringLiteral("effect"));
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		// the device's own XML is kept ...
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
		// ... and the generic settings the user changed are written with it
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("0"));
	}

	//! The same for the placeholder the chain makes for a device whose module
	//! is missing: the file's XML is kept as it was, and the bypass the user
	//! set on the placeholder is saved with it.
	void theGenericSettingsOfAMissingDeviceAreSaved()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);
		// the user bypasses the placeholder
		placeholder->setEnabled(false);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = chainElement.firstChildElement(QStringLiteral("effect"));
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		// the device's own XML is kept ...
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
		// ... and the generic settings the user changed are written with it
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("0"));
	}

	//! A generic setting that is not the bypass is written the same way: the
	//! auto-quit knob the user turned on the placeholder survives the save.
	void theAutoQuitOfAMissingDeviceIsSaved()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);
		// the user sets the placeholder's auto-quit knob
		placeholder->autoQuitModel()->setValue(42);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = chainElement.firstChildElement(QStringLiteral("effect"));
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		QCOMPARE(saved.attribute(QStringLiteral("autoquit")), QStringLiteral("42"));
		// the device's own XML is still kept
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
	}

	//! A placeholder nobody touched keeps the file's bytes exactly: the generic
	//! settings it holds are the ones the file said, so nothing is written over
	//! them.
	void anUntouchedPlaceholderKeepsTheFilesXml()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = chainElement.firstChildElement(QStringLiteral("effect"));
		QVERIFY(!saved.isNull());
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("1"));
		QCOMPARE(saved.attribute(QStringLiteral("wet")), QStringLiteral("1"));
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
	}
};

QTEST_MAIN(MissingDevicesPreserveTest)
#include "MissingDevicesPreserveTest.moc"
