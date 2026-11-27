/*
 * BandLimitedWave.cpp - helper functions for band-limited
 *                    	waveform generation
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
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

#include "BandLimitedWave.h"

#include <QDataStream>
#include <QFile>

namespace lmms
{

namespace
{

//! A harmonic of a table with \a len samples is kept while its period, len / harm, is at least two samples
//! (Nyquist). The generation loops test this before they add a harmonic. The exactly-Nyquist harmonic is kept.
//! Harmonic 1 always passes for the tables that exist (len >= 2), so the two-sample table is unchanged.
bool withinNyquist( int len, int harm )
{
	return 2 * harm <= len;
}

//! Reads the wavetable stored in \a path into \a waveMipMap. Reports failure when the file cannot be opened or
//! ends before the table does; the caller then generates the table, exactly as it does for an absent file, so
//! the samples the file did hold - or the sample a failed read did not supply - never reach playback.
bool loadWavetable( const QString& path, WaveMipMap& waveMipMap )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return false; }

	QDataStream in( &file );
	in >> waveMipMap;
	return in.status() == QDataStream::Ok;
}

} // namespace

std::array<WaveMipMap, BandLimitedWave::NumWaveforms> BandLimitedWave::s_waveforms = {  };
bool BandLimitedWave::s_wavesGenerated = false;
QString BandLimitedWave::s_wavetableDir = "";


QDataStream& operator<< ( QDataStream &out, WaveMipMap &waveMipMap )
{
	for( int tbl = 0; tbl <= MAXTBL; tbl++ )
	{
		for( int i = 0; i < TLENS[tbl]; i++ )
		{
			out << waveMipMap.sampleAt( tbl, i );
		}
	}
    return out;
}

QDataStream& operator>> ( QDataStream &in, WaveMipMap &waveMipMap )
{
	for( int tbl = 0; tbl <= MAXTBL; tbl++ )
	{
		for( int i = 0; i < TLENS[tbl]; i++ )
		{
			// a file that ended partway has left the stream failed: stop instead of storing a sample the
			// stream never supplied
			if( in.status() != QDataStream::Ok ) { return in; }
			sample_t sample = 0.0f;
			in >> sample;
			waveMipMap.setSampleAt( tbl, i, sample );
		}
	}
    return in;
}


void BandLimitedWave::generateWaves()
{
	using namespace std::numbers;
// don't generate if they already exist
	if( s_wavesGenerated ) return;


	// set wavetable directory
	s_wavetableDir = "data:wavetables/";

// saw wave - BLSaw
// use the file when it is there and reads completely, generate the table otherwise
	if( !loadWavetable( s_wavetableDir + "saw.bin",
			s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)] ) )
	{
		for (int i = 0; i <= MAXTBL; i++)
		{
			const int len = TLENS[i];
			//const double om = 1.0 / len;
			double max = 0.0;

			for( int ph = 0; ph < len; ph++ )
			{
				double s = 0.0f;
				for( int harm = 1; withinNyquist( len, harm ); harm++ )
				{
					const double amp = -1.0 / static_cast<double>( harm );
					//const double a2 = std::cos(om * harm * 2 * pi_v<float>);
					s += amp * /*a2 **/ std::sin(static_cast<double>(ph * harm) / static_cast<double>(len) * 2 * pi_v<float>);
				}
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)].setSampleAt( i, ph, s );
				max = std::max(max, std::abs(s));
			}
			// normalize
			for( int ph = 0; ph < len; ph++ )
			{
				sample_t s = s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)].sampleAt( i, ph ) / max;
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)].setSampleAt( i, ph, s );
			}
		}
	}

