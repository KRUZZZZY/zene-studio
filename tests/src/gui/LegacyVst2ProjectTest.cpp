/*
	* LegacyVst2ProjectTest.cpp - legacy VST2 project and reference render.
	*
	* Copyright (c) 2026 Zene Studio contributors
	*
	* This file is part of LMMS - https://lmms.io
	*
	* This program is free software; you can redistribute it and/or
	* modify it under the terms of the GNU General Public License as published
	* by the Free Software Foundation; either version 2 of the License, or
	* (at your option) any later version.
	*
	* This program is distributed in the hope that it will be useful,
	* but WITHOUT ANY WARRANTY; without even the implied warranty of
	* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
	* General Public License for more details.
	*
	* You should have received a copy of the GNU General Public License along
	* with this program (see COPYING); if not, write to the Free Software
	* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
	*/

#include "CarlaVst2EffectTestSupport.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "EffectControls.h"
#include "LegacyVst2Conversion.h"
#include "OfflineRenderBarrier.h"
#include <functional>
#include <QJsonDocument>

namespace
{
FloatModel* model(QObject* parent, const QString& name)
{
	for (auto* item : parent->findChildren<FloatModel*>()) { if (item->displayName() == name) { return item; } }
	return nullptr;
}

QList<Rendered> renderBars(const QList<double>& bars)
{
	QList<Rendered> result;
	Song* song = Engine::getSong();
	song->setPlayPos(0, Song::PlayMode::Song);
	MidiPlayback::instance().stopped();
	song->playSong();
	QElapsedTimer timer; timer.start();
	while (result.size() < bars.size() && timer.elapsed() < 20000)
	{
		offlinerender::waitForAll();
		const auto frames = Engine::audioEngine()->renderNextPeriod();
		if (song->getPlayPos(Song::PlayMode::Song).getTicks() > bars[result.size()] * DefaultTicksPerBar)
		{
			const auto& frame = frames.back();
			result.append({true, frame.left(), frame.right()});
		}
		QCoreApplication::processEvents();
	}
	song->stop();
	for (int i = 0; i < 8; ++i) { offlinerender::waitForAll(); Engine::audioEngine()->renderNextPeriod(); }
	return result;
}
}

class LegacyVst2ProjectTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_home, m_plugins, m_vst, m_variants;
	QString missing() const
	{
		for (const auto& path : {nativeFixture(), effectNativeFixture(), QDir(m_plugins.path()).filePath(QStringLiteral("NativeLinuxRemoteVstPlugin64"))})
			if (!QFileInfo::exists(path)) { return QStringLiteral("missing native fixture or remote host: %1").arg(path); }
		return {};
	}
	InstrumentTrack* firstInstrumentTrack()
	{
		for (auto* item : Engine::getSong()->tracks())
		{
			if (auto* track = dynamic_cast<InstrumentTrack*>(item)) { return track; }
		}
		return nullptr;
	}
	QString fixturePath() { return QDir(QStringLiteral(LEGACY_VST2_FIXTURE_DIR)).filePath(QStringLiteral("vst2-vestige-vsteffect.mmp")); }
	QJsonArray referencePoints()
	{
		QFile file(QDir(QStringLiteral(LEGACY_VST2_FIXTURE_DIR)).filePath(QStringLiteral("vst2-vestige-vsteffect.reference.json")));
		if (!file.open(QIODevice::ReadOnly)) { return {}; }
		return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("points")).toArray();
	}
	void requireReference(const QList<Rendered>& points)
	{
		const auto reference = referencePoints();
		QCOMPARE(reference.size(), 2); QCOMPARE(points.size(), 2);
		for (int i = 0; i < 2; ++i)
		{
			const auto expected = reference[i].toObject();
			QVERIFY(std::abs(points[i].left - expected.value(QStringLiteral("left")).toDouble()) <= Tolerance);
			QVERIFY(std::abs(points[i].right - expected.value(QStringLiteral("right")).toDouble()) <= Tolerance);
		}
		QVERIFY(std::abs(points[0].right - points[1].right) > Tolerance);
	}
	QString writeVariant(const std::function<void(QDomDocument&)>& edit)
	{
		QFile input(fixturePath());
		if (!input.open(QIODevice::ReadOnly)) { return {}; }
		QDomDocument doc;
		if (!doc.setContent(&input)) { return {}; }
		edit(doc);
		const QString path = m_variants.filePath(QStringLiteral("variant.mmp"));
		QFile output(path);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return {}; }
		if (output.write(doc.toByteArray()) < 0) { return {}; }
		return path;
	}
