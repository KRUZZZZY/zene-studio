/*
 * BandLimitedWaveTest.cpp - the stored band-limited tables hold only harmonics
 *                           whose period is at least two samples (Nyquist)
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include "BandLimitedWave.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace lmms;

namespace
{

//! Tolerance for stored samples against the reference. The reference uses the generator's own float pi, so
//! only float storage remains; the aliased harmonics deviate by 1e-3 or more.
constexpr double kTolerance = 1e-4;

//! The generator's pi (pi_v<float>), so the reference and the stored tables use the same phase constant.
constexpr double kGeneratorPi = static_cast<double>(std::numbers::pi_v<float>);

enum class Shape
{
	Saw,
	Square,
	Triangle
};

//! Reference table of \a shape with \a len samples: the sum of the harmonics whose period len / harm is at
//! least two samples, i.e. 2 * harm <= len (the exact-Nyquist harmonic is included), normalised by its peak.
//! Saw takes every harmonic, square and triangle only odd ones. Requires len >= 3: a two-sample table is degenerate.
std::vector<double> inBandReference(Shape shape, int len)
{
	const int step = shape == Shape::Saw ? 1 : 2;
	std::vector<double> table(static_cast<std::size_t>(len), 0.0);
	for (int ph = 0; ph < len; ++ph)
	{
		double s = 0.0;
		for (int harm = 1; 2 * harm <= len; harm += step)
		{
			const double cycles = static_cast<double>(ph * harm) / static_cast<double>(len);
			switch (shape)
			{
			case Shape::Saw:
				s += -std::sin(2.0 * kGeneratorPi * cycles) / harm;
				break;
			case Shape::Square:
				s += std::sin(2.0 * kGeneratorPi * cycles) / harm;
				break;
			case Shape::Triangle:
			{
				const double shift = (harm + 1) % 4 == 0 ? 0.5 : 0.0;
				s += std::sin(2.0 * kGeneratorPi * (cycles + shift)) / (static_cast<double>(harm) * harm);
				break;
			}
			}
		}
		table[static_cast<std::size_t>(ph)] = s;
	}

	double peak = 0.0;
	for (const double s : table) { peak = std::max(peak, std::abs(s)); }
	for (double& s : table) { s /= peak; }
	return table;
}

//! Reference of the moog saw: a 270-degree-phase saw averaged with the triangle, as generateWaves() builds it.
std::vector<double> moogReference(int len)
{
	const std::vector<double> saw = inBandReference(Shape::Saw, len);
	const std::vector<double> tri = inBandReference(Shape::Triangle, len);
	const int shift = static_cast<int>(len * 0.75);
	std::vector<double> moog(static_cast<std::size_t>(len), 0.0);
	for (int ph = 0; ph < len; ++ph)
	{
		const auto sawph = static_cast<std::size_t>((ph + shift) % len);
		moog[static_cast<std::size_t>(ph)] = (saw[sawph] + tri[static_cast<std::size_t>(ph)]) * 0.5;
	}
	return moog;
}

//! Worst deviation of the stored table \a tbl of \a wave from \a reference.
double worstDeviation(BandLimitedWave::Waveform wave, int tbl, const std::vector<double>& reference)
{
	double worst = 0.0;
	for (int ph = 0; ph < TLENS[tbl]; ++ph)
	{
		const auto stored = static_cast<double>(
			BandLimitedWave::s_waveforms[static_cast<std::size_t>(wave)].sampleAt(tbl, ph));
		worst = std::max(worst, std::abs(stored - reference[static_cast<std::size_t>(ph)]));
	}
	return worst;
}

//! The value the short wavetable files carry. It is not a sample of any in-band table, so a table that keeps
//! it, or that keeps the zeroed sample a failed read leaves behind, cannot match its reference.
constexpr float kShortFileValue = 0.5f;

//! Writes \a sampleCount samples of kShortFileValue the way WaveMipMap's operator<< writes a table, but cut
//! short: a complete table holds 20475 samples, so a reader always runs past the end of such a file.
void writeShortTable( const QString& path, int sampleCount )
{
	QFile file( path );
	QVERIFY( file.open( QIODevice::WriteOnly ) );
	QDataStream out( &file );
	for( int i = 0; i < sampleCount; ++i ) { out << kShortFileValue; }
	QVERIFY2( out.status() == QDataStream::Ok, "the short fixture itself must be written completely" );
}

//! Writes the four wavetables of the data search path \a dir as short files of \a sampleCount samples.
void writeShortTables( const QString& dir, int sampleCount )
{
	const QString wavetableDir = QDir( dir ).filePath( QStringLiteral("wavetables") );
	QVERIFY( QDir().mkpath( wavetableDir ) );
	for( const char* name : { "saw", "sqr", "tri", "moog" } )
	{
		writeShortTable( QDir( wavetableDir ).filePath( QStringLiteral("%1.bin").arg( QString::fromLatin1( name ) ) ),
			sampleCount );
	}
}

//! Size of the shipped table \a name, which is a complete table: a fixture shorter than it is truncated.
qint64 shippedTableSize( const char* name )
{
	return QFileInfo( QDir( QStringLiteral(QT_TESTCASE_SOURCEDIR) )
		.filePath( QStringLiteral("../data/wavetables/%1.bin").arg( QString::fromLatin1( name ) ) ) ).size();
}

//! Compares every stored table, except the two-sample one, with the in-band reference.
void verifyStoredTables()
{
	for (int tbl = 0; tbl <= MAXTBL; ++tbl)
	{
		const int len = TLENS[tbl];
		if (len < 3) { continue; }

		const struct
		{
			BandLimitedWave::Waveform wave;
			const char* name;
			std::vector<double> reference;
		} cases[] = {
			{BandLimitedWave::Waveform::BLSaw, "saw", inBandReference(Shape::Saw, len)},
			{BandLimitedWave::Waveform::BLSquare, "square", inBandReference(Shape::Square, len)},
			{BandLimitedWave::Waveform::BLTriangle, "triangle", inBandReference(Shape::Triangle, len)},
			{BandLimitedWave::Waveform::BLMoog, "moog", moogReference(len)},
		};
		for (const auto& c : cases)
		{
			const double deviation = worstDeviation(c.wave, tbl, c.reference);
			QVERIFY2(deviation < kTolerance,
				qPrintable(QStringLiteral("%1 table of %2 samples deviates from its in-band harmonics by %3")
					.arg(QString::fromLatin1(c.name)).arg(len).arg(deviation)));
		}
	}
}

} // namespace

class BandLimitedWaveTest : public QObject
{
	Q_OBJECT

private slots:
	//! generateWaves() reads the shipped files when they exist. An empty data: search path makes both
	//! files absent, so the fallback generator runs. The generator flag is reset so the case is self-contained.
	void fallbackTablesHoldOnlyInBandHarmonics()
	{
		QDir::setSearchPaths(QStringLiteral("data"), QStringList{});
		QVERIFY(!QFile::exists(QStringLiteral("data:wavetables/saw.bin")));
		QVERIFY(!QFile::exists(QStringLiteral("data:wavetables/sqr.bin")));

		BandLimitedWave::s_wavesGenerated = false;
		BandLimitedWave::s_waveforms = {};
		BandLimitedWave::generateWaves();
		QVERIFY(BandLimitedWave::s_wavesGenerated);

		verifyStoredTables();
	}

	//! The shipped files are what the application loads, so they must hold the same in-band harmonics.
	void shippedTablesHoldOnlyInBandHarmonics()
	{
		const QString sourceData = QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}.filePath(QStringLiteral("../data"));
		QDir::setSearchPaths(QStringLiteral("data"), QStringList{sourceData});
		QVERIFY(QFile::exists(QStringLiteral("data:wavetables/sqr.bin")));

		BandLimitedWave::s_wavesGenerated = false;
		BandLimitedWave::s_waveforms = {};
		BandLimitedWave::generateWaves();
		QVERIFY(BandLimitedWave::s_wavesGenerated);

		verifyStoredTables();
	}

	//! A wavetable file that ends partway through its data must not feed the samples it does hold into the
	//! tables: a failed load is treated like an absent file and the table is generated instead.
	void truncatedTablesAreRegenerated()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		writeShortTables( dir.path(), 64 );
		const QString shortSaw = QDir( dir.path() ).filePath( QStringLiteral("wavetables/saw.bin") );
		QVERIFY( QFileInfo( shortSaw ).size() < shippedTableSize( "saw" ) );

		QDir::setSearchPaths( QStringLiteral("data"), QStringList{ dir.path() } );
		QVERIFY( QFile::exists( QStringLiteral("data:wavetables/saw.bin") ) );

		BandLimitedWave::s_wavesGenerated = false;
		BandLimitedWave::s_waveforms = {};
		BandLimitedWave::generateWaves();
		QVERIFY( BandLimitedWave::s_wavesGenerated );

		verifyStoredTables();
	}

	//! A wavetable file that holds no sample at all is the same failure with nothing read: the tables must be
	//! generated, not left as the indeterminate samples the failed read produced.
	void emptyTablesAreRegenerated()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		writeShortTables( dir.path(), 0 );
		QVERIFY( QFileInfo( QDir( dir.path() ).filePath( QStringLiteral("wavetables/moog.bin") ) ).size() == 0 );

		QDir::setSearchPaths( QStringLiteral("data"), QStringList{ dir.path() } );
		QVERIFY( QFile::exists( QStringLiteral("data:wavetables/sqr.bin") ) );

		BandLimitedWave::s_wavesGenerated = false;
		BandLimitedWave::s_waveforms = {};
		BandLimitedWave::generateWaves();
		QVERIFY( BandLimitedWave::s_wavesGenerated );

		verifyStoredTables();
	}
};

QTEST_GUILESS_MAIN(BandLimitedWaveTest)
#include "BandLimitedWaveTest.moc"
