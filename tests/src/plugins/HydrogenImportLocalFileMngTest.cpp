/*
 * HydrogenImportLocalFileMngTest.cpp - malformed TinyXML escapes must be safe.
 *
 * The converter is run in a child process so the regression catches both an
 * out-of-range read and the old retry loop without hanging the test runner.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QStringList>
#include <QtTest>

#include "LocalFileMng.h"

class HydrogenImportLocalFileMngTest : public QObject
{
	Q_OBJECT

private slots:
	void truncatedEscapeReturnsAndRemainsUnchanged()
	{
		const QList<QByteArray> incompleteEscapes{
			QByteArray("&#x"), QByteArray("&#xA"), QByteArray("&#xAB")};
		for (const QByteArray& escape : incompleteEscapes)
		{
			QProcess process;
			process.start(QCoreApplication::applicationFilePath(),
				QStringList{QStringLiteral("--probe-truncated-escape"),
					QString::fromLatin1(escape)});
			QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
			if (!process.waitForFinished(2000))
			{
				process.kill();
				process.waitForFinished();
				QFAIL("conversion did not return for an incomplete escape");
			}
			QCOMPARE(process.exitStatus(), QProcess::NormalExit);
			QCOMPARE(process.exitCode(), 0);
		}
	}
};

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() == 3
		&& app.arguments().at(1) == QStringLiteral("--probe-truncated-escape"))
	{
		const QByteArray escape = app.arguments().at(2).toLatin1();
		QByteArray value("before &#x41; after ");
		value += escape;
		lmms::LocalFileMng::convertFromTinyXMLString(&value);
		return value == QByteArray("before A after ") + escape ? 0 : 1;
	}

	HydrogenImportLocalFileMngTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "HydrogenImportLocalFileMngTest.moc"
