#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtTest>

class SaSpectrumViewTest : public QObject
{
	Q_OBJECT

private slots:
	void decayAccumulatorStartsAtZero()
	{
		QFile header(QString::fromUtf8(SA_SPECTRUM_VIEW_HEADER));
		QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(header.errorString()));

		const QByteArray source = header.readAll();
		QVERIFY2(source.contains("float m_decaySum = 0.0f;"),
			"SaSpectrumView must initialize m_decaySum before its first paint");
	}

	void spectrumReadsUseDataAccessLock()
	{
		QFile header(QString::fromUtf8(SA_SPECTRUM_VIEW_HEADER));
		QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(header.errorString()));

		const auto sourcePath = QFileInfo(header.fileName()).absolutePath() + "/SaSpectrumView.cpp";
		QFile source(sourcePath);
		QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(source.errorString()));

		const QString sourceText = QString::fromUtf8(source.readAll());
		const auto refreshStart = sourceText.indexOf("void SaSpectrumView::refreshPaths()");
		const auto updateStart = sourceText.indexOf("void SaSpectrumView::updateBuffers(", refreshStart);
		QVERIFY(refreshStart >= 0);
		QVERIFY(updateStart > refreshStart);

		const auto refresh = sourceText.mid(refreshStart, updateStart - refreshStart);
		const QRegularExpression lockedReads(
			R"(\{\s*QMutexLocker\s+\w+\(&m_processor->m_dataAccess\);\s*updateBuffers\(m_processor->getSpectrumL\(\),[^;]*;\s*updateBuffers\(m_processor->getSpectrumR\(\),[^;]*;\s*\})",
			QRegularExpression::DotMatchesEverythingOption);
		QVERIFY2(lockedReads.match(refresh).hasMatch(),
			"both spectrum channels must be read while m_dataAccess is held");
	}
};

QTEST_APPLESS_MAIN(SaSpectrumViewTest)
#include "SaSpectrumViewTest.moc"
