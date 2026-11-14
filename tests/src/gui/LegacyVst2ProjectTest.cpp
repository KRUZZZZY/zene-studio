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
  const auto frames = Engine::audioEngine()->renderNextPeriod();
  if (song->getPlayPos(Song::PlayMode::Song).getTicks() > bars[result.size()] * DefaultTicksPerBar)
  {
   const auto& frame = frames.back();
   result.append({true, frame.left(), frame.right()});
  }
  QCoreApplication::processEvents();
 }
 song->stop();
 for (int i = 0; i < 8; ++i) { Engine::audioEngine()->renderNextPeriod(); }
 return result;
}
}

class LegacyVst2ProjectTest : public QObject
{
 Q_OBJECT
 QTemporaryDir m_home, m_plugins, m_vst;
 QString missing() const
 {
  for (const auto& path : {nativeFixture(), effectNativeFixture(), QDir(m_plugins.path()).filePath(QStringLiteral("NativeLinuxRemoteVstPlugin64"))})
   if (!QFileInfo::exists(path)) { return QStringLiteral("missing native fixture or remote host: %1").arg(path); }
  return {};
 }
private slots:
 void initTestCase()
 {
  (void)&sourceWithEffect; (void)&effectWineAvailable; (void)&missingEffectRuntime; (void)&setEffectParam;
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
};
QTEST_MAIN(LegacyVst2ProjectTest)
#include "LegacyVst2ProjectTest.moc"
