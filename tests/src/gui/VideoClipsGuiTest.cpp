/*
 * VideoClipsGuiTest.cpp - Q050-139/Q050-180.1: the arranger's video lane with several positioned clips
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
/*! The lane half of Q050-180.1, driven through the real widgets of a running interface:
 *  - each clip is drawn over its own span and the gap between them is not; a click on either span seeks into that
 *    clip, and the video window (View > Video Window's command) shows that clip while the other stays on the track;
 *  - View > Add Video Clip... and the lane's double-click add clips through video.clip_add (the file dialog answered
 *    by the test); a drag moves one clip through video.set_offset as ONE undo step; the context menu moves a clip's
 *    start to the play position and renders the loop range's frames (video.render_frames);
 *  - the added and moved clips save and reopen with their addresses. */

#include <QtTest>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTimer>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "VideoDecoder.h"
#include "VideoLane.h"
#include "VideoReference.h"
#include "VideoWindow.h"
#include "../core/VideoTestClip.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray clipList()
{
	return run(QStringLiteral("video.get_state")).result.value(QStringLiteral("clips")).toArray();
}

QJsonObject clipState(const QString& id)
{
	for (const QJsonValue& value : clipList())
	{
		const QJsonObject clip = value.toObject();
		if (clip.value(QStringLiteral("id")).toString() == id) { return clip; }
	}
	return {};
}

QString addClip(const QString& path, int offset)
{
	const ControlResult added = run(QStringLiteral("video.clip_add"), {{QStringLiteral("path"), path}, {QStringLiteral("offset"), offset}});
	return added.ok ? added.result.value(QStringLiteral("clip")).toObject().value(QStringLiteral("id")).toString() : QString();
}

void settle()
{
	QCoreApplication::processEvents();
}

//! Accept the next QFileDialog with @a path, retrying until the dialog is up.
void acceptFileDialog(const QString& path)
{
	auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
	if (dialog == nullptr)
	{
		QTimer::singleShot(20, [path] { acceptFileDialog(path); });
		return;
	}
	if (dialog->fileMode() == QFileDialog::Directory) { dialog->setDirectory(path); }  // the folder it is in is the answer
	else { dialog->selectFile(path); }
	QMetaObject::invokeMethod(dialog, "accept");
}

//! Trigger the item @a text of the next popup menu, retrying until it is up; @a found says whether it was there.
void pickFromPopup(const QString& text, bool* found)
{
	auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
	if (menu == nullptr)
	{
		QTimer::singleShot(20, [text, found] { pickFromPopup(text, found); });
		return;
	}
	for (QAction* action : menu->actions())
	{
		if (action->text() != text) { continue; }
		*found = true;
		menu->close();
		action->trigger();
		return;
	}
	menu->close();
}

bool reddish(QRgb pixel) { return qRed(pixel) > 150 && qGreen(pixel) < 100 && qBlue(pixel) < 100; }
bool greenish(QRgb pixel) { return qGreen(pixel) > 150 && qRed(pixel) < 100 && qBlue(pixel) < 100; }

} // namespace

class VideoClipsGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QVERIFY(m_editor != nullptr);
		settle();
		m_lane = m_editor->findChild<VideoLane*>();
		QVERIFY(m_lane != nullptr);
		QVERIFY(run(QStringLiteral("arrangement.zoom"),
			{{QStringLiteral("pixels_per_bar"), 120}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		settle();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);  // a second is 96 ticks
		settle();
	}

	//! Criterion 2: each clip over its own span; a click seeks into it and the window follows, the other clip stays.
	void eachSpanIsDrawnAndAClickSeeksTheWindowToThatClip()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		const QString a = addClip(m_a, 96);   // 96..288
		const QString b = addClip(m_b, 480);  // 480..672
		QVERIFY(!a.isEmpty() && !b.isEmpty());
		settle();
		QTRY_VERIFY(m_lane->isVisible());
		const auto x = [this](tick_t tick) { return m_editor->timeLine()->markerX(TimePos(tick)); };
		const int y = m_lane->height() / 2;
		const QImage strip = m_lane->grab().toImage();
		QCOMPARE(m_lane->drawnSpan().first, x(96));
		QCOMPARE(m_lane->drawnSpan().second, x(672));
		const QColor ground = tokens::palette().panel;
		QVERIFY2(strip.pixelColor(x(192), y) != ground, "the first clip's span was not drawn");
		QVERIFY2(strip.pixelColor(x(576), y) != ground, "the second clip's span was not drawn");
		QCOMPARE(strip.pixelColor(x(384), y), ground);  // the gap between them is the lane's own ground

		QVERIFY(run(QStringLiteral("video.window_show"), {{QStringLiteral("shown"), true}}).ok);
		VideoWindow* window = VideoWindow::find(getGUI()->mainWindow());
		QVERIFY(window != nullptr);
		const auto picture = [window] {
			const QImage image = window->grab().toImage();
			return image.pixel(image.width() / 2, image.height() / 6);
		};

		// A click 0.3 s into the first clip: the play position lands there and the window shows its red.
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(x(96 + 29), y));
		const tick_t first = Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks();
		QVERIFY2(first >= 96 + 20 && first <= 96 + 40, qPrintable(QString::number(first)));
		window->follow();
		QTRY_VERIFY_WITH_TIMEOUT(reddish(picture()), 10000);

		// A click 0.3 s into the second clip: the window follows it; the first clip is still on the track.
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(x(480 + 29), y));
		const tick_t second = Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks();
		QVERIFY2(second >= 480 + 20 && second <= 480 + 40, qPrintable(QString::number(second)));
		window->follow();
		QVERIFY(!reddish(picture()));
		QTRY_VERIFY_WITH_TIMEOUT(greenish(picture()), 10000);
		QCOMPARE(clipList().size(), 2);
		QCOMPARE(clipState(a).value(QStringLiteral("offset")).toInt(), 96);
		QCOMPARE(clipState(b).value(QStringLiteral("offset")).toInt(), 480);
		// A click never moved a clip.
		QVERIFY(run(QStringLiteral("video.window_show"), {{QStringLiteral("shown"), false}}).ok);
	}

	//! Criteria 4/5: the add / move workflow through the menus and the lane, each one undo step, saved and reopened.
	void theMenusAndTheLaneAddAndMoveClips()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		// View > Add Video Clip...: the first clip, at the play position, while the lane is still hidden.
		QVERIFY(!m_lane->isVisible());
		Engine::getSong()->setPlayPos(48, Song::PlayMode::Song);
		QAction* add = viewMenuItem(QStringLiteral("Add Video Clip..."));
		QVERIFY(add != nullptr);
		QVERIFY(add->isEnabled());
		QTimer::singleShot(0, [path = m_a] { acceptFileDialog(path); });
		add->trigger();
		settle();
		QCOMPARE(clipList().size(), 1);
		const QString firstId = clipList().first().toObject().value(QStringLiteral("id")).toString();
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 48);
		QTRY_VERIFY(m_lane->isVisible());

		// A double-click on empty lane space adds the second file at the clicked tick.
		const int y = m_lane->height() / 2;
		const int addX = m_editor->timeLine()->markerX(TimePos(480));
		QTimer::singleShot(0, [path = m_b] { acceptFileDialog(path); });
		QTest::mouseDClick(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(addX, y));
		settle();
		QCOMPARE(clipList().size(), 2);
		const QString secondId = clipList().at(1).toObject().value(QStringLiteral("id")).toString();
		QCOMPARE(clipList().at(1).toObject().value(QStringLiteral("path")).toString(), QFileInfo(m_b).absoluteFilePath());
		const int added = clipState(secondId).value(QStringLiteral("offset")).toInt();
		QVERIFY2(qAbs(added - 480) <= 2, qPrintable(QString::number(added)));

		// Drag the second clip 40 px right: only its offset changes, and one undo puts it back.
		const int secondX = m_editor->timeLine()->markerX(TimePos(added + 24));
		QTest::mousePress(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(secondX, y));
		QTest::mouseRelease(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(secondX + 40, y));
		settle();
		const int moved = clipState(secondId).value(QStringLiteral("offset")).toInt();
		QVERIFY2(moved >= added + 60 && moved <= added + 68, qPrintable(QString::number(moved)));  // 40 px at 120 px/bar
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 48);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), added);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), moved);

		// The context menu on the first clip: Move Clip Start to Play Position.
		Engine::getSong()->setPlayPos(144, Song::PlayMode::Song);
		bool found = false;
		const QPoint onFirst(m_editor->timeLine()->markerX(TimePos(48 + 24)), y);
		QTimer::singleShot(0, [&found] { pickFromPopup(QStringLiteral("Move Clip Start to Play Position"), &found); });
		QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, onFirst, m_lane->mapToGlobal(onFirst));
		QApplication::sendEvent(m_lane, &menuEvent);
		settle();
		QVERIFY(found);
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 144);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), moved);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 48);
		QVERIFY(run(QStringLiteral("control.redo")).ok);

		// Save and reopen: both addresses, files and moved positions survive.
		const QString project = m_home.filePath(QStringLiteral("lane-clips.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		settle();
		QVERIFY2(run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}}).ok, "reopen");
		settle();
		QCOMPARE(clipList().size(), 2);
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 144);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), moved);
		QCOMPARE(clipState(secondId).value(QStringLiteral("path")).toString(), QFileInfo(m_b).absoluteFilePath());
	}

	//! Criterion 3 from the lane: the context menu renders the loop range's frames into a chosen folder.
	void theContextMenuRendersTheLoopRange()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(!addClip(m_a, 0).isEmpty());
		QVERIFY(!addClip(m_b, 288).isEmpty());
		settle();
		QTRY_VERIFY(m_lane->isVisible());
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(192));  // two seconds of song: 50 frames at 25 fps
		const QString folder = m_home.filePath(QStringLiteral("lane-render"));
		QVERIFY(QDir().mkpath(folder));
		bool found = false;
		const int y = m_lane->height() / 2;
		const QPoint onFirst(m_editor->timeLine()->markerX(TimePos(24)), y);
		QTimer::singleShot(0, [folder] { acceptFileDialog(folder); });
		QTimer::singleShot(0, [&found] { pickFromPopup(QStringLiteral("Render Video Frames of Loop Range..."), &found); });
		QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, onFirst, m_lane->mapToGlobal(onFirst));
		QApplication::sendEvent(m_lane, &menuEvent);
		settle();
		QVERIFY(found);
		const QStringList frames = QDir(folder).entryList({QStringLiteral("frame-*.png")}, QDir::Files, QDir::Name);
		QCOMPARE(frames.size(), 50);
		const QImage early(QDir(folder).filePath(frames.at(5)));  // 0.2 s: the first clip's red
		QVERIFY(reddish(early.pixel(early.width() / 2, early.height() / 2)));
	}

private:
	QAction* viewMenuItem(const QString& text)
	{
		for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
		{
			if (top->menu() == nullptr || top->text() != QStringLiteral("&View")) { continue; }
			emit top->menu()->aboutToShow();  // the View menu is built as it opens
			for (QAction* action : top->menu()->actions())
			{
				if (action->text() == text) { return action; }
			}
		}
		return nullptr;
	}

	bool needClips()
	{
		if (!VideoDecoder::available()) { return false; }
		if (!m_a.isEmpty() && !m_b.isEmpty()) { return true; }
		const QString a = m_home.filePath(QStringLiteral("first.mkv"));
		const QString b = m_home.filePath(QStringLiteral("second.mkv"));
		if (!videotest::makeClip(a) || !videotest::makeClip(b, videotest::Palette::GreenYellow)) { return false; }
		m_a = a;
		m_b = b;
		return true;
	}

	QTemporaryDir m_home;
	QString m_a;
	QString m_b;
	SongEditor* m_editor = nullptr;
	VideoLane* m_lane = nullptr;
};

QTEST_MAIN(VideoClipsGuiTest)
#include "VideoClipsGuiTest.moc"
