/*
 * BrowserDragUrlTest.cpp - a browser file drag carries its file as a URL, and still drives Zene's own drops
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
/*! 0.5.0 L11 (Q050-133): a drag from the file browser or a file-backed search result carries its file as a
 *  `text/uri-list` URL beside the StringPair payload, so a plug-in editor or a file manager that takes file
 *  URLs receives the source file, while every drop target inside Zene (a sample clip, the arranger, the session
 *  grid) still decodes the unchanged StringPair payload. Only the browser's file keys - whose value IS the
 *  file's path - gain the URL: a serialized clip, embedded sample data or a model id is never read as a path. */

#include <QtTest>

#include <cmath>
#include <memory>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

#include "BrowserResults.h"
#include "Clipboard.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "Song.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QByteArray wavFile()
{
	QByteArray data;
	for (int i = 0; i < 100; ++i)
	{
		const auto sample = static_cast<qint16>(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 100.0));
		data.append(static_cast<char>(sample & 0xFF));
		data.append(static_cast<char>((sample >> 8) & 0xFF));
	}
	QByteArray out("RIFF", 4);
	auto le32 = [&out](quint32 v) { for (int i = 0; i < 4; ++i) { out.append(static_cast<char>((v >> (8 * i)) & 0xFF)); } };
	auto le16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>(v >> 8)); };
	le32(36 + static_cast<quint32>(data.size()));
	out.append("WAVEfmt ", 8);
	le32(16); le16(1); le16(1); le32(44100); le32(44100 * 2); le16(2); le16(16);
	out.append("data", 4);
	le32(static_cast<quint32>(data.size()));
	out.append(data);
	return out;
}

bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

//! The drag key every file-backed search-result kind drags under, as the file lists' rows drag it.
const QVector<QString>& fileBackedResultKeys()
{
	static const QVector<QString> keys = {
		browserresults::dragKey(QStringLiteral("audio")),
		browserresults::dragKey(QStringLiteral("preset")),
		browserresults::dragKey(QStringLiteral("project")),
		browserresults::dragKey(QStringLiteral("midi")),
	};
	return keys;
}

} // namespace

/*! What a file manager or a plug-in editor that takes file URLs does with a drag: accept it for its
 *  `text/uri-list` and read the local files it carries - none of Zene's own drag keys involved. */
class UriListRecipient final : public QWidget
{
	Q_OBJECT

public:
	void dragEnterEvent(QDragEnterEvent* event) override
	{
		if (event->mimeData()->hasUrls()) { event->acceptProposedAction(); }
		else { event->ignore(); }
	}

	void dropEvent(QDropEvent* event) override
	{
		for (const QUrl& url : event->mimeData()->urls())
		{
			if (url.isLocalFile()) { m_files.append(url.toLocalFile()); }
		}
		event->acceptProposedAction();
	}

	QStringList m_files;
};

class BrowserDragUrlTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_files.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QVERIFY(write(m_files.filePath(QStringLiteral("kick.wav")), wavFile()));
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
	}

	QString file(const QString& name) const { return m_files.filePath(name); }

	//! Criterion 1, the file browser's own sample row: the URL names the real file, the StringPair is untouched.
	void aBrowserSampleDragCarriesTheFileUrlAlongsideItsStringPair()
	{
		const QString sample = file(QStringLiteral("kick.wav"));
		QVERIFY(QFileInfo::exists(sample));
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"), sample));

		QVERIFY(mime->hasFormat(Clipboard::mimeType(Clipboard::MimeType::StringPair)));
		QDropEvent drop(QPointF(0, 0), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		QCOMPARE(StringPairDrag::decodeKey(&drop), QStringLiteral("samplefile"));
		QCOMPARE(StringPairDrag::decodeValue(&drop), sample);

		QVERIFY2(mime->hasUrls(), "the browser's sample drag carries no file URL");
		const QList<QUrl> urls = mime->urls();
		QCOMPARE(static_cast<int>(urls.size()), 1);
		QVERIFY2(urls.front().isLocalFile(), "the drag's URL is not a local file URL");
		QCOMPARE(urls.front(), QUrl::fromLocalFile(sample));
		QCOMPARE(urls.front().toLocalFile(), sample);
		QVERIFY(QFileInfo::exists(urls.front().toLocalFile()));
	}

	//! Criterion 1 for every kind the file lists drag: each key's value IS the file's path, each carries the URL.
	void everyFileKeyTheBrowserDragsCarriesTheFileUrl()
	{
		const QString path = file(QStringLiteral("kick.wav"));
		const QVector<QString> browserKeys = {
			QStringLiteral("samplefile"), QStringLiteral("presetfile"), QStringLiteral("pluginpresetfile"),
			QStringLiteral("soundfontfile"), QStringLiteral("patchfile"), QStringLiteral("vstpluginfile"),
			QStringLiteral("importedproject"), QStringLiteral("projectfile"),
		};
		for (const QString& key : browserKeys)
		{
			std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, path));
			QVERIFY2(mime->hasUrls(), qPrintable(key + " drags a file with no file URL"));
			QCOMPARE(mime->urls().front().toLocalFile(), path);
			QVERIFY(mime->hasFormat(Clipboard::mimeType(Clipboard::MimeType::StringPair)));
		}
	}

	//! Criterion 1 for the search results: every file-backed kind drags a key the URL publishing covers.
	void everySearchResultKindDragsAKeyThatCarriesTheFileUrl()
	{
		QCOMPARE(browserresults::dragKey(QStringLiteral("audio")), QStringLiteral("samplefile"));
		QCOMPARE(browserresults::dragKey(QStringLiteral("preset")), QStringLiteral("presetfile"));
		QCOMPARE(browserresults::dragKey(QStringLiteral("project")), QStringLiteral("projectfile"));
		QCOMPARE(browserresults::dragKey(QStringLiteral("midi")), QStringLiteral("importedproject"));
		const QString path = file(QStringLiteral("kick.wav"));
		for (const QString& key : fileBackedResultKeys())
		{
			std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, path));
			QVERIFY2(mime->hasUrls(), qPrintable("a search result drags " + key + " with no file URL"));
			QCOMPARE(mime->urls().front().toLocalFile(), path);
		}
	}

	//! Criterion 2: a recipient that takes file URLs - a file manager, a compatible plug-in editor - receives the file.
	void aFileUrlRecipientReceivesTheFileTheBrowserDragged()
	{
		const QString sample = file(QStringLiteral("kick.wav"));
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"), sample));
		UriListRecipient recipient;
		recipient.setAcceptDrops(true);

		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		enter.setAccepted(false);
		QCoreApplication::sendEvent(&recipient, &enter);
		QVERIFY2(enter.isAccepted(), "a recipient that takes file URLs refused the browser's drag");

		QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		drop.setAccepted(false);
		QCoreApplication::sendEvent(&recipient, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(recipient.m_files, QStringList{sample});
	}

	//! Criterion 3: the real internal consumer - a sample clip - still takes the drag and loads the path.
	void theStringPairPayloadStillDrivesAnInternalSampleClipDrop()
	{
		Track* sampleTrack = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), control::trackIdOf(sampleTrack)}, {QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();

		SampleClipView* view = nullptr;
		for (SampleClipView* candidate : m_editor->findChildren<SampleClipView*>())
		{
			if (candidate->getClip() == sampleTrack->getClip(0)) { view = candidate; }
		}
		QVERIFY(view != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(sampleTrack->getClip(0));
		QVERIFY(clip != nullptr);

		const QString replacement = file(QStringLiteral("kick.wav"));
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"), replacement));
		bool entered = false;
		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		enter.setAccepted(false);
		QCoreApplication::sendEvent(view, &enter);
		entered = enter.isAccepted();
		QVERIFY2(entered, "a sample clip refuses the browser's drag now that it carries a URL too");

		QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		drop.setAccepted(false);
		QCoreApplication::sendEvent(view, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(clip->sampleFile(), replacement);
	}

	/*! Cluster new-p14-262 BUG 6: the samplefile drop branch replaces the clip's source through
	 *  SampleClip::setSampleFile - which, unlike setSampleBuffer, marks nothing. The drop is a
	 *  user edit, so the song must come out modified: close would otherwise not prompt and the
	 *  new source could be lost. Loading stays quiet because SampleClip::loadSettings reaches
	 *  the same setter. */
	void droppingASampleFileMarksTheSongModified()
	{
		Track* sampleTrack = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), control::trackIdOf(sampleTrack)}, {QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();

		auto* clip = dynamic_cast<SampleClip*>(sampleTrack->getClip(0));
		QVERIFY(clip != nullptr);
		SampleClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		// The track's own construction names it, which marks the song; a successful save is the
		// product's way back to clean, so the drop below is the only dirtying change under test.
		Engine::getSong()->noteProjectSaved(m_home.filePath(QStringLiteral("clean.mmpz")));
		QVERIFY2(!Engine::getSong()->isModified(), "the test setup left the song modified");

		const QString replacement = file(QStringLiteral("kick.wav"));
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"), replacement));
		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		enter.setAccepted(false);
		QCoreApplication::sendEvent(view, &enter);
		QVERIFY(enter.isAccepted());

		QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		drop.setAccepted(false);
		QCoreApplication::sendEvent(view, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(clip->sampleFile(), replacement);
		QVERIFY2(Engine::getSong()->isModified(),
			"a dropped sample file left the song unmodified: closing would not prompt and the new source could be lost");
	}

	/*! The other side of BUG 6: the SAME setter the drop calls is also where a project load
	 *  restores a clip's source (SampleClip::loadSettings). Marking the song inside
	 *  SampleClip::setSampleFile would dirty every project the moment it was opened, so the
	 *  mark belongs on the user-edit path and a restore must stay quiet. This is the regression
	 *  that fails if someone "simplifies" the fix into the setter. */
	void restoringASampleFileDoesNotMarkTheSongModified()
	{
		Track* sampleTrack = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), control::trackIdOf(sampleTrack)}, {QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
		auto* clip = dynamic_cast<SampleClip*>(sampleTrack->getClip(0));
		QVERIFY(clip != nullptr);
		clip->setSampleFile(file(QStringLiteral("kick.wav")));
		QCOMPARE(clip->sampleFile(), file(QStringLiteral("kick.wav")));

		// What a save writes for this clip, and what a load reads back from it.
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("clipcontainer"));
		doc.appendChild(parent);
		const QDomElement element = clip->saveState(doc, parent);
		QVERIFY(!element.attribute(QStringLiteral("src")).isEmpty());

		Engine::getSong()->noteProjectSaved(m_home.filePath(QStringLiteral("clean.mmpz")));
		QVERIFY2(!Engine::getSong()->isModified(), "the test setup left the song modified");
		clip->restoreState(element);
		QVERIFY2(!Engine::getSong()->isModified(),
			"restoring a clip through SampleClip::loadSettings dirtied the project it was loaded from");
	}

	//! The StringPair value of a key that is not a browser file - embedded data, a serialized clip - is never a URL.
	void aDragThatIsNotABrowserFileCarriesNoUrl()
	{
		const QVector<QString> nonFileKeys = {
			QStringLiteral("sampledata"), QStringLiteral("clip_2"), QStringLiteral("instrument"),
			QStringLiteral("automatable_model"), QStringLiteral("float_value"),
		};
		const QString value = QStringLiteral("not a path: a base64 body or a serialized clip");
		for (const QString& key : nonFileKeys)
		{
			std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, value));
			QVERIFY2(!mime->hasUrls(), qPrintable(key + " publishes a file URL for a value that is not a file"));
			QVERIFY(mime->hasFormat(Clipboard::mimeType(Clipboard::MimeType::StringPair)));
			QDropEvent drop(QPointF(0, 0), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
			QCOMPARE(StringPairDrag::decodeKey(&drop), key);
			QCOMPARE(StringPairDrag::decodeValue(&drop), value);
		}
	}

	//! A key that carries a colon survives the StringPair payload: the receiver reads the whole key.
	void aKeyWithAColonSurvivesTheStringPairPayload()
	{
		const QString key = QStringLiteral("preset:factory");
		const QString value = QStringLiteral("a value with a colon: and a percent % sign");
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, value));

		QDropEvent drop(QPointF(0, 0), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		QCOMPARE(StringPairDrag::decodeKey(&drop), key);
		QCOMPARE(StringPairDrag::decodeValue(&drop), value);
	}

	//! A key that carries a percent sign survives it too - and a literal "%3A" in a key stays literal.
	void aKeyWithAPercentSignSurvivesTheStringPairPayload()
	{
		const QVector<QString> keys = {
			QStringLiteral("100%"), QStringLiteral("a%3A"), QStringLiteral("a%25"), QStringLiteral("%:"),
		};
		for (const QString& key : keys)
		{
			std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, QStringLiteral("v")));
			QDropEvent drop(QPointF(0, 0), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
			QCOMPARE(StringPairDrag::decodeKey(&drop), key);
			QCOMPARE(StringPairDrag::decodeValue(&drop), QStringLiteral("v"));
		}
	}

	//! The payload of a key that carries neither colon nor percent sign is byte for byte what it was.
	void anOrdinaryKeyKeepsItsPayloadBytes()
	{
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"),
									   QStringLiteral("/tmp/dropped.wav")));
		QCOMPARE(mime->data(Clipboard::mimeType(Clipboard::MimeType::StringPair)),
			QByteArrayLiteral("samplefile:/tmp/dropped.wav"));
	}

	//! A target that allows a colon key takes the drag; a target that allows another key does not.
	void aDragEnterAcceptsTheWholeColonKey()
	{
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("preset:factory"),
									   QStringLiteral("v")));
		QCOMPARE(QString::fromUtf8(
			mime->data(Clipboard::mimeType(Clipboard::MimeType::StringPair))).section(':', 0, 0),
			QStringLiteral("%1preset%3Afactory"));

		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		enter.setAccepted(false);
		QVERIFY2(StringPairDrag::processDragEnterEvent(&enter, QStringLiteral("preset:factory")),
			"a target that allows the colon key refuses the drag");
		QVERIFY(enter.isAccepted());

		QDragEnterEvent other(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		other.setAccepted(false);
		QVERIFY(!StringPairDrag::processDragEnterEvent(&other, QStringLiteral("preset")));
		QVERIFY(!other.isAccepted());
	}

	//! The clipboard writes the same payload, so a colon key survives the copy too.
	void theClipboardKeepsAColonKeyIntact()
	{
		Clipboard::copyStringPair(QStringLiteral("preset:factory"), QStringLiteral("v"));
		QCOMPARE(Clipboard::getMimeData()->data(Clipboard::mimeType(Clipboard::MimeType::StringPair)),
			QByteArrayLiteral("%1preset%3Afactory:v"));
		QVERIFY(Clipboard::getMimeData()->hasFormat(
			Clipboard::mimeType(Clipboard::MimeType::StringPairV2)));
		QCOMPARE(Clipboard::decodeKey(Clipboard::getMimeData()), QStringLiteral("preset:factory"));
		QCOMPARE(Clipboard::decodeValue(Clipboard::getMimeData()), QStringLiteral("v"));
	}

	//! A payload an older Zene wrote - the key as raw text, never escaped - is read literally: a key
	//! that carries the text "%3A" or "%25" keeps its spelling instead of being decoded into a
	//! colon or a percent sign.
	void aLegacyPayloadIsReadLiterally()
	{
		const QVector<QString> legacyKeys = {
			QStringLiteral("a%3A"), QStringLiteral("a%25"), QStringLiteral("%1legacy"),
			QStringLiteral("%1a%3A"), QStringLiteral("%1a%25"), QStringLiteral("%1"),
			QStringLiteral("samplefile"),
		};
		for (const QString& key : legacyKeys)
		{
			QMimeData mime;
			mime.setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),
				(key + QStringLiteral(":v")).toUtf8());
			QCOMPARE(Clipboard::decodeKey(&mime), key);
			QCOMPARE(Clipboard::decodeValue(&mime), QStringLiteral("v"));
		}
	}

	//! A legacy payload whose key carries a colon is read exactly as the old decoder read it - the
	//! information the old format lost is still lost, but nothing else changed for it.
	void aLegacyPayloadWithAColonKeyIsReadAsBefore()
	{
		QMimeData mime;
		mime.setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),
			QByteArrayLiteral("preset:factory:v"));
		QCOMPARE(Clipboard::decodeKey(&mime), QStringLiteral("preset"));
		QCOMPARE(Clipboard::decodeValue(&mime), QStringLiteral("factory:v"));
	}

	//! An escaped key uses a distinct MIME type, and it still comes back whole.
	void anEscapedKeyUsesTheVersionedMimeType()
	{
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("a%3A"),
									   QStringLiteral("v")));
		const QByteArray raw = mime->data(Clipboard::mimeType(Clipboard::MimeType::StringPair));
		QCOMPARE(raw, QByteArrayLiteral("%1a%253A:v"));
		const QByteArray v2 = mime->data(Clipboard::mimeType(Clipboard::MimeType::StringPairV2));
		QVERIFY(mime->hasFormat(Clipboard::mimeType(Clipboard::MimeType::StringPairV2)));
		QCOMPARE(v2, QByteArrayLiteral("%1a%253A:v"));
		QDropEvent drop(QPointF(0, 0), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
		QCOMPARE(StringPairDrag::decodeKey(&drop), QStringLiteral("a%3A"));
		QCOMPARE(StringPairDrag::decodeValue(&drop), QStringLiteral("v"));
	}

private:
	//! The live view of @a clip in the song editor, or nullptr when it has none.
	SampleClipView* viewOf(Clip* clip)
	{
		for (SampleClipView* candidate : m_editor->findChildren<SampleClipView*>())
		{
			if (candidate->getClip() == clip) { return candidate; }
		}
		return nullptr;
	}

	QTemporaryDir m_home;
	QTemporaryDir m_files;
	SongEditor* m_editor = nullptr;
};

QTEST_MAIN(BrowserDragUrlTest)
#include "BrowserDragUrlTest.moc"