// square wave - BLSquare
// use the file when it is there and reads completely, generate the table otherwise
	if( !loadWavetable( s_wavetableDir + "sqr.bin",
			s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSquare)] ) )
	{
		for (int i = 0; i <= MAXTBL; i++)
		{
			const int len = TLENS[i];
			//const double om = 1.0 / len;
			double max = 0.0;

			for( int ph = 0; ph < len; ph++ )
			{
				double s = 0.0f;
				for( int harm = 1; withinNyquist( len, harm ); harm += 2 )
				{
					const double amp = 1.0 / static_cast<double>( harm );
					//const double a2 = std::cos(om * harm * 2 * pi_v<float>);
					s += amp * /*a2 **/ std::sin(static_cast<double>(ph * harm) / static_cast<double>(len) * 2 * pi_v<float>);
				}
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSquare)].setSampleAt( i, ph, s );
				max = std::max(max, std::abs(s));
			}
			// normalize
			for( int ph = 0; ph < len; ph++ )
			{
				sample_t s = s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSquare)].sampleAt( i, ph ) / max;
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSquare)].setSampleAt( i, ph, s );
			}
		}
	}

// triangle wave - BLTriangle
// use the file when it is there and reads completely, generate the table otherwise
	if( !loadWavetable( s_wavetableDir + "tri.bin",
			s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)] ) )
	{
		for (int i = 0; i <= MAXTBL; i++)
		{
			const int len = TLENS[i];
			//const double om = 1.0 / len;
			double max = 0.0;

			for( int ph = 0; ph < len; ph++ )
			{
				double s = 0.0f;
				for( int harm = 1; withinNyquist( len, harm ); harm += 2 )
				{
					const double amp = 1.0 / static_cast<double>( harm * harm );
					//const double a2 = std::cos(om * harm * 2 * pi_v<float>);
					s += amp * /*a2 **/ std::sin((static_cast<double>(ph * harm) / static_cast<double>(len) +
							((harm + 1) % 4 == 0 ? 0.5 : 0.0)) * 2 * pi_v<float>);
				}
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)].setSampleAt( i, ph, s );
				max = std::max(max, std::abs(s));
			}
			// normalize
			for( int ph = 0; ph < len; ph++ )
			{
				sample_t s = s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)].sampleAt( i, ph ) / max;
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)].setSampleAt( i, ph, s );
			}
		}
	}

// moog saw wave - BLMoog
// basically, just add in triangle + 270-phase saw
// use the file when it is there and reads completely, generate the table otherwise
	if( !loadWavetable( s_wavetableDir + "moog.bin",
			s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLMoog)] ) )
	{
		for (int i = 0; i <= MAXTBL; i++)
		{
			const int len = TLENS[i];

			for( int ph = 0; ph < len; ph++ )
			{
				const int sawph = ( ph + static_cast<int>( len * 0.75 ) ) % len;
				const sample_t saw = s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)].sampleAt( i, sawph );
				const sample_t tri = s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)].sampleAt( i, ph );
				s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLMoog)].setSampleAt( i, ph, ( saw + tri ) * 0.5f );
			}
		}
	}

// set the generated flag so we don't load/generate them again needlessly
	s_wavesGenerated = true;


// generate files, serialize mipmaps as QDataStreams and save them on disk
//
// normally these are now provided with LMMS as pre-generated so we don't have to do this,
// but I'm leaving the code here in case it's needed in the future
// (maybe we add more waveforms or change the generation code or mipmap format, etc.)

/*

// if you want to generate the files, you need to set the filenames and paths here -
// can't use the usual wavetable directory here as it can require permissions on
// some systems...

QFile sawfile( "path-to-wavetables/saw.bin" );
QFile sqrfile( "path-to-wavetables/sqr.bin" );
QFile trifile( "path-to-wavetables/tri.bin" );
QFile moogfile( "path-to-wavetables/moog.bin" );

sawfile.open( QIODevice::WriteOnly );
QDataStream sawout( &sawfile );
sawout << s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSaw)];
sawfile.close();

sqrfile.open( QIODevice::WriteOnly );
QDataStream sqrout( &sqrfile );
sqrout << s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLSquare)];
sqrfile.close();

trifile.open( QIODevice::WriteOnly );
QDataStream triout( &trifile );
triout << s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLTriangle)];
trifile.close();

moogfile.open( QIODevice::WriteOnly );
QDataStream moogout( &moogfile );
moogout << s_waveforms[static_cast<std::size_t>(BandLimitedWave::Waveform::BLMoog)];
moogfile.close();

*/

}

} // namespace lmms