private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid()); QVERIFY(m_plugins.isValid()); QVERIFY(m_vst.isValid());
		if (QFileInfo::exists(nativeFixture())) { QVERIFY(QFile::link(nativeFixture(), m_vst.filePath(NativeFile))); }
		if (QFileInfo::exists(effectNativeFixture())) { QVERIFY(QFile::link(effectNativeFixture(), m_vst.filePath(EffectNativeFile))); }
		initCarlaHostingProcess(m_home, m_plugins, {m_vst.path()}, QString());
		ConfigManager::inst()->setVSTDir(m_vst.path());
	}
	void cleanup() { legacyvst2::setConversionEnabled(true); }
	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false); Engine::destroy();
	}
	void writeTheFixtureWithTheOldHost()
	{
		const QString dir = QString::fromLocal8Bit(qgetenv("ZENE_WRITE_LEGACY_VST2_FIXTURE"));
		if (dir.isEmpty()) { QSKIP("fixture generator: set ZENE_WRITE_LEGACY_VST2_FIXTURE=<dir>"); }
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		legacyvst2::setConversionEnabled(false);
		Engine::getSong()->setTempo(600);
		InstrumentTrack* track = newTrack(); QVERIFY(track);
		track->loadInstrument(QStringLiteral("vestige"));
		QVERIFY(track->instrument());
		QDomDocument doc;
		auto instrument = doc.createElement(QStringLiteral("vestige"));
		instrument.setAttribute(QStringLiteral("plugin"), QStringLiteral("uservst:ZeneVst2TestInstrument.so"));
		track->instrument()->restoreState(instrument);
		auto* level = model(track->instrument(), QStringLiteral("0"));
		auto* tone = model(track->instrument(), QStringLiteral("1"));
		QVERIFY(level); QVERIFY(tone); level->setValue(0.7f);
		EffectChain* chain = track->audioBusHandle()->effects();
		Plugin::Descriptor::SubPluginFeatures::Key key(nullptr, QStringLiteral("test"), {{QStringLiteral("file"), QStringLiteral("uservst:ZeneVst2TestEffect.so")}});
		Effect* effect = Effect::instantiate(QStringLiteral("vsteffect"), chain, &key); QVERIFY(effect);
		chain->appendEffect(effect); chain->setEnabledUnjournalled(true); effect->setEnabled(true);
		auto settings = doc.createElement(effect->controls()->nodeName());
		effect->controls()->restoreState(settings);
		auto* left = model(effect->controls(), QStringLiteral("0")); QVERIFY(left); left->setValue(0.6f);
		QVERIFY(renderNote(track, 10000).sounded);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0))); QVERIFY(clip);
		clip->addNote(Note(TimePos(4 * DefaultTicksPerBar), TimePos(0), 60, 100), false);
		auto* automation = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong())); QVERIFY(automation);
		auto* autoClip = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0))); QVERIFY(autoClip);
		QVERIFY(autoClip->addObject(tone));
		autoClip->setProgressionType(AutomationClip::ProgressionType::Discrete);
		autoClip->putValue(TimePos(0), 0.25f); autoClip->putValue(TimePos(2 * DefaultTicksPerBar), 0.8f);
		QVERIFY(QDir().mkpath(dir));
		const QString path = QDir(dir).filePath(QStringLiteral("vst2-vestige-vsteffect.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray xml = file.readAll();
		QVERIFY(xml.contains("uservst:")); QVERIFY(!xml.contains(m_vst.path().toUtf8()));
		QDir buildDir(QStringLiteral(LMMS_TEST_PLUGIN_DIR)); QVERIFY(buildDir.cdUp());
		QVERIFY(!xml.contains(buildDir.absolutePath().toUtf8()));
		QDomDocument saved; QVERIFY(saved.setContent(xml));
		const auto instState = saved.elementsByTagName(QStringLiteral("vestige")).at(0).toElement();
		const auto effectState = saved.elementsByTagName(QStringLiteral("vsteffectcontrols")).at(0).toElement();
		const auto decode = [](const QDomElement& state) {
			return QString::fromLatin1(QByteArray::fromBase64(state.attribute(QStringLiteral("chunk")).toLatin1()));
		};
		QVERIFY(chunkField(decode(instState), QStringLiteral("noteons")) >= 1);
		QVERIFY(chunkField(decode(effectState), QStringLiteral("heard")) > 0);
		const auto points = renderBars({1.5, 3.5}); QCOMPARE(points.size(), 2);
		QFile reference(QDir(dir).filePath(QStringLiteral("vst2-vestige-vsteffect.reference.json")));
		QVERIFY(reference.open(QIODevice::WriteOnly | QIODevice::Truncate));
		const QByteArray json = QByteArray("{\n  \"host\": \"VeSTige + VstEffect (NativeLinuxRemoteVstPlugin64)\",\n  \"points\": [\n")
			+ QByteArray("    {\"bar\": 1.5, \"left\": ") + QByteArray::number(points[0].left, 'g', 9) + ", \"right\": " + QByteArray::number(points[0].right, 'g', 9) + "},\n"
			+ "    {\"bar\": 3.5, \"left\": " + QByteArray::number(points[1].left, 'g', 9) + ", \"right\": " + QByteArray::number(points[1].right, 'g', 9) + "}\n  ]\n}\n";
		QCOMPARE(reference.write(json), qint64(json.size()));
	}
	void theOldHostRendersTheReference()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		legacyvst2::setConversionEnabled(false);
		const QDir dir(QStringLiteral(LEGACY_VST2_FIXTURE_DIR));
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), dir.filePath(QStringLiteral("vst2-vestige-vsteffect.mmp"))}}).ok);
		InstrumentTrack* track = nullptr;
		for (auto* item : Engine::getSong()->tracks()) { if ((track = dynamic_cast<InstrumentTrack*>(item))) { break; } }
		QVERIFY(track); QVERIFY(track->instrument());
		QCOMPARE(QString::fromLatin1(track->instrument()->descriptor()->name), QStringLiteral("vestige"));
		QVERIFY(effectOn(track)); QCOMPARE(QString::fromLatin1(effectOn(track)->descriptor()->name), QStringLiteral("vsteffect"));
		QFile file(dir.filePath(QStringLiteral("vst2-vestige-vsteffect.reference.json"))); QVERIFY(file.open(QIODevice::ReadOnly));
		const auto reference = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("points")).toArray(); QCOMPARE(reference.size(), 2);
		const auto points = renderBars({1.5, 3.5}); QCOMPARE(points.size(), 2);
		for (int i = 0; i < 2; ++i)
		{
			const auto expected = reference[i].toObject();
			QVERIFY(std::abs(points[i].left - expected.value(QStringLiteral("left")).toDouble()) <= Tolerance);
			QVERIFY(std::abs(points[i].right - expected.value(QStringLiteral("right")).toDouble()) <= Tolerance);
		}
		QVERIFY(std::abs(points[0].right - points[1].right) > Tolerance);
	}
	void aLegacyProjectOpensAsCarla()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), fixturePath()}}).ok);
		auto* track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument());
		auto* effect = effectOn(track); QVERIFY(effect);
		QCOMPARE(QString::fromLatin1(track->instrument()->descriptor()->name), QStringLiteral("carlavst2"));
		const auto ikey = track->instrument()->key().attributes;
		QCOMPARE(QFileInfo(ikey.value("file")).canonicalFilePath(), QFileInfo(nativeFixture()).canonicalFilePath());
		QCOMPARE(ikey.value("uniqueid"), QStringLiteral("1517180466")); QCOMPARE(ikey.value("binary"), QStringLiteral("native"));
		QCOMPARE(QString::fromLatin1(effect->descriptor()->name), QStringLiteral("carlavst2effect"));
		QCOMPARE(effect->key().attributes.value("uniqueid"), QStringLiteral("1517176114"));
		QVERIFY(std::abs(paramValue(track, 0) - 0.7) <= Tolerance);
		QVERIFY(std::abs(effectParam(track, "Left").second - 0.6) <= Tolerance);
		QVERIFY(std::abs(effectParam(track, "Right").second - 0.5) <= Tolerance);
		QFile input(fixturePath()); QVERIFY(input.open(QIODevice::ReadOnly)); QDomDocument doc; QVERIFY(doc.setContent(&input));
		const auto decoded = [&doc](const QString& tag) { return QString::fromLatin1(QByteArray::fromBase64(doc.elementsByTagName(tag).at(0).toElement().attribute("chunk").toLatin1())); };
		QCOMPARE(chunkField(track->instrument(), "noteons"), chunkField(decoded("vestige"), "noteons"));
		QCOMPARE(chunkField(track->instrument(), "noteons"), 1);
		QCOMPARE(effectChunkField(effect, "heard"), chunkField(decoded("vsteffectcontrols"), "heard"));
		QCOMPARE(effectChunkField(effect, "heard"), 5);
		AutomationClip* clip = nullptr;
		for (auto* item : Engine::getSong()->tracks())
		{
			if (auto* automation = dynamic_cast<AutomationTrack*>(item))
			{
				for (auto* candidate : automation->getClips()) { if ((clip = dynamic_cast<AutomationClip*>(candidate))) { break; } }
			}
			if (clip) { break; }
		}
		QVERIFY(clip); QCOMPARE(clip->objects().size(), size_t(1));
		auto* object = clip->objects().front().data(); QVERIFY(object); QCOMPARE(object->id(), decltype(object->id())(23571));
		QVERIFY(track->instrument()->findChildren<AutomatableModel*>().contains(object));
		requireReference(renderBars({1.5, 3.5}));
	}
	void aConvertedProjectSavesReopensAndRenders()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		QVERIFY(run("project.open", {{"path", fixturePath()}}).ok);
		auto* track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument());
		const int noteons = chunkField(track->instrument(), "noteons");
		const QString path = m_variants.filePath("saved.mmp"); QVERIFY(run("project.save", {{"path", path}}).ok);
		QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray xml = file.readAll();
		QVERIFY(xml.contains("CARLA-PROJECT")); QVERIFY(xml.contains("carlavst2effect"));
		QVERIFY(!xml.contains("zene-legacy-vst2")); QVERIFY(!xml.contains("name=\"vestige\"")); QVERIFY(!xml.contains("name=\"vsteffect\""));
		QDomDocument doc; QVERIFY(doc.setContent(xml));
		const auto objects = doc.elementsByTagName("object"); const auto knobs = doc.elementsByTagName("PARAM_KNOB_1");
		QCOMPARE(objects.size(), 1); QVERIFY(!knobs.isEmpty());
		QCOMPARE(objects.at(0).toElement().attribute("id"), knobs.at(0).toElement().attribute("id"));
		QVERIFY(run("project.open", {{"path", path}}).ok);
		track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument()); auto* effect = effectOn(track); QVERIFY(effect);
		QCOMPARE(QString::fromLatin1(track->instrument()->descriptor()->name), QStringLiteral("carlavst2"));
		QCOMPARE(QString::fromLatin1(effect->descriptor()->name), QStringLiteral("carlavst2effect"));
		QCOMPARE(track->instrument()->key().attributes.value("uniqueid"), QStringLiteral("1517180466"));
		QCOMPARE(effect->key().attributes.value("uniqueid"), QStringLiteral("1517176114"));
		QVERIFY(std::abs(paramValue(track, 0) - 0.7) <= Tolerance);
		QVERIFY(std::abs(effectParam(track, "Left").second - 0.6) <= Tolerance);
		QCOMPARE(chunkField(track->instrument(), "noteons"), noteons);
		requireReference(renderBars({1.5, 3.5}));
	}
	void aChunklessLegacyProjectRestoresItsDump()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const auto path = writeVariant([](QDomDocument& doc) {
			auto inst = doc.elementsByTagName("vestige").at(0).toElement(); inst.removeAttribute("chunk");
			inst.setAttribute("numparams", "3"); inst.setAttribute("param0", "0:Level:0.7"); inst.setAttribute("param1", "1:Tone:0.25"); inst.setAttribute("param2", "9:Bogus:x");
			auto fx = doc.elementsByTagName("vsteffectcontrols").at(0).toElement(); fx.removeAttribute("chunk");
			fx.setAttribute("numparams", "2"); fx.setAttribute("param0", "0:Left:0.6"); fx.setAttribute("param1", "1:Right:0.5");
		});
		QVERIFY(!path.isEmpty()); QVERIFY(run("project.open", {{"path", path}}).ok);
		auto* track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument()); auto* effect = effectOn(track); QVERIFY(effect);
		QCOMPARE(QString::fromLatin1(track->instrument()->descriptor()->name), QStringLiteral("carlavst2"));
		QCOMPARE(QString::fromLatin1(effect->descriptor()->name), QStringLiteral("carlavst2effect"));
		QVERIFY(std::abs(paramValue(track, 0) - 0.7) <= Tolerance);
		QVERIFY(std::abs(effectParam(track, "Left").second - 0.6) <= Tolerance);
		requireReference(renderBars({1.5, 3.5}));
	}
	void aMissingBinaryKeepsTheLegacyDevice()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const auto path = writeVariant([](QDomDocument& doc) {
			doc.elementsByTagName("vestige").at(0).toElement().setAttribute("plugin", "uservst:Missing.so");
			doc.elementsByTagName("effect").at(0).toElement().firstChildElement("key").firstChildElement("attribute").setAttribute("value", "uservst:MissingFx.so");
		});
		QVERIFY(!path.isEmpty()); QVERIFY(run("project.open", {{"path", path}}).ok);
		auto* track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument()); auto* effect = effectOn(track); QVERIFY(effect);
		QVERIFY(QString::fromLatin1(track->instrument()->descriptor()->name) != "carlavst2");
		QVERIFY(QString::fromLatin1(effect->descriptor()->name) != "carlavst2effect");
		const auto saved = m_variants.filePath("missing.mmp"); QVERIFY(run("project.save", {{"path", saved}}).ok);
		QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly)); const auto xml = file.readAll();
		QVERIFY(xml.contains("uservst:Missing.so")); QVERIFY(xml.contains("uservst:MissingFx.so"));
	}
	void aBinaryCarlaCannotLoadFallsBackToTheLegacyReader()
	{
		if (const QString why = missing(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const auto binary = m_variants.filePath("NotAVst.so"); QFile invalid(binary); QVERIFY(invalid.open(QIODevice::WriteOnly)); QVERIFY(invalid.write("not a VST") > 0); invalid.close();
		const auto path = writeVariant([&](QDomDocument& doc) { doc.elementsByTagName("vestige").at(0).toElement().setAttribute("plugin", binary); });
		QVERIFY(!path.isEmpty()); QVERIFY(run("project.open", {{"path", path}}).ok);
		auto* track = firstInstrumentTrack(); QVERIFY(track); QVERIFY(track->instrument());
		QVERIFY(QString::fromLatin1(track->instrument()->descriptor()->name) != "carlavst2");
		const auto saved = m_variants.filePath("invalid.mmp"); QVERIFY(run("project.save", {{"path", saved}}).ok);
		QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(file.readAll().contains(binary.toUtf8()));
	}

};
QTEST_MAIN(LegacyVst2ProjectTest)
#include "LegacyVst2ProjectTest.moc"
