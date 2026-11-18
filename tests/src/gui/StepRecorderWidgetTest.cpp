/*
 * StepRecorderWidgetTest.cpp - bounded regressions for the step recorder ruler
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
 */

#include <QtTest>

#include <QApplication>
#include <QImage>
#include <QProcess>

#include "StepRecorderWidget.h"

namespace
{

void paintWithZeroStepLength()
{
	QWidget parent;
	parent.resize(320, 200);
	lmms::gui::StepRecorderWidget recorder(&parent, 192, 0, 0, 0, 0);
	recorder.setStepsLength(lmms::TimePos(0));
	recorder.resize(parent.size());
	QImage image(recorder.size(), QImage::Format_ARGB32);
	recorder.render(&image);
}

} // namespace

class StepRecorderWidgetTest : public QObject
{
	Q_OBJECT

private slots:
	void zeroStepLengthDoesNotHangPainting()
	{
		QProcess painter;
		painter.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--paint-zero-step")});
		QVERIFY2(painter.waitForStarted(5000), qPrintable(painter.errorString()));

		const bool finished = painter.waitForFinished(5000);
		if (!finished)
		{
			painter.kill();
			painter.waitForFinished();
		}
		QVERIFY2(finished, "painting with a zero step length did not finish");
		QCOMPARE(painter.exitStatus(), QProcess::NormalExit);
		QCOMPARE(painter.exitCode(), 0);
	}
};

int main(int argc, char** argv)
{
	const bool paintChild = argc > 1 && qstrcmp(argv[1], "--paint-zero-step") == 0;
	QApplication app(argc, argv);
	if (paintChild)
	{
		paintWithZeroStepLength();
		return 0;
	}

	StepRecorderWidgetTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "StepRecorderWidgetTest.moc"
